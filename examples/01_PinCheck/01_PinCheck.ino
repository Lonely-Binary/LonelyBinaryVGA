/*
 * 01_PinCheck - wiring self-test. The first thing to run on new hardware.
 *
 * Get one of the sixteen R2R data lines wrong and you usually still get a
 * picture, just with wrong colours - which looks exactly like a bug in your
 * drawing code. People then go and "fix" the drawing code and make things
 * worse. Rule out the wiring first.
 *
 * Four bands, each catching a different class of fault:
 *
 *   1  Colour bars: white yellow cyan green magenta red blue black.
 *      Wrong order  -> the R/G/B groups are crossed.
 *
 *   2  Three gradients (red, green, blue), black on the left, full on the right.
 *      Stair-stepping in one of them -> that group's bit order is reversed.
 *
 *   3  Sixteen single-bit patches: patch i lights only data line Di, so each
 *      patch should be exactly twice as bright as the one to its left.
 *      A black patch -> that line is open. Two swapped -> those two are crossed.
 *      This is the strongest test of the four.
 *
 *   4  A 2 px white border all the way round. A missing edge means the
 *      resolution or the porches are wrong.
 *
 * Uses 320x240 full colour: the gradients need more than 16 colours.
 */
#include <LB_VGA.h>

void setup()
{
  Serial.begin(115200);
  delay(300);
  if (!VGA.begin(LB_VGA_320x240))
  {
    Serial.println("VGA init failed");
    return;
  }

  const int W = VGA.width(), H = VGA.height();
  const int h1 = H / 3, h2 = H / 3, y2 = h1, y3 = h1 + h2;

  /* 1. Colour bars. The constants are raw RGB565 bit patterns, not colour565()
   *    calls, so what you see on screen is literally what is on the data pins. */
  static const uint16_t bars[8] = {
      0xFFFF, 0xFFE0, 0x07FF, 0x07E0, 0xF81F, 0xF800, 0x001F, 0x0000};
  for (int x = 0; x < W; x++)
    VGA.fillRect(x, 0, 1, h1, bars[x * 8 / W]);

  /* 2. Gradients. Red and blue have 5 bits (32 steps), green has 6 (64). */
  const int hb = h2 / 3;
  for (int x = 0; x < W; x++)
  {
    VGA.fillRect(x, y2, 1, hb, (uint16_t)((x * 32 / W) << 11));
    VGA.fillRect(x, y2 + hb, 1, hb, (uint16_t)((x * 64 / W) << 5));
    VGA.fillRect(x, y2 + hb * 2, 1, h2 - hb * 2, (uint16_t)(x * 32 / W));
  }

  /* 3. Single-bit patches: patch i is the colour 1 << i, so only Di is high. */
  const int bw = W / 16, h3 = H - y3;
  for (int i = 0; i < 16; i++)
    VGA.fillRect(i * bw, y3, (i == 15) ? (W - 15 * bw) : bw, h3, (uint16_t)(1u << i));

  VGA.setTextColor(0xFFFF);
  VGA.setTextSize(1);
  static const char *names[16] = {"B0", "B1", "B2", "B3", "B4",
                                  "G0", "G1", "G2", "G3", "G4", "G5",
                                  "R0", "R1", "R2", "R3", "R4"};
  for (int i = 0; i < 16; i++)
    VGA.drawString(names[i], i * bw + 2, y3 + h3 - 10);

  /* 4. Border. */
  VGA.drawRect(0, 0, W, H, 0xFFFF);
  VGA.drawRect(1, 1, W - 2, H - 2, 0xFFFF);

  Serial.println("Test pattern up - check it band by band against the comments");
}

void loop()
{
  static uint32_t last = 0, lastFrames = 0;
  uint32_t now = millis();
  if (now - last >= 2000)
  {
    uint32_t f = VGA.frameCount();
    /* Divide by the time that actually elapsed, not by the nominal 2000 ms.
     * The delay(50) below means this fires anywhere from 2000 to 2050 ms, and
     * dividing by 2.0 exactly reports up to 61 Hz - above the 59.52 Hz the pixel
     * clock physically allows, which looks like a fault and is not one. */
    Serial.printf("field rate %.1f Hz (theoretical 59.52)   video costs %.0f%% of one core\n",
                  (f - lastFrames) * 1000.0f / (now - last), VGA.isrLoadPercent());
    lastFrames = f;
    last = now;
  }
  delay(50);
}
