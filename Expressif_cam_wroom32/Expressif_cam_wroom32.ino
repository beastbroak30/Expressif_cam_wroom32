/*
  Simple ESP32-CAM Photo Capture with ST7735 TFT Display
  - VGA Resolution (640x480) - best balance for ESP32-CAM
  - Button triggered capture
  - Flash white effect before showing photo
  - Linux-style boot sequence display
  - Modular architecture with improved color accuracy
*/
// powershell -ExecutionPolicy Bypass -File build-and-upload-ota.ps1
#define CAMERA_MODEL_AI_THINKER

#include "img_converters.h"
#include "esp_camera.h"
#include "camera_pins.h"
#include "SD_MMC.h"  // SD card - temporarily enabled during save only
#include "FS.h"      // File system for SD card
#include <WiFi.h>
#include <ESPmDNS.h>
#include <ArduinoOTA.h>
#include <Preferences.h>  // For NVS storage management
#include <nvs_flash.h>    // For low-level NVS operations

// Custom modules for better organization and improved color handling
#include "camera_settings.h"
#include "sd_card_handler.h"

#include <Adafruit_GFX.h>    // Core graphics library
#include <Adafruit_ST7735.h> // Hardware-specific library for ST7735

#define TFT_SCLK 14 // SCL
#define TFT_MOSI 13 // SDA
#define TFT_RST  12 // RES (RESET)
#define TFT_DC    2 // Data Command control pin
#define TFT_CS   15 // Chip select control pin
                    // BL (back light) and VCC -> 3V3

#define BTN       4 // button (shared with flash led)

// Display dimensions - adjust to your ST7735 variant
// Common sizes: 128x128, 128x160, 160x128
#define DISPLAY_WIDTH  128
#define DISPLAY_HEIGHT 160

// For live video: QVGA (320x240) displayed on 128x160 TFT
// We'll crop to fit the display
#define CAMERA_WIDTH   320
#define CAMERA_HEIGHT  240

// For photo mode: VGA (640x480) / 4 = 160x120 after JPEG decode
#define SCALED_WIDTH   160
#define SCALED_HEIGHT  120

// Live video mode settings
constexpr bool LIVE_VIDEO_MODE = true;  // true = live feed, false = button capture only
bool sdCardMounted = false;
volatile bool freezeFrame = false;  // When true, freeze the current frame
unsigned long lastFrameTime = 0;
unsigned long frameCount = 0;
float currentFps = 0.0f;

// Dual-core processing
TaskHandle_t cameraTaskHandle = NULL;
volatile bool newFrameReady = false;
volatile bool frameBeingDisplayed = false;
volatile bool cameraPaused = false;  // Pause camera task during SD operations
uint16_t *displayBuffer = NULL;  // Pre-processed frame buffer for display
SemaphoreHandle_t frameMutex = NULL;

// Photo saving
unsigned int photoCounter = 0;
volatile bool saveRequested = false;  // Flag to request photo save from main loop

// Boot log settings
#define BOOT_LINE_HEIGHT 10
#define BOOT_START_Y     4
#define BOOT_START_X     2

constexpr bool OTA_ENABLED = true;
const char* WIFI_SSID = "REDACTED_SSID ";
const char* WIFI_PASSWORD = "REDACTED_PASSWORD ";
const char* OTA_HOSTNAME = "edge-impulse-esp32-cam";
IPAddress WIFI_LOCAL_IP(192, 168, 1, 51);
IPAddress WIFI_GATEWAY(192, 168, 1, 1);
IPAddress WIFI_SUBNET(255, 255, 255, 0);

Adafruit_ST7735 tft = Adafruit_ST7735(TFT_CS, TFT_DC, TFT_MOSI, TFT_SCLK, TFT_RST);

volatile bool photoDisplayed = false;
volatile bool buttonPressed = false;
volatile bool buttonInterruptFlag = false;  // Set by ISR, cleared by main loop
unsigned int otaLastPercentShown = 101;
unsigned long lastButtonTime = 0;  // For debouncing

// Button interrupt handler (IRAM for speed)
void IRAM_ATTR buttonISR() {
  unsigned long now = millis();
  if (now - lastButtonTime > 200) {  // 200ms debounce
    buttonInterruptFlag = true;
    lastButtonTime = now;
  }
}

// RGB565 buffer for live video (allocated in setup)
uint8_t *videoBuffer = NULL;

// Boot sequence variables
int bootLine = 0;

// Function declarations
void clearPersistentStorage();
void bootLog(const char* message, bool newLine = true);
void bootLogStatus(const char* status, uint16_t color);
void bootLogOK();
void bootLogFAIL();
void bootLogValue(const char* label, int value, const char* unit = "");
void tft_drawtext(int16_t x, int16_t y, const char* text, uint8_t font_size, uint16_t color);
void setupOTA();
void showOTAStatus(const char* line1, const char* line2, uint16_t color);
void showOTAProgress(unsigned int progress, unsigned int total);
const char* otaErrorToText(ota_error_t error);
void displayLiveFrame();
void showCameraReady();

// Clear all persistent storage (NVS/Preferences/EEPROM)
// This ensures clean state on every boot
void clearPersistentStorage() {
  Serial.println("Clearing persistent storage (NVS)...");
  
  // Method 1: Clear Arduino Preferences namespace
  Preferences prefs;
  prefs.begin("camera", false);  // Open in read/write mode
  prefs.clear();                  // Clear all keys in this namespace
  prefs.end();
  
  // Method 2: Erase entire NVS partition (nuclear option)
  // This clears ALL NVS data including WiFi credentials if stored
  esp_err_t err = nvs_flash_erase();
  if (err == ESP_OK) {
    Serial.println("NVS flash erased successfully");
    // Re-initialize NVS after erase
    err = nvs_flash_init();
    if (err == ESP_OK) {
      Serial.println("NVS re-initialized");
    } else {
      Serial.printf("NVS re-init failed: %d\n", err);
    }
  } else {
    Serial.printf("NVS erase failed: %d\n", err);
  }
  
  Serial.println("Persistent storage cleared - starting fresh");
}

// Boot log: print message (always moves to next line for display)
void bootLog(const char* message, bool newLine) {
  int y = BOOT_START_Y + (bootLine * BOOT_LINE_HEIGHT);
  
  // Scroll if needed
  if (y > DISPLAY_HEIGHT - BOOT_LINE_HEIGHT) {
    tft.fillScreen(ST77XX_BLACK);
    bootLine = 0;
    y = BOOT_START_Y;
  }
  
  tft.setCursor(BOOT_START_X, y);
  tft.setTextSize(1);
  tft.setTextColor(ST77XX_WHITE);
  tft.print(message);
  
  Serial.print(message);
  
  // Always increment bootLine for display (newLine only affects Serial)
  bootLine++;
  
  if (newLine) {
    Serial.println();
  }
}

// Boot log: print status at end of line
void bootLogStatus(const char* status, uint16_t color) {
  // Calculate position for right-aligned status
  int statusLen = strlen(status);
  int x = DISPLAY_WIDTH - (statusLen * 6) - 2;  // 6 pixels per char at size 1
  int y = BOOT_START_Y + ((bootLine - 1) * BOOT_LINE_HEIGHT);
  
  // If bootLine was just incremented, we need to go back one line
  if (bootLine > 0) {
    y = BOOT_START_Y + ((bootLine - 1) * BOOT_LINE_HEIGHT);
  }
  
  tft.setCursor(x, y);
  tft.setTextColor(color);
  tft.print(status);
  
  Serial.print(" ");
  Serial.println(status);
}

// Boot log: OK status
void bootLogOK() {
  // Print on same line as previous message
  int y = BOOT_START_Y + ((bootLine - 1) * BOOT_LINE_HEIGHT);
  int x = DISPLAY_WIDTH - (4 * 6) - 2;  // "[ OK ]" but we'll use "[OK]"
  
  tft.setCursor(x, y);
  tft.setTextColor(ST77XX_GREEN);
  tft.print("[OK]");
  
  Serial.println("[OK]");
  delay(100);  // Brief pause to see status
}

// Boot log: FAIL status
void bootLogFAIL() {
  int y = BOOT_START_Y + ((bootLine - 1) * BOOT_LINE_HEIGHT);
  int x = DISPLAY_WIDTH - (6 * 6) - 2;
  
  tft.setCursor(x, y);
  tft.setTextColor(ST77XX_RED);
  tft.print("[FAIL]");
  
  Serial.println("[FAIL]");
  delay(500);
}

// Boot log: print a value
void bootLogValue(const char* label, int value, const char* unit) {
  int y = BOOT_START_Y + (bootLine * BOOT_LINE_HEIGHT);
  
  if (y > DISPLAY_HEIGHT - BOOT_LINE_HEIGHT) {
    tft.fillScreen(ST77XX_BLACK);
    bootLine = 0;
    y = BOOT_START_Y;
  }
  
  tft.setCursor(BOOT_START_X, y);
  tft.setTextSize(1);
  tft.setTextColor(ST77XX_CYAN);
  tft.print(label);
  tft.setTextColor(ST77XX_YELLOW);
  tft.print(value);
  tft.setTextColor(ST77XX_CYAN);
  tft.print(unit);
  
  Serial.print(label);
  Serial.print(value);
  Serial.println(unit);
  
  bootLine++;
  delay(50);
}

void showOTAStatus(const char* line1, const char* line2, uint16_t color) {
  tft.fillScreen(ST77XX_BLACK);
  tft_drawtext(16, 52, "OTA Update", 1, ST77XX_WHITE);
  tft_drawtext(8, 76, line1, 1, color);
  if (line2 && line2[0] != '\0') {
    tft_drawtext(8, 96, line2, 1, ST77XX_CYAN);
  }
}

void showOTAProgress(unsigned int progress, unsigned int total) {
  if (total == 0) {
    return;
  }

  unsigned int percent = (progress * 100U) / total;
  if (percent == otaLastPercentShown) {
    return;
  }

  otaLastPercentShown = percent;
  char percentText[20];
  snprintf(percentText, sizeof(percentText), "Progress: %u%%", percent);

  const int barX = 8;
  const int barY = 120;
  const int barWidth = DISPLAY_WIDTH - 16;
  const int barHeight = 12;
  int filled = (barWidth * static_cast<int>(percent)) / 100;

  tft.fillRect(0, 72, DISPLAY_WIDTH, 40, ST77XX_BLACK);
  tft_drawtext(8, 76, "Receiving firmware", 1, ST77XX_YELLOW);
  tft_drawtext(8, 96, percentText, 1, ST77XX_CYAN);
  tft.drawRect(barX, barY, barWidth, barHeight, ST77XX_WHITE);
  tft.fillRect(barX + 1, barY + 1, barWidth - 2, barHeight - 2, ST77XX_BLACK);
  if (filled > 2) {
    tft.fillRect(barX + 1, barY + 1, filled - 2, barHeight - 2, ST77XX_GREEN);
  }
}

const char* otaErrorToText(ota_error_t error) {
  switch (error) {
    case OTA_AUTH_ERROR:
      return "Auth Failed";
    case OTA_BEGIN_ERROR:
      return "Begin Failed";
    case OTA_CONNECT_ERROR:
      return "Connect Failed";
    case OTA_RECEIVE_ERROR:
      return "Receive Failed";
    case OTA_END_ERROR:
      return "End Failed";
    default:
      return "Unknown Error";
  }
}

void setupOTA() {
  bootLog("WiFi init...", false);
  WiFi.mode(WIFI_STA);

  if (!WiFi.config(WIFI_LOCAL_IP, WIFI_GATEWAY, WIFI_SUBNET)) {
    bootLogFAIL();
    bootLog("WiFi config failed");
    return;
  }

  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

  unsigned long startMs = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - startMs < 15000) {
    delay(500);
    Serial.print('.');
  }

  if (WiFi.status() != WL_CONNECTED) {
    bootLogFAIL();
    bootLog("WiFi connect timeout");
    WiFi.disconnect(true, true);
    WiFi.mode(WIFI_OFF);
    return;
  }

  bootLogOK();
  Serial.print("WiFi ready: ");
  Serial.println(WiFi.localIP());

  if (!MDNS.begin(OTA_HOSTNAME)) {
    bootLog("mDNS failed");
  }

  bootLog("OTA init...", false);
  ArduinoOTA.setHostname(OTA_HOSTNAME);
  ArduinoOTA.setTimeout(20000);
  ArduinoOTA.onStart([]() {
    otaLastPercentShown = 101;
    showOTAStatus("Starting update", "Preparing flash", ST77XX_GREEN);
    Serial.println("OTA start");
  });
  ArduinoOTA.onEnd([]() {
    showOTAStatus("Update complete", "Rebooting...", ST77XX_GREEN);
    Serial.println("\nOTA end");
  });
  ArduinoOTA.onProgress([](unsigned int progress, unsigned int total) {
    showOTAProgress(progress, total);
    Serial.printf("OTA Progress: %u%%\r", (progress * 100U) / total);
  });
  ArduinoOTA.onError([](ota_error_t error) {
    char errorText[24];
    snprintf(errorText, sizeof(errorText), "Error code: %u", error);
    showOTAStatus(otaErrorToText(error), errorText, ST77XX_RED);
    Serial.printf("OTA Error[%u]: %s\n", error, otaErrorToText(error));
  });
  ArduinoOTA.begin();
  bootLogOK();
  bootLog("IP: REDACTED_IP");
  bootLog("OTA: edge-impulse-esp32-cam");
}

// setup
void setup() {
  Serial.begin(115200);
  delay(100);
  Serial.println("\n\n");
  
  // ===== STEP 0: Clear any persistent storage (NVS/EEPROM) =====
  // This ensures photo counter and any other data starts fresh on every boot
  clearPersistentStorage();
  
  // ===== STEP 1: Initialize Display FIRST =====
  tft.initR(INITR_GREENTAB);
  tft.setRotation(0);
  tft.fillScreen(ST77XX_BLACK);
  
  // Show boot header
  tft.fillRect(0, 0, DISPLAY_WIDTH, 14, ST77XX_BLUE);
  tft_drawtext(8, 3, "ESP32-CAM Boot", 1, ST77XX_WHITE);
  bootLine = 2;  // Start below header
  
  delay(300);
  
  // ===== STEP 2: Display init confirmation =====
  bootLog("TFT Display...", false);
  delay(100);
  bootLogOK();
  
  // ===== STEP 3: Serial init =====
  bootLog("Serial 115200...", false);
  delay(100);
  bootLogOK();
  
  // ===== STEP 4: GPIO init =====
  bootLog("GPIO Init...", false);
  pinMode(BTN, INPUT);
  // Attach interrupt for responsive button handling
  attachInterrupt(digitalPinToInterrupt(BTN), buttonISR, RISING);
  delay(100);
  bootLogOK();
  
  // ===== STEP 5: Check PSRAM =====
  bootLog("Checking PSRAM...", false);
  delay(200);
  bool hasPSRAM = psramFound();
  if (hasPSRAM) {
    bootLogOK();
    bootLogValue("  PSRAM: ", ESP.getPsramSize() / 1024, " KB");
  } else {
    int y = BOOT_START_Y + ((bootLine - 1) * BOOT_LINE_HEIGHT);
    int x = DISPLAY_WIDTH - (6 * 6) - 2;
    tft.setCursor(x, y);
    tft.setTextColor(ST77XX_YELLOW);
    tft.print("[NONE]");
    Serial.println("[NONE]");
    bootLine++;
  }
  
  // ===== STEP 6: Show memory info =====
  bootLogValue("  Heap: ", ESP.getFreeHeap() / 1024, " KB");
  
  // ===== STEP 7: Configure Camera =====
  bootLog("Camera config...", false);
  delay(100);
  
  camera_config_t config;
  config.ledc_channel = LEDC_CHANNEL_0;
  config.ledc_timer = LEDC_TIMER_0;
  config.pin_d0 = Y2_GPIO_NUM;
  config.pin_d1 = Y3_GPIO_NUM;
  config.pin_d2 = Y4_GPIO_NUM;
  config.pin_d3 = Y5_GPIO_NUM;
  config.pin_d4 = Y6_GPIO_NUM;
  config.pin_d5 = Y7_GPIO_NUM;
  config.pin_d6 = Y8_GPIO_NUM;
  config.pin_d7 = Y9_GPIO_NUM;
  config.pin_xclk = XCLK_GPIO_NUM;
  config.pin_pclk = PCLK_GPIO_NUM;
  config.pin_vsync = VSYNC_GPIO_NUM;
  config.pin_href = HREF_GPIO_NUM;
  config.pin_sscb_sda = SIOD_GPIO_NUM;
  config.pin_sscb_scl = SIOC_GPIO_NUM;
  config.pin_pwdn = PWDN_GPIO_NUM;
  config.pin_reset = RESET_GPIO_NUM;
  config.xclk_freq_hz = 24000000;  // Increased from 20MHz to 24MHz for faster operation
  
  // Live video: RGB565 for direct display (no JPEG decode needed)
  // QVGA (320x240) fits well on 128x160 TFT
  if (LIVE_VIDEO_MODE) {
    config.pixel_format = PIXFORMAT_RGB565;
    config.frame_size = FRAMESIZE_QVGA;  // 320x240
    config.jpeg_quality = 12;  // Not used for RGB565
    config.fb_count = 2;  // Double buffer for smooth video
    config.grab_mode = CAMERA_GRAB_LATEST;  // Always get newest frame
  } else {
    // Photo capture mode: JPEG for quality
    config.pixel_format = PIXFORMAT_JPEG;
    config.frame_size = FRAMESIZE_VGA;
    config.jpeg_quality = 8;  // Improved from 10 to 8 for better quality (lower = better)
    config.fb_count = 1;
    config.grab_mode = CAMERA_GRAB_WHEN_EMPTY;
  }

  if (hasPSRAM) {
    config.fb_location = CAMERA_FB_IN_PSRAM;
  } else {
    config.fb_location = CAMERA_FB_IN_DRAM;
  }
  
  bootLogOK();
  if (LIVE_VIDEO_MODE) {
    bootLog("  Mode: RGB565 320x240");
  } else {
    bootLog("  Mode: JPEG VGA");
  }
  
  // ===== STEP 8: Initialize Camera =====
  bootLog("Camera init...", false);
  delay(200);
  
  esp_err_t err = esp_camera_init(&config);
  if (err != ESP_OK) {
    bootLogFAIL();
    bootLog("ERR: 0x", false);
    
    char errStr[10];
    sprintf(errStr, "%x", err);
    int y = BOOT_START_Y + ((bootLine - 1) * BOOT_LINE_HEIGHT);
    tft.setCursor(tft.getCursorX(), y);
    tft.setTextColor(ST77XX_RED);
    tft.print(errStr);
    
    // Halt with error
    bootLog("");
    bootLog("** BOOT FAILED **");
    while(1) delay(1000);
  }
  bootLogOK();
  
  // ===== STEP 9: Configure Sensor =====
  bootLog("Sensor config...", false);
  delay(100);
  
  sensor_t * s = esp_camera_sensor_get();
  // Use improved sensor configuration for natural colors
  CameraSettings::configureSensorForLiveView(s);
  
  bootLogOK();
  
  // ===== STEP 10: Test capture =====
  bootLog("Test capture...", false);
  delay(100);
  
  camera_fb_t *fb = esp_camera_fb_get();
  if (fb) {
    esp_camera_fb_return(fb);
    bootLogOK();
  } else {
    bootLogFAIL();
  }

  // ===== STEP 11: SD Card (on-demand) =====
  // SD_MMC uses pins shared with TFT, so we init/deinit only when saving photos
  bootLog("SD Card...", false);
  sdCardMounted = false;  // Will be mounted on-demand when saving
  int y = BOOT_START_Y + ((bootLine - 1) * BOOT_LINE_HEIGHT);
  int x = DISPLAY_WIDTH - (6 * 6) - 2;
  tft.setCursor(x, y);
  tft.setTextColor(ST77XX_CYAN);
  tft.print("[BTN]");
  Serial.println("[ON-DEMAND]");

  // ===== STEP 12: Optional OTA =====
  if (OTA_ENABLED) {
    setupOTA();
  } else {
    bootLog("OTA/WiFi disabled");
  }
  
  // ===== STEP 13: Final memory check =====
  bootLogValue("Free heap: ", ESP.getFreeHeap() / 1024, " KB");
  
  // ===== Boot Complete =====
  delay(200);
  bootLine++;
  
  // Draw completion bar
  tft.fillRect(0, DISPLAY_HEIGHT - 20, DISPLAY_WIDTH, 20, ST77XX_GREEN);
  tft_drawtext(14, DISPLAY_HEIGHT - 14, "BOOT COMPLETE", 1, ST77XX_BLACK);
  
  Serial.println("\n=== Boot Complete ===\n");
  
  // Wait 2 seconds to show boot log
  delay(2000);
  
  // Transition to Camera Ready
  showCameraReady();
}

// Show "Camera Ready" screen
void showCameraReady() {
  tft.fillScreen(ST77XX_BLACK);
  if (LIVE_VIDEO_MODE) {
    // Allocate display buffer for dual-core processing
    if (displayBuffer == NULL) {
      displayBuffer = (uint16_t*)ps_malloc(DISPLAY_WIDTH * DISPLAY_HEIGHT * sizeof(uint16_t));
      if (!displayBuffer) {
        displayBuffer = (uint16_t*)malloc(DISPLAY_WIDTH * DISPLAY_HEIGHT * sizeof(uint16_t));
      }
      if (!displayBuffer) {
        Serial.println("ERROR: Failed to allocate display buffer!");
        return;
      }
      Serial.printf("Display buffer allocated: %d bytes\n", DISPLAY_WIDTH * DISPLAY_HEIGHT * 2);
    }
    
    // Create mutex for frame synchronization
    if (frameMutex == NULL) {
      frameMutex = xSemaphoreCreateMutex();
    }
    
    // Start camera task on Core 0 (if not already running)
    if (cameraTaskHandle == NULL) {
      xTaskCreatePinnedToCore(
        cameraTask,       // Task function
        "CameraTask",     // Task name
        4096,             // Stack size
        NULL,             // Parameters
        2,                // Priority (higher than idle)
        &cameraTaskHandle,// Task handle
        0                 // Core 0
      );
      Serial.println("Camera task created on Core 0");
    }
    
    freezeFrame = false;
    newFrameReady = false;
    lastFrameTime = millis();
    frameCount = 0;
    Serial.println("Starting dual-core live video feed...");
    yield();
    delay(100);
  } else {
    // In photo mode, show ready screen
    tft_drawtext(16, 76, "Camera Ready", 1, ST77XX_WHITE);
    Serial.println("Showing: Camera Ready");
  }
  photoDisplayed = false;
}

// Flash white effect
void flashWhite() {
  tft.fillScreen(ST77XX_WHITE);
  delay(150);  // brief flash
}

// Swap bytes in RGB565 pixel (ESP32 little-endian -> display big-endian)
inline uint16_t swapBytes(uint16_t pixel) {
  return (pixel >> 8) | (pixel << 8);
}

// Camera capture task - runs on Core 0
void cameraTask(void *parameter) {
  Serial.println("Camera task started on Core 0");
  
  while (true) {
    // Skip if frozen, paused, or frame not consumed yet
    if (freezeFrame || cameraPaused || newFrameReady) {
      vTaskDelay(1);  // Small delay to prevent busy loop
      continue;
    }
    
    camera_fb_t *fb = esp_camera_fb_get();
    if (!fb) {
      vTaskDelay(1);
      continue;
    }
    
    // Take mutex to write to display buffer
    if (xSemaphoreTake(frameMutex, pdMS_TO_TICKS(10)) == pdTRUE) {
      // Center crop and byte-swap directly into display buffer
      int srcOffsetX = (CAMERA_WIDTH - DISPLAY_WIDTH) / 2;
      int srcOffsetY = (CAMERA_HEIGHT - DISPLAY_HEIGHT) / 2;
      uint16_t *srcPixels = (uint16_t*)fb->buf;
      
      for (int y = 0; y < DISPLAY_HEIGHT; y++) {
        int srcRow = srcOffsetY + y;
        uint16_t *rowStart = srcPixels + (srcRow * CAMERA_WIDTH) + srcOffsetX;
        uint16_t *dstRow = displayBuffer + (y * DISPLAY_WIDTH);
        
        for (int x = 0; x < DISPLAY_WIDTH; x++) {
          dstRow[x] = swapBytes(rowStart[x]);
        }
      }
      
      newFrameReady = true;
      xSemaphoreGive(frameMutex);
    }
    
    esp_camera_fb_return(fb);
    
    // Small yield for other tasks
    taskYIELD();
  }
}

// Display frame from buffer (called from main loop on Core 1)
void displayFrameFromBuffer() {
  if (!newFrameReady) return;
  
  if (xSemaphoreTake(frameMutex, pdMS_TO_TICKS(5)) == pdTRUE) {
    // Draw entire frame row by row
    for (int y = 0; y < DISPLAY_HEIGHT; y++) {
      tft.drawRGBBitmap(0, y, displayBuffer + (y * DISPLAY_WIDTH), DISPLAY_WIDTH, 1);
      
      // Yield every 32 rows
      if ((y & 31) == 0) {
        xSemaphoreGive(frameMutex);
        yield();
        if (xSemaphoreTake(frameMutex, pdMS_TO_TICKS(5)) != pdTRUE) {
          return;  // Couldn't get mutex back, abort
        }
      }
    }
    
    newFrameReady = false;
    xSemaphoreGive(frameMutex);
    
    // Always show FPS counter on display
    frameCount++;
    unsigned long now = millis();
    if (now - lastFrameTime >= 1000) {
      currentFps = frameCount * 1000.0f / (now - lastFrameTime);
      lastFrameTime = now;
      frameCount = 0;
    }
    // Draw FPS every frame for always-visible counter
    char fpsStr[12];
    snprintf(fpsStr, sizeof(fpsStr), "%.1ffps", currentFps);
    tft.fillRect(0, 0, 42, 10, ST77XX_BLACK);
    tft.setCursor(2, 1);
    tft.setTextSize(1);
    tft.setTextColor(ST77XX_GREEN);
    tft.print(fpsStr);
  }
}

// Display live video frame (RGB565 direct from camera) - LEGACY single-core version
void displayLiveFrame() {
  // Now uses dual-core: just call displayFrameFromBuffer()
  displayFrameFromBuffer();
}

// Handle button press (called from main loop, triggered by ISR)
void handleButtonPress() {
  Serial.println("Button pressed - requesting photo save!");
  saveRequested = true;  // Will be handled in main loop
}

// Show status message with box
void showStatusBox(const char* msg, uint16_t bgColor, uint16_t textColor) {
  int boxW = 100;
  int boxH = 30;
  int boxX = (DISPLAY_WIDTH - boxW) / 2;
  int boxY = (DISPLAY_HEIGHT - boxH) / 2;
  
  tft.fillRect(boxX, boxY, boxW, boxH, bgColor);
  tft.drawRect(boxX, boxY, boxW, boxH, ST77XX_WHITE);
  
  int textX = boxX + 10;
  int textY = boxY + 10;
  tft.setCursor(textX, textY);
  tft.setTextSize(1);
  tft.setTextColor(textColor);
  tft.print(msg);
}

// Save photo to SD card (runs on main loop, Core 1)
void savePhotoToSD() {
  Serial.println("=== Starting photo save ===");
  
  // 1. Pause camera task
  cameraPaused = true;
  freezeFrame = true;
  delay(50);  // Wait for camera task to pause
  Serial.println("Camera paused");
  
  // 2. Show "Saving..." message
  showStatusBox("Saving to SDCARD...", ST77XX_BLUE, ST77XX_WHITE);
  
  // 3. Initialize SD card (takes over TFT pins temporarily)
  Serial.println("Initializing SD card...");
  SDCardHandler sdCard(&photoCounter);
  
  if (!sdCard.begin()) {
    Serial.println("SD Card init failed!");
    
    // Re-init TFT and show error
    delay(50);
    tft.initR(INITR_GREENTAB);
    tft.setRotation(0);
    showStatusBox("SD FAILED!", ST77XX_RED, ST77XX_WHITE);
    delay(1500);
    
    // Resume camera
    cameraPaused = false;
    freezeFrame = false;
    return;
  }
  Serial.println("SD Card initialized");
  
  // 3b. Scan SD card and update counter to avoid overwriting
  // This ensures we always use a unique filename
  sdCard.updateCounterFromExistingPhotos();
  
  // 4. Save the current camera config for restoration
  camera_config_t config;
  config.ledc_channel = LEDC_CHANNEL_0;
  config.ledc_timer = LEDC_TIMER_0;
  config.pin_d0 = Y2_GPIO_NUM;
  config.pin_d1 = Y3_GPIO_NUM;
  config.pin_d2 = Y4_GPIO_NUM;
  config.pin_d3 = Y5_GPIO_NUM;
  config.pin_d4 = Y6_GPIO_NUM;
  config.pin_d5 = Y7_GPIO_NUM;
  config.pin_d6 = Y8_GPIO_NUM;
  config.pin_d7 = Y9_GPIO_NUM;
  config.pin_xclk = XCLK_GPIO_NUM;
  config.pin_pclk = PCLK_GPIO_NUM;
  config.pin_vsync = VSYNC_GPIO_NUM;
  config.pin_href = HREF_GPIO_NUM;
  config.pin_sscb_sda = SIOD_GPIO_NUM;
  config.pin_sscb_scl = SIOC_GPIO_NUM;
  config.pin_pwdn = PWDN_GPIO_NUM;
  config.pin_reset = RESET_GPIO_NUM;
  config.xclk_freq_hz = 24000000;  // Increased from 20MHz to 24MHz for faster operation
  
  // 5. Capture and save photo with correct colors (using SDCardHandler module)
  //    This module applies the correct sensor settings to fix greenish tint
  bool success = sdCard.captureAndSave(config, psramFound());
  //test
  // 6. Close SD card
  sdCard.end();
  Serial.println("SD Card closed");
  
  // 7. Restore camera to RGB565 mode for live view
  esp_camera_deinit();
  delay(100);
  
  config.pixel_format = PIXFORMAT_RGB565;
  config.frame_size = FRAMESIZE_QVGA;  // 320x240
  config.jpeg_quality = 12;
  config.fb_count = 2;
  config.grab_mode = CAMERA_GRAB_LATEST;
  config.fb_location = psramFound() ? CAMERA_FB_IN_PSRAM : CAMERA_FB_IN_DRAM;
  
  if (esp_camera_init(&config) != ESP_OK) {
    Serial.println("Camera restore failed! Rebooting...");
    ESP.restart();
  }
  
  // Restore sensor settings using CameraSettings module
  sensor_t *s = esp_camera_sensor_get();
  CameraSettings::configureSensorForLiveView(s);
  s->set_special_effect(s, 0);
  
  // 9. Re-init TFT (SD may have messed with pins)
  delay(50);
  tft.initR(INITR_GREENTAB);
  tft.setRotation(0);
  tft.fillScreen(ST77XX_BLACK);
  
  // 10. Show success message
  char msg[20];
  snprintf(msg, sizeof(msg), "Saved #%d", photoCounter - 1);
  showStatusBox(msg, ST77XX_GREEN, ST77XX_BLACK);
  delay(1000);
  
  // 11. Resume camera task
  tft.fillScreen(ST77XX_BLACK);
  newFrameReady = false;
  cameraPaused = false;
  freezeFrame = false;
  
  Serial.println("=== Photo save complete, camera resumed ===");
}

// main loop
void loop() {
  // Check for button interrupt (async, responsive)
  if (buttonInterruptFlag) {
    buttonInterruptFlag = false;
    handleButtonPress();
  }
  
  // Handle save request (runs on Core 1, main loop)
  if (saveRequested) {
    saveRequested = false;
    savePhotoToSD();
  }
  
  if (OTA_ENABLED) {
    ArduinoOTA.handle();
  }

  // Live video mode
  if (LIVE_VIDEO_MODE && !freezeFrame && !cameraPaused && !photoDisplayed) {
    displayLiveFrame();
  }
  
  // Small delay only in non-live mode to prevent excessive polling
  if (!LIVE_VIDEO_MODE) {
    delay(10);
  } else {
    // Give WiFi/OTA tasks time to run (1ms is enough)
    delay(1);
  }
}

// Clear old frames from camera buffer
void clearCameraBuffer() {
  camera_fb_t *fb = NULL;
  // Clear up to 3 old frames
  for (int i = 0; i < 3; i++) {
    fb = esp_camera_fb_get();
    if (fb) {
      esp_camera_fb_return(fb);
    }
  }
}

// Capture and display photo
void captureAndDisplay() {
  Serial.println("Starting capture...");
  Serial.printf("Free heap before: %d bytes\n", ESP.getFreeHeap());
  
  // Clear old frames from buffer
  clearCameraBuffer();
  delay(100);  // let camera settle
  
  // Capture fresh image
  camera_fb_t *fb = esp_camera_fb_get();
  if (!fb) {
    Serial.println("Camera capture failed!");
    tft.fillScreen(ST77XX_BLACK);
    tft_drawtext(10, 70, "Capture Failed", 1, ST77XX_RED);
    photoDisplayed = false;
    return;
  }

  Serial.printf("Captured: %dx%d, %d bytes, format: %d\n", 
                fb->width, fb->height, fb->len, fb->format);

  // Allocate buffer for scaled RGB565 image
  // VGA/4 = 160x120, each pixel = 2 bytes
  size_t bufferSize = SCALED_WIDTH * SCALED_HEIGHT * 2;
  uint8_t *rgb565 = NULL;
  
  // Try PSRAM first, then DRAM
  if (psramFound()) {
    rgb565 = (uint8_t *)ps_malloc(bufferSize);
  }
  if (!rgb565) {
    rgb565 = (uint8_t *)malloc(bufferSize);
  }
  
  if (!rgb565) {
    Serial.println("Memory allocation failed!");
    Serial.printf("Needed: %d bytes, Free: %d bytes\n", bufferSize, ESP.getFreeHeap());
    esp_camera_fb_return(fb);
    tft.fillScreen(ST77XX_BLACK);
    tft_drawtext(10, 70, "Memory Error", 1, ST77XX_RED);
    photoDisplayed = false;
    return;
  }

  Serial.println("Decoding JPEG...");
  
  // Decode JPEG to RGB565 with 4x downscaling
  // JPG_SCALE_4X: 640/4=160, 480/4=120
  bool decoded = jpg2rgb565(fb->buf, fb->len, rgb565, JPG_SCALE_4X);
  
  // Release camera frame buffer immediately to free memory
  esp_camera_fb_return(fb);
  fb = NULL;
  
  if (!decoded) {
    Serial.println("JPEG decode failed!");
    free(rgb565);
    tft.fillScreen(ST77XX_BLACK);
    tft_drawtext(10, 70, "Decode Error", 1, ST77XX_RED);
    photoDisplayed = false;
    return;
  }

  Serial.println("Displaying image...");
  
  // Clear display
  tft.fillScreen(ST77XX_BLACK);
  
  // Calculate centering offset
  // Scaled image: 160x120, Display: 128x160
  // We need to crop/center - image is wider than display
  int offsetX = 0;
  int offsetY = (DISPLAY_HEIGHT - SCALED_HEIGHT) / 2;  // center vertically
  
  // Since scaled width (160) > display width (128), we need to crop horizontally
  // Take center portion of the image
  int srcOffsetX = (SCALED_WIDTH - DISPLAY_WIDTH) / 2;  // 16 pixels from each side
  
  // Draw row by row, taking center portion
  for (int y = 0; y < SCALED_HEIGHT; y++) {
    // Point to the start of the row we want (skipping srcOffsetX pixels)
    uint16_t *rowStart = (uint16_t*)(rgb565 + (y * SCALED_WIDTH + srcOffsetX) * 2);
    tft.drawRGBBitmap(offsetX, offsetY + y, rowStart, DISPLAY_WIDTH, 1);
  }
  
  // Free the buffer
  free(rgb565);
  
  photoDisplayed = true;
  Serial.println("Photo displayed successfully!");
  Serial.printf("Free heap after: %d bytes\n", ESP.getFreeHeap());
}

// Draw text on TFT
void tft_drawtext(int16_t x, int16_t y, const char* text, uint8_t font_size, uint16_t color) {
  tft.setCursor(x, y);
  tft.setTextSize(font_size);
  tft.setTextColor(color);
  tft.setTextWrap(true);
  tft.print(text);
}
