/*
 * 11_Menu - interaction with nothing but the on-board BOOT button.
 *
 * A menu needs select, confirm and back; the hardware offers one button. So you
 * have to invent a vocabulary, which is far more interesting than wiring three
 * more buttons:
 *
 *     short press = next item
 *     long press  = enter / confirm
 *     double click = back
 *
 * The screen is in two halves: diagnostics on top (raw level, debounced level,
 * how long it has been held, the recogniser's state, the last gesture), and a
 * working menu below. Press the button and you can watch the whole chain move.
 *
 *
 * ========================= Four things to get right =========================
 *
 * 1) **Pressed is LOW.** The BOOT button shorts GPIO0 to ground, so pressed
 *    reads LOW and released reads HIGH. The internal pull-up is required, or
 *    the pin floats when released and the reading is noise. Most people write
 *    this backwards the first time.
 *
 * 2) **Debounce.** A mechanical contact bounces dozens of times over a few
 *    milliseconds. Untreated, one press looks like fifteen and the menu jumps
 *    several items. The fix: only accept a level once it has held steady for
 *    DEBOUNCE_MS.
 *
 * 3) **Never use delay() for debouncing or for timing a long press.** Write
 *    `if (pressed) { delay(20); ... }` and every animation on screen freezes
 *    while the CPU sits there. Record timestamps and check them each pass
 *    through loop() instead - a state machine. The animations in this sketch
 *    will expose a blocking implementation immediately.
 *
 * 4) **GPIO0 is a strapping pin.** Held low at power-up it puts the chip in
 *    download mode instead of running your program. So do not hold it while
 *    resetting; once running it is an ordinary input.
 *
 *
 * =========================== An unavoidable trade ===========================
 *
 * Distinguishing a single click from a double click means **waiting after the
 * first release** to see whether a second press arrives. So a single click is
 * inherently DOUBLE_MS late (250 ms here). That is the price, not a defect. The
 * only way to avoid the delay is to give up double clicks.
 *
 * A long press, by contrast, fires **while the button is still down**. Waiting
 * for release feels broken: you have held it long enough and nothing happens
 * until you let go. Small difference, large change in feel.
 */
#include <LB_VGA.h>
#include "photo_small.h"
#include "button_types.h" /* enums must live in a header - see that file */

#define BTN_PIN 0      /* the on-board BOOT button */
#define DEBOUNCE_MS 20 /* a level must hold this long to count */
#define LONG_MS 600    /* held at least this long is a long press */
#define DOUBLE_MS 250  /* after release, wait this long for a second press */

static uint16_t C(uint8_t r, uint8_t g, uint8_t b) { return VGA.color565(r, g, b); }
static uint16_t BG, PANEL, ACCENT, DIM, HILITE, OKC, OFFC;

/* ---------- button: debounce plus gesture recognition ---------- */
static const char *STATE_NAME[] = {"IDLE", "PRESSED", "LONG_FIRED", "WAIT_2ND"};

static BtnState bstate = S_IDLE;
static bool rawLevel = true;     /* pin as read, HIGH = not pressed */
static bool stable = true;       /* level after debouncing */
static uint32_t lastChange = 0;  /* when the raw level last moved */
static uint32_t pressStart = 0;  /* when the current press began */
static uint32_t releaseTime = 0; /* when the last release happened */
static uint32_t heldMs = 0;      /* for display only */

/*
 * Called once per loop, returns the gesture recognised on this pass.
 * **Nothing here blocks** - there is no delay anywhere, every "wait" is a
 * comparison against a timestamp.
 */
static BtnEvent buttonPoll()
{
  uint32_t now = millis();
  bool raw = digitalRead(BTN_PIN); /* HIGH = released */

  /* --- debounce: any change restarts the timer; stable only follows once the
   *     level has been quiet for long enough --- */
  if (raw != rawLevel)
  {
    rawLevel = raw;
    lastChange = now;
  }
  bool prevStable = stable;
  if (now - lastChange >= DEBOUNCE_MS)
    stable = rawLevel;
  bool pressedEdge = (prevStable && !stable);  /* high -> low */
  bool releasedEdge = (!prevStable && stable); /* low -> high */

  heldMs = (!stable) ? (now - pressStart) : 0;

  switch (bstate)
  {
  case S_IDLE:
    if (pressedEdge)
    {
      pressStart = now;
      bstate = S_PRESSED;
    }
    break;

  case S_PRESSED:
    /* Fires while the button is still held - see the header comment. */
    if (!stable && now - pressStart >= LONG_MS)
    {
      bstate = S_LONG_FIRED;
      return EV_LONG;
    }
    if (releasedEdge)
    {
      releaseTime = now;
      bstate = S_WAIT_SECOND; /* not a click yet; a second press may follow */
    }
    break;

  case S_LONG_FIRED:
    /* The long press was already reported; this release means nothing. */
    if (releasedEdge)
      bstate = S_IDLE;
    break;

  case S_WAIT_SECOND:
    if (pressedEdge)
    {
      bstate = S_IDLE;
      return EV_DOUBLE;
    }
    if (now - releaseTime >= DOUBLE_MS)
    {
      bstate = S_IDLE;
      return EV_SHORT; /* waited long enough, nothing came - it was a click */
    }
    break;
  }
  return EV_NONE;
}

/* ---------- menu ---------- */
static Item items[] = {
    {"Shapes", true},
    {"Bouncing ball", true},
    {"Photo", true},
    {"Bar chart", true},
    /* Greyed out on purpose: palette animation needs 16-colour mode and this
     * sketch runs in full colour. The mode is fixed at begin(). */
    {"Palette animation", false},
};
#define ITEM_N (sizeof(items) / sizeof(items[0]))

static int sel = 0;
static int running = -1; /* -1 = in the menu, otherwise the running item */

/* ---------- the screens behind the menu; deliberately small ---------- */
static void screenShapes()
{
  VGA.fillScreen(BG);
  VGA.drawRect(20, 40, 80, 50, C(0, 220, 255));
  VGA.fillRect(200, 40, 80, 50, C(0, 220, 255));
  VGA.drawCircle(60, 140, 28, C(80, 230, 120));
  VGA.fillCircle(240, 140, 28, C(80, 230, 120));
  VGA.drawTriangle(110, 200, 190, 200, 150, 150, C(255, 120, 200));
}

/* Speeds in pixels per second, as established in 05_Bounce. */
static float bx = 40, by = 60, bvx = 126, bvy = 96;
static void screenBounceStep(float dt)
{
  VGA.fillCircle((int)bx, (int)by, 10, BG);
  bx += bvx * dt;
  by += bvy * dt;
  if (bx < 42 || bx > 278) bvx = -bvx;
  if (by < 52 || by > 228) bvy = -bvy;
  VGA.fillCircle((int)bx, (int)by, 10, C(255, 200, 60));
}

static void screenPhoto()
{
  VGA.fillScreen(BG);
  VGA.drawJpg(photo_jpg, PHOTO_JPG_LEN, (320 - PHOTO_W) / 2, (240 - PHOTO_H) / 2);
  VGA.drawRect((320 - PHOTO_W) / 2 - 1, (240 - PHOTO_H) / 2 - 1, PHOTO_W + 2, PHOTO_H + 2, PANEL);
}

static void screenChart()
{
  VGA.fillScreen(BG);
  static const int v[] = {25, 46, 68};
  static const char *n[] = {"320", "640/16", "PSRAM"};
  for (int i = 0; i < 3; i++)
  {
    int h = v[i] * 140 / 100;
    int x = 50 + i * 80;
    VGA.fillRect(x, 200 - h, 50, h, C(80 + i * 60, 220 - i * 50, 255 - i * 80));
    VGA.setTextColor(C(255, 255, 255));
    char t[8]; snprintf(t, sizeof(t), "%d%%", v[i]);
    VGA.drawString(t, x + (50 - VGA.textWidth(t)) / 2, 200 - h - 12);
    VGA.setTextColor(DIM);
    VGA.drawString(n[i], x + (50 - VGA.textWidth(n[i])) / 2, 204);
  }
  VGA.drawFastHLine(40, 200, 250, C(255, 255, 255));
}

static void drawRunningBar()
{
  VGA.fillRect(0, 0, 320, 14, PANEL);
  VGA.setTextColor(ACCENT);
  VGA.drawString(items[running].name, 4, 3);
  VGA.setTextColor(DIM);
  const char *tip = "double-click = back";
  VGA.drawString(tip, 316 - VGA.textWidth(tip), 3);
}

/* ---------- diagnostics ---------- */
static const char *lastEventName = "-";
static uint32_t lastEventAt = 0;

static void drawDiag()
{
  VGA.fillRect(0, 16, 320, 92, BG);
  VGA.setTextColor(DIM);
  VGA.drawString("raw", 8, 20);
  VGA.drawString("debounced", 8, 34);
  VGA.drawString("held", 8, 48);
  VGA.drawString("state", 8, 62);
  VGA.drawString("last event", 8, 76);

  /* Raw and debounced side by side: on a press the left one flickers and the
   * right one does not. Debouncing explained without a word of theory. */
  VGA.setTextColor(rawLevel ? OFFC : OKC);
  VGA.drawString(rawLevel ? "HIGH (up)" : "LOW (pressed)", 90, 20);
  VGA.setTextColor(stable ? OFFC : OKC);
  VGA.drawString(stable ? "HIGH (up)" : "LOW (pressed)", 90, 34);

  char buf[32];
  VGA.setTextColor(C(255, 255, 255));
  snprintf(buf, sizeof(buf), "%4lu ms", (unsigned long)heldMs);
  VGA.drawString(buf, 90, 48);
  /* A progress bar for the long press: hold the button and watch it fill. */
  int w = (int)(heldMs > LONG_MS ? 120 : heldMs * 120 / LONG_MS);
  VGA.drawRect(160, 48, 122, 9, PANEL);
  VGA.fillRect(161, 49, w, 7, heldMs >= LONG_MS ? OKC : ACCENT);

  VGA.setTextColor(ACCENT);
  VGA.drawString(STATE_NAME[bstate], 90, 62);

  /* Hold the gesture name bright for a second so the eye can catch it. */
  VGA.setTextColor((millis() - lastEventAt < 1000) ? C(255, 240, 120) : DIM);
  VGA.drawString(lastEventName, 90, 76);
}

static void drawMenu()
{
  VGA.fillRect(0, 110, 320, 130, BG);
  VGA.drawFastHLine(0, 112, 320, PANEL);
  for (size_t i = 0; i < ITEM_N; i++)
  {
    int y = 120 + (int)i * 22;
    if ((int)i == sel)
    {
      VGA.fillRect(4, y - 3, 312, 20, HILITE);
      VGA.setTextColor(C(255, 255, 255));
      VGA.drawString(">", 10, y);
    }
    else
      VGA.setTextColor(items[i].enabled ? C(200, 210, 230) : C(90, 95, 110));
    VGA.drawString(items[i].name, 26, y);
    if (!items[i].enabled)
    {
      VGA.setTextColor(C(90, 95, 110));
      VGA.drawString("(needs 16-color mode, see 08_Palette)", 150, y);
    }
  }
}

static void drawHeader()
{
  VGA.fillRect(0, 0, 320, 14, PANEL);
  VGA.setTextColor(C(255, 255, 255));
  VGA.drawString("6. One button: short=next  long=enter  2x=back", 4, 3);
}

void setup()
{
  Serial.begin(115200);
  VGA.begin(LB_VGA_320x240);

  BG = C(10, 12, 24); PANEL = C(30, 36, 62); ACCENT = C(90, 220, 255);
  DIM = C(120, 130, 160); HILITE = C(40, 70, 130);
  OKC = C(120, 235, 150); OFFC = C(150, 155, 175);

  /* Without the pull-up the pin floats when released and the reading is noise. */
  pinMode(BTN_PIN, INPUT_PULLUP);

  VGA.fillScreen(BG);
  drawHeader();
  drawDiag();
  drawMenu();
  Serial.println("short = next, long = enter, double-click = back");
}

void loop()
{
  BtnEvent e = buttonPoll();

  if (e != EV_NONE)
  {
    lastEventAt = millis();
    lastEventName = (e == EV_SHORT) ? "SHORT" : (e == EV_LONG) ? "LONG" : "DOUBLE";
    Serial.printf("gesture: %s\n", lastEventName);
  }

  /*
   * Entering a screen on a long press leaves the button still held; the release
   * that follows must not be mistaken for new input. A delay() here would be
   * the obvious fix and the wrong one - this sketch is about not blocking. So
   * ignore gestures for a moment using a timestamp, like everything else.
   */
  static uint32_t ignoreUntil = 0;
  if (millis() < ignoreUntil)
    e = EV_NONE;

  if (running < 0)
  {
    /* ---- in the menu ---- */
    if (e == EV_SHORT)
      sel = (sel + 1) % ITEM_N;
    else if (e == EV_LONG && items[sel].enabled)
    {
      running = sel;
      ignoreUntil = millis() + 400;
      VGA.fillScreen(BG);
      switch (running)
      {
      case 0: screenShapes(); break;
      case 2: screenPhoto(); break;
      case 3: screenChart(); break;
      default: break; /* the ball is drawn per frame below */
      }
      drawRunningBar();
      return;
    }

    static uint32_t last = 0;
    if (millis() - last >= 50) /* 50 ms is plenty for the diagnostics */
    {
      last = millis();
      VGA.waitVSync();
      drawDiag();
      drawMenu();
    }
  }
  else
  {
    /* ---- inside a screen ---- */
    if (e == EV_DOUBLE)
    {
      running = -1;
      ignoreUntil = millis() + 200;
      VGA.fillScreen(BG);
      drawHeader();
      drawDiag();
      drawMenu();
      return;
    }
    if (running == 1)
    {
      static uint32_t lastUs = 0;
      uint32_t nowUs = micros();
      float dt = lastUs ? (nowUs - lastUs) / 1000000.0f : 1.0f / 60;
      lastUs = nowUs;
      if (dt > 0.1f) dt = 0.1f;

      VGA.waitVSync();
      screenBounceStep(dt);
      drawRunningBar();
    }
  }
  delay(5);
}
