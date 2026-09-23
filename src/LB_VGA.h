#pragma once
/*
 * LB_VGA - VGA 640x480@60 output for the ESP32-S3, drawn with LovyanGFX.
 *
 * Three lines to a picture:
 *
 *     #include <LB_VGA.h>
 *     void setup() { VGA.begin(); VGA.fillScreen(TFT_BLACK); VGA.drawString("Hi", 10, 10); }
 *
 * VGA *is* an LGFX_Sprite, so everything LovyanGFX can do works directly on it:
 * drawString / fillCircle / drawJpg / drawPng / setFont / anti-aliased shapes.
 * There is no separate canvas object to fetch.
 *
 *
 * ============================ Why two modes ============================
 *
 * This is forced by measurement, not taste. Full numbers are in the project
 * README; the short version:
 *
 *   A 640x480 full-colour (RGB565) framebuffer needs 600 KB. It does not fit in
 *   internal SRAM (the chip has 512 KB total), and putting it in PSRAM gets it
 *   killed by bandwidth: PSRAM reads measure ~60 MB/s here, and displaying
 *   640x480@60 alone costs 36 MB/s of that (68%). The remaining third has to be
 *   shared with your drawing, so redrawing a quarter of the screen per frame
 *   already starts dropping scan lines, and a full-screen redraw breaks the
 *   picture outright - while fps keeps happily reporting 60, so the frame rate
 *   tells you nothing.
 *
 * Both modes therefore keep the framebuffer in internal SRAM, and both happen to
 * need exactly 150 KB:
 *
 *   LB_VGA_320x240      320x240 RGB565   65536 colours   ISR 25%   text 40x15
 *   LB_VGA_640x480_16   640x480 4bpp     16 colours      ISR 46%   text 80x30
 *
 * Measured late-line count stays 0 in both modes even when redrawing the whole
 * screen three times per frame. The cost is constant CPU (pixel doubling or a
 * palette lookup) and is completely independent of how much you draw - unlike
 * the PSRAM approach, which collapses as soon as you touch the framebuffer.
 *
 * Picking a mode:
 *   photos, gradients, smooth animation -> LB_VGA_320x240
 *   crisp text and charts               -> LB_VGA_640x480_16
 *                                          (80x30 chars: classic DOS text mode)
 *
 *
 * ============================ Two things to know ============================
 *
 * 1) The scan-line ISR runs on core 0 (esp_intr_alloc binds the interrupt to
 *    whichever core calls it; begin() pins that for you). In 16-colour mode it
 *    takes 46% of core 0, so put your own heavy work on core 1.
 *
 * 2) Do not write per-pixel loops in 16-colour mode. Changing one pixel in a
 *    4bpp buffer is a read-modify-write of half a byte; going through
 *    LovyanGFX's drawPixel measures ~0.92 us per pixel (282 ms for a full
 *    640x480 screen). Use fillRect / drawString / drawJpg, which take the
 *    byte-wide fast path.
 */
#include <Arduino.h>
#include <LovyanGFX.hpp>
#include "LB_VGA_pins.h"

enum LB_VGA_Mode
{
  LB_VGA_320x240 = 0,    /* RGB565, for photos and animation */
  LB_VGA_640x480_16 = 1, /* 4bpp palette, for text and charts */
};

class LB_VGA_Class : public LGFX_Sprite
{
public:
  /*
   * Call once from setup(). On failure the reason is printed to Serial; the
   * usual cause is PSRAM not set to OPI in the Tools menu, which leaves the
   * heap too small.
   *
   * The mode cannot be changed at run time: the framebuffer and the scan-line
   * callback are fixed here. A second call returns the first call's result
   * without switching mode and without reporting an error. One mode per sketch;
   * flash twice to compare them.
   */
  bool begin(LB_VGA_Mode mode = LB_VGA_320x240);

  /*
   * Block until the next frame starts. Drawing right after this returns gives
   * you the vertical blanking interval (~1.44 ms) before the beam reaches the
   * top of the screen, which is how you avoid tearing without a second buffer.
   *
   * In 16-colour mode this also refreshes the palette copy the ISR uses, so a
   * palette written through the LGFX_Sprite base class still takes effect.
   */
  void waitVSync(uint32_t timeout_ms = 100);

  LB_VGA_Mode mode() const { return _mode; }

  /* Fields since boot. Subtract two readings a second apart to get the refresh
   * rate; it should sit at 59.5 Hz. */
  uint32_t frameCount() const;

  /* ---- 16-colour mode only ---- */

  /*
   * Set one palette entry. Prefer this over the LGFX_Sprite versions: the ISR
   * reads a private RGB565 copy of the palette, and only this overload updates
   * both. (waitVSync() also refreshes the copy, so the base-class versions are
   * not dangerous, just one frame late.)
   */
  void setPaletteColor(uint8_t index, uint8_t r, uint8_t g, uint8_t b);

  /* Rebuild the ISR's palette copy from the sprite's palette. Called for you by
   * waitVSync(); you only need it if you change the palette and then draw
   * without waiting for a frame. */
  void syncPalette();

  /*
   * Share of one CPU core spent generating the video signal, in percent.
   * Expect ~25 in 320x240 and ~46 in 640x480x16.
   *
   * Reads are destructive: the accumulators are reset, so each call reports the
   * interval since the previous call. Calling it twice in a row gives you a
   * meaningful number followed by a meaningless one.
   */
  float isrLoadPercent();

private:
  LB_VGA_Mode _mode = LB_VGA_320x240;
  bool _ready = false;
};

extern LB_VGA_Class VGA;
