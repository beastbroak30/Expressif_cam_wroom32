#ifndef CONFIG_H
#define CONFIG_H

// ============================================
// ESP32-CAM Configuration
// Central config file for all hardware pins and constants
// ============================================

// === Camera Model ===
#define CAMERA_MODEL_AI_THINKER

// === TFT Display Pins (ST7735) ===
#define TFT_SCLK 14   // SCL
#define TFT_MOSI 13   // SDA
#define TFT_RST  12   // RES (RESET)
#define TFT_DC    2   // Data Command control pin
#define TFT_CS   15   // Chip select control pin
                      // BL (back light) and VCC -> 3V3

// === Button (shared with flash LED) ===
#define BTN       4

// === Display Dimensions ===
#define DISPLAY_WIDTH  128
#define DISPLAY_HEIGHT 160

// === Camera Resolutions ===
// Live video: QVGA (320x240)
#define CAMERA_WIDTH   320
#define CAMERA_HEIGHT  240

// Downsampled for TFT with 90° CW rotation
#define DOWNSAMPLE_WIDTH  160   // Pre-rotation intermediate width
#define DOWNSAMPLE_HEIGHT 120   // Pre-rotation intermediate height
#define DISPLAY_SCALED_WIDTH  120  // After rotation: fits 128-wide display
#define DISPLAY_SCALED_HEIGHT 160  // After rotation: fills 160-tall display
#define DISPLAY_X_OFFSET      4    // Center horizontally: (128 - 120) / 2
#define DISPLAY_Y_OFFSET      0    // No vertical offset

// Photo: SXGA (1280x1024)
#define PHOTO_WIDTH    1280
#define PHOTO_HEIGHT   1024

// === Boot Log Settings ===
#define BOOT_LINE_HEIGHT 10
#define BOOT_START_Y     4
#define BOOT_START_X     2

// === OTA Settings ===
constexpr bool OTA_ENABLED = true;
#define OTA_HOSTNAME "KANCAM"

// === Operating Modes ===
constexpr bool LIVE_VIDEO_MODE = true;

// === Image Rotation ===
// 0 = No rotation, 1 = 90° CW, 2 = 180°, 3 = 270° (90° CCW)
constexpr uint8_t IMAGE_ROTATION = 1;  // User configurable: 0-3

#endif // CONFIG_H
