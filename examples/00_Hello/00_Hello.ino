/*
 * 00_Hello - the smallest possible LB_VGA sketch. Run this first.
 *
 * Three lines do the work: begin, fillScreen, drawString.
 *
 * It also doubles as a bisection tool. When nothing shows up later on:
 *   Hello appears     -> the library and the wiring are fine, look at your code
 *   Hello does not    -> run 01_PinCheck, which names the faulty data line
 *
 * Wiring is in src/LB_VGA_pins.h. Before compiling, check the Tools menu:
 *   Board            = ESP32S3 Dev Module
 *   PSRAM            = OPI PSRAM
 *   USB CDC On Boot  = Disabled     (serial goes to the on-board UART - see README)
 */
#include <LB_VGA.h>

void setup()
{
  VGA.begin();                 /* no argument = 320x240, full colour */
  VGA.fillScreen(TFT_BLACK);
  VGA.setTextColor(TFT_WHITE);
  VGA.setTextSize(3);
  VGA.drawString("Hello, VGA!", 40, 100);
}

void loop()
{
}
