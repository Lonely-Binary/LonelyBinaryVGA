/*
 * 08_Palette - the picture animates while zero pixels are redrawn.
 *
 * This is what 16-colour mode buys you, and it is easy to miss.
 *
 * In an indexed mode the framebuffer holds **indices**, not colours. What a
 * pixel actually looks like is decided at display time by looking the index up
 * in a palette. Rewrite the palette and **every pixel already on screen changes
 * colour at once**, without touching a byte of framebuffer.
 *
 * DOS games made water flow, fire flicker and neon buzz this way; the technique
 * is called colour cycling. The modern equivalent is theme switching: draw the
 * interface once, swap palettes to go from light to dark instantly.
 *
 * This sketch draws the pattern once (and times it), then changes **only the
 * fourteen palette entries** each frame. Both timings are printed bottom-left.
 *
 * Change the palette after waitVSync(). The ISR is reading that table line by
 * line; overwrite it mid-frame and a band of wrong colours appears. The 1.44 ms
 * of blanking is far more than the fourteen entries need.
 */
#include <LonelyBinaryVGA.h>

LB_VGA vga(LB_VGA_640x480_16);

/* 0 stays the background and 15 stays white for text; 1..14 cycle. */
#define CYC_FIRST 1
#define CYC_COUNT 14

static uint8_t wheel[CYC_COUNT][3];

static void buildWheel()
{
  for (int i = 0; i < CYC_COUNT; i++)
  {
    /* Minimal HSV to RGB with saturation and value pinned; only hue moves. */
    float h = 6.0f * i / CYC_COUNT;
    int seg = (int)h;
    float f = h - seg;
    uint8_t v = 255, p = 0, q = (uint8_t)(255 * (1 - f)), t = (uint8_t)(255 * f);
    uint8_t r, g, b;
    switch (seg % 6)
    {
    case 0: r = v; g = t; b = p; break;
    case 1: r = q; g = v; b = p; break;
    case 2: r = p; g = v; b = t; break;
    case 3: r = p; g = q; b = v; break;
    case 4: r = t; g = p; b = v; break;
    default: r = v; g = p; b = q; break;
    }
    wheel[i][0] = r; wheel[i][1] = g; wheel[i][2] = b;
  }
}

/* Draw the pattern once. Returns microseconds - the denominator of the
 * comparison printed on screen. */
static uint32_t drawPattern()
{
  uint32_t t0 = micros();
  /* Concentric rings: index follows the radius. Rings read as "flowing" better
   * than anything else when the palette rotates. */
  for (int y = 0; y < 480; y++)
  {
    for (int x = 0; x < 640; x++)
    {
      int dx = x - 320, dy = (y - 240) * 2; /* x2 compensates the 640x480 aspect */
      int d = (int)(sqrtf((float)(dx * dx + dy * dy)) / 12.0f);
      vga.drawPixel(x, y, LB_INDEX(CYC_FIRST + (d % CYC_COUNT)));
    }
  }
  return micros() - t0;
}

static uint32_t patternUs = 0;

void setup()
{
  Serial.begin(115200);
  vga.begin();
  buildWheel();

  vga.setPaletteColor(0, 0, 0, 20);
  vga.setPaletteColor(15, 255, 255, 255);
  for (int i = 0; i < CYC_COUNT; i++)
    vga.setPaletteColor(CYC_FIRST + i, wheel[i][0], wheel[i][1], wheel[i][2]);

  /*
   * drawPixel per pixel is used here on purpose: the rings have to be computed
   * pixel by pixel anyway, and it gives us the denominator for the comparison.
   *
   * It measures 282 ms for 307200 pixels, about 0.92 us each. **Most of that is
   * drawPixel's fixed per-call overhead** (clipping, colour conversion, virtual
   * dispatch), not the 4bpp read-modify-write - do not conflate the two. For
   * ordinary drawing use fillRect / drawString (see LB_VGA.h).
   */
  patternUs = drawPattern();
  Serial.printf("pattern drawn in %lu us\n", (unsigned long)patternUs);
}

void loop()
{
  static int offset = 0;
  static uint32_t rotUs = 0;

  vga.waitVSync(); /* blanking first, then rewrite the table */

  uint32_t t0 = micros();
  for (int i = 0; i < CYC_COUNT; i++)
  {
    const uint8_t *c = wheel[(i + offset) % CYC_COUNT];
    vga.setPaletteColor(CYC_FIRST + i, c[0], c[1], c[2]);
  }
  rotUs = micros() - t0;
  offset = (offset + 1) % CYC_COUNT;

  /* This text block *is* genuinely repainted, which makes the contrast nicely
   * literal: the rings move without being redrawn, these words cannot. */
  static uint32_t last = 0;
  if (millis() - last >= 500)
  {
    last = millis();
    vga.fillRect(0, 400, 470, 80, LB_INDEX(0));
    vga.setFont(&LB_Font8x16);
    vga.setTextColor(LB_INDEX(15));
    vga.drawString("The rings are animating.", 8, 404);
    vga.drawString("Pixels redrawn per frame: 0", 8, 420);
    char buf[80];
    snprintf(buf, sizeof(buf), "redraw whole pattern : %lu us", (unsigned long)patternUs);
    vga.drawString(buf, 8, 440);
    snprintf(buf, sizeof(buf), "rotate palette       : %lu us  (%lux faster)",
             (unsigned long)rotUs, (unsigned long)(rotUs ? patternUs / rotUs : 0));
    vga.drawString(buf, 8, 456);

    Serial.printf("redraw %lu us  vs  palette rotate %lu us\n",
                  (unsigned long)patternUs, (unsigned long)rotUs);
  }
}
