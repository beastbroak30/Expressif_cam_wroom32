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

    // Only set RTC time if it lost power (battery dead/first use)
    // Otherwise keep the running time from the DS3231 battery backup
    if (rtc.lostPower()) {
      // __DATE__/__TIME__ are IST when compiled with TZ=Asia/Kolkata in workflow
      DateTime compileTime(F(__DATE__), F(__TIME__));
      rtc.adjust(compileTime);
      Serial.println("RTC lost power, set to compile time (IST)");
    } else {
      Serial.println("RTC running from battery backup");
    }

    return true;
  }

  bool isAvailable() { return initialized; }

  DateTime now() {
    if (!initialized) return DateTime(2000, 1, 1, 0, 0, 0);
    return rtc.now();
  }

  // Get temperature from DS3231 in Celsius
  float getTemperature() {
    if (!initialized) return 0.0f;
    return rtc.getTemperature();
  }

  // Format: "HH:MM:SS" for live display
  void getTimeStr(char* buf, size_t len) {
    DateTime dt = now();
    snprintf(buf, len, "%02d:%02d:%02d", dt.hour(), dt.minute(), dt.second());
  }

  // Format: "DD/MM HH:MM" compact date+time for TFT overlay
  void getDateTimeCompactStr(char* buf, size_t len) {
    DateTime dt = now();
    snprintf(buf, len, "%02d/%02d %02d:%02d",
             dt.day(), dt.month(), dt.hour(), dt.minute());
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
};

#endif // RTC_HANDLER_H
