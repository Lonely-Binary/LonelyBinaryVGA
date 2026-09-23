/*
 * 04_Shapes - lines and shapes.
 *
 * The previous example put pixels down one at a time. These calls do the same
 * job an order of magnitude faster: a horizontal line is a memory fill, not a
 * loop over pixels.
 *
 * One naming rule runs through the whole API: **draw* outlines, fill* fills**.
 * drawRect/fillRect, drawCircle/fillCircle, and so on. Remember that and you
 * rarely need to look anything up.
 *
 * The screen is split into two columns to make the rule obvious: everything on
 * the left is a draw*, everything on the right is the matching fill*.
 */
#include <LB_VGA.h>

/* Short helper, purely to keep the lines below readable. */
static uint16_t C(uint8_t r, uint8_t g, uint8_t b) { return VGA.color565(r, g, b); }

void setup()
{
  Serial.begin(115200);
  VGA.begin(LB_VGA_320x240);
  VGA.fillScreen(C(0, 0, 30));

  VGA.setTextColor(C(255, 255, 255));
  VGA.drawString("2. Lines & shapes", 4, 2);

  VGA.setTextColor(C(255, 220, 100));
  VGA.drawString("draw = outline", 20, 18);
  VGA.drawString("fill = solid", 190, 18);
  VGA.drawFastHLine(0, 30, 320, C(80, 80, 120));

  const uint16_t cyan = C(0, 220, 255), green = C(80, 230, 120),
                 red = C(255, 90, 80), pink = C(255, 120, 220);

  VGA.drawRect(20, 40, 80, 40, cyan);
  VGA.fillRect(190, 40, 80, 40, cyan);

  VGA.drawCircle(60, 110, 22, green);
  VGA.fillCircle(230, 110, 22, green);

  VGA.drawTriangle(30, 180, 90, 180, 60, 142, red);
  VGA.fillTriangle(200, 180, 260, 180, 230, 142, red);

  VGA.drawRoundRect(20, 192, 80, 34, 10, pink);
  VGA.fillRoundRect(190, 192, 80, 34, 10, pink);

  /* A fan of lines in the middle. The colour is computed from the angle rather
   * than picked by hand - colours are numbers like everything else. */
  for (int i = 0; i <= 12; i++)
  {
    int x = 130 + i * 5;
    int y = 40 + i * 12;
    VGA.drawLine(130, 40, x, y, C(255 - i * 18, i * 20, 200));
  }

  /* textWidth() measures a string, which is how you centre it. Never nudge an
   * x coordinate by eye until it looks right. */
  const char *tip = "draw* outlines, fill* fills";
  VGA.setTextColor(C(160, 180, 220));
  VGA.drawString(tip, (320 - VGA.textWidth(tip)) / 2, 230);

  Serial.println("Screen up");
}

void loop()
{
  delay(1000);
}
