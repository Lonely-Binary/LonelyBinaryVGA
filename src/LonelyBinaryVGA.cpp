/*
 * LonelyBinaryVGA.cpp - the panel: esp_lcd RGB parallel output in HV mode, fed
 * through a bounce buffer.
 *
 * The timing, the sync polarity and the no_fb + bounce structure are unchanged
 * from the version that was verified on hardware; only the drawing side moved
 * to Lonely Binary GFX. What that move removed is worth recording:
 *
 *   - No more byte-order trap. The old build had to ask LovyanGFX for
 *     rgb565_nonswapped rather than its default rgb565_2Byte, because LCD_CAM
 *     puts a 16-bit word straight onto D0..D15. Getting it wrong left the
 *     geometry perfect and scrambled only the colours, which looks exactly like
 *     a wiring fault. We now own the format, so the trap does not exist.
 *
 *   - No more coupling to someone else's 4bpp layout. The old scan-out ISR
 *     hard-coded the nibble order LovyanGFX happens to use, reverse-engineered
 *     from its pixelcopy internals. It is now OUR format, declared in LB_Panel.h
 *     and used by both sides, so an upstream change cannot silently swap every
 *     pixel pair.
 */
#include "LonelyBinaryVGA.h"

/* These used to arrive transitively through LovyanGFX. Now that the graphics
 * dependency is gone they have to be asked for by name - which is the honest
 * state of affairs either way. */
#include <Arduino.h>
#include <esp_cpu.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <freertos/task.h>

#include <esp_lcd_panel_ops.h>
#include <esp_lcd_panel_rgb.h>
#include <esp_heap_caps.h>

/* ---- VGA 640x480@60 ---- */
#define OUT_W 640
#define OUT_H 480
/* Nominal 25.175 MHz; 25.0 is -0.7%, inside VGA tolerance, and the S3's
 * fractional divider is cleaner there. Measured field rate 59.5 Hz against a
 * theoretical 25e6/(800*525) = 59.52. */
#define PCLK_HZ 25000000
#define H_FRONT 16
#define H_SYNC 96
#define H_BACK 48
#define V_FRONT 10
#define V_SYNC 2
#define V_BACK 33
#define H_TOTAL (OUT_W + H_FRONT + H_SYNC + H_BACK)

/*
 * Output lines per bounce callback.
 *
 * Do not raise this to "reduce interrupt overhead". Measured at 40 lines: the
 * busy percentage did not move at all - the bottleneck is memory bandwidth -
 * and a single ISR grew long enough to trip the interrupt watchdog and panic
 * the chip. 640*480 % (640*10) == 0, so a callback never straddles a line.
 */
#define BOUNCE_LINES 10

/* ISR state. File-static: faster to reach than an object member, and it keeps
 * the interrupt away from anything that might live in flash. */
static uint8_t *s_fb = nullptr;
static uint32_t s_stride = 0;
static uint16_t s_pal16[16];
static esp_lcd_panel_handle_t s_panel = nullptr;
static SemaphoreHandle_t s_vsyncSem = nullptr;
static volatile uint32_t s_frames = 0;
static volatile uint32_t s_isrCalls = 0;
static volatile uint64_t s_isrCycles = 0;
static uint32_t s_budgetCycles = 1;

/* ---- 320x240 RGB565, doubled to 640x480 ----
 * One 32-bit store writes two identical pixels; (y >> 1) makes two output lines
 * read the same source line. */
static bool IRAM_ATTR isr_upscale2x(esp_lcd_panel_handle_t, void *bounce,
                                    int pos_px, int len_bytes, void *)
{
  const uint32_t t0 = esp_cpu_get_cycle_count();
  uint16_t *out = (uint16_t *)bounce;
  int y = pos_px / OUT_W;
  const int lines = len_bytes / 2 / OUT_W;
  for (int l = 0; l < lines; l++, y++)
  {
    const uint16_t *src = (const uint16_t *)(s_fb + (size_t)(y >> 1) * s_stride);
    uint32_t *d = (uint32_t *)(out + (size_t)l * OUT_W);
    for (int x = 0; x < OUT_W / 2; x++)
    {
      const uint32_t c = src[x];
      d[x] = c | (c << 16);
    }
  }
  s_isrCalls++;
  s_isrCycles += esp_cpu_get_cycle_count() - t0;
  return false;
}

/* ---- 640x480 4bpp, expanded through the 16-entry palette ----
 * LEFT (even) pixel in the HIGH nibble - our format, declared in LB_Panel.h.
 * Two 16-bit stores rather than one 32-bit store with a shift and an or: it
 * measured 46% of a core against 71% for the shift-and-or form, because s16i is
 * a single instruction. */
static bool IRAM_ATTR isr_palette4(esp_lcd_panel_handle_t, void *bounce,
                                   int pos_px, int len_bytes, void *)
{
  const uint32_t t0 = esp_cpu_get_cycle_count();
  uint16_t *out = (uint16_t *)bounce;
  int y = pos_px / OUT_W;
  const int lines = len_bytes / 2 / OUT_W;
  for (int l = 0; l < lines; l++, y++)
  {
    const uint8_t *src = s_fb + (size_t)y * s_stride;
    uint16_t *d = out + (size_t)l * OUT_W;
    for (int x = 0; x < OUT_W / 2; x++)
    {
      const uint32_t b = src[x];
      d[x * 2] = s_pal16[b >> 4];
      d[x * 2 + 1] = s_pal16[b & 0x0F];
    }
  }
  s_isrCalls++;
  s_isrCycles += esp_cpu_get_cycle_count() - t0;
  return false;
}

static bool IRAM_ATTR isr_vsync(esp_lcd_panel_handle_t,
                                const esp_lcd_rgb_panel_event_data_t *, void *)
{
  s_frames++;
  BaseType_t woken = pdFALSE;
  if (s_vsyncSem) xSemaphoreGiveFromISR(s_vsyncSem, &woken);
  return woken == pdTRUE;
}

/* Bring-up runs in a task pinned to a core: esp_intr_alloc installs the
 * interrupt on whichever core calls it. Core 0, leaving core 1 for user code. */
static volatile bool s_initOk = false, s_initDone = false;
static esp_lcd_rgb_panel_event_callbacks_t s_cbs;

static void panelInitTask(void *)
{
  esp_lcd_rgb_panel_config_t cfg = {};
  cfg.clk_src = LCD_CLK_SRC_DEFAULT;
  cfg.timings.pclk_hz = PCLK_HZ;
  cfg.timings.h_res = OUT_W;
  cfg.timings.v_res = OUT_H;
  cfg.timings.hsync_pulse_width = H_SYNC;
  cfg.timings.hsync_back_porch = H_BACK;
  cfg.timings.hsync_front_porch = H_FRONT;
  cfg.timings.vsync_pulse_width = V_SYNC;
  cfg.timings.vsync_back_porch = V_BACK;
  cfg.timings.vsync_front_porch = V_FRONT;
  cfg.timings.flags.hsync_idle_low = 0; /* idle high, pulse low = VGA's */
  cfg.timings.flags.vsync_idle_low = 0; /* negative sync polarity        */
  cfg.timings.flags.pclk_active_neg = 1;
  cfg.data_width = 16;
  cfg.bits_per_pixel = 16;
  cfg.num_fbs = 0;                                  /* we own the framebuffer   */
  cfg.bounce_buffer_size_px = OUT_W * BOUNCE_LINES; /* DMA only reads this      */
  cfg.sram_trans_align = 8;
  cfg.psram_trans_align = 64;
  cfg.hsync_gpio_num = LB_VGA_PIN_HSYNC;
  cfg.vsync_gpio_num = LB_VGA_PIN_VSYNC;
  cfg.de_gpio_num = -1;   /* VGA has no DE; -1 selects HV mode */
  cfg.pclk_gpio_num = -1; /* the pixel clock stays on chip     */
  cfg.disp_gpio_num = -1;
  static const int pins[16] = LB_VGA_PIN_DATA_INIT;
  for (int i = 0; i < 16; i++) cfg.data_gpio_nums[i] = pins[i];
  cfg.flags.no_fb = 1;

  const esp_err_t e = esp_lcd_new_rgb_panel(&cfg, &s_panel);
  if (e != ESP_OK)
    Serial.printf("[LB_VGA] esp_lcd_new_rgb_panel failed: %s\n", esp_err_to_name(e));
  else
  {
    esp_lcd_rgb_panel_register_event_callbacks(s_panel, &s_cbs, nullptr);
    if (esp_lcd_panel_reset(s_panel) == ESP_OK && esp_lcd_panel_init(s_panel) == ESP_OK)
      s_initOk = true;
    else
      Serial.println("[LB_VGA] panel reset/init failed");
  }
  s_initDone = true;
  vTaskDelete(nullptr);
}

/* ─────────────────────────── LB_VGA_Panel ───────────────────────────── */

bool LB_VGA_Panel::setPaletteColor(uint8_t index, uint8_t r, uint8_t g, uint8_t b)
{
  if (index > 15 || _mode == LB_VGA_320x240) return false;
  _pal[index] = LB_RGB(r, g, b);
  /* The ISR reads its own RGB565 copy - it cannot afford a conversion per
   * pixel. Both are updated here, which is why this override exists. */
  s_pal16[index] = lb_to_rgb565(_pal[index]);
  return true;
}

bool LB_VGA_Panel::start()
{
  const size_t bytes = (size_t)stride() * panelHeight();

  /* !! Internal SRAM, never PSRAM !!
   * The scan-out ISR reads this buffer 36 MB/s, every frame, forever. In PSRAM
   * it compiles, boots and shows a picture - and then produces drifting
   * interference bands the moment anything is drawn. */
  _fb = (uint8_t *)heap_caps_calloc(1, bytes, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
  if (!_fb)
  {
    Serial.printf("[LB_VGA] framebuffer allocation failed (%u bytes of internal SRAM)\n",
                  (unsigned)bytes);
    Serial.printf("[LB_VGA] largest free internal block: %u\n",
                  (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT));
    return false;
  }

  s_fb = _fb;
  s_stride = stride();

  if (_mode == LB_VGA_640x480_16)
  {
    /* A classic EGA palette, so the mode is usable out of the box. */
    static const uint8_t ega[16][3] = {
        {0, 0, 0}, {0, 0, 170}, {0, 170, 0}, {0, 170, 170},
        {170, 0, 0}, {170, 0, 170}, {170, 85, 0}, {170, 170, 170},
        {85, 85, 85}, {85, 85, 255}, {85, 255, 85}, {85, 255, 255},
        {255, 85, 85}, {255, 85, 255}, {255, 255, 85}, {255, 255, 255}};
    for (uint8_t i = 0; i < 16; i++) setPaletteColor(i, ega[i][0], ega[i][1], ega[i][2]);
  }

  /* Display time available per bounce callback, in cycles at the clock actually
   * in use - the Tools menu can select 240, 160 or 80 MHz. */
  s_budgetCycles = (uint32_t)((uint64_t)H_TOTAL * BOUNCE_LINES *
                              getCpuFrequencyMhz() * 1000000ULL / PCLK_HZ);
  if (!s_budgetCycles) s_budgetCycles = 1;

  s_vsyncSem = xSemaphoreCreateBinary();
  s_cbs = {};
  s_cbs.on_bounce_empty = (_mode == LB_VGA_320x240) ? isr_upscale2x : isr_palette4;
  s_cbs.on_vsync = isr_vsync;

  s_initOk = s_initDone = false;
  xTaskCreatePinnedToCore(panelInitTask, "lbvgaInit", 4096, nullptr, 3, nullptr, 0);
  while (!s_initDone) delay(5);

  if (s_initOk)
    Serial.printf("[LB_VGA] %dx%d %s up (framebuffer %u bytes @ %p, %u internal SRAM left)\n",
                  panelWidth(), panelHeight(),
                  _mode == LB_VGA_320x240 ? "RGB565" : "16-colour",
                  (unsigned)bytes, _fb,
                  (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT));
  return s_initOk;
}

/* ───────────────────────────── LB_VGA ───────────────────────────────── */

bool LB_VGA::begin() { return start(); }

void LB_VGA::waitVSync(uint32_t timeout_ms)
{
  if (s_vsyncSem) xSemaphoreTake(s_vsyncSem, pdMS_TO_TICKS(timeout_ms));
}

uint32_t LB_VGA::frameCount() const { return s_frames; }

float LB_VGA::isrLoadPercent()
{
  const uint32_t n = s_isrCalls;
  const uint64_t c = s_isrCycles;
  s_isrCalls = 0;
  s_isrCycles = 0;
  if (!n) return 0;
  return (float)((double)c / n) * 100.0f / s_budgetCycles;
}
