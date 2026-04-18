#ifndef RTC_HANDLER_H
#define RTC_HANDLER_H

#include <Wire.h>
#include <RTClib.h>
#include <time.h>       // For NTP sync
#include <esp_sntp.h>   // For sntp_get_sync_status

// NTP Configuration for IST (India Standard Time = GMT+5:30)
#define NTP_SERVER1       "pool.ntp.org"
#define NTP_SERVER2       "time.google.com"
// TZ string: IST-5:30 (no DST in India)
#define NTP_TZ_IST        "IST-5:30"

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
      // Add 5 minutes to compensate for compile/upload delay
      DateTime compileTime(F(__DATE__), F(__TIME__));
      DateTime adjustedTime = compileTime + TimeSpan(0, 0, 5, 0);  // +5 minutes
      rtc.adjust(adjustedTime);
      Serial.println("RTC lost power, set to compile time + 5min (IST)");
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

  // Format: "DD/MM/YYYY HH:MM" full date+time for display
  void getDateTimeFullStr(char* buf, size_t len) {
    DateTime dt = now();
    snprintf(buf, len, "%02d/%02d/%04d %02d:%02d",
             dt.day(), dt.month(), dt.year(), dt.hour(), dt.minute());
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

  // Sync RTC with NTP time (call after WiFi connected)
  // Returns true if sync successful
  bool syncWithNTP(unsigned long timeoutMs = 10000) {
    if (!initialized) return false;
    
    Serial.println("Starting NTP sync...");
    
    // Configure timezone and NTP servers using TZ string
    configTzTime(NTP_TZ_IST, NTP_SERVER1, NTP_SERVER2);
    
    // Wait for SNTP sync to actually complete
    unsigned long startMs = millis();
    while (sntp_get_sync_status() != SNTP_SYNC_STATUS_COMPLETED) {
      if (millis() - startMs > timeoutMs) {
        Serial.println("NTP sync failed - timeout waiting for SNTP");
        return false;
      }
      delay(100);
    }
    
    // Now get the synced time
    struct tm timeinfo;
    if (!getLocalTime(&timeinfo, 1000)) {
      Serial.println("NTP sync failed - getLocalTime failed");
      return false;
    }
    
    // Update DS3231 RTC with NTP time
    DateTime ntpTime(
      timeinfo.tm_year + 1900,
      timeinfo.tm_mon + 1,
      timeinfo.tm_mday,
      timeinfo.tm_hour,
      timeinfo.tm_min,
      timeinfo.tm_sec
    );
    
    rtc.adjust(ntpTime);
    
    Serial.printf("RTC synced via NTP: %02d/%02d/%04d %02d:%02d:%02d\n",
                  timeinfo.tm_mday, timeinfo.tm_mon + 1, timeinfo.tm_year + 1900,
                  timeinfo.tm_hour, timeinfo.tm_min, timeinfo.tm_sec);
    return true;
  }
};

#endif // RTC_HANDLER_H
