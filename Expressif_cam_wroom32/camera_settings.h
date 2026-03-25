#ifndef CAMERA_SETTINGS_H
#define CAMERA_SETTINGS_H

#include "esp_camera.h"

// Camera sensor settings for realistic colors
// Based on community best practices and Random Nerd Tutorials guidance
class CameraSettings {
public:
  // Apply optimal sensor settings for RGB565 live view mode
  static void configureSensorForLiveView(sensor_t *s) {
    if (!s) return;
    
    // Color correction settings
    s->set_brightness(s, 0);      // -2 to 2 (0 = neutral)
    s->set_contrast(s, 0);        // -2 to 2 (0 = neutral)
    s->set_saturation(s, 0);      // -2 to 2 (0 is natural, avoid over-saturation)
    
    // White balance - CRITICAL for fixing greenish tint
    s->set_whitebal(s, 1);        // Enable auto white balance
    s->set_awb_gain(s, 1);        // Enable AWB gain control
    s->set_wb_mode(s, 0);         // 0=Auto, 1=Sunny, 2=Cloudy, 3=Office, 4=Home
    
    // Exposure settings
    s->set_exposure_ctrl(s, 1);   // Enable auto exposure
    s->set_aec2(s, 1);            // Enable AEC DSP
    s->set_ae_level(s, 0);        // -2 to 2 (exposure compensation)
    s->set_aec_value(s, 300);     // 0-1200 (manual exposure value, used when AEC disabled)
    
    // Gain settings
    s->set_gain_ctrl(s, 1);       // Enable auto gain
    s->set_agc_gain(s, 0);        // 0-30 (manual gain, used when AGC disabled)
    s->set_gainceiling(s, (gainceiling_t)2);  // 0-6 (gain ceiling: 2x, 4x, 8x, 16x, 32x, 64x, 128x)
    
    // Image quality enhancements
    s->set_bpc(s, 1);             // Enable black pixel correction
    s->set_wpc(s, 1);             // Enable white pixel correction  
    s->set_raw_gma(s, 1);         // Enable gamma correction
    s->set_lenc(s, 1);            // Enable lens correction
    
    // Orientation
    s->set_hmirror(s, 0);         // Horizontal mirror: 0=off, 1=on
    s->set_vflip(s, 0);           // Vertical flip: 0=off, 1=on
    
    // Other settings
    s->set_dcw(s, 1);             // Enable downsize (improves frame rate)
    s->set_colorbar(s, 0);        // Disable color bar test pattern
    s->set_special_effect(s, 0);  // 0=No effect, 1=Negative, 2=Grayscale, etc.
  }
  
  // Apply optimal sensor settings for JPEG photo capture
  // This is where we fix the greenish tint in saved photos
  static void configureSensorForPhotoCapture(sensor_t *s) {
    if (!s) return;
    
    // Color correction - slightly different for JPEG
    s->set_brightness(s, 0);      // Neutral brightness
    s->set_contrast(s, 0);        // Neutral contrast
    s->set_saturation(s, 0);      // Natural saturation (not 1, which can over-saturate)
    
    // White balance - KEY FIX FOR GREENISH TINT
    s->set_whitebal(s, 1);        // Enable auto white balance - CRITICAL!
    s->set_awb_gain(s, 1);        // Enable AWB gain - CRITICAL!
    s->set_wb_mode(s, 0);         // Auto WB mode (let camera decide)
    
    // Exposure settings for photo
    s->set_exposure_ctrl(s, 1);   // Enable auto exposure
    s->set_aec2(s, 1);            // Enable AEC DSP
    s->set_ae_level(s, 0);        // Neutral exposure compensation
    s->set_aec_value(s, 300);     // Manual exposure value
    
    // Gain settings for photo
    s->set_gain_ctrl(s, 1);       // Enable auto gain
    s->set_agc_gain(s, 0);        // Manual gain value
   s->set_gainceiling(s, (gainceiling_t)2);  // Moderate gain ceiling
    
    // Image quality enhancements
    s->set_bpc(s, 1);             // Black pixel correction
    s->set_wpc(s, 1);             // White pixel correction
    s->set_raw_gma(s, 1);         // Gamma correction
    s->set_lenc(s, 1);            // Lens correction - helps with color uniformity
    
    // Orientation (same as live view)
    s->set_hmirror(s, 0);
    s->set_vflip(s, 0);
    
    // Photo-specific settings
    s->set_dcw(s, 1);             // Enable downsize
    s->set_colorbar(s, 0);        // Disable test pattern
    s->set_special_effect(s, 0);  // No special effects
  }
  
  // Wait for auto white balance and auto exposure to stabilize
  // Call this after changing camera mode or settings
  static void waitForAutoSettingsToStabilize(int delayMs = 500) {
    delay(delayMs);
    
    // Discard first few frames to let auto settings converge
    // This is CRITICAL for good color accuracy
    for (int i = 0; i < 5; i++) {  // Increased from 3 to 5 frames
      camera_fb_t *fb = esp_camera_fb_get();
      if (fb) {
        esp_camera_fb_return(fb);
      }
      delay(100);  // Give camera time between frames
    }
  }
};

#endif // CAMERA_SETTINGS_H
