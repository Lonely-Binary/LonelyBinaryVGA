/*
 * 02_Text - 16-colour mode: an 8x16 font on 640x480 gives exactly 80x30
 * characters, the geometry of the classic DOS text mode.
 *
 * The 320x240 mode only manages 40x15, so text-heavy screens are worth a mode
 * of their own even though it costs you all but sixteen colours. Text never
 * needed 65536 of them.
 *
 * Also shows:
 *   - changing the palette (use vga.setPaletteColor, not the base-class one)
 *   - waitVSync() to update part of the screen without tearing
 *   - isrLoadPercent(), because displaying a picture is not free
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

void setup()
{
  Serial.begin(115200);
  delay(300);
  if (!vga.begin())
  {
    Serial.println("VGA init failed");
    return;
  }

  /* Repoint index 0 at a dark blue. Editing the palette recolours everything
   * already on screen without redrawing a single pixel - see 08_Palette. */
  vga.setPaletteColor(C_BLACK, 0, 0, 40);
  vga.fillScreen(C_BLACK);

  vga.fillRect(0, 0, 640, 24, C_BLUE);
  vga.setTextColor(C_YELLOW);
  vga.setTextSize(2);
  vga.drawString("LB_VGA  640x480  16 colors", 8, 4);

  /* A ruler every ten columns, so you can count the 80 yourself. */
  vga.setTextSize(1);
  vga.setTextColor(C_GRAY);
  for (int c = 0; c < 80; c += 10)
    vga.drawString(String(c).c_str(), c * 8, 30);

  vga.setTextColor(C_WHITE);
  const char *lines[] = {
      "8x16 font -> exactly 80 columns x 30 rows.",
      "That is the classic DOS text mode geometry.",
      "",
      "Framebuffer lives in INTERNAL SRAM (150 KB).",
      "PSRAM is left entirely to you: 8 MB free.",
      "",
      "Drawing never disturbs the picture here:",
      "measured late-line count stays 0 even when",
      "redrawing the whole screen 3x every frame.",
  };
  for (size_t i = 0; i < sizeof(lines) / sizeof(lines[0]); i++)
    vga.drawString(lines[i], 8, 56 + (int)i * 16);

  vga.setTextColor(C_WHITE);
  vga.drawString("palette:", 8, 230);
  for (int i = 0; i < 16; i++)
  {
    vga.fillRect(80 + i * 34, 226, 30, 20, LB_INDEX(i));
    vga.setTextColor(C_GRAY);
    vga.drawString(String(i).c_str(), 80 + i * 34 + 2, 248);
  }

  Serial.println("Text screen up");
}

void loop()
{
  static uint32_t last = 0, lastFrames = 0;
  if (millis() - last >= 1000)
  {
    uint32_t f = vga.frameCount();
    float fps = (f - lastFrames);
    float load = vga.isrLoadPercent();
    lastFrames = f;
    last = millis();

    /* Wait for blanking, then repaint just the status line. */
    vga.waitVSync();
    vga.fillRect(0, 456, 640, 24, C_BLUE);
    vga.setTextColor(C_LCYAN);
    vga.setTextSize(1);
    char buf[96];
    snprintf(buf, sizeof(buf), "fps %.0f   display costs %.0f%% of one core   uptime %lus",
             fps, load, (unsigned long)(millis() / 1000));
    vga.drawString(buf, 8, 462);

    Serial.printf("fps=%.0f  ISR load=%.0f%%\n", fps, load);
  }
  delay(20);
}
