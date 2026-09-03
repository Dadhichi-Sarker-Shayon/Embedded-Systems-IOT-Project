# Drowning Prevention Kit (Acoustic Transmitter and Hydrophone Receiver)

An IoT project for underwater drowning detection and rescue alert. It combines three parts: a wearable acoustic transmitter, a poolside hydrophone receiver, and a Flutter mobile application.

The wearable unit watches the swimmer. If a drowning event is confirmed, it emits a 1500 Hz tone underwater. The hydrophone receiver detects the tone using an ESP32, which runs a Goertzel single-tone detector. A verified detection triggers a local LED and buzzer alarm and sends a WebSocket alert. The Flutter app then shows a full-screen emergency alert with siren and vibration.

## Repository Layout

```
Hardware/    - ESP32 receiver firmware
docs/        - Design chapter and Flutter app specification
flutter_app/ - Flutter mobile application
```

## Author

Dadhichi Sarker Shayon
