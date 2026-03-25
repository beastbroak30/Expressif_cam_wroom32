# ESP32-CAM with ST7735 TFT Display

ESP32-CAM project with live video display on ST7735 TFT, photo capture to SD card, and OTA firmware updates. Features modular architecture with improved color accuracy and intelligent photo numbering.

## 📁 Project Structure

```
Expressif_cam_wroom32/
├── Expressif_cam_wroom32.ino    # Main sketch with setup/loop/display logic
├── camera_pins.h                # GPIO pin definitions for AI-Thinker ESP32-CAM
├── camera_settings.h            # Camera sensor configuration module (color fixes)
├── sd_card_handler.h            # SD card operations and smart photo naming
├── ota_handler.h                # WiFi and OTA update management (ready for use)
├── build-and-upload-ota.ps1     # PowerShell script for WSL build + OTA upload
├── .gitignore                   # Excludes build/ and binary files
├── .github/
│   └── workflows/
│       ├── ota-compile.yml      # GitHub Actions: Build firmware
│       └── ota-flash.yml        # GitHub Actions: OTA upload
└── build/                       # Build artifacts (ignored by git)
    ├── *.bin                    # Compiled firmware binaries
    ├── *.elf                    # Executable and linkable format files
    └── *.map                    # Memory map files
```

## ✨ Features

- **Live Video Feed**: RGB565 QVGA (320×240) direct to TFT display @ ~15-20 FPS
- **Photo Capture**: VGA (640×480) JPEG photos saved to SD card
- **Color Accuracy**: Fixed greenish tint issue with proper AWB settings and saturation tuning
- **Smart Photo Numbering**: Scans SD card on each save to prevent overwriting existing photos
- **OTA Updates**: Over-the-air firmware updates via WiFi
- **Modular Design**: Clean separation of concerns (camera, SD card, OTA, display)
- **Dual-Core Processing**: Camera task on Core 0, display/main loop on Core 1
- **Boot Sequence Display**: Linux-style boot log on TFT with color-coded status
- **Persistent Storage Clearing**: Clears NVS/EEPROM on every boot for fresh state

## 🧩 Module Architecture

### `camera_settings.h`
Centralized camera sensor configuration with color accuracy improvements:
- `configureSensorForLiveView()` - Optimized for RGB565 live display
- `configureSensorForPhotoCapture()` - **Fixes greenish tint** (saturation=0, proper AWB/AEC)
- `waitForAutoSettingsToStabilize()` - Discards 5 frames for white balance convergence

### `sd_card_handler.h`
Smart SD card management with automatic file numbering:
- `begin()` / `end()` - Mount/unmount SD card (1-bit mode, pin-safe)
- `findNextPhotoNumber()` - Scans existing photos, finds next available number
- `updateCounterFromExistingPhotos()` - Sets counter to prevent overwrites
- `captureAndSave()` - Switches camera to JPEG mode, captures, saves with correct colors
- `savePhoto()` - Ensures unique filename even if counter is wrong

### `ota_handler.h`
WiFi and OTA update management (ready for future integration):
- `begin()` - Connects WiFi, initializes mDNS and ArduinoOTA
- `handle()` - Processes OTA requests (call in loop)
- `showOTAProgress()` - Visual feedback on TFT during updates

### `camera_pins.h`
GPIO pin definitions for AI-Thinker ESP32-CAM (standard pinout)

### `Expressif_cam_wroom32.ino`
Main application logic:
- Setup: Display init, camera init, OTA configuration, boot logging
- Loop: Button handling, live video display, photo save requests
- Helper functions: TFT drawing, status boxes, boot log system

## Setup

### Hardware Requirements
- **ESP32-CAM** (AI-Thinker model with OV2640 camera)
- **ST7735 TFT Display** (128×160 pixels, SPI interface)
- **MicroSD Card** (FAT32 formatted)
- **Push Button** (connected to GPIO 4)
- **USB-to-Serial Adapter** (for initial upload)

### Pin Connections

**TFT Display (ST7735):**
- SCLK → GPIO 14
- MOSI (SDA) → GPIO 13
- RST → GPIO 12
- DC → GPIO 2
- CS → GPIO 15
- VCC → 3.3V
- GND → GND

**Button:**
- One side → GPIO 4
- Other side → GND
- Note: GPIO 4 is shared with onboard flash LED

**SD Card:**
- Uses SD_MMC interface (shares pins with TFT)
- Mounted on-demand only during photo save

### Software Setup

1. **Install Arduino IDE** (1.8.19 or newer) or Arduino CLI
2. **Add ESP32 Board Support:**
   - File → Preferences → Additional Board Manager URLs
   - Add: `https://raw.githubusercontent.com/espressif/arduino-esp32/gh-pages/package_esp32_index.json`
   - Tools → Board → Boards Manager → Search "ESP32" → Install
3. **Install Required Libraries:**
   - Adafruit GFX Library
   - Adafruit ST7735 Library
   - (ESP32 camera libraries are built-in)
4. **Configure WiFi Credentials:**
   - Edit `Expressif_cam_wroom32.ino`
   - Set `WIFI_SSID` and `WIFI_PASSWORD`
   - Optionally adjust `WIFI_LOCAL_IP`
5. **Board Settings:**
   - Board: "AI Thinker ESP32-CAM"
   - Partition Scheme: "Minimal SPIFFS (Large APPS with OTA)"
   - PSRAM: "Enabled"
   - Upload Speed: 921600

### Building and Uploading

**Option 1: PowerShell Script (Recommended for OTA)**
```powershell
.\build-and-upload-ota.ps1
```

**Option 2: Arduino IDE**
- Connect ESP32-CAM via USB-to-Serial adapter
- Select correct COM port
- Press upload button

**Option 3: Arduino CLI (WSL/Linux)**
```bash
arduino-cli compile --fqbn esp32:esp32:esp32cam --board-options PartitionScheme=min_spiffs .
arduino-cli upload -p /dev/ttyUSB0 --fqbn esp32:esp32:esp32cam .
```

## 📸 Usage

1. **Power On**: Boot sequence displays on TFT
2. **Live View**: Camera feed displays automatically
3. **Take Photo**: Press button on GPIO 4
4. **Status**: "Saving to SDCARD..." → "Saved #XXXX"
5. **Photos**: Saved as `/photo_0000.jpg`, `/photo_0001.jpg`, etc.

## Camera Pinouts (AI-Thinker)

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

## 🔄 OTA Updates

Over-the-air firmware updates allow uploading new code without USB connection.

### Using PowerShell Script
```powershell
# Build and upload via OTA (default target: 192.168.1.51:3232)
.\build-and-upload-ota.ps1

# Use custom IP address
.\build-and-upload-ota.ps1 -TargetIp 192.168.1.100

# Build only (skip upload)
.\build-and-upload-ota.ps1 -SkipUpload

# USB upload (refreshes partition table)
.\build-and-upload-ota.ps1 -UsbUpload -UsbPort /dev/ttyUSB0
```

### OTA Configuration
- **Hostname**: `edge-impulse-esp32-cam`
- **Default IP**: `192.168.1.51`
- **Port**: `3232`
- Edit WiFi credentials in `Expressif_cam_wroom32.ino` before first upload

## 🔧 Technical Details

### Camera Configuration
- **Sensor**: OV2640 (AI-Thinker ESP32-CAM)
- **Clock**: 20 MHz XCLK
- **Live View**: RGB565, QVGA (320×240), 2 frame buffers, ~15-20 FPS
- **Photo Capture**: JPEG, VGA (640×480), quality=10
- **Color Fix**: Saturation=0, AWB enabled, 5-frame stabilization period

### Display
- **Model**: ST7735 TFT
- **Resolution**: 128×160 pixels
- **Interface**: SPI (hardware)
- **Initialization**: `INITR_GREENTAB` mode
- **Boot Display**: Custom Linux-style boot sequence with color-coded status

### Storage
- **SD Card**: SD_MMC (1-bit mode for pin compatibility)
- **File System**: FAT32
- **Photo Format**: JPEG with sequential numbering
- **Smart Naming**: Scans existing files, prevents overwrites
- **On-Demand Mount**: SD initialized only during photo save to avoid pin conflicts

### Memory Management
- **PSRAM**: Utilized for frame buffers when available
- **NVS/EEPROM**: Cleared on every boot (fresh state)
- **Photo Counter**: Reset to 0 on boot, updated by scanning SD card

### Architecture
- **Dual-Core**: Core 0 (camera task), Core 1 (main loop, display, SD operations)
- **Modular Design**: Separated camera settings, SD handling, and OTA logic
- **Thread-Safe**: Mutex protection for shared frame buffer
- **Interrupt-Driven**: Button ISR with debouncing (200ms)

### Network
- **WiFi**: Static IP configuration
- **OTA**: ArduinoOTA with password protection (optional)
- **mDNS**: Hostname resolution for easy discovery
- **Default Settings**:
  - SSID: `BSNLFTTH63 2.4`
  - IP: `192.168.1.51`
  - Gateway: `192.168.1.1`
  - Subnet: `255.255.255.0`

### Dependencies
- ESP32 Arduino Core (2.0.0+)
- Adafruit GFX Library
- Adafruit ST7735 Library
- Built-in: `esp_camera`, `SD_MMC`, `WiFi`, `ESPmDNS`, `ArduinoOTA`, `Preferences`, `nvs_flash`

## 🐛 Troubleshooting

**Greenish tint in saved photos?**
- Fixed in v2.0 with `camera_settings.h` module
- Saturation set to 0 (natural) instead of 1 (oversaturated)
- 5-frame AWB stabilization period

**Photos overwriting existing files?**
- Fixed with smart photo numbering in `sd_card_handler.h`
- SD card is scanned before each save
- Counter automatically set to next available number

**OTA upload fails?**
- Check WiFi credentials in `Expressif_cam_wroom32.ino`
- Verify ESP32 is on same network
- Confirm IP address matches target
- Check firewall settings (port 3232)

**SD card not detected?**
- Ensure FAT32 formatting
- Check SD card is inserted before taking photo
- SD is mounted on-demand, not at boot

**Display shows garbled colors?**
- Verify TFT initialization mode (`INITR_GREENTAB`)
- Check SPI pin connections
- Ensure 3.3V power supply (not 5V)

## 📝 License

This project is open source. Feel free to modify and distribute.

## 🙏 Acknowledgments

- Random Nerd Tutorials for ESP32-CAM examples
- Adafruit for GFX and ST7735 libraries
- ESP32 Arduino community
