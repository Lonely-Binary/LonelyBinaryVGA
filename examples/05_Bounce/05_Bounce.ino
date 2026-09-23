/*
 * 05_Bounce - animation, and where tearing comes from.
 *
 * Animation is one sentence: erase the old, draw the new, repeat. The hard part
 * is not how to move something, it is *when*.
 *
 * --- Tearing ---
 * The display reads the framebuffer sixty times a second, top to bottom, one
 * line at a time. Change the framebuffer while it is halfway down and that
 * frame shows the old picture above the split and the new one below it: a
 * horizontal seam.
 *
 * The textbook fix is double buffering - draw into a second buffer and swap
 * whole buffers at once. **That does not fit here.** One framebuffer is 150 KB,
 * and after allocating it the largest free block of internal SRAM is about
 * 135 KB. (In PSRAM? See the README - the bandwidth is not there.)
 *
 * So this uses the next best thing: **wait for the frame to start, then finish
 * drawing before the beam catches up.** When waitVSync() returns, the beam is
 * off-screen in the vertical blanking interval, about 1.44 ms. Draw fast enough
 * and it never overtakes you.
 *
 * This sketch flips waitVSync on and off every four seconds and says which is
 * active in the top-left corner. **With it off, watch the edges of the large
 * rectangle** - you will see them sliced and offset. With it on they are clean.
 * Look at the screen; do not just take the code's word for it.
 *
 * (12_DoubleBuffer revisits this with a back buffer in PSRAM, which does work,
 * but only pays off when you replace the whole screen at once.)
 */
#include <LB_VGA.h>

static uint16_t C(uint8_t r, uint8_t g, uint8_t b) { return VGA.color565(r, g, b); }

static const uint16_t BG_R = 0, BG_G = 0, BG_B = 30;

/*
 * Speeds are in **pixels per second, not pixels per frame**.
 *
 * The obvious `x += 2` means "two pixels per frame", and then the speed changes
 * whenever the frame rate does: this sketch runs at 60 Hz with waitVSync on and
 * at whatever delay(16) gives with it off, and adding drawing load would change
 * it again. Multiplying by the elapsed time instead keeps the motion identical
 * however the frame rate wobbles. Every animation should do this.
 */
static float bx = 40, by = 60, bvx = 138, bvy = 102; /* px/s */

/* Avoid one- and two-letter ALL-CAPS names: Xtensa's specreg.h defines BR, and
 * the collision is reported inside specreg.h, nowhere near your own code. */
static const int BALL_R = 10;

/* Deliberately large: repainting it costs ~9600 pixels a frame, which is what
 * makes the tearing visible. Small objects tear too finely to see. */
static float sx = 200, sy = 150, svx = -186, svy = 132; /* px/s */
static const int BOX_W = 110, BOX_H = 88;

static bool useVSync = true;

void setup()
{
  Serial.begin(115200);
  VGA.begin(LB_VGA_320x240);
  VGA.fillScreen(C(BG_R, BG_G, BG_B));
  Serial.println("waitVSync toggles every 4 s - watch the edges of the big rectangle");
}

static void moveAndBounce(float &x, float &y, float &vx, float &vy, int w, int h, float dt)
{
  x += vx * dt; /* distance = speed x time, independent of frame rate */
  y += vy * dt;
  /* Test whether the next step would leave the screen rather than whether we
   * have already left it; the latter lets objects sink into the edge and jitter. */
  if (x < 0) { x = 0; vx = -vx; }
  if (y < 24) { y = 24; vy = -vy; } /* leave room for the status bar */
  if (x + w > 320) { x = 320 - w; vx = -vx; }
  if (y + h > 240) { y = 240 - h; vy = -vy; }
}

void loop()
{
  static uint32_t lastToggle = 0;
  if (millis() - lastToggle >= 4000)
  {
    lastToggle = millis();
    useVSync = !useVSync;
  }

  /* Wait first, then draw. The other order achieves nothing. */
  if (useVSync)
    VGA.waitVSync();

  const uint16_t bg = C(BG_R, BG_G, BG_B);

  /* Erase only what the objects cover. A fillScreen would be 76800 pixels;
   * erasing these two is a little over ten thousand. */
  VGA.fillRect((int)sx, (int)sy, BOX_W, BOX_H, bg);
  VGA.fillCircle((int)bx + BALL_R, (int)by + BALL_R, BALL_R, bg);

  /* How long this frame actually took. No previous frame on the first pass, so
   * fall back to 1/60 s. */
  static uint32_t lastUs = 0;
  uint32_t nowUs = micros();
  float dt = lastUs ? (nowUs - lastUs) / 1000000.0f : 1.0f / 60;
  lastUs = nowUs;
  if (dt > 0.1f) dt = 0.1f; /* after a stall, do not teleport out of bounds */

  moveAndBounce(sx, sy, svx, svy, BOX_W, BOX_H, dt);
  moveAndBounce(bx, by, bvx, bvy, BALL_R * 2, BALL_R * 2, dt);

  /* Fill plus outline: the crisp edge makes tearing much easier to spot. */
  VGA.fillRect((int)sx, (int)sy, BOX_W, BOX_H, C(40, 90, 200));
  VGA.drawRect((int)sx, (int)sy, BOX_W, BOX_H, C(255, 255, 255));
  VGA.fillCircle((int)bx + BALL_R, (int)by + BALL_R, BALL_R, C(255, 200, 60));

  static uint32_t last = 0, lastFrames = 0;
  uint32_t nowMs = millis();
  if (nowMs - last >= 500)
  {
    uint32_t f = VGA.frameCount();
    VGA.fillRect(0, 0, 320, 22, C(0, 0, 0));
    VGA.setTextColor(useVSync ? C(120, 255, 140) : C(255, 120, 120));
    VGA.drawString(useVSync ? "waitVSync ON  - clean" : "waitVSync OFF - look for tearing", 4, 2);
    VGA.setTextColor(C(150, 150, 170));
    char buf[40];
    /* Scale by the real interval; a nominal 500 would over-report the rate. */
    snprintf(buf, sizeof(buf), "%luHz",
             (unsigned long)((f - lastFrames) * 1000UL / (nowMs - last)));
    VGA.drawString(buf, 280, 2);
    lastFrames = f;
    last = nowMs;
  }

  /* Without the vsync wait we have to throttle by hand, otherwise this loop
   * repaints several times per frame and the tearing smears into mush. */
  if (!useVSync)
    delay(16);
}
