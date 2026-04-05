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
  
  // Build a minimal EXIF APP1 segment with DateTime tag
  // Returns allocated buffer (caller must free) and sets len
  // EXIF DateTime format: "YYYY:MM:DD HH:MM:SS\0" (20 bytes)
  static uint8_t* buildExifSegment(const char* dateTime, size_t& segLen) {
    // Minimal EXIF: APP1 marker + Exif header + TIFF header + IFD0 with DateTime
    // TIFF is little-endian (II)
    static const uint8_t exifTemplate[] = {
      0xFF, 0xE1,       // APP1 marker
      0x00, 0x00,       // Length placeholder (filled below)
      'E','x','i','f',0x00,0x00, // Exif header
      // TIFF header (offset 10 from APP1 data start)
      'I','I',          // Little-endian
      0x2A, 0x00,       // TIFF magic
      0x08, 0x00, 0x00, 0x00, // Offset to IFD0 (8 bytes from TIFF start)
      // IFD0 (offset 8 from TIFF start)
      0x01, 0x00,       // 1 entry
      // Entry: DateTime (tag 0x0132)
      0x32, 0x01,       // Tag: DateTime
      0x02, 0x00,       // Type: ASCII
      0x14, 0x00, 0x00, 0x00, // Count: 20 bytes
      0x1A, 0x00, 0x00, 0x00, // Offset to value (26 from TIFF start)
      0x00, 0x00, 0x00, 0x00, // Next IFD: none
      // DateTime value starts here (offset 26 from TIFF)
      // 20 bytes of datetime string filled below
    };
    
    segLen = sizeof(exifTemplate) + 20; // template + datetime string
    uint8_t* seg = (uint8_t*)malloc(segLen);
    if (!seg) return nullptr;
    
    memcpy(seg, exifTemplate, sizeof(exifTemplate));
    // Copy datetime string (20 bytes including null)
    memcpy(seg + sizeof(exifTemplate), dateTime, 20);
    
    // Fill APP1 length (everything after the 2-byte marker)
    uint16_t app1Len = segLen - 2;
    seg[2] = (app1Len >> 8) & 0xFF;
    seg[3] = app1Len & 0xFF;
    
    return seg;
  }
  
  // Write JPEG with EXIF injected: SOI + EXIF APP1 + rest of JPEG (skip original SOI)
  bool writeJpegWithExif(File& file, const uint8_t* jpgData, size_t jpgLen, const char* exifDateTime) {
    // Write SOI marker
    const uint8_t soi[] = {0xFF, 0xD8};
    file.write(soi, 2);
    
    // Build and write EXIF segment
    size_t exifLen = 0;
    uint8_t* exifSeg = buildExifSegment(exifDateTime, exifLen);
    if (exifSeg) {
      file.write(exifSeg, exifLen);
      free(exifSeg);
    }
    
    // Write rest of JPEG (skip original SOI 0xFFD8)
    size_t offset = 2;
    const size_t chunkSize = 4096;
    bool ok = true;
    while (offset < jpgLen) {
      size_t toWrite = ((jpgLen - offset) > chunkSize) ? chunkSize : (jpgLen - offset);
      size_t written = file.write(jpgData + offset, toWrite);
      if (written != toWrite) { ok = false; break; }
      offset += written;
      yield();
    }
    return ok;
  }
  
  // Capture and save photo with correct colors
  // This reconfigures camera to JPEG mode, captures, saves, then restores
  bool captureAndSave(camera_config_t& currentConfig, bool hasPSRAM, RTCHandler* rtc = nullptr) {
    // Store reference to sensor for later
    sensor_t *s = esp_camera_sensor_get();
    
    // Deinit current camera mode
    esp_camera_deinit();
    delay(200);  // Give hardware time to release
    
    // Configure for JPEG capture
    camera_config_t jpegConfig = currentConfig;
    jpegConfig.pixel_format = PIXFORMAT_JPEG;
    jpegConfig.frame_size = FRAMESIZE_SXGA;  // 1280x1024 - highest quality
    jpegConfig.jpeg_quality = 10;
    jpegConfig.fb_count = 1;
    jpegConfig.grab_mode = CAMERA_GRAB_WHEN_EMPTY;
    jpegConfig.fb_location = hasPSRAM ? CAMERA_FB_IN_PSRAM : CAMERA_FB_IN_DRAM;
    jpegConfig.xclk_freq_hz = 10000000;     // 10MHz for SXGA stability
    
    // Reinit camera in JPEG mode
    if (esp_camera_init(&jpegConfig) != ESP_OK) {
      Serial.println("ERROR: Camera reinit for JPEG failed!");
      return false;
    }
    
    s = esp_camera_sensor_get();
    CameraSettings::configureSensorForPhotoCapture(s);
    CameraSettings::waitForAutoSettingsToStabilize(500);
    
    // Discard first 2 frames (often corrupt/green)
    for (int i = 0; i < 2; i++) {
      camera_fb_t *discard = esp_camera_fb_get();
      if (discard) esp_camera_fb_return(discard);
      delay(100);
    }
    
    // Capture JPEG frame
    camera_fb_t *fb = esp_camera_fb_get();
    if (!fb) {
      Serial.println("ERROR: JPEG capture failed!");
      return false;
    }
    
    Serial.printf("Captured JPEG: %d bytes (%dx%d)\n", 
                  fb->len, fb->width, fb->height);
    delay(50);
    
    // Build EXIF DateTime string: "YYYY:MM:DD HH:MM:SS\0" (exactly 20 bytes)
    char exifDateTime[20] = "2000:01:01 00:00:00";
    if (rtc && rtc->isAvailable()) {
      DateTime dt = rtc->now();
      snprintf(exifDateTime, sizeof(exifDateTime), "%04d:%02d:%02d %02d:%02d:%02d",
               dt.year(), dt.month(), dt.day(),
               dt.hour(), dt.minute(), dt.second());
    }
    
    // Build filename with timestamp if RTC available
    char filename[48];
    unsigned int attemptNumber = *photoCounterPtr;
    
    if (rtc && rtc->isAvailable()) {
      char tsFile[20];
      rtc->getFilenameStr(tsFile, sizeof(tsFile));
      snprintf(filename, sizeof(filename), "/IMG_%s_%04d.jpg", tsFile, attemptNumber);
    } else {
      snprintf(filename, sizeof(filename), "/photo_%04d.jpg", attemptNumber);
    }
    
    // Ensure unique
    while (SD_MMC.exists(filename)) {
      attemptNumber++;
      if (rtc && rtc->isAvailable()) {
        char tsFile[20];
        rtc->getFilenameStr(tsFile, sizeof(tsFile));
        snprintf(filename, sizeof(filename), "/IMG_%s_%04d.jpg", tsFile, attemptNumber);
      } else {
        snprintf(filename, sizeof(filename), "/photo_%04d.jpg", attemptNumber);
      }
      if (attemptNumber > *photoCounterPtr + 100) {
        esp_camera_fb_return(fb);
        return false;
      }
    }
    *photoCounterPtr = attemptNumber;
    
    // Try full-res decode → stamp → re-encode at SXGA (1280x1024)
    // Needs 2.6MB RGB565 in PSRAM — fits in 4MB PSRAM
    bool savedWithStamp = false;
    if (rtc && rtc->isAvailable() && hasPSRAM) {
      Serial.println("Decoding full-res for timestamp...");
      
      size_t rgbSize = fb->width * fb->height * 2;
      uint16_t* rgbBuf = (uint16_t*)ps_malloc(rgbSize);
      
      if (rgbBuf) {
        yield();
        bool decoded = jpg2rgb565(fb->buf, fb->len, (uint8_t*)rgbBuf, JPG_SCALE_NONE);
        yield();
        
        if (decoded) {
          Serial.printf("Decoded %dx%d, byte-swap + stamp...\n", fb->width, fb->height);
          
          // Fix byte order: jpg2rgb565 = big-endian, fmt2jpg = little-endian
          int totalPixels = fb->width * fb->height;
          for (int i = 0; i < totalPixels; i++) {
            rgbBuf[i] = (rgbBuf[i] >> 8) | (rgbBuf[i] << 8);
          }
          yield();
          
          // Burn visible timestamp at bottom-right
          char stamp[22];
          snprintf(stamp, sizeof(stamp), "%02d/%02d/%04d %02d:%02d:%02d",
                   rtc->now().day(), rtc->now().month(), rtc->now().year(),
                   rtc->now().hour(), rtc->now().minute(), rtc->now().second());
          
          int scale = 3;  // Scale 3 for SXGA readability
          int textW = strlen(stamp) * 4 * scale;
          int textH = 5 * scale;
          int tx = fb->width - textW - 8;
          int ty = fb->height - textH - 8;
          RTCHandler::drawTimestampOnRGB565Scaled(rgbBuf, fb->width, fb->height,
                                                  tx, ty, stamp, scale);
          
          Serial.println("Re-encoding full-res JPEG...");
          yield();
          
          uint8_t* jpgBuf = NULL;
          size_t jpgLen = 0;
          bool encoded = fmt2jpg((uint8_t*)rgbBuf, rgbSize, fb->width, fb->height,
                                  PIXFORMAT_RGB565, 10, &jpgBuf, &jpgLen);
          free(rgbBuf);
          yield();
          
          if (encoded && jpgBuf && jpgLen > 0) {
            Serial.printf("Stamped JPEG: %d bytes, saving with EXIF...\n", jpgLen);
            
            File file = SD_MMC.open(filename, FILE_WRITE);
            if (file) {
              bool writeOk = writeJpegWithExif(file, jpgBuf, jpgLen, exifDateTime);
              file.close();
              delay(50);
              
              if (writeOk) {
                Serial.printf("SUCCESS: Saved %s with stamp+EXIF\n", filename);
                (*photoCounterPtr)++;
                savedWithStamp = true;
              }
            }
            free(jpgBuf);
          } else {
            Serial.println("Re-encode failed, falling back");
            if (jpgBuf) free(jpgBuf);
          }
        } else {
          free(rgbBuf);
          Serial.println("Decode failed, falling back");
        }
      } else {
        Serial.println("PSRAM alloc failed, saving original");
      }
    }
    
    // Fallback: save original JPEG with EXIF DateTime (no visible stamp)
    if (!savedWithStamp) {
      Serial.printf("Saving original to %s with EXIF...\n", filename);
      File file = SD_MMC.open(filename, FILE_WRITE);
      if (file) {
        bool writeOk = writeJpegWithExif(file, fb->buf, fb->len, exifDateTime);
        file.close();
        delay(50);
        
        if (writeOk) {
          Serial.printf("SUCCESS: Saved %s\n", filename);
          (*photoCounterPtr)++;
        } else {
          Serial.println("Write failed!");
          esp_camera_fb_return(fb);
          return false;
        }
      } else {
        Serial.println("Failed to open file!");
        esp_camera_fb_return(fb);
        return false;
      }
    }
    
    esp_camera_fb_return(fb);
    return true;
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
