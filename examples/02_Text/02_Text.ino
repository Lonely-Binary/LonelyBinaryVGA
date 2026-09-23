/*
 * 02_Text - 16-colour mode: an 8x16 font on 640x480 gives exactly 80x30
 * characters, the geometry of the classic DOS text mode.
 *
 * The 320x240 mode only manages 40x15, so text-heavy screens are worth a mode
 * of their own even though it costs you all but sixteen colours. Text never
 * needed 65536 of them.
 *
 * Also shows:
 *   - changing the palette (use VGA.setPaletteColor, not the base-class one)
 *   - waitVSync() to update part of the screen without tearing
 *   - isrLoadPercent(), because displaying a picture is not free
 */
#include <LB_VGA.h>

/* In 16-colour mode a "colour" is a palette index, 0..15. */
enum { C_BLACK = 0, C_BLUE, C_GREEN, C_CYAN, C_RED, C_MAGENTA, C_BROWN, C_GRAY,
       C_DGRAY, C_LBLUE, C_LGREEN, C_LCYAN, C_LRED, C_LMAGENTA, C_YELLOW, C_WHITE };

void setup()
{
  Serial.begin(115200);
  delay(300);
  if (!VGA.begin(LB_VGA_640x480_16))
  {
    Serial.println("VGA init failed");
    return;
  }

  /* Repoint index 0 at a dark blue. Editing the palette recolours everything
   * already on screen without redrawing a single pixel - see 08_Palette. */
  VGA.setPaletteColor(C_BLACK, 0, 0, 40);
  VGA.fillScreen(C_BLACK);

  VGA.fillRect(0, 0, 640, 24, C_BLUE);
  VGA.setTextColor(C_YELLOW);
  VGA.setTextSize(2);
  VGA.drawString("LB_VGA  640x480  16 colors", 8, 4);

  /* A ruler every ten columns, so you can count the 80 yourself. */
  VGA.setTextSize(1);
  VGA.setTextColor(C_GRAY);
  for (int c = 0; c < 80; c += 10)
    VGA.drawString(String(c).c_str(), c * 8, 30);

  VGA.setTextColor(C_WHITE);
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
    VGA.drawString(lines[i], 8, 56 + (int)i * 16);

  VGA.setTextColor(C_WHITE);
  VGA.drawString("palette:", 8, 230);
  for (int i = 0; i < 16; i++)
  {
    VGA.fillRect(80 + i * 34, 226, 30, 20, i);
    VGA.setTextColor(C_GRAY);
    VGA.drawString(String(i).c_str(), 80 + i * 34 + 2, 248);
  }

  Serial.println("Text screen up");
}

void loop()
{
  static uint32_t last = 0, lastFrames = 0;
  if (millis() - last >= 1000)
  {
    uint32_t f = VGA.frameCount();
    float fps = (f - lastFrames);
    float load = VGA.isrLoadPercent();
    lastFrames = f;
    last = millis();

    /* Wait for blanking, then repaint just the status line. */
    VGA.waitVSync();
    VGA.fillRect(0, 456, 640, 24, C_BLUE);
    VGA.setTextColor(C_LCYAN);
    VGA.setTextSize(1);
    char buf[96];
    snprintf(buf, sizeof(buf), "fps %.0f   display costs %.0f%% of one core   uptime %lus",
             fps, load, (unsigned long)(millis() / 1000));
    VGA.drawString(buf, 8, 462);

    Serial.printf("fps=%.0f  ISR load=%.0f%%\n", fps, load);
  }
  delay(20);
}
