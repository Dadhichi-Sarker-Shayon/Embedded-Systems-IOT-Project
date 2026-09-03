# Drowning Prevention Kit (Acoustic Transmitter and Hydrophone Receiver)

## Overview

This project presents a low-cost, IoT-based underwater drowning detection and rescue alert system. When a swimmer's wearable transmitter unit detects a likely emergency, it emits a 1500 Hz acoustic distress tone underwater. A poolside hydrophone receiver station captures this tone, verifies it against false positives, and raises an immediate alarm for lifeguards or caregivers through local LED and buzzer outputs, as well as a connected Flutter mobile application over Wi-Fi.

The system consists of three cooperating units: a wearable acoustic transmitter, a hydrophone receiver station, and a Flutter mobile application. Together, they form a closed acoustic-to-digital alert pipeline from underwater tone detection to on-screen emergency notification.

## System Architecture

The wearable transmitter continuously evaluates heart rate, water-contact, and motion data on the swimmer's body. When an emergency condition is confirmed, it drives a submerged piezo transmitter to emit a 1500 Hz acoustic tone underwater. This acoustic-first approach is a deliberate design decision: RF signals such as those used by the NRF24L01 module attenuate to near-zero within centimetres of submersion, making underwater radio unreliable, whereas acoustic energy propagates efficiently through water.

The receiver station, moored at a fixed poolside location, listens continuously for this tone using its hydrophone front end. Upon a confirmed detection, the ESP32 receiver raises a local LED and buzzer alarm and simultaneously pushes a JSON alert over a self-hosted Wi-Fi Access Point to the Flutter application, which displays the emergency alert, plays a siren, and vibrates the caregiver's phone.

[ Figure 5.1 - Overall System Architecture Diagram ]

## Receiver Hardware Design

### Hardware Components

| Component | Specification | Function |
| --- | --- | --- |
| ESP32 DevKit | Dual-core, Wi-Fi + ADC | Signal sampling, Goertzel detection, WebSocket server |
| DIY Hydrophone | 35 mm piezo disc, waterproofed | Underwater acoustic-to-electrical transduction |
| LM386 | Audio power amplifier, 20-200x gain | Pre-amplification of raw piezo signal |
| TL082 | Dual JFET op-amp | Active band-pass (MFB) filtering around 1500 Hz |
| Virtual Ground Network | 10 kOhm + 10 kOhm + 470 uF | Mid-supply bias (4.5 V) for single-rail op-amp operation |
| Signal Conditioning Divider | 10 kOhm + 10 kOhm + 10 uF | Level-shifts and scales filtered signal into ESP32 ADC range |
| LED | 5 mm, red | Visual alarm indicator |
| Active Buzzer | 5 V, self-oscillating | Audible local alarm |
| 9 V Battery | Alkaline / rechargeable | Analog front-end power supply |

[ Figure 5.2 - Complete Receiver Circuit Diagram ]

### Stage 1: Power Supply and Virtual Ground

Because the analog front end is powered from a single 9 V rail rather than a dual-polarity supply, a virtual ground (VGND) reference is generated at approximately 4.5 V using a resistive divider (10 kOhm + 10 kOhm) buffered by a 470 uF electrolytic capacitor. This mid-rail reference biases the LM386 and TL082 stages so that AC signals from the hydrophone can swing symmetrically above and below the reference without clipping against the supply rails.

### Stage 2: Hydrophone and LM386 Amplifier

The DIY hydrophone - a bare 35 mm piezo disc, waterproofed and submerged near the pool floor - generates a low-amplitude AC voltage in response to incident acoustic pressure. This signal is fed into the LM386 low-voltage audio amplifier, configured for a nominal gain in the 20-200x range, which raises the microvolt-to-millivolt-level piezo output to an amplitude suitable for further filtering. The amplifier output is AC-coupled through a 10 uF capacitor to remove any residual DC offset before entering the filter stage.

### Stage 3: TL082 Active Band-Pass Filter

The amplified signal is passed through a Multiple Feedback (MFB) active band-pass filter built around one half of a TL082 dual op-amp. The filter is tuned so that its centre frequency coincides with the 1500 Hz transmitter tone, attenuating pool pump noise, splashing, and other broadband underwater interference outside this narrow band. In the finalised design, the two frequency-setting capacitors are 3.3 nF, which - together with the 10 kOhm and 200 kOhm feedback resistors - sets the centre frequency at approximately 1500 Hz.

### Stage 4: ESP32 Signal Conditioning

The filtered AC signal is AC-coupled through a 10 uF capacitor into a resistive bias divider built from two 10 kOhm resistors, which centres the signal at approximately 1.65 V - half of the ESP32's 3.3 V ADC reference. Both resistors of this divider are referenced between the 3.3 V rail and true circuit ground (0 V), not the 4.5 V virtual ground used by the analog front end. The conditioned signal is fed into an ADC-capable GPIO pin, from which the ESP32 performs continuous sampling for Goertzel analysis.

### Stage 5: LED and Buzzer Alarm

Two ESP32 GPIO pins directly drive a red LED and an active buzzer through current-limiting resistors. When a valid 1500 Hz detection is confirmed by the firmware, both outputs are asserted, giving an immediate local visual and audible indication at the receiver station itself.

## Receiver Software Design

The receiver firmware is developed in C++ using the Arduino framework for ESP32.

### System Initialization

On boot, the ESP32 initialises its ADC peripheral, configures the LED and buzzer GPIOs as outputs, starts a Wi-Fi Access Point with a fixed SSID and password, and launches a WebSocket server that listens for client connections from the Flutter application.

### Hydrophone Signal Sampling

The conditioned analog signal is sampled at a fixed rate chosen to satisfy the Nyquist criterion for 1500 Hz detection with adequate margin. Samples are read from the ADC in a tight loop and accumulated into a fixed-length block sized to match the Goertzel algorithm's block length, after which the block is passed to the detection routine.

### Goertzel Algorithm

Rather than computing a full spectrum via the Fast Fourier Transform, the receiver uses the Goertzel algorithm, which evaluates the discrete Fourier transform at a single target frequency using a second-order IIR recursion:

    coeff = 2*cos(2*pi*k/N)
    s(n) = x(n) + coeff*s(n-1) - s(n-2)
    magnitude^2 = s(N-1)^2 + s(N-2)^2 - coeff*s(N-1)*s(N-2)

where x(n) is the sampled input, N is the block length, and k = round(N*f_target/f_sample) selects the target bin corresponding to 1500 Hz. The Goertzel algorithm is preferred over the FFT for this application because only a single frequency bin is of interest; computing a full N-point FFT would require O(N log N) operations and produce an entire spectrum that is discarded except for one bin, whereas the Goertzel algorithm computes the same bin in O(N) time with a fixed, small memory footprint.

**Listing 5.1 - Goertzel Algorithm Implementation:**

    float goertzelMagnitude(int16_t *samples, int N, float targetFreq, float sampleRate) {
      float k = roundf((N * targetFreq) / sampleRate);
      float omega = (2.0 * PI * k) / N;
      float coeff = 2.0 * cos(omega);
      float s0 = 0, s1 = 0, s2 = 0;
      for (int n = 0; n < N; n++) {
        s0 = samples[n] + coeff * s1 - s2;
        s2 = s1;
        s1 = s0;
      }
      float real = s1 - s2 * cos(omega);
      float imag = s2 * sin(omega);
      return sqrt(real * real + imag * imag);
    }

### Signal Verification

A single high-magnitude Goertzel result is not, by itself, treated as a confirmed detection, since transient noise or impact sounds can momentarily produce a false-positive reading. Instead, the firmware requires the computed magnitude to exceed a calibrated threshold across a configurable number of consecutive sample blocks - the confirmation window - before raising an alarm.

**Listing 5.2 - Threshold and Consecutive-Detection Verification:**

    const float THRESHOLD = 4500.0;
    const int CONFIRM_COUNT = 5;
    int consecutiveHits = 0;
    
    bool verifySignal(float magnitude) {
      if (magnitude > THRESHOLD) {
        consecutiveHits++;
      } else {
        consecutiveHits = 0;
      }
      return (consecutiveHits >= CONFIRM_COUNT);
    }

### WebSocket Communication

The ESP32 hosts a WebSocket server over its self-created Wi-Fi Access Point, allowing the Flutter application to connect directly without requiring an external router or internet connection.

**Listing 5.3 - WebSocket Server Initialisation:**

    AsyncWebServer server(80);
    AsyncWebSocket ws("/ws");
    
    void setupWebSocket() {
      ws.onEvent(onWsEvent);
      server.addHandler(&ws);
      server.begin();
    }
    
    void onWsEvent(AsyncWebSocket *server, AsyncWebSocketClient *client,
                   AwsEventType type, void *arg, uint8_t *data, size_t len) {
      if (type == WS_EVT_CONNECT) {
        Serial.printf("Flutter client connected: %u\\n", client->id());
      }
    }

### JSON Alarm Message Construction

**Listing 5.4 - JSON Alarm Construction and Transmission:**

    void sendAlarm() {
      StaticJsonDocument<128> doc;
      doc["type"] = "ALARM";
      doc["source"] = "hydrophone";
      doc["frequency"] = 1500;
      doc["timestamp"] = millis();
      
      String payload;
      serializeJson(doc, payload);
      ws.textAll(payload);
      
      digitalWrite(LED_PIN, HIGH);
      digitalWrite(BUZZER_PIN, HIGH);
    }


### Complete Receiver Firmware

The following is the complete, working ESP32 receiver firmware:

    #include <WiFi.h>
    #include <WebSocketsServer.h>
    #include <math.h>
    
    const char* ssid = "test";
    const char* password = "1234567890";
    
    WebSocketsServer webSocket(81);
    
    const int HYDROPHONE_PIN = 32;
    const int BUZZER_PIN = 25;
    const int LED_PIN = 2;
    
    const float SAMPLE_RATE_HZ = 6000.0;
    const int N_SAMPLES = 200;
    const float TARGET_FREQ_HZ = 1500.0;
    const float REFERENCE_FREQ_HZ = 1000.0;
    
    float GOERTZEL_THRESHOLD = 50.0;
    float SIGNAL_RATIO_THRESHOLD = 1.3;
    
    const int ADC_CENTER = 2048;
    float coeffTarget;
    float coeffReference;
    uint16_t samplePeriodUs;
    
    bool alarmActive = false;
    unsigned long alarmStartTime = 0;
    const unsigned long ALARM_DURATION_MS = 30000;
    unsigned long lastBlinkTime = 0;
    bool ledBlinkState = false;
    const unsigned long BLINK_INTERVAL_MS = 300;
    
    const unsigned long CONFIRM_DURATION_MS = 2000;
    float REQUIRED_HIT_RATIO = 0.5f;
    const int MAX_HISTORY_BLOCKS = 300;
    bool detectionHistory[MAX_HISTORY_BLOCKS];
    int historyIndex = 0;
    int historyCount = 0;
    int verifiedCount = 0;
    int CONFIRM_WINDOW_BLOCKS;
    
    void resetConfirmationWindow() {
      historyIndex = 0; historyCount = 0; verifiedCount = 0;
    }
    
    void stopAlarm() {
      digitalWrite(BUZZER_PIN, LOW);
      digitalWrite(LED_PIN, LOW);
      ledBlinkState = false;
      alarmActive = false;
      resetConfirmationWindow();
      webSocket.broadcastTXT("ALARM_STOPPED");
    }
    
    void webSocketEvent(uint8_t num, WStype_t type, uint8_t *payload, size_t length) {
      switch (type) {
        case WStype_CONNECTED:
          webSocket.sendTXT(num, "CONNECTED");
          break;
        case WStype_TEXT: {
          String msg = String((char*)payload).substring(0, length);
          msg.trim();
          if (msg == "STOP_ALARM") stopAlarm();
          break;
        }
        default: break;
      }
    }
    
    void triggerAlarm() {
      if (alarmActive) return;
      ledBlinkState = true;
      digitalWrite(BUZZER_PIN, HIGH);
      digitalWrite(LED_PIN, HIGH);
      lastBlinkTime = millis();
      alarmActive = true;
      alarmStartTime = millis();
      webSocket.broadcastTXT("ALARM");
    }

    
    void goertzelMagnitudes(float &targetMag, float &referenceMag) {
      float q1t=0,q2t=0,q1r=0,q2r=0;
      unsigned long nextSampleTime = micros();
      for (int i=0; i<N_SAMPLES; i++) {
        while ((long)(micros()-nextSampleTime)<0) {}
        nextSampleTime += samplePeriodUs;
        float sample = (float)(analogRead(HYDROPHONE_PIN)-ADC_CENTER);
        float q0t=coeffTarget*q1t-q2t+sample; q2t=q1t; q1t=q0t;
        float q0r=coeffReference*q1r-q2r+sample; q2r=q1r; q1r=q0r;
      }
      float pT=q1t*q1t+q2t*q2t-q1t*q2t*coeffTarget;
      targetMag=sqrtf(pT>0?pT:0)/(N_SAMPLES/2.0f);
      float pR=q1r*q1r+q2r*q2r-q1r*q2r*coeffReference;
      referenceMag=sqrtf(pR>0?pR:0)/(N_SAMPLES/2.0f);
    }
    
    bool isVerifiedSignal(float t, float r) {
      float ratio=t/(r+1.0f);
      return (t>GOERTZEL_THRESHOLD)&&(ratio>SIGNAL_RATIO_THRESHOLD);
    }
    
    void setup() {
      Serial.begin(115200);
      analogReadResolution(12);
      pinMode(BUZZER_PIN, OUTPUT);
      pinMode(LED_PIN, OUTPUT);
      WiFi.mode(WIFI_AP);
      WiFi.softAP(ssid, password);
      webSocket.begin();
      webSocket.onEvent(webSocketEvent);
      int kT=(int)(0.5+(N_SAMPLES*TARGET_FREQ_HZ)/SAMPLE_RATE_HZ);
      coeffTarget=2.0*cosf((2.0*PI*kT)/N_SAMPLES);
      int kR=(int)(0.5+(N_SAMPLES*REFERENCE_FREQ_HZ)/SAMPLE_RATE_HZ);
      coeffReference=2.0*cosf((2.0*PI*kR)/N_SAMPLES);
      samplePeriodUs=(uint16_t)(1000000.0/SAMPLE_RATE_HZ);
      float bDur=(N_SAMPLES/SAMPLE_RATE_HZ)*1000.0;
      CONFIRM_WINDOW_BLOCKS=(int)(0.5+(CONFIRM_DURATION_MS/bDur));
      if (CONFIRM_WINDOW_BLOCKS>MAX_HISTORY_BLOCKS) CONFIRM_WINDOW_BLOCKS=MAX_HISTORY_BLOCKS;
      if (CONFIRM_WINDOW_BLOCKS<1) CONFIRM_WINDOW_BLOCKS=1;
    }
    
    void loop() {
      webSocket.loop();
      if (Serial.available()) {
        String cmd=Serial.readStringUntil(0x0A); cmd.trim();
        if (cmd=="reset") stopAlarm();
        if (cmd=="alarm") triggerAlarm();
        if (cmd=="stop") stopAlarm();
        if (cmd.startsWith("threshold ")) GOERTZEL_THRESHOLD=cmd.substring(10).toFloat();
        if (cmd.startsWith("ratio ")) SIGNAL_RATIO_THRESHOLD=cmd.substring(6).toFloat();
        if (cmd.startsWith("hitratio ")) REQUIRED_HIT_RATIO=cmd.substring(9).toFloat();
      }
      if (alarmActive&&(millis()-alarmStartTime>=ALARM_DURATION_MS)) stopAlarm();
      if (alarmActive&&(millis()-lastBlinkTime>=BLINK_INTERVAL_MS)) {
        ledBlinkState=!ledBlinkState;
        digitalWrite(LED_PIN,ledBlinkState?HIGH:LOW);
        digitalWrite(BUZZER_PIN,ledBlinkState?HIGH:LOW);
        lastBlinkTime=millis();
      }
      float tMag,rMag;
      goertzelMagnitudes(tMag,rMag);
      if (!alarmActive) {
        bool v=isVerifiedSignal(tMag,rMag);
        if (historyCount==CONFIRM_WINDOW_BLOCKS) { if (detectionHistory[historyIndex]) verifiedCount--; }
        else historyCount++;
        detectionHistory[historyIndex]=v; if (v) verifiedCount++;
        historyIndex=(historyIndex+1)%CONFIRM_WINDOW_BLOCKS;
        float hit=(historyCount>0)?(float)verifiedCount/historyCount:0;
        if ((historyCount>=CONFIRM_WINDOW_BLOCKS)&&(hit>=REQUIRED_HIT_RATIO)) {
          triggerAlarm(); resetConfirmationWindow();
        }
      }
    }

[ Figure 5.3 - Receiver Software Flowchart ]


## Flutter Mobile Application

### Features

- Live dashboard summarising heart rate, water-detection status, motion status, and receiver connection status.
- Real-time sensor monitoring screen with continuously updating values streamed from the receiver.
- Dedicated emergency alert screen triggered automatically on receipt of an ALARM message.
- Audible siren playback and phone vibration to ensure the caregiver notices an emergency.
- Persistent WebSocket connection to the receiver Access Point, with automatic reconnection handling.

### Live Camera Monitoring Dashboard

The home screen shows a real-time background camera feed alongside live connection status for both the monitoring session and the paired ESP32 hardware. A "Monitoring" indicator confirms the app is actively watching for alerts, and a separate "ESP32" status chip shows whether the WiFi link to the receiver station is currently connected.

### Automated Emergency Alarm Screen

When the ESP32 confirms a detection and broadcasts its ALARM message, the app immediately takes over the screen with a full-screen "DROWNING DETECTED" alert rendered in high-contrast red. A loud siren plays, the device vibrates, and the screen displays the automatically generated Emergency Clip with a precise timestamp. A buffer timeline lets the user scrub back through the moments leading up to the alert.

### Video Ring Buffer and Clip Stitching

Underneath the live view, the app continuously records video into a rolling ring buffer, discarding footage older than the configured retention window. The moment an alarm fires, the app automatically stitches together the last several seconds of buffered footage into a single Emergency Clip, preserving the critical seconds before detection without requiring manual recording.

### Incident History

A dedicated Event History tab keeps a permanent, browsable log of every past emergency event, each entry showing an auto-generated thumbnail preview, the event filename (timestamped), and its exact date and time. The user can play back any past clip or delete events that are no longer needed.

### Customizable Settings

The Settings screen lets the user tune the app to their device and environment:
- Video Quality: controls camera resolution (e.g. 1080p).
- ESP32 Connection: set the receiver IP address (default 192.168.4.1).
- Alarm Configuration: toggle for automatic siren playback.
- Buffer Configuration: Buffer Size (e.g. 5 minutes) and Segment Duration (e.g. 10 seconds).
- Clear Video Buffer: immediately free up memory.

### Flutter Communication

The Flutter application connects to the receiver WebSocket endpoint over the receiver Wi-Fi Access Point. Incoming frames are parsed as JSON and routed based on their type field.

**Listing 5.5 - WebSocket Connection:**

    final channel = WebSocketChannel.connect(
      Uri.parse("ws://192.168.4.1/ws"),
    );
    channel.stream.listen(
      (message) => handleIncomingMessage(message),
      onError: (e) => setConnectionStatus(false),
      onDone: () => setConnectionStatus(false),
    );

**Listing 5.6 - Parsing Incoming JSON:**

    void handleIncomingMessage(String message) {
      final data = jsonDecode(message);
      if (data["type"] == "ALARM") {
        triggerEmergencyAlert();
        return;
      }
      setState(() {
        heartRate = data["bpm"];
        waterDetected = data["water"];
        motionLevel = data["motion"];
        isConnected = true;
      });
    }

**Listing 5.7 - Emergency Alert Handling:**

    void triggerEmergencyAlert() {
      Navigator.pushNamed(context, "/emergency");
      audioPlayer.play(AssetSource("siren.mp3"), volume: 1.0);
      Vibration.vibrate(pattern: [0, 500, 200, 500], repeat: 0);
    }

[ Figure 5.4 - Flutter Dashboard Screen ]
[ Figure 5.5 - Live Monitoring Screen ]
[ Figure 5.6 - Emergency Alert Screen ]
[ Figure 5.7 - Flutter Application Flowchart ]


## Complete Receiver and Flutter Workflow

Figure 5.8 consolidates the end-to-end workflow: hydrophone signal acquisition, analog conditioning, Goertzel-based detection and verification on the ESP32, JSON transmission over WebSocket, and reception, display, and alerting on the Flutter application. This workflow operates continuously and independently of the emergency path, so that live telemetry is always available to the caregiver while the system remains ready to escalate to a full emergency alert at any moment.

[ Figure 5.8 - Complete Receiver and Flutter Workflow Diagram ]

## Experimental Results

The receiver subsystem was evaluated in a controlled pool environment to characterise its detection performance and end-to-end alerting latency. Hydrophone sensitivity was first verified by generating a 1500 Hz tone underwater at a known source distance and confirming a consistent, clearly distinguishable rise in the amplified signal amplitude at the LM386 output. The TL082 band-pass filter response was checked with a signal generator sweep to confirm that its passband was centred close to 1500 Hz and that out-of-band tones were attenuated sufficiently to avoid triggering false detections.

The Goertzel detector was then tested against this filtered signal, and the computed magnitude at the target bin was observed to rise sharply above the calibrated threshold whenever the transmitter tone was present, and to remain below threshold under quiet and typical ambient pool noise conditions. The consecutive-detection verification window was tuned experimentally to suppress transient false triggers while keeping detection latency within an acceptable range for a rescue-alert application.

[ Figure 5.9 - Prototype Hydrophone Receiver Photograph ]
[ Figure 5.10 - Pool Testing Setup ]
[ Figure 5.11 - Serial Monitor Output During Detection ]
[ Figure 5.12 - Flutter Application Screenshots During Live Test ]

## Advantages

- Acoustic detection remains reliable underwater where RF-based approaches such as NRF24L01 fail due to severe signal attenuation in water.
- The Goertzel algorithm provides efficient, real-time single-tone detection with minimal computational and memory overhead on the ESP32.
- Consecutive-detection verification substantially reduces false alarms from ambient pool noise.
- A self-hosted Wi-Fi Access Point and WebSocket server remove any dependency on existing network infrastructure at the deployment site.
- The Flutter application provides immediate, high-visibility alerting through simultaneous visual, audible, and vibratory notification.
- The overall hardware cost is low, relying on commonly available components such as the LM386, TL082, and a DIY piezo hydrophone.

## Limitations

- Detection range is limited by hydrophone sensitivity and acoustic attenuation over distance, requiring careful placement within the pool.
- A single fixed-frequency detector cannot distinguish between multiple simultaneous transmitters in a multi-swimmer deployment without frequency or time-division extensions.
- The self-hosted Access Point restricts the mobile application to a single local network, limiting simultaneous connections and remote monitoring.
- Ambient acoustic noise sources with strong energy near 1500 Hz could, in principle, still trigger false detections despite threshold and confirmation-window safeguards.
- The analog front end requires careful manual calibration (gain, filter tuning, bias points) during assembly, which limits ease of replication.

## Conclusion

This project has presented the design, implementation, and evaluation of a hydrophone receiver and Flutter mobile application that together complete the drowning detection and rescue alert system. By combining a purpose-built analog front end, an efficient Goertzel-based detection algorithm, and a responsive mobile interface, the receiver subsystem reliably converts an underwater acoustic distress signal into a timely, high-visibility alert. Experimental testing confirmed that the system detects the 1500 Hz transmitter tone consistently while rejecting typical ambient pool noise, and delivers the resulting alarm to the caregiver phone with minimal latency, validating the acoustic-first design approach adopted to overcome the limitations of underwater RF communication.

## Author

Dadhichi Sarker Shayon
