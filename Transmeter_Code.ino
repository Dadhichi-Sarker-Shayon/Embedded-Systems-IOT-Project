#include <Wire.h>

#define PULSE_PIN 34          // pulse sensor Analog Pin
#define WATER_PIN 35          // water sensor Analog Pin 
#define MOSFET_GATE_PIN 18    // mosfet gate pin
#define DATA_SWITCH_PIN 12    // main switch

const int MPU_addr = 0x68;   

const int PULSE_THRESHOLD = 2200; 
const float HEART_LOW = 70.0;
const float HEART_HIGH = 130.0;
const unsigned long MAX_SUBMERGED_3_FACTOR = 300000; // 5min
const unsigned long MAX_ABSOLUTE_SUBMERGED = 10000; // 10s
const int MOTION_THRESHOLD = 5000;                  

int16_t AcX, AcY, AcZ, Tmp, GyX, GyY, GyZ;
int16_t lastAcX = 0, lastAcY = 0, lastAcZ = 0;

unsigned long lastBeatTime = 0;
int beatCounter = 0;
float totalBPM = 0;
float averageBPM = 0;
bool pulseDetected = false;

bool isDrowned = false;
unsigned long drownStartTime = 0;
unsigned long totalDrownDuration = 0;

unsigned long lastMotionCheck = 0;
unsigned long motionChangeCount = 0;

void setup() {
  Serial.begin(115200);
  Wire.begin(21, 22); // SDA=21, SCL=22
  
  pinMode(MOSFET_GATE_PIN, OUTPUT);
  pinMode(DATA_SWITCH_PIN, INPUT_PULLUP);
  digitalWrite(MOSFET_GATE_PIN, LOW);

  Wire.beginTransmission(MPU_addr);
  Wire.write(0x6B); 
  Wire.write(0);    
  Wire.endTransmission(true);

  Serial.println("System Initialized");
}

void loop() {
  unsigned long currentMillis = millis();
  int switchState = digitalRead(DATA_SWITCH_PIN);

  int analogPulse = analogRead(PULSE_PIN);
  if (analogPulse > PULSE_THRESHOLD && !pulseDetected) {
    pulseDetected = true;
    unsigned long currentBeatTime = currentMillis;
    if (lastBeatTime > 0) {
      unsigned long duration = currentBeatTime - lastBeatTime;
      float bpm = 60000.0 / duration;
      if (bpm >= 40 && bpm <= 200) {
        totalBPM += bpm;
        beatCounter++;
        if (beatCounter >= 5) {
          averageBPM = totalBPM / 5;
          totalBPM = 0;
          beatCounter = 0;
        }
      }
    }
    lastBeatTime = currentBeatTime;
  }
  if (analogPulse < (PULSE_THRESHOLD - 100)) {
    pulseDetected = false;
  }

  int waterValue = analogRead(WATER_PIN);
  if (waterValue > 50) {
    if (!isDrowned) {
      isDrowned = true;
      drownStartTime = currentMillis;
    }
    totalDrownDuration = (currentMillis - drownStartTime) / 1000;
  } else {
    isDrowned = false;
    totalDrownDuration = 0;
  }

  Wire.beginTransmission(MPU_addr);
  Wire.write(0x3B);
  Wire.endTransmission(false);
  Wire.requestFrom(MPU_addr, 6, true);
  AcX = Wire.read() << 8 | Wire.read();
  AcY = Wire.read() << 8 | Wire.read();
  AcZ = Wire.read() << 8 | Wire.read();

  if (currentMillis - lastMotionCheck > 100) {
    lastMotionCheck = currentMillis;
    if (abs(AcX - lastAcX) > MOTION_THRESHOLD  ||
        abs(AcY - lastAcY) > MOTION_THRESHOLD  ||
        abs(AcZ - lastAcZ) > MOTION_THRESHOLD) {
      motionChangeCount++; 
    }
    lastAcX = AcX; lastAcY = AcY; lastAcZ = AcZ;
  }

  bool point1_Water = false; 
  bool point2_Heart = false; 
  bool point3_Motion = false; 

  if (isDrowned && (currentMillis - drownStartTime >= MAX_SUBMERGED_3_FACTOR)) {
    point1_Water = true;
  }
  if (averageBPM > 120 || (averageBPM < 60 && averageBPM > 0)) {
    point2_Heart = true; 
  }
  if (motionChangeCount > 15) { 
    point3_Motion = true;
  }

  static unsigned long lastMotionReset = 0;
  if (currentMillis - lastMotionReset > 10000) {
    lastMotionReset = currentMillis;
    motionChangeCount = 0;
  }

  bool triggerAlert = false;
  String reason = "";
  //condition 1
  if (point1_Water && point2_Heart && point3_Motion) {
    triggerAlert = true;
    reason = "3-Factors Critical (Time > 5m + Heart Abnormal + High Panic Motion)";
  }
  // condition 2
  else if (averageBPM < HEART_LOW && averageBPM > 50) {
    triggerAlert = true;
    reason = "CRITICAL: Heart Rate Below 70 BPM!";
  }
  // condition 3
  else if (averageBPM > HEART_HIGH) {
    triggerAlert = true;
    reason = "CRITICAL: Heart Rate Exceeded!";
  }
  // condition 4
  else if (isDrowned && (currentMillis - drownStartTime >= MAX_ABSOLUTE_SUBMERGED)) {
    triggerAlert = true;
    reason = "CRITICAL: Absolute Submersion Time Exceeded!";
  }

  if (triggerAlert) {
    Serial.println("\nEMERGENCY DETECTED");
    Serial.print("Reason: "); Serial.println(reason);
    sendEmergencyPing();
  }

  static unsigned long lastLog = 0;
  if (currentMillis - lastLog > 2000) {
    lastLog = currentMillis;
    Serial.print("\nBPM: "); Serial.print(averageBPM, 1);
    Serial.print(" | Water: "); Serial.print(totalDrownDuration); Serial.print("s");
    Serial.print(" | Motion Spikes: "); Serial.print(motionChangeCount);
  }
  delay(10); 
}

void sendEmergencyPing() {
  while(true) {
    tone(MOSFET_GATE_PIN, 1500); //frequency
    delay(150);
    noTone(MOSFET_GATE_PIN);
    delay(100);
  }
}