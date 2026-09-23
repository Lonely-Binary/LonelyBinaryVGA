/*
 * 00_Hello - the smallest possible sketch. Run this first.
 *
 * Three lines do the work: begin, fillScreen, drawString.
 *
 * It is also a bisection tool. When nothing shows up later:
 *   Hello appears     -> library and wiring are fine, look at your code
 *   Hello does not    -> run 01_PinCheck, which names the faulty data line
 *
 * Wiring is in src/LB_VGA_Pins.h. Before compiling, check Tools:
 *   Board            = ESP32S3 Dev Module
 *   PSRAM            = OPI PSRAM
 *   USB CDC On Boot  = Disabled     (serial goes to the on-board UART)
 */
#include <LonelyBinaryVGA.h>

LB_VGA vga(LB_VGA_320x240);   /* the only line that changes per mode */

void setup()
{
  vga.begin();
  vga.fillScreen(LB_BLACK);
  vga.setTextColor(LB_WHITE);
  vga.setTextSize(3);
  vga.drawString("Hello, VGA!", 40, 100);
  vga.flush();               /* a no-op on VGA, and portable sketches call it */
}

void loop()
{
}
