# ESP32 Camera Project

This repository contains an Arduino project for the ESP32 WROOM-32 module with camera functionality.

## Files

- `Expressif_cam_wroom32.ino`: Main Arduino sketch
- `camera_pins.h`: Camera pin definitions
- `build-and-upload-ota.ps1`: PowerShell script for building and uploading via OTA
- `build/`: Build directory

## Setup

1. Install Arduino IDE with ESP32 board support.
2. Configure camera pins in `camera_pins.h`.
3. Build and upload using the provided script.

## Camera Pinouts

The project supports multiple camera models defined in `camera_pins.h`. Default configuration is for AI Thinker ESP32-CAM:

- PWDN: GPIO 32
- RESET: -1 (not used)
- XCLK: GPIO 0
- SIOD: GPIO 26
- SIOC: GPIO 27
- Y9: GPIO 35
- Y8: GPIO 34
- Y7: GPIO 39
- Y6: GPIO 36
- Y5: GPIO 21
- Y4: GPIO 19
- Y3: GPIO 18
- Y2: GPIO 5
- VSYNC: GPIO 25
- HREF: GPIO 23
- PCLK: GPIO 22

For other models (WROVER_KIT, ESP_EYE, M5STACK variants, TTGO_T_JOURNAL), uncomment the corresponding #define in `camera_pins.h`.

## OTA Update

Use `build-and-upload-ota.ps1` for over-the-air updates.