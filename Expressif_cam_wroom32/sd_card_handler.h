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
  
  // Helper: write 16-bit little-endian
  static void writeU16(uint8_t* p, uint16_t v) { p[0] = v & 0xFF; p[1] = (v >> 8) & 0xFF; }
  // Helper: write 32-bit little-endian
  static void writeU32(uint8_t* p, uint32_t v) { 
    p[0] = v & 0xFF; p[1] = (v >> 8) & 0xFF; 
    p[2] = (v >> 16) & 0xFF; p[3] = (v >> 24) & 0xFF; 
  }
  // Helper: write IFD entry (12 bytes)
  static void writeIFDEntry(uint8_t* p, uint16_t tag, uint16_t type, uint32_t count, uint32_t valOrOff) {
    writeU16(p, tag);
    writeU16(p + 2, type);
    writeU32(p + 4, count);
    writeU32(p + 8, valOrOff);
  }

  // Build comprehensive EXIF APP1 segment with full camera metadata
  // Includes: Make, Model, Artist, Copyright, DateTime, Camera specs, SubSecTime
  // Author: beastbroak30 | Camera: ESP32-CAM AI-Thinker with OV2640
  static uint8_t* buildExifSegment(const char* dateTime, size_t& segLen, 
                                    uint16_t imgWidth = 1280, uint16_t imgHeight = 1024,
                                    uint16_t subsecMs = 0) {
    // String pool (null-terminated)
    const char* strMake       = "Espressif Systems";                    // 18 bytes
    const char* strModel      = "ESP32-CAM AI-Thinker (OV2640)";        // 30 bytes
    const char* strSoftware   = "github.com/beastbroak30";              // 24 bytes
    const char* strArtist     = "beastbroak30";                         // 13 bytes
    const char* strCopyright  = "CC BY-NC 4.0 beastbroak30";            // 26 bytes
    const char* strLensMake   = "OmniVision";                           // 11 bytes
    const char* strLensModel  = "OV2640 1/4\" CMOS";                    // 17 bytes
    
    size_t lenMake      = strlen(strMake) + 1;       // 18
    size_t lenModel     = strlen(strModel) + 1;      // 30
    size_t lenSoftware  = strlen(strSoftware) + 1;   // 24
    size_t lenArtist    = strlen(strArtist) + 1;     // 13
    size_t lenCopyright = strlen(strCopyright) + 1;  // 26
    size_t lenLensMake  = strlen(strLensMake) + 1;   // 11
    size_t lenLensModel = strlen(strLensModel) + 1;  // 17
    
    // SubSec string "XXX\0" (4 bytes, 3 digits)
    char strSubSec[4];
    snprintf(strSubSec, sizeof(strSubSec), "%03d", subsecMs % 1000);
    size_t lenSubSec = 4;

    // Layout calculation (T+ = offset from TIFF header start at byte 10)
    // APP1: marker(2) + length(2) + "Exif\0\0"(6) = 10 bytes header
    // TIFF header: T+0, 8 bytes
    // IFD0: T+8, 7 entries -> 2 + 7*12 + 4 = 90 bytes (ends at T+98)
    // IFD0 Data area starts at T+98
    // Exif SubIFD: variable position
    // SubIFD Data area: after SubIFD

    const int IFD0_ENTRIES = 7;  // Make, Model, Software, DateTime, Artist, Copyright, ExifIFD
    const int SUBIFD_ENTRIES = 14;  // Full camera metadata

    // Calculate IFD0 size
    size_t ifd0Size = 2 + IFD0_ENTRIES * 12 + 4;  // count + entries + next IFD ptr
    uint32_t ifd0DataStart = 8 + ifd0Size;  // T+98

    // IFD0 data area: strings that don't fit inline (>4 bytes)
    uint32_t offMake      = ifd0DataStart;                           // T+98
    uint32_t offModel     = offMake + lenMake;                       // T+116
    uint32_t offSoftware  = offModel + lenModel;                     // T+146
    uint32_t offDateTime  = offSoftware + lenSoftware;               // T+170
    uint32_t offArtist    = offDateTime + 20;                        // T+190
    uint32_t offCopyright = offArtist + lenArtist;                   // T+203
    uint32_t subIfdStart  = offCopyright + lenCopyright;             // T+229

    // Exif SubIFD
    size_t subIfdSize = 2 + SUBIFD_ENTRIES * 12 + 4;  // 2 + 168 + 4 = 174 bytes
    uint32_t subIfdDataStart = subIfdStart + subIfdSize;  // T+403

    // SubIFD data area
    uint32_t offDateTimeOrig = subIfdDataStart;                      // T+403
    uint32_t offDateTimeDig  = offDateTimeOrig + 20;                 // T+423
    uint32_t offSubSecOrig   = offDateTimeDig + 20;                  // T+443
    uint32_t offSubSecDig    = offSubSecOrig + lenSubSec;            // T+447
    uint32_t offExposure     = offSubSecDig + lenSubSec;             // T+451 (RATIONAL = 8 bytes)
    uint32_t offFNumber      = offExposure + 8;                      // T+459
    uint32_t offFocalLen     = offFNumber + 8;                       // T+467
    uint32_t offLensMake     = offFocalLen + 8;                      // T+475
    uint32_t offLensModel    = offLensMake + lenLensMake;            // T+486
    uint32_t totalTiffSize   = offLensModel + lenLensModel;          // T+503

    // Total segment size
    segLen = 10 + totalTiffSize;  // 10 header + TIFF data
    
    uint8_t* s = (uint8_t*)malloc(segLen);
    if (!s) return nullptr;
    memset(s, 0, segLen);

    // APP1 marker
    s[0] = 0xFF; s[1] = 0xE1;
    // APP1 length (big-endian): segLen - 2
    uint16_t appLen = segLen - 2;
    s[2] = (appLen >> 8) & 0xFF;
    s[3] = appLen & 0xFF;
    // Exif\0\0
    memcpy(s + 4, "Exif\0\0", 6);

    // TIFF header at T+0 (byte 10)
    uint8_t* T = s + 10;  // TIFF base
    T[0] = 'I'; T[1] = 'I';           // little-endian
    writeU16(T + 2, 0x002A);          // magic 42
    writeU32(T + 4, 8);               // IFD0 at T+8

    // IFD0 at T+8
    uint8_t* ifd0 = T + 8;
    writeU16(ifd0, IFD0_ENTRIES);
    uint8_t* entry = ifd0 + 2;

    // IFD0 entries (must be in ascending tag order!)
    // 0x010F Make
    writeIFDEntry(entry, 0x010F, 2, lenMake, offMake); entry += 12;
    // 0x0110 Model
    writeIFDEntry(entry, 0x0110, 2, lenModel, offModel); entry += 12;
    // 0x0131 Software
    writeIFDEntry(entry, 0x0131, 2, lenSoftware, offSoftware); entry += 12;
    // 0x0132 DateTime
    writeIFDEntry(entry, 0x0132, 2, 20, offDateTime); entry += 12;
    // 0x013B Artist
    writeIFDEntry(entry, 0x013B, 2, lenArtist, offArtist); entry += 12;
    // 0x8298 Copyright
    writeIFDEntry(entry, 0x8298, 2, lenCopyright, offCopyright); entry += 12;
    // 0x8769 ExifIFD pointer
    writeIFDEntry(entry, 0x8769, 4, 1, subIfdStart); entry += 12;
    // Next IFD = 0 (already zeroed)

    // IFD0 data area
    memcpy(T + offMake, strMake, lenMake);
    memcpy(T + offModel, strModel, lenModel);
    memcpy(T + offSoftware, strSoftware, lenSoftware);
    memcpy(T + offDateTime, dateTime, 20);
    memcpy(T + offArtist, strArtist, lenArtist);
    memcpy(T + offCopyright, strCopyright, lenCopyright);

    // Exif SubIFD at subIfdStart
    uint8_t* subIfd = T + subIfdStart;
    writeU16(subIfd, SUBIFD_ENTRIES);
    entry = subIfd + 2;

    // SubIFD entries (ascending tag order!)
    // 0x829A ExposureTime (RATIONAL) = 1/50 sec (typical indoor)
    writeIFDEntry(entry, 0x829A, 5, 1, offExposure); entry += 12;
    // 0x829D FNumber (RATIONAL) = f/2.0 (OV2640 fixed aperture)
    writeIFDEntry(entry, 0x829D, 5, 1, offFNumber); entry += 12;
    // 0x8827 ISOSpeedRatings (SHORT) = 100 (inline)
    writeIFDEntry(entry, 0x8827, 3, 1, 100); entry += 12;
    // 0x9000 ExifVersion (UNDEFINED) = "0230" (inline)
    entry[0] = 0x00; entry[1] = 0x90; entry[2] = 0x07; entry[3] = 0x00;
    entry[4] = 0x04; entry[5] = 0x00; entry[6] = 0x00; entry[7] = 0x00;
    entry[8] = '0'; entry[9] = '2'; entry[10] = '3'; entry[11] = '0';
    entry += 12;
    // 0x9003 DateTimeOriginal
    writeIFDEntry(entry, 0x9003, 2, 20, offDateTimeOrig); entry += 12;
    // 0x9004 DateTimeDigitized
    writeIFDEntry(entry, 0x9004, 2, 20, offDateTimeDig); entry += 12;
    // 0x9291 SubSecTimeOriginal
    writeIFDEntry(entry, 0x9291, 2, lenSubSec, offSubSecOrig); entry += 12;
    // 0x9292 SubSecTimeDigitized
    writeIFDEntry(entry, 0x9292, 2, lenSubSec, offSubSecDig); entry += 12;
    // 0x920A FocalLength (RATIONAL) = 2.8mm
    writeIFDEntry(entry, 0x920A, 5, 1, offFocalLen); entry += 12;
    // 0xA001 ColorSpace (SHORT) = 1 (sRGB)
    writeIFDEntry(entry, 0xA001, 3, 1, 1); entry += 12;
    // 0xA002 PixelXDimension (LONG) - inline
    writeIFDEntry(entry, 0xA002, 4, 1, imgWidth); entry += 12;
    // 0xA003 PixelYDimension (LONG) - inline
    writeIFDEntry(entry, 0xA003, 4, 1, imgHeight); entry += 12;
    // 0xA433 LensMake
    writeIFDEntry(entry, 0xA433, 2, lenLensMake, offLensMake); entry += 12;
    // 0xA434 LensModel
    writeIFDEntry(entry, 0xA434, 2, lenLensModel, offLensModel); entry += 12;
    // Next IFD = 0 (already zeroed)

    // SubIFD data area
    memcpy(T + offDateTimeOrig, dateTime, 20);
    memcpy(T + offDateTimeDig, dateTime, 20);
    memcpy(T + offSubSecOrig, strSubSec, lenSubSec);
    memcpy(T + offSubSecDig, strSubSec, lenSubSec);
    
    // RATIONAL values: numerator(4) + denominator(4)
    // ExposureTime = 1/50
    writeU32(T + offExposure, 1);
    writeU32(T + offExposure + 4, 50);
    // FNumber = 2.0 = 20/10
    writeU32(T + offFNumber, 20);
    writeU32(T + offFNumber + 4, 10);
    // FocalLength = 2.8mm = 28/10
    writeU32(T + offFocalLen, 28);
    writeU32(T + offFocalLen + 4, 10);
    
    // Lens strings
    memcpy(T + offLensMake, strLensMake, lenLensMake);
    memcpy(T + offLensModel, strLensModel, lenLensModel);

    return s;
  }
  
  // Write JPEG with EXIF injected: SOI + EXIF APP1 + rest of JPEG (skip original SOI)
  bool writeJpegWithExif(File& file, const uint8_t* jpgData, size_t jpgLen, 
                         const char* exifDateTime, uint16_t imgWidth, uint16_t imgHeight,
                         uint16_t subsecMs = 0) {
    // Write SOI marker
    const uint8_t soi[] = {0xFF, 0xD8};
    file.write(soi, 2);
    
    // Build and write comprehensive EXIF segment
    size_t exifLen = 0;
    uint8_t* exifSeg = buildExifSegment(exifDateTime, exifLen, imgWidth, imgHeight, subsecMs);
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
    uint16_t subsecMs = (uint16_t)(millis() % 1000);  // Capture subsecond for precision
    if (rtc && rtc->isAvailable()) {
      DateTime dt = rtc->now();
      snprintf(exifDateTime, sizeof(exifDateTime), "%04d:%02d:%02d %02d:%02d:%02d",
               dt.year(), dt.month(), dt.day(),
               dt.hour(), dt.minute(), dt.second());
    }
    
    // Store image dimensions for EXIF
    uint16_t imgWidth = fb->width;
    uint16_t imgHeight = fb->height;
    
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
    
    // Save original JPEG with EXIF DateTime metadata (no visible overlay)
    // Timestamp is embedded in EXIF APP1 segment — parseable but invisible
    Serial.printf("Saving to %s with EXIF timestamp...\n", filename);
    File file = SD_MMC.open(filename, FILE_WRITE);
    if (file) {
      bool writeOk = writeJpegWithExif(file, fb->buf, fb->len, exifDateTime, imgWidth, imgHeight, subsecMs);
      file.close();
      delay(50);
      
      if (writeOk) {
        Serial.printf("SUCCESS: Saved %s (%d bytes)\n", filename, fb->len);
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
