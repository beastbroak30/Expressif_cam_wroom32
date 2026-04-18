#ifndef CAMERA_HUD_H
#define CAMERA_HUD_H

#include <Adafruit_GFX.h>
#include <Adafruit_ST7735.h>

// Camera HUD Overlay — Horizontal DSLR-style viewfinder UI for 128x160 ST7735
// Optimized for 90° rotated display (landscape orientation)
// Minimal RAM usage: histogram uses a single 32-bin array (64 bytes)
// All drawing is direct to TFT — no secondary framebuffer needed

#define HUD_TOP_BAR_H    10
#define HUD_WAVE_W       32   // Wave render width at bottom corner
#define HUD_WAVE_H       14   // Wave render height
#define HUD_TEMP_W       28   // Temperature box width
#define HUD_TEMP_H       12   // Temperature box height
#define HUD_HIST_BINS    32   // Bins for wave visualization

class CameraHUD {
private:
  Adafruit_ST7735* tft;
  int dispW, dispH;

  // Histogram/wave bins (reused each frame, no alloc)
  uint16_t histBins[HUD_HIST_BINS];
  uint16_t histMax;

public: 
  CameraHUD() : tft(nullptr), dispW(128), dispH(160), histMax(0) {}

  void begin(Adafruit_ST7735* display, int w, int h) {
    tft = display;
    dispW = w;
    dispH = h;
  }

  // Compute luminance histogram from RGB565 display buffer
  // Samples every 4th pixel for speed (128*160/4 = ~5K samples)
  void computeHistogram(uint16_t* buf, int bufW, int bufH) {
    memset(histBins, 0, sizeof(histBins));
    histMax = 0;

    for (int i = 0; i < bufW * bufH; i += 4) {
      uint16_t px = buf[i];
      // Extract RGB565 components (already big-endian from downsample)
      uint8_t r5 = (px >> 11) & 0x1F;
      uint8_t g6 = (px >> 5) & 0x3F;
      uint8_t b5 = px & 0x1F;

      // Fast luminance approximation: (r*2 + g + b*2) mapped to 0-255 range
      uint16_t lum = (r5 * 2 + g6 + b5 * 2);  // 0..187
      uint8_t bin = (lum * (HUD_HIST_BINS - 1)) / 187;
      if (bin >= HUD_HIST_BINS) bin = HUD_HIST_BINS - 1;

      histBins[bin]++;
    }

    // Find max for normalization
    for (int i = 0; i < HUD_HIST_BINS; i++) {
      if (histBins[i] > histMax) histMax = histBins[i];
    }
  }

  // Draw the horizontal HUD overlay
  // fps: current FPS value
  // dateTimeStr: e.g. "18/04 14:30" (compact format)  
  // photoCount: number of photos taken
  // sdReady: SD card status
  // tempC: temperature from DS3231 in Celsius
  void draw(float fps, const char* dateTimeStr, unsigned int photoCount, bool sdReady, float tempC = 0.0f) {
    if (!tft) return;

    // === TOP BAR: semi-transparent black strip (horizontal) ===
    tft->fillRect(0, 0, dispW, HUD_TOP_BAR_H, ST77XX_BLACK);

    // FPS — top-left, green
    char fpsStr[8];
    snprintf(fpsStr, sizeof(fpsStr), "%.1f", fps);
    tft->setCursor(2, 1);
    tft->setTextSize(1);
    tft->setTextColor(ST77XX_GREEN, ST77XX_BLACK);
    tft->print(fpsStr);

    // Date+Time — top-right, white
    if (dateTimeStr && dateTimeStr[0]) {
      int tw = strlen(dateTimeStr) * 6;
      tft->setCursor(dispW - tw - 1, 1);
      tft->setTextColor(ST77XX_WHITE, ST77XX_BLACK);
      tft->print(dateTimeStr);
    }

    // === BOTTOM: Temperature box (left) + Wave render (right) ===
    // Temperature display
    if (tempC != 0.0f) {
      drawTemperature(2, dispH - HUD_TEMP_H - 2, tempC);
    }
    
    // Wave render — bottom-right corner
    drawWaveform(dispW - HUD_WAVE_W - 2, dispH - HUD_WAVE_H - 2);
  }

  // Draw temperature box at given position
  void drawTemperature(int x, int y, float tempC) {
    if (!tft) return;
    
    // Dark background
    tft->fillRect(x, y, HUD_TEMP_W, HUD_TEMP_H, 0x0000);
    
    // Temperature text: "25°C" format
    char tempStr[8];
    snprintf(tempStr, sizeof(tempStr), "%d%cC", (int)tempC, 0xF8);  // 0xF8 = degree symbol
    
    tft->setCursor(x + 2, y + 2);
    tft->setTextSize(1);
    tft->setTextColor(ST77XX_YELLOW, ST77XX_BLACK);
    tft->print(tempStr);
  }

  // Draw waveform visualization at given position (audio-style wave bars)
  // Uses luminance histogram data to create wave effect
  void drawWaveform(int x, int y) {
    if (!tft || histMax == 0) return;

    // Semi-transparent dark background
    tft->fillRect(x, y, HUD_WAVE_W, HUD_WAVE_H, 0x0000);

    // Draw wave bars
    for (int i = 0; i < HUD_HIST_BINS; i++) {
      int barH = (histBins[i] * (HUD_WAVE_H - 2)) / histMax;
      if (barH < 1 && histBins[i] > 0) barH = 1;
      if (barH > HUD_WAVE_H - 2) barH = HUD_WAVE_H - 2;

      if (barH > 0) {
        // Wave color: cyan gradient
        uint16_t barColor;
        if (barH > (HUD_WAVE_H * 2 / 3)) {
          barColor = ST77XX_WHITE;  // Peak
        } else if (barH > (HUD_WAVE_H / 3)) {
          barColor = ST77XX_CYAN;   // Mid
        } else {
          barColor = 0x0410;        // Dark cyan
        }

        int bx = x + i;
        int by = y + HUD_WAVE_H - 1 - barH;
        tft->drawFastVLine(bx, by, barH, barColor);
      }
    }
  }
};

#endif // CAMERA_HUD_H
