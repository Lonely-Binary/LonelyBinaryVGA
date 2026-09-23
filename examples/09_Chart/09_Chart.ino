/*
 * 09_Chart - charts, deliberately without a charting library.
 *
 * Bar charts and line charts come apart into three things: mapping a value to a
 * pixel, drawing a line, and aligning text. All of it appeared in the previous
 * examples. Wrapping that in a library hides exactly the parts worth learning,
 * and leaves you unable to draw a chart anywhere else.
 *
 * 16-colour mode, because charts live or die on readable axes and labels.
 *
 * The single most important function here is mapY(). **Screen y grows downward
 * while chart values grow upward** - every upside-down first chart comes from
 * getting that one line wrong.
 *
 * Data:
 *   bars  - the three measured ISR costs from this project's README (real)
 *   lines - two series:
 *             yellow = ESP32-S3 internal temperature (real, no wiring needed)
 *             cyan   = a sine wave, labelled SIMULATED
 *
 * Why include a fake series? The internal temperature sensor is quantised to
 * roughly 1 C: it sits on 27.8 or 28.8 and steps between them every few tens of
 * seconds. That is a true reading, but plotted it looks like a flat line, and
 * the natural conclusion is "my chart code is broken" - so people go and edit
 * working code. A series that definitely moves, drawn right next to it, settles
 * the question at a glance.
 *
 * That is the general habit: **before concluding something is broken, put a
 * known-good control next to it.**
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

/*
 * Value to screen y. The whole chart rests on these seven lines.
 *
 * top is the top edge of the plot area, h its height. Note it is top + h - ...:
 * a larger value gives a smaller y, i.e. higher up. Writing top + ... flips the
 * chart upside down.
 */
static int mapY(float v, float vmin, float vmax, int top, int h)
{
  if (vmax <= vmin) return top + h;
  float t = (v - vmin) / (vmax - vmin);
  if (t < 0) t = 0;
  if (t > 1) t = 1;
  return top + h - (int)(t * h);
}

/* ---------- bar chart ---------- */
struct Bar { const char *name; float value; lb_color_t color; };
static const Bar bars[] = {
    {"320x240", 25, C_LGREEN},
    {"640x480x16", 46, C_LCYAN},
    {"PSRAM", 68, C_LRED},
};
#define BAR_N (sizeof(bars) / sizeof(bars[0]))

static void drawBarChart(int x0, int y0, int w, int h)
{
  const float vmax = 100;

  /* Grid and y labels first, so the bars are drawn over them rather than
   * scratched through by them. */
  vga.setFont(&LB_Font8x16);
  for (int v = 0; v <= 100; v += 25)
  {
    int y = mapY(v, 0, vmax, y0, h);
    vga.drawFastHLine(x0, y, w, C_DGRAY);
    vga.setTextColor(C_GRAY);
    char t[8];
    snprintf(t, sizeof(t), "%3d%%", v);
    vga.drawString(t, x0 - 36, y - 8); /* -8 = half a line, to sit on the tick */
  }

  /* Bar width and spacing derive from the count, so adding a bar needs no other
   * change. */
  const int slot = w / BAR_N, bw = slot * 2 / 3;
  for (size_t i = 0; i < BAR_N; i++)
  {
    int bx = x0 + i * slot + (slot - bw) / 2;
    int by = mapY(bars[i].value, 0, vmax, y0, h);
    vga.fillRect(bx, by, bw, y0 + h - by, bars[i].color);

    char t[8];
    snprintf(t, sizeof(t), "%d%%", (int)bars[i].value);
    vga.setTextColor(C_WHITE);
    vga.drawString(t, bx + (bw - vga.textWidth(t)) / 2, by - 18); /* centred by measurement */

    vga.setTextColor(C_GRAY);
    vga.drawString(bars[i].name,
                   bx + (bw - vga.textWidth(bars[i].name)) / 2, y0 + h + 4);
  }

  vga.drawFastHLine(x0, y0 + h, w, C_WHITE);
  vga.drawFastVLine(x0, y0, h, C_WHITE);
}

/* ---------- live line chart ---------- */
#define HIST 120 /* 120 samples, one per second = two minutes */
static float hTemp[HIST]; /* real: chip temperature  */
static float hSim[HIST];  /* fake: sine, labelled as such */
static int histN = 0;

static void pushSample(float t, float sim)
{
  if (histN < HIST)
  {
    hTemp[histN] = t;
    hSim[histN] = sim;
    histN++;
  }
  else
  {
    for (int i = 0; i < HIST - 1; i++)
    {
      hTemp[i] = hTemp[i + 1];
      hSim[i] = hSim[i + 1];
    }
    hTemp[HIST - 1] = t;
    hSim[HIST - 1] = sim;
  }
}

/* One series; both share it, and a third would just be another call. */
static void plot(const float *d, int n, float vmin, float vmax,
                 int x0, int y0, int w, int h, uint8_t color)
{
  for (int i = 1; i < n; i++)
    vga.drawLine(x0 + (i - 1) * w / (HIST - 1), mapY(d[i - 1], vmin, vmax, y0, h),
                 x0 + i * w / (HIST - 1), mapY(d[i], vmin, vmax, y0, h), color);
}

static void drawLineChart(int x0, int y0, int w, int h)
{
  vga.fillRect(x0 - 44, y0 - 20, w + 48, h + 40, C_BG);
  if (histN < 2) return;

  /* The range has to follow the data. Hard-coding 0..100 would leave the
   * temperature pinned to the bottom edge and unreadable. Both series share one
   * range so they stay comparable. */
  float vmin = hTemp[0], vmax = hTemp[0];
  for (int i = 0; i < histN; i++)
  {
    if (hTemp[i] < vmin) vmin = hTemp[i];
    if (hTemp[i] > vmax) vmax = hTemp[i];
    if (hSim[i] < vmin) vmin = hSim[i];
    if (hSim[i] > vmax) vmax = hSim[i];
  }
  float pad = (vmax - vmin) * 0.15f;
  if (pad < 0.5f) pad = 0.5f; /* flat data still needs a range, or we divide by zero */
  vmin -= pad;
  vmax += pad;

  vga.setFont(&LB_Font8x16);
  for (int i = 0; i <= 2; i++)
  {
    float v = vmin + (vmax - vmin) * i / 2;
    int y = mapY(v, vmin, vmax, y0, h);
    vga.drawFastHLine(x0, y, w, C_DGRAY);
    vga.setTextColor(C_GRAY);
    char t[10];
    snprintf(t, sizeof(t), "%.1f", v);
    vga.drawString(t, x0 - 44, y - 8);
  }

  /* Join consecutive samples with segments. Plotting isolated points leaves
   * gaps between them. */
  plot(hSim, histN, vmin, vmax, x0, y0, w, h, C_LCYAN);
  plot(hTemp, histN, vmin, vmax, x0, y0, w, h, C_YELLOW);

  /* A legend is not optional when one of the series is invented. */
  vga.fillRect(x0 + w - 200, y0 + 4, 12, 3, C_YELLOW);
  vga.setTextColor(C_YELLOW);
  vga.drawString("chip temp (real)", x0 + w - 184, y0 - 4);
  vga.fillRect(x0 + w - 200, y0 + 24, 12, 3, C_LCYAN);
  vga.setTextColor(C_LCYAN);
  vga.drawString("sine (SIMULATED)", x0 + w - 184, y0 + 16);

  int lx = x0 + (histN - 1) * w / (HIST - 1);
  int ly = mapY(hTemp[histN - 1], vmin, vmax, y0, h);
  vga.fillCircle(lx, ly, 3, C_LRED);
  char t[16];
  snprintf(t, sizeof(t), "%.1fC", hTemp[histN - 1]);
  vga.setTextColor(C_WHITE);
  vga.drawString(t, lx - vga.textWidth(t) - 6, ly - 18);

  vga.drawFastHLine(x0, y0 + h, w, C_WHITE);
  vga.drawFastVLine(x0, y0, h, C_WHITE);
}

void setup()
{
  Serial.begin(115200);
  vga.begin();
  vga.setPaletteColor(C_BG, 0, 0, 25);
  vga.fillScreen(C_BG);

  vga.fillRect(0, 0, 640, 28, C_BLUE);
  vga.setFont(&LB_Font8x16);
  vga.setTextColor(C_YELLOW);
  vga.drawString("5. Charts  -  drawn by hand, no chart library", 8, 6);

  vga.setTextColor(C_WHITE);
  vga.drawString("ISR cost per display mode (measured)", 60, 40);
  drawBarChart(60, 66, 520, 180);

  vga.setTextColor(C_WHITE);
  vga.drawString("1 sample/s   (see legend: one line is simulated)", 60, 276);

  Serial.println("Bars are this project's own measurements; lines are temperature + a control");
}

void loop()
{
  static uint32_t last = 0;
  if (last == 0 || millis() - last >= 1000)
  {
    last = millis();
    /* Match the sine's amplitude to the temperature so both fit one range. */
    float t = temperatureRead();
    float sim = t + 3.0f * sinf(millis() / 8000.0f);
    pushSample(t, sim);
    vga.waitVSync();
    drawLineChart(60, 302, 520, 150);
  }
  delay(20);
}
