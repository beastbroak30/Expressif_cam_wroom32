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

## OTA Update

Use `build-and-upload-ota.ps1` for over-the-air updates.