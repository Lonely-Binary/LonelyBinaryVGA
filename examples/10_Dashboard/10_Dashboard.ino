/*
 * 10_Dashboard - the wrap-up: everything so far assembled into a usable panel.
 *
 * Text, shapes, a chart and a photo all at once. No new API here; the subject is
 * how to organise a program you would actually leave running on a desk.
 *
 * Three habits, each learned the hard way in earlier examples:
 *
 *   1. **Draw the fixed parts once, then repaint only what changes.** The photo
 *      is decoded at startup and never again (a 320x240 JPEG costs 59 ms; even
 *      this small one takes about eleven). Re-decoding every second would waste
 *      the time and make the panel flicker.
 *
 *   2. **waitVSync() before touching the screen.** Only the clock, a few status
 *      lines and the plot change each second - a few thousand pixels, easily
 *      finished inside the 1.44 ms of blanking, so nothing tears.
 *
 *   3. **Let the range follow the data.** A fixed 0..100 axis would flatten
 *      every real signal into a straight line.
 *
 * 320x240 full colour, because there is a photo and panel text wants to be large
 * anyway; 40 columns is plenty. A text-only instrument panel would use
 * LB_VGA_640x480_16 instead.
 */
#include <LB_VGA.h>
#include "photo_small.h"

static uint16_t C(uint8_t r, uint8_t g, uint8_t b) { return VGA.color565(r, g, b); }

static uint16_t BG, PANEL, ACCENT, DIM, OK_C, WARN_C;

/* ---- plotted history ---- */
#define HIST 76
static float hist[HIST];
static int histN = 0;

static void pushSample(float v)
{
  if (histN < HIST) hist[histN++] = v;
  else { for (int i = 0; i < HIST - 1; i++) hist[i] = hist[i + 1]; hist[HIST - 1] = v; }
}

static int mapY(float v, float vmin, float vmax, int top, int h)
{
  if (vmax <= vmin) return top + h;
  float t = (v - vmin) / (vmax - vmin);
  if (t < 0) t = 0;
  if (t > 1) t = 1;
  return top + h - (int)(t * h);
}

/* ---- only the changing parts get repainted ---- */

static void drawClock()
{
  uint32_t s = millis() / 1000;
  char buf[16];
  snprintf(buf, sizeof(buf), "%02lu:%02lu:%02lu",
           (unsigned long)(s / 3600), (unsigned long)((s / 60) % 60), (unsigned long)(s % 60));
  VGA.fillRect(150, 4, 166, 24, PANEL);
  VGA.setTextSize(2);
  VGA.setTextColor(ACCENT);
  VGA.drawString(buf, 316 - VGA.textWidth(buf), 6); /* right-aligned by measurement */
  VGA.setTextSize(1);
}

static void drawStatus(float tempC)
{
  const int x = 140, y = 36;
  VGA.fillRect(x, y, 180, 96, BG);

  char buf[40];
  VGA.setTextColor(DIM);
  VGA.drawString("display", x, y);
  VGA.setTextColor(OK_C);
  snprintf(buf, sizeof(buf), "%dx%d  %.0fHz", VGA.width(), VGA.height(), 60.0f);
  VGA.drawString(buf, x + 56, y);

  VGA.setTextColor(DIM);
  VGA.drawString("ISR", x, y + 16);
  float load = VGA.isrLoadPercent();
  VGA.setTextColor(load > 60 ? WARN_C : OK_C);
  snprintf(buf, sizeof(buf), "%.0f%% of one core", load);
  VGA.drawString(buf, x + 56, y + 16);

  VGA.setTextColor(DIM);
  VGA.drawString("temp", x, y + 32);
  VGA.setTextColor(tempC > 60 ? WARN_C : OK_C);
  snprintf(buf, sizeof(buf), "%.1f C", tempC);
  VGA.drawString(buf, x + 56, y + 32);

  VGA.setTextColor(DIM);
  VGA.drawString("sram", x, y + 48);
  VGA.setTextColor(OK_C);
  snprintf(buf, sizeof(buf), "%u KB free",
           (unsigned)(heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT) / 1024));
  VGA.drawString(buf, x + 56, y + 48);

  VGA.setTextColor(DIM);
  VGA.drawString("psram", x, y + 64);
  VGA.setTextColor(OK_C);
  snprintf(buf, sizeof(buf), "%u KB free",
           (unsigned)(heap_caps_get_free_size(MALLOC_CAP_SPIRAM) / 1024));
  VGA.drawString(buf, x + 56, y + 64);
}

static void drawSpark()
{
  const int x0 = 30, y0 = 146, w = 284, h = 80;
  VGA.fillRect(0, y0 - 12, 320, h + 28, BG);
  if (histN < 2) return;

  float vmin = hist[0], vmax = hist[0];
  for (int i = 1; i < histN; i++)
  {
    if (hist[i] < vmin) vmin = hist[i];
    if (hist[i] > vmax) vmax = hist[i];
  }
  float pad = (vmax - vmin) * 0.25f;
  if (pad < 0.3f) pad = 0.3f;
  vmin -= pad;
  vmax += pad;

  VGA.setTextColor(DIM);
  /*
   * This plots frames per second, not temperature.
   *
   * Temperature was the obvious choice, but the internal sensor is quantised to
   * roughly 1 C - it only ever reads 27.8 or 28.8 - so the trace is a flat line
   * with an occasional step, which looks exactly like a broken chart. Frame rate
   * is real, wobbles a little (59/60/61), and is the right health metric for a
   * display panel anyway. Temperature stays as a text readout above.
   */
  VGA.drawString("frames per second", x0, y0 - 12);

  char t[10];
  for (int i = 0; i <= 1; i++)
  {
    float v = i ? vmax : vmin;
    int y = mapY(v, vmin, vmax, y0, h);
    VGA.drawFastHLine(x0, y, w, PANEL);
    snprintf(t, sizeof(t), "%.1f", v);
    VGA.setTextColor(DIM);
    VGA.drawString(t, 0, y - 4);
  }

  for (int i = 1; i < histN; i++)
    VGA.drawLine(x0 + (i - 1) * w / (HIST - 1), mapY(hist[i - 1], vmin, vmax, y0, h),
                 x0 + i * w / (HIST - 1), mapY(hist[i], vmin, vmax, y0, h), ACCENT);

  int lx = x0 + (histN - 1) * w / (HIST - 1);
  VGA.fillCircle(lx, mapY(hist[histN - 1], vmin, vmax, y0, h), 2, WARN_C);
  VGA.drawFastHLine(x0, y0 + h, w, DIM);
}

void setup()
{
  Serial.begin(115200);
  VGA.begin(LB_VGA_320x240);

  BG = C(10, 12, 24);
  PANEL = C(28, 34, 60);
  ACCENT = C(90, 220, 255);
  DIM = C(120, 130, 160);
  OK_C = C(120, 235, 150);
  WARN_C = C(255, 160, 70);

  /* ---- drawn once; none of this changes again ---- */
  VGA.fillScreen(BG);
  VGA.fillRect(0, 0, 320, 30, PANEL);
  VGA.setTextColor(C(255, 255, 255));
  VGA.drawString("LB_VGA DASHBOARD", 8, 5);
  VGA.setTextColor(DIM);
  VGA.drawString("uptime", 8, 17);

  uint32_t t0 = micros();
  VGA.drawJpg(photo_jpg, PHOTO_JPG_LEN, 4, 36);
  Serial.printf("photo decoded in %lu us (once, at startup)\n",
                (unsigned long)(micros() - t0));
  VGA.drawRect(3, 35, PHOTO_W + 2, PHOTO_H + 2, PANEL);

  Serial.println("Dashboard up");
}

void loop()
{
  static uint32_t lastSec = 0;
  uint32_t nowMs = millis();
  if (nowMs - lastSec < 1000)
  {
    delay(20);
    return;
  }
  uint32_t elapsed = nowMs - lastSec;
  lastSec = nowMs;

  float tempC = temperatureRead();

  /* Frames actually delivered, scaled by the real interval rather than the
   * nominal 1000 ms - otherwise the trace sits slightly above the 59.52 Hz that
   * the pixel clock allows. */
  static uint32_t lastFrames = 0;
  uint32_t f = VGA.frameCount();
  pushSample((f - lastFrames) * 1000.0f / elapsed);
  lastFrames = f;

  /* Blanking first, then repaint the three live regions - a few thousand
   * pixels, comfortably inside 1.44 ms. */
  VGA.waitVSync();
  drawClock();
  drawStatus(tempC);
  drawSpark();

  /* Mirror the panel to serial so it is still useful with no display attached. */
  static uint32_t n = 0;
  if ((n++ % 5) == 0)
    Serial.printf("temp %.1fC  ISR %.0f%%  sram %u KB\n", tempC, VGA.isrLoadPercent(),
                  (unsigned)(heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT) / 1024));
}
