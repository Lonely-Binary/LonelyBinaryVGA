#pragma once
/*
 * LonelyBinaryVGA - VGA 640x480@60 from an ESP32-S3, through the LCD_CAM
 * parallel peripheral and a 16-bit R2R ladder.
 *
 *     #include <LonelyBinaryVGA.h>
 *
 *     LB_VGA vga(LB_VGA_320x240);
 *
 *     void setup() {
 *       vga.begin();
 *       vga.fillScreen(LB_BLACK);
 *       vga.drawString("Hello", 10, 10);
 *     }
 *
 * Drawing comes from Lonely Binary GFX, so the same calls work on a TFT and on
 * e-paper. A function that takes an LB_Canvas& runs on all three.
 *
 *
 * ============================ Why two modes ============================
 *
 * Forced by measurement, not taste. A 640x480 full-colour framebuffer needs
 * 600 KB: it does not fit in the chip's 512 KB of internal SRAM, and putting it
 * in PSRAM gets it killed by bandwidth. PSRAM reads measure ~60 MB/s here and
 * displaying 640x480@60 alone costs 36 MB/s of that, so redrawing a quarter of
 * the screen per frame already drops scan lines and a full-screen redraw breaks
 * the picture - while fps keeps reporting 60, so the frame rate tells you
 * nothing.
 *
 * Both modes therefore keep the framebuffer in internal SRAM, and both happen
 * to need exactly 150 KB:
 *
 *   LB_VGA_320x240      320x240 RGB565   65536 colours   ISR 25%   text 40x15
 *   LB_VGA_640x480_16   640x480 4bpp     16 colours      ISR 46%   text 80x30
 *
 * Measured late-line count stays 0 in both, even when redrawing the whole
 * screen three times per frame: the cost is constant CPU work and does not
 * depend on how much you draw.
 *
 *   photos, gradients, smooth animation -> LB_VGA_320x240
 *   crisp text and charts               -> LB_VGA_640x480_16
 *                                          (80x30 characters: DOS text mode)
 *
 *
 * ============================ Two things to know ============================
 *
 * 1) The scan-line ISR runs on core 0 and takes 25% of it in 320x240, 46% in
 *    640x480x16. Pin heavy work of your own to core 1.
 *
 * 2) Do not write per-pixel loops in 16-colour mode: one pixel is a
 *    read-modify-write of half a byte. fillRect, drawString and drawJpg fill
 *    whole bytes and are 57x faster (measured).
 */
#include <LonelyBinaryGFX.h>
#include "LB_VGA_Pins.h"

enum LB_VGA_Mode : uint8_t
{
  LB_VGA_320x240 = 0,    /* RGB565, for photos and animation */
  LB_VGA_640x480_16 = 1, /* 4bpp palette, for text and charts */
};

/* The device side: geometry, framebuffer, palette, scan-out. You do not use
 * this directly - LB_VGA inherits it privately and hands it to LB_Canvas. */
class LB_VGA_Panel : public LB_Panel
{
public:
  explicit LB_VGA_Panel(LB_VGA_Mode mode) : _mode(mode) {}

  bool start(); /* allocate the framebuffer and bring up the LCD peripheral */

  int16_t panelWidth() const override { return _mode == LB_VGA_320x240 ? 320 : 640; }
  int16_t panelHeight() const override { return _mode == LB_VGA_320x240 ? 240 : 480; }
  lb_format_t format() const override
  {
    return _mode == LB_VGA_320x240 ? LB_FMT_RGB565 : LB_FMT_PAL4;
  }
  uint8_t *buffer() const override { return _fb; }
  uint32_t stride() const override { return _mode == LB_VGA_320x240 ? 640 : 320; }

  /* A no-op, and it has to exist anyway. VGA scans the framebuffer continuously
   * sixty times a second, so nothing needs pushing - but a portable sketch
   * calls flush() and must not have to know that. */
  void flush(lb_flush_t = LB_FLUSH_FULL) override {}

  uint16_t paletteSize() const override { return _mode == LB_VGA_320x240 ? 0 : 16; }
  const lb_color_t *palette() const override
  {
    return _mode == LB_VGA_320x240 ? nullptr : _pal;
  }
  bool setPaletteColor(uint8_t index, uint8_t r, uint8_t g, uint8_t b) override;

  LB_VGA_Mode mode() const { return _mode; }

private:
  LB_VGA_Mode _mode;
  uint8_t *_fb = nullptr;
  lb_color_t _pal[16] = {0};
};

class LB_VGA : private LB_VGA_Panel, public LB_Canvas
{
public:
  /*
   * Two things about this declaration, both deliberate.
   *
   * Order: base classes are initialised in declaration order, so LB_VGA_Panel
   * is fully constructed before LB_Canvas is handed a reference to it. Holding
   * the panel as a *member* instead would pass a reference to memory that is
   * not built yet - legal only by a technicality, and not worth relying on.
   *
   * PRIVATE, plus the using-declarations below: LB_Panel and LB_Canvas both
   * declare flush(), sleep(), supportsPartial(), paletteSize() and
   * setPaletteColor(). Private inheritance is NOT enough on its own - C++ looks
   * names up before it checks access, so the call stays ambiguous and the error
   * points at the customer's sketch rather than at this header. The
   * using-declarations pick the canvas side explicitly.
   */
  explicit LB_VGA(LB_VGA_Mode mode = LB_VGA_320x240)
      : LB_VGA_Panel(mode), LB_Canvas(*static_cast<LB_VGA_Panel *>(this)) {}

  /*
   * Call once from setup(). Prints the reason on failure; the usual cause is
   * PSRAM not set to OPI in the Tools menu, which leaves the heap too small
   * even though this library never uses PSRAM itself.
   *
   * The mode cannot change afterwards: the framebuffer and the scan-line
   * callback are fixed here. One mode per sketch.
   */
  bool begin();

  /*
   * Block until the next frame starts, then you have ~1.44 ms of vertical
   * blanking before the beam reaches the top of the screen. That is how you
   * avoid tearing without a second buffer - and there is no room for a second
   * buffer, since after the 150 KB framebuffer the largest free block of
   * internal SRAM is about 135 KB.
   */
  void waitVSync(uint32_t timeout_ms = 100);

  /* Fields since boot. Two readings a second apart give the refresh rate, which
   * should sit at 59.5 Hz. Divide by the REAL elapsed time, not by a nominal
   * 1000 ms, or you will compute a rate above what the pixel clock allows. */
  uint32_t frameCount() const;

  /*
   * Share of one core spent generating the video signal. Expect ~25 in 320x240
   * and ~46 in 640x480x16.
   *
   * Reads are destructive: each call reports the interval since the previous
   * one, so calling it twice in a row gives a real number and then a
   * meaningless one.
   */
  float isrLoadPercent();

  /* Resolve the five names LB_Panel and LB_Canvas share, in favour of the
   * canvas. Without these every one of them is ambiguous at the call site. */
  using LB_Canvas::flush;
  using LB_Canvas::sleep;
  using LB_Canvas::supportsPartial;
  using LB_Canvas::paletteSize;
  using LB_Canvas::setPaletteColor;

  /* Re-exported past the private base: which mode begin() brought up. */
  using LB_VGA_Panel::mode;
};
