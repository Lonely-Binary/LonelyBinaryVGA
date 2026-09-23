/*
 * 06_TextConsole - fonts, alignment, and a serial console on the screen.
 *
 * In 16-colour mode the arithmetic is worth seeing:
 *   320x240 with an 8x16 font = 40 columns x 15 rows
 *   640x480 with an 8x16 font = 80 columns x 30 rows
 * Four times the text. Sixteen colours is a small price for that, and text
 * never needed more.
 *
 * The practical part is a console: whatever you type into the Serial Monitor
 * appears on the display. Handy when you have no serial cable to hand, or when
 * an audience needs to see the log.
 *
 * Do not write per-pixel loops in this mode - a 4bpp pixel write is a
 * read-modify-write of half a byte. Repainting the console goes through
 * drawString, which fills whole bytes at a time and is an order of magnitude
 * faster.
 */
#include <LB_VGA.h>

enum { C_BG = 0, C_BLUE, C_GREEN, C_CYAN, C_RED, C_MAGENTA, C_BROWN, C_GRAY,
       C_DGRAY, C_LBLUE, C_LGREEN, C_LCYAN, C_LRED, C_LMAGENTA, C_YELLOW, C_WHITE };

#define CON_TOP 150                             /* console area starts here */
#define CON_LINE_H 16                           /* AsciiFont8x16 line height */
#define CON_ROWS ((480 - CON_TOP) / CON_LINE_H) /* 20 rows */
#define CON_COLS 80

static String lines[CON_ROWS];
static int lineCount = 0;
static String pending; /* the line being typed */

static void conPush(const String &s)
{
  /* Scroll by moving Strings, not pixels. A pixel-level scroll would be faster,
   * but this is easier to follow and measures fast enough. */
  if (lineCount < CON_ROWS)
    lines[lineCount++] = s;
  else
  {
    for (int i = 0; i < CON_ROWS - 1; i++)
      lines[i] = lines[i + 1];
    lines[CON_ROWS - 1] = s;
  }
}

static void conDraw()
{
  VGA.fillRect(0, CON_TOP, 640, 480 - CON_TOP, C_BG);
  VGA.setFont(&fonts::AsciiFont8x16);
  for (int i = 0; i < lineCount; i++)
  {
    /* Lines starting with "> " were typed by the user; colour them differently. */
    VGA.setTextColor(lines[i].startsWith("> ") ? C_LGREEN : C_GRAY);
    VGA.drawString(lines[i].c_str(), 0, CON_TOP + i * CON_LINE_H);
  }
  VGA.setTextColor(C_WHITE);
  String cur = "> " + pending + "_";
  VGA.drawString(cur.c_str(), 0, CON_TOP + lineCount * CON_LINE_H);
}

void setup()
{
  Serial.begin(115200);
  VGA.begin(LB_VGA_640x480_16);
  VGA.setPaletteColor(C_BG, 0, 0, 30);
  VGA.fillScreen(C_BG);

  VGA.fillRect(0, 0, 640, 28, C_BLUE);
  VGA.setFont(&fonts::AsciiFont8x16);
  VGA.setTextColor(C_YELLOW);
  VGA.drawString("3. Text  -  640x480, 16 colors", 8, 6);

  /* Evidence rather than a claim: a ruler you can count. */
  VGA.setTextColor(C_DGRAY);
  String ruler;
  for (int c = 0; c < CON_COLS; c++)
    ruler += (c % 10 == 0) ? '|' : '.';
  VGA.drawString(ruler.c_str(), 0, 34);
  VGA.setTextColor(C_GRAY);
  for (int c = 0; c < CON_COLS; c += 10)
    VGA.drawString(String(c).c_str(), c * 8, 50);
  VGA.setTextColor(C_LCYAN);
  VGA.drawString("AsciiFont8x16 -> 80 cols x 30 rows (classic DOS text mode)", 0, 68);

  /* setFont picks a face, setTextSize scales it by an integer. They combine. */
  VGA.setFont(&fonts::Font0);
  VGA.setTextColor(C_WHITE);
  VGA.drawString("Font0 - small 6x8", 0, 92);

  VGA.setFont(&fonts::Font8x8C64);
  VGA.setTextColor(C_LGREEN);
  VGA.drawString("FONT8X8C64 - RETRO", 220, 92);

  VGA.setFont(&fonts::AsciiFont24x48);
  VGA.setTextColor(C_LRED);
  VGA.drawString("24x48", 480, 76);

  /* Centre by measuring, never by eye. */
  VGA.setFont(&fonts::AsciiFont8x16);
  const char *mid = "centered with textWidth()";
  VGA.setTextColor(C_LMAGENTA);
  VGA.drawString(mid, (640 - VGA.textWidth(mid)) / 2, 112);

  VGA.drawFastHLine(0, 140, 640, C_DGRAY);

  conPush("Type in the Serial Monitor -> it shows up here.");
  conPush("(115200 baud, send with newline)");
  conDraw();

  Serial.println("Type something and press enter, then look at the display");
}

void loop()
{
  bool dirty = false;

  while (Serial.available())
  {
    char c = (char)Serial.read();
    if (c == '\n' || c == '\r')
    {
      if (pending.length())
      {
        conPush("> " + pending);
        pending = "";
      }
      dirty = true;
    }
    else if (c == 8 || c == 127) /* backspace */
    {
      if (pending.length())
        pending.remove(pending.length() - 1);
      dirty = true;
    }
    else if (pending.length() < CON_COLS - 4)
    {
      pending += c;
      dirty = true;
    }
  }

  /* Blink the cursor twice a second, and flush any typing at the same time. */
  static uint32_t last = 0;
  if (dirty || millis() - last >= 500)
  {
    last = millis();
    VGA.waitVSync();
    conDraw();
  }
  delay(10);
}
