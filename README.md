# Expressif\_cam\_wroom32
 
> ESP32-CAM firmware with a fully automated GitHub Actions CI/CD pipeline — compile in the cloud, flash over-the-air via Raspberry Pi Zero 2W.
 
---

## Hardware

| Component | Detail |
|---|---|
| MCU | ESP32-CAM (AI Thinker) |
| Camera (included in board) | OV2640 |
| button | Tactile Button |
| Display | ST7735 TFT 128×160 |
| Storage | MicroSD via SD\_MMC |
| OTA Flasher | Raspberry Pi Zero 2W (on same LAN) |

### Button Configuration

A capture button is connected to **GPIO 4** with a pull-down resistor configuration:

```
Button Pin[1] ──── 3V3            Button Pin[2] ──── 10kΩ ──── GPIO 4 
                                      │
                                     10kΩ 
                                      │
                                      GND
```

Pressing the button triggers photo capture and stores the image to the SD card.

---

## Pipeline Architecture

```
Git Push  →  GitHub-hosted runner (ubuntu-latest)
                    │
              arduino-cli compile
              secrets injected at build time
                    │
         ┌──────────┴──────────┐
      (ota)                 (flash)
         │                     │
   Version bump            No release
   GitHub Release           dev-SHA label
         │                     │
         └──────────┬──────────┘
                    │
          RPi Zero 2W (self-hosted runner)
          downloads binary via GitHub artifact
                    │
              espota.py → ESP32
              192.168.1.xx:3232
              retry after 1h if failed
```

---

## Commit Triggers

| Commit message contains | What happens |
|---|---|
| `(ota)` | Compile → version bump → GitHub Release → flash to ESP32 |
| `(flash)` | Compile → flash to ESP32 — no release, no version change |
| anything else | Nothing, regular commit |

### Examples

```bash
# Full OTA release pipeline
git commit -m "improve motion detection (ota)"

# Quick flash, no release
git commit -m "tweak exposure (flash)"

# Normal commit, no flash
git commit -m "update readme"
```

---

## Versioning

Versions follow `MAJOR.MINOR.0` and are auto-incremented on every `(ota)` commit stored in `version.txt`.

```
v1.0.0 → v1.1.0 → ... → v1.49.0 → v2.0.0 → ...
```

Every `(ota)` release is permanently stored as a GitHub Release with the `.bin` attached — roll back anytime by running the manual flash workflow with a specific version number.

---

## Retry Logic

If the OTA flash fails (ESP32 offline, network blip), the runner waits **1 hour** and retries once automatically. If the second attempt also fails, the job is marked failed and GitHub notifies you.

---

## Rollback

Go to **Actions → OTA Flash via RPi → Run workflow** and enter the version to rollback to (e.g. `1.3.0`). Leave blank to re-flash the latest release.

---

## Secrets Setup

Go to **Settings → Secrets and variables → Actions** and add:

| Secret | Value |
|---|---|
| `WIFI_SSID` | Your WiFi network name |
| `WIFI_PASSWORD` | Your WiFi password |
| `ESP32_IP` | Static IP of the ESP32 (e.g. `192.168.1.xx`) |
| `ESP32_OTA_PORT` | OTA port (default `3232`) |

Credentials are injected into a `secrets.h` header at compile time and never stored in the repository.

---

## Local Development Setup

**1. Create `Expressif_cam_wroom32/secrets.h`** (never commit this):

```cpp
#pragma once
#define WIFI_SSID     "your_wifi_name"
#define WIFI_PASSWORD "your_wifi_password"
```

**2. Verify `.gitignore` contains:**

```
Expressif_cam_wroom32/secrets.h
Expressif_cam_wroom32/build/
```

**3. Board settings in Arduino IDE:**

| Setting | Value |
|---|---|
| Board | AI Thinker ESP32-CAM |
| Partition Scheme | Minimal SPIFFS |
| PSRAM | Enabled |

---

## Raspberry Pi Zero 2W Runner Setup

Run once on the RPi:

```bash
# Install arduino-cli (needed for espota.py)
curl -fsSL https://raw.githubusercontent.com/arduino/arduino-cli/master/install.sh | sh
sudo mv ~/bin/arduino-cli /usr/local/bin/

# Install ESP32 core to get espota.py
arduino-cli core update-index \
  --additional-urls https://raw.githubusercontent.com/espressif/arduino-esp32/gh-pages/package_esp32_index.json
arduino-cli core install esp32:esp32 \
  --additional-urls https://raw.githubusercontent.com/espressif/arduino-esp32/gh-pages/package_esp32_index.json

# Register as GitHub Actions self-hosted runner
# Get your token from: repo → Settings → Actions → Runners → New self-hosted runner
mkdir ~/actions-runner && cd ~/actions-runner
curl -o runner.tar.gz -L \ 
  https://github.com/actions/runner/releases/download/v2.333.0/actions-runner-linux-arm64-2.333.0.tar.gz
tar xzf runner.tar.gz
./config.sh --url https://github.com/beastbroak30/Expressif_cam_wroom32 --token YOUR_TOKEN

# Install as a permanent systemd service (survives reboots)
sudo ./svc.sh install
sudo ./svc.sh start
```

The runner registration token is only used once during `./config.sh`. After `svc.sh install`, the runner starts automatically on every reboot — no further setup needed.

---

## Workflow Files

| File | Runs on | Purpose |
|---|---|---|
| `.github/workflows/ota-compile.yml` | GitHub-hosted (ubuntu-latest) | Compile, release, and trigger flash |
| `.github/workflows/ota-flash.yml` | RPi Zero 2W (self-hosted) | Manual rollback flash |

---

## First-Time USB Flash

OTA requires the correct partition table to already be on the device. Flash via USB once to set it up:

```powershell
.\build-and-upload-ota.ps1 -UsbUpload -UsbPort "COM3"
```

After this, all subsequent updates can be done via OTA.

---

## Project Structure

```
Expressif_cam_wroom32/
├── Expressif_cam_wroom32.ino   # Main sketch
├── camera_pins.h               # GPIO pin definitions
├── camera_settings.h           # Sensor config
├── sd_card_handler.h           # SD card operations
├── ota_handler.h               # WiFi and OTA management
├── secrets.h                   # LOCAL ONLY — never committed
├── build/                      # LOCAL ONLY — never committed
├── .github/
│   └── workflows/
│       ├── ota-compile.yml     # CI compile + release + flash
│       └── ota-flash.yml       # Manual rollback
├── version.txt                 # Auto-managed by CI
├── build-and-upload-ota.ps1    # Legacy PowerShell OTA script
└── .gitignore
```

---

## Camera Pin Reference (AI Thinker ESP32-CAM)

| Signal | GPIO |
|---|---|
| PWDN | 32 |
| XCLK | 0 |
| SIOD | 26 |
| SIOC | 27 |
| VSYNC | 25 |
| HREF | 23 |
| PCLK | 22 |
| Y9–Y2 | 35, 34, 39, 36, 21, 19, 18, 5 |

---

## TFT Display Pins

| Signal | GPIO |
|---|---|
| SCLK | 14 |
| MOSI | 13 |
| RST | 12 |
| DC | 2 |
| CS | 15 |

---

## Dependencies

- `esp_camera` + `img_converters`
- `Adafruit_GFX`
- `Adafruit_ST7735`
- `SD_MMC`
- `ArduinoOTA`
- `ESPmDNS`

---

*Built by [@beastbroak30](https://github.com/beastbroak30)*
