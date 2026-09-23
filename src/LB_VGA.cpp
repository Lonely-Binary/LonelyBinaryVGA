/*
 * LB_VGA.cpp - panel layer: esp_lcd RGB parallel output in HV mode, fed through
 * a bounce buffer.
 *
 * The timing, the sync polarity and the no_fb + bounce-buffer structure are
 * inherited from ESP32S3_NES_VGA in the same repository, where they have been
 * running for two projects. What is new here:
 *
 *   1. the framebuffer is a LovyanGFX sprite, so the user draws straight into it
 *   2. one bounce callback per mode: pixel doubling, or 16-colour expansion
 */
#include "LB_VGA.h"
#include <esp_lcd_panel_ops.h>
#include <esp_lcd_panel_rgb.h>
#include <esp_heap_caps.h>

/* ---- VGA 640x480@60 timing ---- */
#define OUT_W 640
#define OUT_H 480
/* Nominal is 25.175 MHz; 25.0 MHz is -0.7%, well inside VGA tolerance, and the
 * S3's fractional divider is noticeably cleaner at 25.0. Measured field rate is
 * 59.5 Hz against a theoretical 25e6/(800*525) = 59.52. */
#define PCLK_HZ 25000000
#define H_FRONT 16
#define H_SYNC 96
#define H_BACK 48
#define V_FRONT 10
#define V_SYNC 2
#define V_BACK 33
#define H_TOTAL (OUT_W + H_FRONT + H_SYNC + H_BACK)

/*
 * Output lines produced per bounce callback.
 *
 * Do not raise this to "reduce interrupt overhead". Measured at 40 lines: the
 * busy percentage did not move at all (the bottleneck is memory bandwidth, not
 * per-call overhead) and a single ISR grew long enough to trip the interrupt
 * watchdog and panic the chip.
 *
 * 640*480 % (640*10) == 0, so a callback never straddles a line boundary.
 */
#define BOUNCE_LINES 10

/* ---- ISR state. File-static: faster to reach than object members, and it
 *      keeps the interrupt away from anything that might live in flash. ---- */
static uint8_t *s_fb = nullptr; /* sprite buffer, internal SRAM */
static uint32_t s_stride = 0;   /* bytes per framebuffer line */
static uint16_t s_pal16[16];    /* 16-colour mode: RGB565 copy of the palette */
static esp_lcd_panel_handle_t s_panel = nullptr;
static SemaphoreHandle_t s_vsyncSem = nullptr;
static volatile uint32_t s_frames = 0;
static volatile uint32_t s_isrCalls = 0;
static volatile uint64_t s_isrCycles = 0;

/* CPU cycles available per bounce callback. Derived from the actual clock at
 * begin(): the Tools menu can set 240, 160 or 80 MHz, and hard-coding 240 made
 * isrLoadPercent() silently wrong on the other two. */
static uint32_t s_budgetCycles = 1;

LB_VGA_Class VGA;

/* ---- Mode A: 320x240 RGB565, doubled to 640x480 ----
 * Horizontally, one 32-bit store writes two identical pixels (both halves are
 * the same value, so half-word order does not matter here). Vertically, (y >> 1)
 * makes two output lines read the same source line. */
static bool IRAM_ATTR isr_upscale2x(esp_lcd_panel_handle_t p, void *bounce,
                                    int pos_px, int len_bytes, void *ctx)
{
  uint32_t t0 = esp_cpu_get_cycle_count();
  uint16_t *out = (uint16_t *)bounce;
  int y = pos_px / OUT_W;
  int lines = len_bytes / 2 / OUT_W;
  for (int l = 0; l < lines; l++, y++)
  {
    const uint16_t *src = (const uint16_t *)(s_fb + (size_t)(y >> 1) * s_stride);
    uint32_t *d = (uint32_t *)(out + (size_t)l * OUT_W);
    for (int x = 0; x < OUT_W / 2; x++)
    {
      uint32_t c = src[x];
      d[x] = c | (c << 16);
    }
  }
  s_isrCalls++;
  s_isrCycles += esp_cpu_get_cycle_count() - t0;
  return false;
}

/* ---- Mode B: 640x480 4bpp, expanded through a 16-entry palette ----
 *
 * Nibble order: LovyanGFX stores the left (even) pixel in the HIGH nibble.
 * That follows from pixelcopy.hpp, where the shift for pixel n is
 * (-(n*bits + bits) & 7), which is 4 for pixel 0.
 *
 * Getting this backwards swaps every pixel pair. The error is one pixel wide,
 * so the picture still looks broadly right - it is very hard to spot in
 * anything but text. */
static bool IRAM_ATTR isr_palette4(esp_lcd_panel_handle_t p, void *bounce,
                                   int pos_px, int len_bytes, void *ctx)
{
  uint32_t t0 = esp_cpu_get_cycle_count();
  uint16_t *out = (uint16_t *)bounce;
  int y = pos_px / OUT_W;
  int lines = len_bytes / 2 / OUT_W;
  for (int l = 0; l < lines; l++, y++)
  {
    const uint8_t *src = s_fb + (size_t)y * s_stride;
    uint16_t *d = out + (size_t)l * OUT_W;
    for (int x = 0; x < OUT_W / 2; x++)
    {
      uint32_t b = src[x];
      d[x * 2] = s_pal16[b >> 4];       /* high nibble = left pixel */
      d[x * 2 + 1] = s_pal16[b & 0x0F]; /* low nibble  = right pixel */
    }
  }
  s_isrCalls++;
  s_isrCycles += esp_cpu_get_cycle_count() - t0;
  return false;
}

static bool IRAM_ATTR isr_vsync(esp_lcd_panel_handle_t p,
                                const esp_lcd_rgb_panel_event_data_t *e, void *ctx)
{
  s_frames++;
  BaseType_t woken = pdFALSE;
  if (s_vsyncSem)
    xSemaphoreGiveFromISR(s_vsyncSem, &woken);
  return woken == pdTRUE;
}

/* ---- Panel bring-up ----
 * This has to run in a task pinned to a core: esp_intr_alloc installs the
 * interrupt on whichever core calls it. Core 0, leaving core 1 for user code. */
static volatile bool s_initOk = false, s_initDone = false;
static esp_lcd_rgb_panel_event_callbacks_t s_cbs;

static void panelInitTask(void *arg)
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
  cfg.timings.flags.vsync_idle_low = 0; /* negative sync polarity          */
  cfg.timings.flags.pclk_active_neg = 1;
  cfg.data_width = 16;
  cfg.bits_per_pixel = 16;
  cfg.num_fbs = 0;                                  /* we own the framebuffer */
  cfg.bounce_buffer_size_px = OUT_W * BOUNCE_LINES; /* DMA only ever reads this */
  cfg.sram_trans_align = 8;
  cfg.psram_trans_align = 64;
  cfg.hsync_gpio_num = LB_VGA_PIN_HSYNC;
  cfg.vsync_gpio_num = LB_VGA_PIN_VSYNC;
  cfg.de_gpio_num = -1;   /* VGA has no DE; -1 selects HV mode */
  cfg.pclk_gpio_num = -1; /* the pixel clock stays on chip     */
  cfg.disp_gpio_num = -1;
  static const int pins[16] = LB_VGA_PIN_DATA_INIT;
  for (int i = 0; i < 16; i++)
    cfg.data_gpio_nums[i] = pins[i];
  cfg.flags.no_fb = 1;

  esp_err_t e = esp_lcd_new_rgb_panel(&cfg, &s_panel);
  if (e != ESP_OK)
  {
    Serial.printf("[LB_VGA] esp_lcd_new_rgb_panel failed: %s\n", esp_err_to_name(e));
  }
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

bool LB_VGA_Class::begin(LB_VGA_Mode m)
{
  if (_ready)
    return true; /* mode is fixed after the first call - see LB_VGA.h */
  _mode = m;

  const int fbw = (m == LB_VGA_320x240) ? 320 : 640;
  const int fbh = (m == LB_VGA_320x240) ? 240 : 480;

  /*
   * rgb565_nonswapped, not rgb565_2Byte.
   *
   * LovyanGFX's default 16-bit format is byte-swapped, because SPI panels want
   * the high byte first. LCD_CAM puts a 16-bit word straight onto D0..D15, so it
   * needs native byte order. Getting this wrong leaves the geometry perfectly
   * correct and scrambles only the colours, which looks exactly like a wiring
   * fault and sends you off measuring the R2R ladder.
   */
  setColorDepth(m == LB_VGA_320x240 ? lgfx::color_depth_t::rgb565_nonswapped
                                    : lgfx::color_depth_t::palette_4bit);

  /*
   * false = allocate in internal SRAM. This is the decision the whole library
   * rests on (see LB_VGA.h). Setting it to true compiles, boots and shows a
   * picture - and then produces drifting interference bands the moment anything
   * is drawn.
   */
  setPsram(false);

  if (!createSprite(fbw, fbh))
  {
    Serial.printf("[LB_VGA] framebuffer allocation failed (%d bytes of internal SRAM)\n",
                  m == LB_VGA_320x240 ? fbw * fbh * 2 : fbw * fbh / 2);
    Serial.printf("[LB_VGA] largest free internal block: %u bytes\n",
                  (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT));
    return false;
  }

  s_fb = (uint8_t *)getBuffer();
  s_stride = (m == LB_VGA_320x240) ? (uint32_t)fbw * 2 : (uint32_t)fbw / 2;

  if (m == LB_VGA_640x480_16)
  {
    /* A classic EGA/VGA palette, so the mode is usable out of the box. */
    static const uint8_t ega[16][3] = {
        {0, 0, 0}, {0, 0, 170}, {0, 170, 0}, {0, 170, 170},
        {170, 0, 0}, {170, 0, 170}, {170, 85, 0}, {170, 170, 170},
        {85, 85, 85}, {85, 85, 255}, {85, 255, 85}, {85, 255, 255},
        {255, 85, 85}, {255, 85, 255}, {255, 255, 85}, {255, 255, 255}};
    for (int i = 0; i < 16; i++)
      setPaletteColor(i, ega[i][0], ega[i][1], ega[i][2]);
  }

  /* Cycles of display time per bounce callback, at the clock actually in use. */
  s_budgetCycles = (uint32_t)((uint64_t)H_TOTAL * BOUNCE_LINES *
                              getCpuFrequencyMhz() * 1000000ULL / PCLK_HZ);
  if (!s_budgetCycles)
    s_budgetCycles = 1;

  s_vsyncSem = xSemaphoreCreateBinary();
  s_cbs = {};
  s_cbs.on_bounce_empty = (m == LB_VGA_320x240) ? isr_upscale2x : isr_palette4;
  s_cbs.on_vsync = isr_vsync;

  s_initOk = s_initDone = false;
  xTaskCreatePinnedToCore(panelInitTask, "lbvgaInit", 4096, nullptr, 3, nullptr, 0);
  while (!s_initDone)
    delay(5);

  _ready = s_initOk;
  if (_ready)
    Serial.printf("[LB_VGA] %dx%d %s up (framebuffer %u bytes @ %p, %u bytes internal SRAM left)\n",
                  fbw, fbh, m == LB_VGA_320x240 ? "RGB565" : "16-colour",
                  (unsigned)(s_stride * fbh), s_fb,
                  (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT));
  return _ready;
}

void LB_VGA_Class::waitVSync(uint32_t timeout_ms)
{
  if (!s_vsyncSem)
    return;
  xSemaphoreTake(s_vsyncSem, pdMS_TO_TICKS(timeout_ms));
  /* Refresh the ISR's palette copy here rather than making the caller remember
   * syncPalette(). Sixteen conversions per frame is free, and it means a palette
   * written through the base class still reaches the screen. */
  if (_mode == LB_VGA_640x480_16)
    syncPalette();
}

uint32_t LB_VGA_Class::frameCount() const { return s_frames; }

void LB_VGA_Class::setPaletteColor(uint8_t index, uint8_t r, uint8_t g, uint8_t b)
{
  if (index > 15)
    return;
  LGFX_Sprite::setPaletteColor(index, r, g, b);
  s_pal16[index] = (uint16_t)(((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3));
}

void LB_VGA_Class::syncPalette()
{
  auto *p = getPalette();
  if (!p)
    return;
  for (int i = 0; i < 16; i++)
    s_pal16[i] = (uint16_t)(((p[i].R8() & 0xF8) << 8) |
                            ((p[i].G8() & 0xFC) << 3) | (p[i].B8() >> 3));
}

float LB_VGA_Class::isrLoadPercent()
{
  uint32_t n = s_isrCalls;
  uint64_t c = s_isrCycles;
  s_isrCalls = 0;
  s_isrCycles = 0;
  if (!n)
    return 0;
  return (float)((double)c / n) * 100.0f / s_budgetCycles;
}
