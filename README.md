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



## OTA Update

Use `build-and-upload-ota.ps1` for over-the-air updates.## Technical Details

- **MCU / Board:** ESP32 (WROOM-32). Select the appropriate board in Arduino IDE (AI Thinker ESP32-CAM or ESP32 Wrover Module). Enable PSRAM in board settings when using a PSRAM-equipped module.
- **Camera model:** Set by `#define CAMERA_MODEL_*` at the top of `Expressif_cam_wroom32.ino` (default: `CAMERA_MODEL_AI_THINKER`).
- **Camera configuration (from sketch):** `xclk_freq_hz = 20000000` (20 MHz). Live/video mode uses `PIXFORMAT_RGB565` with `FRAMESIZE_QVGA` (320x240) and double buffering (`fb_count = 2`). Photo/save mode uses `PIXFORMAT_JPEG` with `FRAMESIZE_VGA` (640x480), `jpeg_quality` tuned in the sketch, and single-frame capture for saving.
- **PSRAM usage:** The sketch checks `psramFound()` and places frame buffers in PSRAM when available (`CAMERA_FB_IN_PSRAM`). Display and temporary image buffers also prefer PSRAM.
- **TFT display:** ST7735 via `Adafruit_ST7735`. Pins in the sketch: `TFT_SCLK=14`, `TFT_MOSI=13`, `TFT_RST=12`, `TFT_DC=2`, `TFT_CS=15`. Display dimensions configured as `128x160`.
- **SD card:** Uses `SD_MMC` and is mounted on-demand for saves (`SD_MMC.begin("/sdcard", true)` — 1-bit mode). SD is only initialized when saving photos to avoid pin conflicts with the TFT.
- **Controls:** Button on pin `BTN=4` (shared with the onboard flash LED) with an interrupt handler for capture/save.
- **Networking & OTA:** OTA is enabled by `OTA_ENABLED` in the sketch. The sketch configures a static IP with `WiFi.config(...)` (example IP `REDACTED_IP`) and sets the ArduinoOTA hostname to `edge-impulse-esp32-cam`. Update `WIFI_SSID` and `WIFI_PASSWORD` in `Expressif_cam_wroom32.ino` before deployment.
- **Libraries / dependencies:** ESP32 Arduino core (board support), `esp_camera` (and `img_converters.h`), `Adafruit_GFX`, `Adafruit_ST7735`, `SD_MMC`, `ESPmDNS`, `ArduinoOTA`.
- **Notes:**
  - If you change camera model or pinout, update `camera_pins.h` and rebuild.
  - For reliable live video and larger buffers enable PSRAM in board options.
  - The sketch runs a camera task pinned to core 0 and does display work on core 1 (dual-core usage).
## Technical Details

- **MCU / Board:** ESP32 (WROOM-32). Select the appropriate board in Arduino IDE (AI Thinker ESP32-CAM or ESP32 Wrover Module). Enable PSRAM in board settings when using a PSRAM-equipped module.
- **Camera model:** Set by `#define CAMERA_MODEL_*` at the top of `Expressif_cam_wroom32.ino` (default: `CAMERA_MODEL_AI_THINKER`).
- **Camera configuration (from sketch):** `xclk_freq_hz = 20000000` (20 MHz). Live/video mode uses `PIXFORMAT_RGB565` with `FRAMESIZE_QVGA` (320x240) and double buffering (`fb_count = 2`). Photo/save mode uses `PIXFORMAT_JPEG` with `FRAMESIZE_VGA` (640x480), `jpeg_quality` tuned in the sketch, and single-frame capture for saving.
- **PSRAM usage:** The sketch checks `psramFound()` and places frame buffers in PSRAM when available (`CAMERA_FB_IN_PSRAM`). Display and temporary image buffers also prefer PSRAM.
- **TFT display:** ST7735 via `Adafruit_ST7735`. Pins in the sketch: `TFT_SCLK=14`, `TFT_MOSI=13`, `TFT_RST=12`, `TFT_DC=2`, `TFT_CS=15`. Display dimensions configured as `128x160`.
- **SD card:** Uses `SD_MMC` and is mounted on-demand for saves (`SD_MMC.begin("/sdcard", true)` — 1-bit mode). SD is only initialized when saving photos to avoid pin conflicts with the TFT.
- **Controls:** Button on pin `BTN=4` (shared with the onboard flash LED) with an interrupt handler for capture/save.
- **Networking & OTA:** OTA is enabled by `OTA_ENABLED` in the sketch. The sketch configures a static IP with `WiFi.config(...)` (example IP `REDACTED_IP`) and sets the ArduinoOTA hostname to `edge-impulse-esp32-cam`. Update `WIFI_SSID` and `WIFI_PASSWORD` in `Expressif_cam_wroom32.ino` before deployment.
- **Libraries / dependencies:** ESP32 Arduino core (board support), `esp_camera` (and `img_converters.h`), `Adafruit_GFX`, `Adafruit_ST7735`, `SD_MMC`, `ESPmDNS`, `ArduinoOTA`.
- **Notes:**
  - If you change camera model or pinout, update `camera_pins.h` and rebuild.
  - For reliable live video and larger buffers enable PSRAM in board options.
  - The sketch runs a camera task pinned to core 0 and does display work on core 1 (dual-core usage).
