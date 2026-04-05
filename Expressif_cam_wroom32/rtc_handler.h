#ifndef RTC_HANDLER_H
#define RTC_HANDLER_H

#include <Wire.h>
#include <RTClib.h>

// DS3231 RTC Handler
// Uses GPIO 3 (RX/SDA) and GPIO 1 (TX/SCL) for I2C
// NOTE: These are UART pins — Serial.begin() must NOT be called before Wire,
//       and Serial output will be unavailable while I2C is active.
//       The DS3231 is read once at boot and cached, so Serial works after init.

#define RTC_SDA_PIN 3   // GPIO3 = U0RXD (exposed on header)
#define RTC_SCL_PIN 1   // GPIO1 = U0TXD (exposed on header)

class RTCHandler {
private:
  RTC_DS3231 rtc;
  bool initialized;

public:
  RTCHandler() : initialized(false) {}

  bool begin() {
    Wire.begin(RTC_SDA_PIN, RTC_SCL_PIN);
    if (!rtc.begin(&Wire)) {
      Serial.println("DS3231 RTC not found!");
      return false;
    }
    initialized = true;

    // If RTC lost power, set to compile time
    if (rtc.lostPower()) {
      Serial.println("RTC lost power, setting to compile time");
      rtc.adjust(DateTime(F(__DATE__), F(__TIME__)));
    }

    return true;
  }

  bool isAvailable() { return initialized; }

  DateTime now() {
    if (!initialized) return DateTime(2000, 1, 1, 0, 0, 0);
    return rtc.now();
  }

  // Format: "HH:MM:SS" for live display
  void getTimeStr(char* buf, size_t len) {
    DateTime dt = now();
    snprintf(buf, len, "%02d:%02d:%02d", dt.hour(), dt.minute(), dt.second());
  }

  // Format: "DD/MM/YY HH:MM" for photo stamp
  void getTimestampStr(char* buf, size_t len) {
    DateTime dt = now();
    snprintf(buf, len, "%02d/%02d/%02d %02d:%02d",
             dt.day(), dt.month(), dt.year() % 100,
             dt.hour(), dt.minute());
  }

  // Format for filename: "YYYYMMDD_HHMMSS"
  void getFilenameStr(char* buf, size_t len) {
    DateTime dt = now();
    snprintf(buf, len, "%04d%02d%02d_%02d%02d%02d",
             dt.year(), dt.month(), dt.day(),
             dt.hour(), dt.minute(), dt.second());
  }

  // Draw timestamp on raw RGB565 buffer using minimal 3x5 font
  // Draws white text with black outline for visibility
  // x, y = top-left corner, bufW = buffer width in pixels
  static void drawTimestampOnRGB565(uint16_t* buf, int bufW, int bufH,
                                     int x, int y, const char* text) {
    // Minimal 3x5 font for digits, colon, slash, space
    static const uint8_t font3x5[][3] = {
      {0x1F,0x11,0x1F}, // 0
      {0x00,0x1F,0x00}, // 1
      {0x1D,0x15,0x17}, // 2
      {0x15,0x15,0x1F}, // 3
      {0x07,0x04,0x1F}, // 4
      {0x17,0x15,0x1D}, // 5
      {0x1F,0x15,0x1D}, // 6
      {0x01,0x01,0x1F}, // 7
      {0x1F,0x15,0x1F}, // 8
      {0x17,0x15,0x1F}, // 9
      {0x00,0x0A,0x00}, // : (index 10)
      {0x10,0x08,0x04}, // / (index 11)
      {0x00,0x00,0x00}, // space (index 12)
    };

    // White in big-endian RGB565
    const uint16_t white = 0xFFFF;
    const uint16_t black = 0x0000;

    int cx = x;
    for (const char* p = text; *p; p++) {
      int idx = -1;
      if (*p >= '0' && *p <= '9') idx = *p - '0';
      else if (*p == ':') idx = 10;
      else if (*p == '/') idx = 11;
      else idx = 12; // space or unknown

      if (idx < 0) { cx += 4; continue; }

      // Draw black outline first, then white character
      for (int pass = 0; pass < 2; pass++) {
        uint16_t color = (pass == 0) ? black : white;
        int ox = (pass == 0) ? 1 : 0;
        int oy = (pass == 0) ? 1 : 0;

        if (pass == 0) {
          // Draw outline at 4 offsets
          int offsets[][2] = {{-1,0},{1,0},{0,-1},{0,1}};
          for (auto& off : offsets) {
            for (int col = 0; col < 3; col++) {
              uint8_t colData = font3x5[idx][col];
              for (int row = 0; row < 5; row++) {
                if (colData & (1 << row)) {
                  int px = cx + col + off[0];
                  int py = y + row + off[1];
                  if (px >= 0 && px < bufW && py >= 0 && py < bufH) {
                    buf[py * bufW + px] = black;
                  }
                }
              }
            }
          }
        } else {
          for (int col = 0; col < 3; col++) {
            uint8_t colData = font3x5[idx][col];
            for (int row = 0; row < 5; row++) {
              if (colData & (1 << row)) {
                int px = cx + col;
                int py = y + row;
                if (px >= 0 && px < bufW && py >= 0 && py < bufH) {
                  buf[py * bufW + px] = white;
                }
              }
            }
          }
        }
      }
      cx += 4; // 3px char + 1px gap
    }
  }

  // Scale up version for high-res photos (2x scale)
  static void drawTimestampOnRGB565Scaled(uint16_t* buf, int bufW, int bufH,
                                           int x, int y, const char* text, int scale) {
    static const uint8_t font3x5[][3] = {
      {0x1F,0x11,0x1F}, // 0
      {0x00,0x1F,0x00}, // 1
      {0x1D,0x15,0x17}, // 2
      {0x15,0x15,0x1F}, // 3
      {0x07,0x04,0x1F}, // 4
      {0x17,0x15,0x1D}, // 5
      {0x1F,0x15,0x1D}, // 6
      {0x01,0x01,0x1F}, // 7
      {0x1F,0x15,0x1F}, // 8
      {0x17,0x15,0x1F}, // 9
      {0x00,0x0A,0x00}, // :
      {0x10,0x08,0x04}, // /
      {0x00,0x00,0x00}, // space
    };

    const uint16_t white = 0xFFFF;
    const uint16_t black = 0x0000;

    int cx = x;
    for (const char* p = text; *p; p++) {
      int idx = -1;
      if (*p >= '0' && *p <= '9') idx = *p - '0';
      else if (*p == ':') idx = 10;
      else if (*p == '/') idx = 11;
      else idx = 12;

      if (idx < 0) { cx += 4 * scale; continue; }

      // Black outline
      int offsets[][2] = {{-1,0},{1,0},{0,-1},{0,1}};
      for (auto& off : offsets) {
        for (int col = 0; col < 3; col++) {
          uint8_t colData = font3x5[idx][col];
          for (int row = 0; row < 5; row++) {
            if (colData & (1 << row)) {
              for (int sy = 0; sy < scale; sy++) {
                for (int sx = 0; sx < scale; sx++) {
                  int px = cx + col * scale + sx + off[0];
                  int py = y + row * scale + sy + off[1];
                  if (px >= 0 && px < bufW && py >= 0 && py < bufH)
                    buf[py * bufW + px] = black;
                }
              }
            }
          }
        }
      }
      // White character
      for (int col = 0; col < 3; col++) {
        uint8_t colData = font3x5[idx][col];
        for (int row = 0; row < 5; row++) {
          if (colData & (1 << row)) {
            for (int sy = 0; sy < scale; sy++) {
              for (int sx = 0; sx < scale; sx++) {
                int px = cx + col * scale + sx;
                int py = y + row * scale + sy;
                if (px >= 0 && px < bufW && py >= 0 && py < bufH)
                  buf[py * bufW + px] = white;
              }
            }
          }
        }
      }
      cx += 4 * scale;
    }
  }
};

#endif // RTC_HANDLER_H
