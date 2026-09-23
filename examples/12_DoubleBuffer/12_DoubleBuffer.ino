/*
 * 12_DoubleBuffer - a back buffer in PSRAM, and page flipping.
 *
 * ====================== First, a correction to 07_Photo ======================
 *
 * That example's comments said: "to make a photo appear all at once you have to
 * decode into a second buffer and copy the finished image across, and this board
 * cannot spare a second 150 KB of internal SRAM."
 *
 * **It missed an option: there are 8 MB of PSRAM sitting idle.**
 *
 * PSRAM was ruled out for the *framebuffer* because the display ISR reads it at
 * 36 MB/s, every frame, forever. A back buffer is read once per flip, which is a
 * completely different proposition.
 *
 *     front buffer   150 KB internal SRAM   the display ISR reads this
 *     back buffer    150 KB PSRAM           you draw here, unobserved
 *     flip           wait for blanking, copy across
 *
 *
 * =============================== Measurements ===============================
 *
 *   flip, 150 KB PSRAM -> SRAM      2963 - 2996 us   (<1% jitter)
 *   vertical blanking interval        1440 us        -> the flip takes 206%
 *   paint a full screen in SRAM        662 us
 *   paint a full screen in PSRAM      5072 us        -> 7.7x slower
 *
 *
 * ================ Why 206% of blanking still does not tear ================
 *
 * What matters is not whether the copy fits in the blanking interval, but
 * **whether it outruns the electron beam**:
 *
 *     copy    240 framebuffer lines / 2.97 ms  = 12.4 us per line
 *     beam    240 framebuffer lines / 16.13 ms = 67   us per line
 *
 * The copy is 5.4x faster. Both start at line 0, the copy pulls ahead
 * immediately and the beam never catches it, so no seam appears. Confirmed on
 * hardware.
 *
 *
 * ========================= So: worth it, when? =========================
 *
 *     draw straight into SRAM            662 us
 *     draw in PSRAM and flip    5072 + 2963 = 8035 us    -> 12x more expensive
 *
 * Double buffering is not "the better way", it is a technique with a range:
 *
 *   worth it      replacing the whole screen at once - a photo, a page change,
 *                 a splash screen. Decoding a photo costs 59 ms anyway, so 3 ms
 *                 more to have it appear instantly is a bargain.
 *   not worth it  per-frame animation. A bouncing ball touches ten thousand
 *                 pixels; redrawing just those and waiting for vsync is ten
 *                 times faster than flipping the whole screen.
 *
 * Press BOOT to switch between the two demonstrations.
 */
#include <LB_VGA.h>
#include <esp_heap_caps.h>
#include "photo_data.h"

#define BTN_PIN 0
#define FB_W 320
#define FB_H 240
#define FB_BYTES ((size_t)FB_W * FB_H * 2)
/* blanking = (vFront 10 + vSync 2 + vBack 33) lines x 800 px / 25 MHz */
#define BLANK_US ((uint32_t)((10 + 2 + 33) * 800ULL * 1000000ULL / 25000000ULL))

static LGFX_Sprite back; /* the back buffer, in PSRAM */
static uint16_t BG, PANEL, ACCENT, DIM, OKC, BADC;
static uint16_t C(uint8_t r, uint8_t g, uint8_t b) { return VGA.color565(r, g, b); }

static uint32_t lastDrawUs = 0, lastFlipUs = 0;

static void flip()
{
  uint32_t t0 = micros();
  memcpy(VGA.getBuffer(), back.getBuffer(), FB_BYTES);
  lastFlipUs = micros() - t0;
}

/* A tearing magnifier: flat colour so a seam has nowhere to hide, plus a
 * diagonal so you can see how far any seam is offset. */
static void paintAnim(LGFX_Sprite &g, int frame)
{
  uint16_t bg = (frame & 1) ? g.color565(200, 30, 30) : g.color565(30, 60, 200);
  g.fillScreen(bg);
  for (int i = 0; i < FB_H; i++)
    g.drawFastHLine((i * FB_W / FB_H + frame * 3) % FB_W, i, 28, g.color565(255, 255, 255));
}

/* The bars are written straight to the front buffer, so they tear along with
 * everything else in direct mode. That is fine - they only need to be legible. */
static void bar(const char *title, const char *how, uint16_t howColor)
{
  VGA.fillRect(0, 0, 320, 30, PANEL);
  VGA.setTextColor(C(255, 255, 255));
  VGA.drawString(title, 4, 2);
  VGA.setTextColor(howColor);
  VGA.drawString(how, 4, 16);

  VGA.fillRect(0, 210, 320, 30, PANEL);
  VGA.setTextColor(DIM);
  char buf[64];
  snprintf(buf, sizeof(buf), "draw %lu us   flip %lu us",
           (unsigned long)lastDrawUs, (unsigned long)lastFlipUs);
  VGA.drawString(buf, 4, 213);
  snprintf(buf, sizeof(buf), "blanking budget %lu us  -> flip = %lu%%",
           (unsigned long)BLANK_US, (unsigned long)(lastFlipUs * 100 / BLANK_US));
  VGA.setTextColor(lastFlipUs > BLANK_US ? BADC : OKC);
  VGA.drawString(buf, 4, 226);
}

/* The photo: this is where a back buffer actually earns its keep. */
static void demoPhoto(bool useBackBuffer)
{
  if (useBackBuffer)
  {
    /* Decode into PSRAM. Slow does not matter - nobody is looking at it. */
    uint32_t t0 = micros();
    back.fillScreen(back.color565(10, 12, 24));
    back.drawJpg(photo_jpg, PHOTO_JPG_LEN, 0, 0);
    lastDrawUs = micros() - t0;
    VGA.waitVSync();
    flip(); /* the whole image appears together */
  }
  else
  {
    /* Decode straight to the screen. The beam scans the display three and a
     * half times during those 59 ms, so the photo grows in from the top. */
    uint32_t t0 = micros();
    VGA.fillScreen(BG);
    VGA.drawJpg(photo_jpg, PHOTO_JPG_LEN, 0, 0);
    lastDrawUs = micros() - t0;
    lastFlipUs = 0;
  }
}

/* Only short presses are needed here, so minimal debouncing is enough. The full
 * gesture recogniser is in 11_Menu. */
static bool btnPressed()
{
  static bool stable = true, raw = true;
  static uint32_t changed = 0;
  bool r = digitalRead(BTN_PIN);
  if (r != raw) { raw = r; changed = millis(); }
  if (millis() - changed >= 20 && stable != raw)
  {
    stable = raw;
    if (!stable) return true; /* high -> low */
  }
  return false;
}

void setup()
{
  Serial.begin(115200);
  delay(200);
  VGA.begin(LB_VGA_320x240);
  pinMode(BTN_PIN, INPUT_PULLUP);

  BG = C(10, 12, 24); PANEL = C(30, 36, 62); ACCENT = C(90, 220, 255);
  DIM = C(150, 160, 185); OKC = C(120, 235, 150); BADC = C(255, 160, 70);

  /* setPsram(true) - the exact opposite of the front buffer, and for a good
   * reason: this one is read once per flip, not 36 MB/s forever. */
  back.setColorDepth(lgfx::color_depth_t::rgb565_nonswapped);
  back.setPsram(true);
  if (!back.createSprite(FB_W, FB_H))
  {
    VGA.fillScreen(BG);
    VGA.setTextColor(BADC);
    VGA.drawString("back buffer alloc failed - is PSRAM set to OPI?", 8, 100);
    while (1) delay(1000);
  }
  Serial.printf("front @ %p (internal SRAM), back @ %p (PSRAM), %u bytes PSRAM left\n",
                VGA.getBuffer(), back.getBuffer(),
                (unsigned)heap_caps_get_free_size(MALLOC_CAP_SPIRAM));
  Serial.printf("blanking interval %lu us. Press BOOT to switch demo\n",
                (unsigned long)BLANK_US);
}

void loop()
{
  static int demo = 0;        /* 0 = animation, 1 = photo */
  static bool useBack = true; /* toggles every 4 s */
  static uint32_t lastSwitch = 0;
  static int frame = 0;

  if (btnPressed())
  {
    demo = 1 - demo;
    useBack = true;
    lastSwitch = 0;
    VGA.fillScreen(BG);
    Serial.printf(">>> demo: %s\n", demo ? "photo" : "animation");
  }

  if (millis() - lastSwitch >= 4000)
  {
    lastSwitch = millis();
    useBack = !useBack;
    Serial.printf("[%s] %s  draw %lu us  flip %lu us\n",
                  demo ? "photo" : "animation",
                  useBack ? "double buffered" : "direct",
                  (unsigned long)lastDrawUs, (unsigned long)lastFlipUs);
    if (demo == 1)
      demoPhoto(useBack); /* drawn once per switch, not per frame */
  }

  if (demo == 0)
  {
    frame++;
    if (useBack)
    {
      uint32_t t0 = micros();
      paintAnim(back, frame);
      lastDrawUs = micros() - t0;
      VGA.waitVSync();
      flip();
    }
    else
    {
      VGA.waitVSync();
      uint32_t t0 = micros();
      paintAnim(VGA, frame);
      lastDrawUs = micros() - t0;
      lastFlipUs = 0;
    }
    bar("7. Double buffer  (full-screen animation)",
        useBack ? "PSRAM back buffer + flip  -> clean"
                : "direct to front buffer    -> tears",
        useBack ? OKC : BADC);
  }
  else
  {
    bar("7. Double buffer  (photo)",
        useBack ? "decode to PSRAM, then flip -> appears at once"
                : "decode straight to screen  -> paints in from top",
        useBack ? OKC : BADC);
    delay(30);
  }
}
