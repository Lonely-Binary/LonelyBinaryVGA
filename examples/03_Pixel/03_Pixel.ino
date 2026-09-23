/*
 * 03_Pixel - one pixel. Everything else is built on these three ideas.
 *
 *   1. The screen is a block of memory. You write numbers into it and they
 *      become colours. Nothing "tells the display to draw" - you are editing
 *      150 KB of RAM that the video hardware reads out sixty times a second.
 *
 *   2. The origin (0,0) is top-left, x goes right and **y goes down**. Y
 *      pointing down is the opposite of school maths, and it is the convention
 *      on every display, because the electron beam starts at the top-left and
 *      scans downwards.
 *
 *   3. Colour is RGB565: sixteen bits per pixel, five red, six green, five blue.
 *      Green gets the extra bit because the eye is most sensitive to it, so
 *      that is where the bit buys the most.
 *
 * Uses 320x240 full colour, since we are showing colour gradients.
 */
#include <LB_VGA.h>

void setup()
{
  Serial.begin(115200);
  VGA.begin(LB_VGA_320x240);
  VGA.fillScreen(VGA.color565(0, 0, 30));

  VGA.setTextColor(VGA.color565(255, 255, 255));
  VGA.drawString("1. A pixel", 4, 2);

  /* The coordinate system: a pixel in each corner, labelled. */
  const int W = VGA.width(), H = VGA.height();
  VGA.setTextColor(VGA.color565(120, 200, 255));
  VGA.drawPixel(0, 0, VGA.color565(255, 0, 0));
  VGA.drawString("(0,0)", 4, 14);
  VGA.drawPixel(W - 1, 0, VGA.color565(255, 0, 0));
  VGA.drawString("(319,0)", W - 46, 14);
  VGA.drawPixel(0, H - 1, VGA.color565(255, 0, 0));
  VGA.drawString("(0,239)", 4, H - 22);
  VGA.drawPixel(W - 1, H - 1, VGA.color565(255, 0, 0));
  VGA.drawString("(319,239)", W - 58, H - 22);

  /* A diagonal put down one pixel at a time. drawLine() is deliberately not
   * used yet: first see that a line is just a run of pixels. The next example
   * introduces drawLine, which does the same thing far faster. */
  for (int i = 0; i < 100; i++)
    VGA.drawPixel(30 + i, 40 + i, VGA.color565(255, 255, 0));
  VGA.setTextColor(VGA.color565(255, 255, 0));
  VGA.drawString("drawn pixel by pixel", 136, 88);

  /* How many steps RGB565 actually has: 32 red, 64 green, 32 blue. Drawn
   * together you can see the green steps are half the width of the others. */
  const int y0 = 150, bh = 18;
  VGA.setTextColor(VGA.color565(200, 200, 200));
  VGA.drawString("R 5bit = 32", 4, y0 - 12);
  for (int i = 0; i < 32; i++)
    VGA.fillRect(70 + i * 7, y0 - 12, 7, bh, VGA.color565(i * 255 / 31, 0, 0));

  VGA.drawString("G 6bit = 64", 4, y0 + 12);
  for (int i = 0; i < 64; i++)
    VGA.fillRect(70 + i * 3, y0 + 12, 3, bh, VGA.color565(0, i * 255 / 63, 0));

  VGA.drawString("B 5bit = 32", 4, y0 + 36);
  for (int i = 0; i < 32; i++)
    VGA.fillRect(70 + i * 7, y0 + 36, 7, bh, VGA.color565(0, 0, i * 255 / 31));

  Serial.println("Screen up");
}

/*
 * A bright dot walking around the edge of the screen with its coordinates
 * printed next to it - coordinates are easier to grasp moving than static.
 *
 * Note that each frame changes exactly two pixels: erase the old one, light the
 * new one. That is what all animation comes down to. Never repaint the whole
 * screen when only a little of it moved.
 */
void loop()
{
  static int step = 0;
  const int W = VGA.width(), H = VGA.height();

  /* Unroll the border into a single line; step is the distance along it. */
  auto posAt = [&](int s, int &x, int &y) {
    s %= 2 * (W + H) - 4;
    if (s < W)                  { x = s;                 y = 0; }
    else if (s < W + H - 1)     { x = W - 1;             y = s - W + 1; }
    else if (s < 2 * W + H - 2) { x = 2 * W + H - 3 - s; y = H - 1; }
    else                        { x = 0;                 y = 2 * (W + H) - 4 - s; }
  };

  int x, y;
  posAt(step, x, y); /* erase the old dot */
  VGA.drawPixel(x, y, VGA.color565(0, 0, 30));
  step += 2;
  posAt(step, x, y);
  VGA.drawPixel(x, y, VGA.color565(255, 255, 255));

  VGA.fillRect(120, 110, 100, 12, VGA.color565(0, 0, 30));
  VGA.setTextColor(VGA.color565(255, 255, 255));
  char buf[24];
  snprintf(buf, sizeof(buf), "(%d,%d)", x, y);
  VGA.drawString(buf, 120, 110);

  delay(8);
}
