#ifndef CAMERA_HUD_H
#define CAMERA_HUD_H

#include <Adafruit_GFX.h>
#include <Adafruit_ST7735.h>

// Camera HUD Overlay — DSLR-style viewfinder UI for 128x160 ST7735
// Designed for minimal RAM usage: histogram uses a single 64-bin array (64 bytes)
// All drawing is direct to TFT — no secondary framebuffer needed

#define HUD_TOP_BAR_H    10
#define HUD_BOTTOM_BAR_H 22
#define HUD_HIST_W       48   // Histogram width in pixels
#define HUD_HIST_H       18   // Histogram height in pixels
#define HUD_HIST_BINS    48   // One bin per pixel column

class CameraHUD {
private:
  Adafruit_ST7735* tft;
  int dispW, dispH;

  // Histogram bins (reused each frame, no alloc)
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
      // r5 max=31, g6 max=63, b5 max=31; weighted sum max = 31*2+63+31*2 = 187
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

  // Draw the full HUD overlay
  // fps: current FPS value
  // dateTimeStr: e.g. "05/04/2026 14:30" (horizontal, top-right)
  // photoCount: number of photos taken
  // sdReady: SD card status
  void draw(float fps, const char* dateTimeStr, unsigned int photoCount, bool sdReady) {
    if (!tft) return;

    // === TOP BAR: semi-transparent black strip ===
    tft->fillRect(0, 0, dispW, HUD_TOP_BAR_H, ST77XX_BLACK);

    // FPS — top-left, green
    char fpsStr[10];
    snprintf(fpsStr, sizeof(fpsStr), "%.1f", fps);
    tft->setCursor(2, 1);
    tft->setTextSize(1);
    tft->setTextColor(ST77XX_GREEN);
    tft->print(fpsStr);

    // Date+Time — top-right, white
    if (dateTimeStr && dateTimeStr[0]) {
      int tw = strlen(dateTimeStr) * 6;
      tft->setCursor(dispW - tw - 1, 1);
      tft->setTextColor(ST77XX_WHITE);
      tft->print(dateTimeStr);
    }

    // === BOTTOM BAR ===
    int bottomY = dispH - HUD_BOTTOM_BAR_H;
    tft->fillRect(0, bottomY, dispW, HUD_BOTTOM_BAR_H, ST77XX_BLACK);

    // Photo count + SD indicator — bottom-left
    char infoStr[20];
    snprintf(infoStr, sizeof(infoStr), "%c %03d", sdReady ? '\x07' : '!', photoCount);  // bullet or !
    tft->setCursor(2, bottomY + 2);
    tft->setTextSize(1);
    tft->setTextColor(sdReady ? ST77XX_CYAN : ST77XX_RED);
    tft->print("SD");
    tft->setCursor(2, bottomY + 12);
    tft->setTextColor(ST77XX_YELLOW);
    char cntStr[8];
    snprintf(cntStr, sizeof(cntStr), "#%03d", photoCount);
    tft->print(cntStr);

    // Free heap indicator — bottom-center
    uint32_t freeKB = ESP.getFreeHeap() / 1024;
    char heapStr[10];
    snprintf(heapStr, sizeof(heapStr), "%dK", freeKB);
    int heapW = strlen(heapStr) * 6;
    tft->setCursor((dispW - HUD_HIST_W) / 2 - heapW / 2 + 10, bottomY + 7);
    tft->setTextSize(1);
    tft->setTextColor(freeKB > 30 ? ST77XX_GREEN : ST77XX_RED);
    tft->print(heapStr);

    // === HISTOGRAM — bottom-right ===
    drawHistogram(dispW - HUD_HIST_W - 2, bottomY + 2);
  }

  // Draw histogram at given position
  // Uses translucent effect: dark gray background with colored bars
  void drawHistogram(int x, int y) {
    if (!tft || histMax == 0) return;

    // Draw dark background for histogram (translucent feel)
    tft->fillRect(x, y, HUD_HIST_W, HUD_HIST_H, 0x2104);  // Very dark gray RGB565

    // Thin white border
    tft->drawRect(x, y, HUD_HIST_W, HUD_HIST_H, 0x4208);  // Dark gray border

    // Draw histogram bars
    for (int i = 0; i < HUD_HIST_BINS; i++) {
      int barH = (histBins[i] * (HUD_HIST_H - 2)) / histMax;
      if (barH < 1 && histBins[i] > 0) barH = 1;

      if (barH > 0) {
        // Color gradient: shadows=blue, mids=green, highlights=red-ish
        uint16_t barColor;
        if (i < HUD_HIST_BINS / 3) {
          barColor = 0x001F;  // Blue for shadows
        } else if (i < 2 * HUD_HIST_BINS / 3) {
          barColor = 0x07E0;  // Green for midtones
        } else {
          barColor = 0xFFE0;  // Yellow for highlights
        }

        int bx = x + 1 + i;
        int by = y + HUD_HIST_H - 1 - barH;
        tft->drawFastVLine(bx, by, barH, barColor);
      }
    }
  }
};

#endif // CAMERA_HUD_H
