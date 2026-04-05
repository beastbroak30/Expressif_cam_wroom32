#ifndef SD_CARD_HANDLER_H
#define SD_CARD_HANDLER_H

#include "SD_MMC.h"
#include "FS.h"
#include "esp_camera.h"
#include "img_converters.h"
#include "camera_settings.h"
#include "rtc_handler.h"

// SD Card Handler Module
// Handles photo saving to SD card with proper color management
class SDCardHandler {
private:
  unsigned int* photoCounterPtr;
  bool isMounted;
  
public:
  SDCardHandler(unsigned int* counterPtr) 
    : photoCounterPtr(counterPtr), isMounted(false) {}
  
  // Initialize SD card (1-bit mode for pin compatibility)
  bool begin() {
    Serial.println("Initializing SD card...");
    if (!SD_MMC.begin("/sdcard", true)) {  // true = 1-bit mode
      Serial.println("SD Card init failed!");
      isMounted = false;
      return false;
    }
    Serial.println("SD Card initialized");
    isMounted = true;
    return true;
  }
  
  // Scan SD card and find next available photo number
  // This prevents overwriting existing photos
  unsigned int findNextPhotoNumber() {
    if (!isMounted) {
      Serial.println("SD card not mounted, cannot scan for photos");
      return 0;
    }
    
    Serial.println("Scanning SD card for existing photos...");
    
    unsigned int maxNumber = 0;
    bool foundAny = false;
    
    File root = SD_MMC.open("/");
    if (!root) {
      Serial.println("Failed to open root directory");
      return 0;
    }
    
    File file = root.openNextFile();
    while (file) {
      if (!file.isDirectory()) {
        String filename = String(file.name());
        
        // Check if filename matches pattern: photo_XXXX.jpg
        if (filename.startsWith("/photo_") && filename.endsWith(".jpg")) {
          // Extract the number from photo_XXXX.jpg
          int startIdx = filename.indexOf('_') + 1;
          int endIdx = filename.indexOf(".jpg");
          
          if (startIdx > 0 && endIdx > startIdx) {
            String numberStr = filename.substring(startIdx, endIdx);
            unsigned int fileNumber = numberStr.toInt();
            
            if (fileNumber >= maxNumber) {
              maxNumber = fileNumber;
              foundAny = true;
            }
            
            Serial.printf("  Found: %s (number: %d)\n", filename.c_str(), fileNumber);
          }
        }
      }
      file = root.openNextFile();
    }
    
    root.close();
    
    if (foundAny) {
      unsigned int nextNumber = maxNumber + 1;
      Serial.printf("Highest photo number found: %d\n", maxNumber);
      Serial.printf("Next photo will be: photo_%04d.jpg\n", nextNumber);
      return nextNumber;
    } else {
      Serial.println("No existing photos found, starting from 0");
      return 0;
    }
  }
  
  // Set the photo counter based on existing files
  void updateCounterFromExistingPhotos() {
    unsigned int nextNumber = findNextPhotoNumber();
    *photoCounterPtr = nextNumber;
    Serial.printf("Photo counter set to: %d\n", *photoCounterPtr);
  }
  
  // End SD card (release pins)
  void end() {
    SD_MMC.end();
    isMounted = false;
    Serial.println("SD Card closed");
  }
  
  // Check if SD card is mounted
  bool mounted() {
    return isMounted;
  }
  
  // Save JPEG photo to SD card with proper filename
  // Returns true if successful
  // Automatically finds unique filename if file already exists
  bool savePhoto(camera_fb_t* fb) {
    if (!fb || !isMounted) {
      Serial.println("ERROR: Invalid frame buffer or SD not mounted!");
      return false;
    }
    
    // Generate filename and ensure it's unique
    char filename[32];
    unsigned int attemptNumber = *photoCounterPtr;
    
    // Keep trying until we find a filename that doesn't exist
    while (true) {
      snprintf(filename, sizeof(filename), "/photo_%04d.jpg", attemptNumber);
      
      // Check if file already exists
      if (!SD_MMC.exists(filename)) {
        // Found a unique filename!
        break;
      }
      
      Serial.printf("File %s already exists, trying next number...\n", filename);
      attemptNumber++;
      
      // Safety check to prevent infinite loop
      if (attemptNumber > *photoCounterPtr + 1000) {
        Serial.println("ERROR: Too many existing files, cannot find unique name!");
        return false;
      }
    }
    
    // Update the counter to the unique number we found
    *photoCounterPtr = attemptNumber;
    
    Serial.printf("Saving to: %s (%d bytes)\n", filename, fb->len);
    
    // Open file for writing
    File file = SD_MMC.open(filename, FILE_WRITE);
    if (!file) {
      Serial.println("Failed to open file for writing");
      return false;
    }
    
    // Write JPEG data
    size_t written = file.write(fb->buf, fb->len);
    file.close();
    
    if (written != fb->len) {
      Serial.printf("ERROR: Only wrote %d of %d bytes\n", written, fb->len);
      return false;
    }
    
    Serial.printf("SUCCESS: Saved %s\n", filename);
    (*photoCounterPtr)++;  // Increment counter for next photo
    return true;
  }
  
  // Capture and save photo with correct colors
  // This reconfigures camera to JPEG mode, captures, saves, then restores
  bool captureAndSave(camera_config_t& currentConfig, bool hasPSRAM, RTCHandler* rtc = nullptr) {
    // Store reference to sensor for later
    sensor_t *s = esp_camera_sensor_get();
    
    // Deinit current camera mode
    esp_camera_deinit();
    delay(100);
    
    // Configure for JPEG capture
    camera_config_t jpegConfig = currentConfig;
    jpegConfig.pixel_format = PIXFORMAT_JPEG;
    jpegConfig.frame_size = FRAMESIZE_SXGA;  // 1280x1024 - best quality for ESP32-CAM
    jpegConfig.jpeg_quality = 10;            // 10 for SXGA (lower causes artifacts at high res)
    jpegConfig.fb_count = 1;
    jpegConfig.grab_mode = CAMERA_GRAB_WHEN_EMPTY;
    jpegConfig.fb_location = hasPSRAM ? CAMERA_FB_IN_PSRAM : CAMERA_FB_IN_DRAM;
    jpegConfig.xclk_freq_hz = 10000000;     // 10MHz for SXGA stability (24MHz causes line artifacts)
    
    // Reinit camera in JPEG mode
    if (esp_camera_init(&jpegConfig) != ESP_OK) {
      Serial.println("ERROR: Camera reinit for JPEG failed!");
      return false;
    }
    
    // Apply optimal sensor settings for photo capture
    // THIS IS CRITICAL FOR FIXING THE GREENISH TINT!
    s = esp_camera_sensor_get();
    CameraSettings::configureSensorForPhotoCapture(s);
    
    // Wait for auto white balance and exposure to stabilize
    // This ensures accurate colors in the saved photo
    CameraSettings::waitForAutoSettingsToStabilize(500);
    
    // Capture JPEG frame
    camera_fb_t *fb = esp_camera_fb_get();
    if (!fb) {
      Serial.println("ERROR: JPEG capture failed!");
      return false;
    }
    
    Serial.printf("Captured JPEG: %d bytes (%dx%d)\n", 
                  fb->len, fb->width, fb->height);
    
    // Burn timestamp into photo if RTC is available
    if (rtc && rtc->isAvailable() && hasPSRAM) {
      Serial.println("Burning timestamp into photo...");
      
      // Decode JPEG to RGB565 in PSRAM
      size_t rgbSize = fb->width * fb->height * 2;
      uint16_t* rgbBuf = (uint16_t*)ps_malloc(rgbSize);
      
      if (rgbBuf) {
        bool decoded = jpg2rgb565(fb->buf, fb->len, (uint8_t*)rgbBuf, JPG_SCALE_NONE);
        if (decoded) {
          // Get timestamp string
          char stamp[20];
          rtc->getTimestampStr(stamp, sizeof(stamp));
          
          // Draw timestamp bottom-right, scale 3 for SXGA readability
          int scale = 3;
          int textW = strlen(stamp) * 4 * scale;
          int textH = 5 * scale;
          int tx = fb->width - textW - 6;
          int ty = fb->height - textH - 6;
          RTCHandler::drawTimestampOnRGB565Scaled(rgbBuf, fb->width, fb->height,
                                                  tx, ty, stamp, scale);
          
          // Re-encode to JPEG
          uint8_t* jpgBuf = NULL;
          size_t jpgLen = 0;
          bool encoded = fmt2jpg((uint8_t*)rgbBuf, rgbSize, fb->width, fb->height,
                                  PIXFORMAT_RGB565, 10, &jpgBuf, &jpgLen);
          free(rgbBuf);
          
          if (encoded && jpgBuf) {
            Serial.printf("Re-encoded JPEG with timestamp: %d bytes\n", jpgLen);
            // Save the stamped JPEG manually
            esp_camera_fb_return(fb);
            
            char filename[32];
            unsigned int attemptNumber = *photoCounterPtr;
            while (true) {
              snprintf(filename, sizeof(filename), "/photo_%04d.jpg", attemptNumber);
              if (!SD_MMC.exists(filename)) break;
              attemptNumber++;
              if (attemptNumber > *photoCounterPtr + 1000) {
                free(jpgBuf);
                return false;
              }
            }
            *photoCounterPtr = attemptNumber;
            
            File file = SD_MMC.open(filename, FILE_WRITE);
            bool success = false;
            if (file) {
              size_t written = file.write(jpgBuf, jpgLen);
              file.close();
              success = (written == jpgLen);
              if (success) {
                Serial.printf("SUCCESS: Saved %s with timestamp\n", filename);
                (*photoCounterPtr)++;
              }
            }
            free(jpgBuf);
            return success;
          } else {
            Serial.println("JPEG re-encode failed, saving without timestamp");
            if (jpgBuf) free(jpgBuf);
          }
        } else {
          free(rgbBuf);
          Serial.println("JPEG decode failed, saving without timestamp");
        }
      } else {
        Serial.println("No PSRAM for timestamp, saving without timestamp");
      }
    }
    
    // Save to SD card (fallback: no timestamp)
    bool success = savePhoto(fb);
    
    // Release frame buffer
    esp_camera_fb_return(fb);
    
    return success;
  }
  
  // Get free SD card space in MB
  uint64_t getFreeSpaceMB() {
    if (!isMounted) return 0;
    
    uint64_t cardSize = SD_MMC.cardSize() / (1024 * 1024);
    uint64_t usedSize = SD_MMC.usedBytes() / (1024 * 1024);
    return cardSize - usedSize;
  }
  
  // Get total SD card space in MB
  uint64_t getTotalSpaceMB() {
    if (!isMounted) return 0;
    return SD_MMC.cardSize() / (1024 * 1024);
  }
  
  // List photos on SD card
  void listPhotos() {
    if (!isMounted) {
      Serial.println("SD card not mounted");
      return;
    }
    
    File root = SD_MMC.open("/");
    if (!root) {
      Serial.println("Failed to open root directory");
      return;
    }
    
    Serial.println("\n=== Photos on SD Card ===");
    File file = root.openNextFile();
    int count = 0;
    while (file) {
      if (!file.isDirectory() && String(file.name()).endsWith(".jpg")) {
        Serial.printf("%s (%d bytes)\n", file.name(), file.size());
        count++;
      }
      file = root.openNextFile();
    }
    Serial.printf("Total: %d photos\n", count);
    Serial.printf("Free space: %llu MB / %llu MB\n", 
                  getFreeSpaceMB(), getTotalSpaceMB());
    Serial.println("========================\n");
  }
};

#endif // SD_CARD_HANDLER_H
