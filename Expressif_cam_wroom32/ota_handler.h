#ifndef OTA_HANDLER_H
#define OTA_HANDLER_H

#include <WiFi.h>
#include <ESPmDNS.h>
#include <ArduinoOTA.h>
#include <Adafruit_ST7735.h>

// OTA Update Handler Module
// Handles WiFi connection and OTA firmware updates
class OTAHandler {
private:
  const char* ssid;
  const char* password;
  const char* hostname;
  IPAddress localIP;
  IPAddress gateway;
  IPAddress subnet;
  Adafruit_ST7735* tft;
  unsigned int lastPercentShown;
  
  // Boot log function pointers (for display feedback)
  void (*bootLogFunc)(const char*, bool);
  void (*bootLogOKFunc)();
  void (*bootLogFAILFunc)();
  
public:
  OTAHandler(const char* ssid, const char* password, const char* hostname,
             IPAddress localIP, IPAddress gateway, IPAddress subnet)
    : ssid(ssid), password(password), hostname(hostname),
      localIP(localIP), gateway(gateway), subnet(subnet),
      tft(nullptr), lastPercentShown(101),
      bootLogFunc(nullptr), bootLogOKFunc(nullptr), bootLogFAILFunc(nullptr) {}
  
  // Set TFT display for visual feedback
  void setDisplay(Adafruit_ST7735* display) {
    tft = display;
  }
  
  // Set boot log callbacks for feedback
  void setBootLogCallbacks(void (*logFunc)(const char*, bool),
                          void (*okFunc)(),
                          void (*failFunc)()) {
    bootLogFunc = logFunc;
    bootLogOKFunc = okFunc;
    bootLogFAILFunc = failFunc;
  }
  
  // Initialize WiFi and OTA
  bool begin() {
    if (bootLogFunc) bootLogFunc("WiFi init...", false);
    
    WiFi.mode(WIFI_STA);
    
    if (!WiFi.config(localIP, gateway, subnet)) {
      if (bootLogFAILFunc) bootLogFAILFunc();
      if (bootLogFunc) bootLogFunc("WiFi config failed", true);
      return false;
    }
    
    WiFi.begin(ssid, password);
    
    unsigned long startMs = millis();
    while (WiFi.status() != WL_CONNECTED && millis() - startMs < 15000) {
      delay(500);
      Serial.print('.');
    }
    
    if (WiFi.status() != WL_CONNECTED) {
      if (bootLogFAILFunc) bootLogFAILFunc();
      if (bootLogFunc) bootLogFunc("WiFi connect timeout", true);
      WiFi.disconnect(true, true);
      WiFi.mode(WIFI_OFF);
      return false;
    }
    
    if (bootLogOKFunc) bootLogOKFunc();
    Serial.print("WiFi ready: ");
    Serial.println(WiFi.localIP());
    
    if (!MDNS.begin(hostname)) {
      if (bootLogFunc) bootLogFunc("mDNS failed", true);
    }
    
    if (bootLogFunc) bootLogFunc("OTA init...", false);
    
    // Configure OTA
    ArduinoOTA.setHostname(hostname);
    ArduinoOTA.setTimeout(20000);
    
    ArduinoOTA.onStart([this]() {
      lastPercentShown = 101;
      showOTAStatus("Starting update", "Preparing flash", ST77XX_GREEN);
      Serial.println("OTA start");
    });
    
    ArduinoOTA.onEnd([this]() {
      showOTAStatus("Update complete", "Rebooting...", ST77XX_GREEN);
      Serial.println("\nOTA end");
    });
    
    ArduinoOTA.onProgress([this](unsigned int progress, unsigned int total) {
      showOTAProgress(progress, total);
      Serial.printf("OTA Progress: %u%%\r", (progress * 100U) / total);
    });
    
    ArduinoOTA.onError([this](ota_error_t error) {
      char errorText[24];
      snprintf(errorText, sizeof(errorText), "Error code: %u", error);
      showOTAStatus(otaErrorToText(error), errorText, ST77XX_RED);
      Serial.printf("OTA Error[%u]: %s\n", error, otaErrorToText(error));
    });
    
    ArduinoOTA.begin();
    
    if (bootLogOKFunc) bootLogOKFunc();
    
    // Display connection info
    char ipStr[20];
    snprintf(ipStr, sizeof(ipStr), "IP: %d.%d.%d.%d", 
             localIP[0], localIP[1], localIP[2], localIP[3]);
    if (bootLogFunc) bootLogFunc(ipStr, true);
    
    char hostStr[60];
    snprintf(hostStr, sizeof(hostStr), "OTA: %s", hostname);
    if (bootLogFunc) bootLogFunc(hostStr, true);
    
    return true;
  }
  
  // Handle OTA updates (call this in main loop)
  void handle() {
    ArduinoOTA.handle();
  }
  
  // Check if WiFi is connected
  bool isConnected() {
    return WiFi.status() == WL_CONNECTED;
  }
  
private:
  void showOTAStatus(const char* line1, const char* line2, uint16_t color) {
    if (!tft) return;
    
    tft->fillScreen(ST77XX_BLACK);
    drawText(16, 52, "OTA Update", 1, ST77XX_WHITE);
    drawText(8, 76, line1, 1, color);
    if (line2 && line2[0] != '\0') {
      drawText(8, 96, line2, 1, ST77XX_CYAN);
    }
  }
  
  void showOTAProgress(unsigned int progress, unsigned int total) {
    if (!tft || total == 0) return;
    
    unsigned int percent = (progress * 100U) / total;
    if (percent == lastPercentShown) return;
    
    lastPercentShown = percent;
    char percentText[20];
    snprintf(percentText, sizeof(percentText), "Progress: %u%%", percent);
    
    const int barX = 8;
    const int barY = 120;
    const int barWidth = 128 - 16;  // DISPLAY_WIDTH - 16
    const int barHeight = 12;
    int filled = (barWidth * static_cast<int>(percent)) / 100;
    
    tft->fillRect(0, 72, 128, 40, ST77XX_BLACK);
    drawText(8, 76, "Receiving firmware", 1, ST77XX_YELLOW);
    drawText(8, 96, percentText, 1, ST77XX_CYAN);
    tft->drawRect(barX, barY, barWidth, barHeight, ST77XX_WHITE);
    tft->fillRect(barX + 1, barY + 1, barWidth - 2, barHeight - 2, ST77XX_BLACK);
    if (filled > 2) {
      tft->fillRect(barX + 1, barY + 1, filled - 2, barHeight - 2, ST77XX_GREEN);
    }
  }
  
  void drawText(int16_t x, int16_t y, const char* text, uint8_t size, uint16_t color) {
    if (!tft) return;
    tft->setCursor(x, y);
    tft->setTextSize(size);
    tft->setTextColor(color);
    tft->setTextWrap(true);
    tft->print(text);
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
};

#endif // OTA_HANDLER_H
