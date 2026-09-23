/*
 * 07_Photo - showing images. Three screens, five seconds each.
 *
 *   1  An image is an array. A 16x16 icon is 256 RGB565 numbers, all of them
 *      visible in photo_data.h. Scaling it up ten times means drawing each
 *      number as a 10x10 block - that is all "zoom" ever is.
 *
 *   2  A full-screen photo. The same 320x240 image costs
 *        153600 bytes uncompressed (RGB565)
 *         17251 bytes as a JPEG      -> 8.9x smaller
 *      Which is why compression exists: 16 MB of flash holds 900-odd JPEGs but
 *      only about a hundred raw images.
 *
 *   3  Scaling and centring. The centre position is **computed**, never nudged
 *      by eye:  x = (screen_width - image_width) / 2
 *
 * The decoder only reads BASELINE JPEGs. LovyanGFX uses TJpgDec, which cannot
 * handle progressive JPEGs and fails silently - you just get a blank area, which
 * is hard to diagnose. make_assets.py checks the SOF marker on every run. The
 * VGA Gallery project in this repository lost time to the same trap.
 *
 * Assets come from make_assets.py (macOS sips only, no libraries):
 *     python3 make_assets.py your-image.jpg
 *
 * TODO: the bundled photo comes from this repository's VGA Gallery assets.
 *       Replace it with an image we own before publishing the course.
 */
#include <LB_VGA.h>
#include "photo_data.h"

static uint16_t C(uint8_t r, uint8_t g, uint8_t b) { return VGA.color565(r, g, b); }

/* ---- Screen 1: an image is an array ---- */
static void screenArray()
{
  VGA.fillScreen(C(0, 0, 30));
  VGA.setTextColor(C(255, 255, 255));
  VGA.drawString("4. An image IS an array", 4, 2);

  /* Actual size: 16x16, almost too small to read - which is the point. */
  VGA.pushImage(12, 30, ICON_W, ICON_H, icon16);
  VGA.setTextColor(C(160, 180, 220));
  VGA.drawString("1x", 14, 50);

  /* Ten times bigger: one number, one 10x10 square. No interpolation, no
   * smoothing - the most literal possible meaning of "scale up". */
  const int SCALE = 10, X0 = 60, Y0 = 30;
  for (int y = 0; y < ICON_H; y++)
    for (int x = 0; x < ICON_W; x++)
      VGA.fillRect(X0 + x * SCALE, Y0 + y * SCALE, SCALE, SCALE, icon16[y * ICON_W + x]);

  VGA.drawString("10x = one number, one square", 60, 194);

  /* Print the first few numbers so they can be matched against the picture. */
  VGA.setTextColor(C(255, 220, 100));
  char buf[64];
  snprintf(buf, sizeof(buf), "icon16[0..3] = %04X %04X %04X %04X",
           icon16[0], icon16[1], icon16[2], icon16[3]);
  VGA.drawString(buf, 4, 212);
  snprintf(buf, sizeof(buf), "16 x 16 = 256 numbers, %d bytes", (int)sizeof(icon16));
  VGA.drawString(buf, 4, 226);
}

/* ---- Screen 2: full-screen photo and the size comparison ---- */
static void screenPhoto()
{
  /* The photo is exactly 320x240, so it fills the screen. Arguments 3 and 4 are
   * the top-left corner. */
  VGA.drawJpg(photo_jpg, PHOTO_JPG_LEN, 0, 0);

  VGA.fillRect(0, 210, 320, 30, C(0, 0, 0));
  VGA.setTextColor(C(255, 255, 255));
  char buf[64];
  snprintf(buf, sizeof(buf), "raw RGB565: %d bytes", PHOTO_RAW_BYTES);
  VGA.drawString(buf, 4, 212);
  VGA.setTextColor(C(120, 255, 140));
  snprintf(buf, sizeof(buf), "JPEG: %d bytes  (%.1fx smaller)",
           PHOTO_JPG_LEN, (float)PHOTO_RAW_BYTES / PHOTO_JPG_LEN);
  VGA.drawString(buf, 4, 226);
}

/* ---- Screen 3: scaling and centring ---- */
static void screenScale()
{
  VGA.fillScreen(C(20, 20, 35));
  VGA.setTextColor(C(255, 255, 255));
  VGA.drawString("scale & center", 4, 2);

  /* The ninth argument of drawJpg is the scale factor; the zeros before it are
   * maxWidth/maxHeight/offX/offY, unconstrained here. */
  const float s = 0.5f;
  const int w = (int)(PHOTO_W * s), h = (int)(PHOTO_H * s);
  const int x = (320 - w) / 2, y = (240 - h) / 2; /* computed, not eyeballed */
  VGA.drawJpg(photo_jpg, PHOTO_JPG_LEN, x, y, 0, 0, 0, 0, s);

  VGA.drawRect(x - 1, y - 1, w + 2, h + 2, C(255, 220, 100));

  VGA.setTextColor(C(160, 180, 220));
  char buf[64];
  snprintf(buf, sizeof(buf), "scale %.1f -> %dx%d", s, w, h);
  VGA.drawString(buf, 4, 210);
  snprintf(buf, sizeof(buf), "x = (320 - %d) / 2 = %d", w, x);
  VGA.drawString(buf, 4, 224);
}

void setup()
{
  Serial.begin(115200);
  VGA.begin(LB_VGA_320x240);
  Serial.printf("raw RGB565 %d bytes, JPEG %d bytes, %.1fx smaller\n",
                PHOTO_RAW_BYTES, PHOTO_JPG_LEN,
                (float)PHOTO_RAW_BYTES / PHOTO_JPG_LEN);
}

void loop()
{
  static int screen = 0;
  static uint32_t last = 0;

  if (last == 0 || millis() - last >= 5000)
  {
    last = millis();

    /*
     * waitVSync cannot save a photo, and it is worth knowing why.
     *
     * Decoding a full 320x240 JPEG measures **59 ms**, while a frame is 16.7 ms
     * - three and a half times longer. The photo is therefore always painted in
     * from the top; waiting for the frame boundary changes nothing. (Halving the
     * scale only drops it to 53.6 ms, a 9% saving, which says the time goes into
     * decoding rather than into drawing.)
     *
     * waitVSync did help in 05_Bounce because that sketch changes only ten
     * thousand pixels per frame and finishes in a few hundred microseconds.
     *
     * To make a photo appear all at once you have to decode into a second
     * buffer and copy the finished image across - see 12_DoubleBuffer, which
     * uses PSRAM for exactly this.
     *
     * The call stays here so the first 16.7 ms at least does not tear.
     */
    VGA.waitVSync();
    uint32_t t0 = micros();
    switch (screen)
    {
    case 0: screenArray(); break;
    case 1: screenPhoto(); break;
    case 2: screenScale(); break;
    }
    Serial.printf("screen %d drawn in %lu us\n", screen + 1, (unsigned long)(micros() - t0));
    screen = (screen + 1) % 3;
  }
  delay(20);
}
