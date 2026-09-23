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
#include <LonelyBinaryVGA.h>

LB_VGA vga(LB_VGA_640x480_16);

/* In 16-colour mode a "colour" is a palette entry. LB_INDEX() says so
 * explicitly - a bare integer would be read as an RGB value, which compiles,
 * runs, and draws everything in the wrong colour. */
static const lb_color_t
    C_BG = LB_INDEX(0),   C_BLACK = LB_INDEX(0),  C_BLUE = LB_INDEX(1),
    C_GREEN = LB_INDEX(2), C_CYAN = LB_INDEX(3),  C_RED = LB_INDEX(4),
    C_MAGENTA = LB_INDEX(5), C_BROWN = LB_INDEX(6), C_GRAY = LB_INDEX(7),
    C_DGRAY = LB_INDEX(8), C_LBLUE = LB_INDEX(9), C_LGREEN = LB_INDEX(10),
    C_LCYAN = LB_INDEX(11), C_LRED = LB_INDEX(12), C_LMAGENTA = LB_INDEX(13),
    C_YELLOW = LB_INDEX(14), C_WHITE = LB_INDEX(15);

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
  vga.fillRect(0, CON_TOP, 640, 480 - CON_TOP, C_BG);
  vga.setFont(&LB_Font8x16);
  for (int i = 0; i < lineCount; i++)
  {
    /* Lines starting with "> " were typed by the user; colour them differently. */
    vga.setTextColor(lines[i].startsWith("> ") ? C_LGREEN : C_GRAY);
    vga.drawString(lines[i].c_str(), 0, CON_TOP + i * CON_LINE_H);
  }
  vga.setTextColor(C_WHITE);
  String cur = "> " + pending + "_";
  vga.drawString(cur.c_str(), 0, CON_TOP + lineCount * CON_LINE_H);
}

void setup()
{
  Serial.begin(115200);
  vga.begin();
  vga.setPaletteColor(C_BG, 0, 0, 30);
  vga.fillScreen(C_BG);

  vga.fillRect(0, 0, 640, 28, C_BLUE);
  vga.setFont(&LB_Font8x16);
  vga.setTextColor(C_YELLOW);
  vga.drawString("3. Text  -  640x480, 16 colors", 8, 6);

  /* Evidence rather than a claim: a ruler you can count. */
  vga.setTextColor(C_DGRAY);
  String ruler;
  for (int c = 0; c < CON_COLS; c++)
    ruler += (c % 10 == 0) ? '|' : '.';
  vga.drawString(ruler.c_str(), 0, 34);
  vga.setTextColor(C_GRAY);
  for (int c = 0; c < CON_COLS; c += 10)
    vga.drawString(String(c).c_str(), c * 8, 50);
  vga.setTextColor(C_LCYAN);
  vga.drawString("AsciiFont8x16 -> 80 cols x 30 rows (classic DOS text mode)", 0, 68);

  /* setFont picks a face, setTextSize scales it by an integer. They combine. */
  vga.setFont(&LB_Font5x8);
  vga.setTextColor(C_WHITE);
  vga.drawString("Font0 - small 6x8", 0, 92);

  vga.setFont(&LB_Font5x8);
  vga.setTextColor(C_LGREEN);
  vga.drawString("FONT8X8C64 - RETRO", 220, 92);

  vga.setFont(&LB_Font8x16);
  vga.setTextColor(C_LRED);
  vga.drawString("24x48", 480, 76);

  /* Centre by measuring, never by eye. */
  vga.setFont(&LB_Font8x16);
  const char *mid = "centered with textWidth()";
  vga.setTextColor(C_LMAGENTA);
  vga.drawString(mid, (640 - vga.textWidth(mid)) / 2, 112);

  vga.drawFastHLine(0, 140, 640, C_DGRAY);

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
    vga.waitVSync();
    conDraw();
  }
  delay(10);
}
