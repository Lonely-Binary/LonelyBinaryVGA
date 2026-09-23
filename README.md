# Lonely Binary VGA

VGA 640x480@60 from an ESP32-S3, through the LCD_CAM parallel peripheral into a
16-bit R2R ladder.

```cpp
#include <LonelyBinaryVGA.h>

LB_VGA vga(LB_VGA_320x240);

void setup() {
  vga.begin();
  vga.fillScreen(LB_BLACK);
  vga.setTextSize(3);
  vga.drawString("Hello, VGA!", 40, 100);
}
void loop() {}
```

Drawing comes from [Lonely Binary GFX](https://github.com/Lonely-Binary/LonelyBinaryGFX),
so the same calls work on a TFT and on e-paper - and a function that takes an
`LB_Canvas &` runs on all three.

> **v2.0.0 changed the API and the graphics engine.**
> `VGA.` became `vga.` on an object you construct, and LovyanGFX was replaced by
> Lonely Binary GFX. Sketches written for v1 need updating; every bundled
> example already is. In return each sketch is about **47 KB smaller**, there is
> no third-party graphics dependency, and the same code moves to our other
> displays.

## Hardware

An ESP32-S3 with **octal PSRAM** (N16R8 or similar) and a 16-bit R2R resistor
ladder into a VGA connector. The PSRAM is not used for video, but the Arduino
core's heap setup expects the module to be configured correctly.

| Signal | GPIO |
|---|---|
| HSYNC | 2 |
| VSYNC | 1 |
| R0..R4 | 7, 15, 16, 38, 39 |
| G0..G5 | 17, 18, 8, 3, 9, 10 |
| B0..B4 | 11, 12, 13, 14, 21 |

`D0..D15 = B0..B4, G0..G5, R0..R4` - the LCD_CAM peripheral puts one RGB565 word
straight onto the data pins, so each line must reach the matching weight on the
ladder.

**Different board?** Do not edit the library. Override the pins from your sketch:

```cpp
#define LB_VGA_PIN_HSYNC 40
#define LB_VGA_PIN_VSYNC 41
#define LB_VGA_PIN_DATA_INIT { 1,2,3,4,5, 6,7,8,9,10,11, 12,13,14,15,16 }
#include <LB_VGA.h>
```

Run `examples/01_PinCheck` after wiring: it shows colour bars, gradients and 16
single-bit patches that identify any broken or swapped data line.

### Board settings

| Setting | Value | If you get it wrong |
|---|---|---|
| PSRAM | **OPI PSRAM** | Quad mode does not detect an N16R8 module; the heap ends up too small |
| USB CDC On Boot | **Disabled** | `Serial` goes to native USB and nothing appears on the UART |

Log over the on-board UART, not native USB: the USB CDC wedges permanently after
a firmware panic, and then neither esptool nor the serial monitor can reach the
chip.

## Two modes

|  | `LB_VGA_320x240` | `LB_VGA_640x480_16` |
|---|---|---|
| Format | 320x240 RGB565 | 640x480 4bpp palette |
| Framebuffer | 150 KB internal SRAM | 150 KB internal SRAM |
| Colours | 65536 | 16 |
| Text (8x16 font) | 40 x 15 | **80 x 30** |
| CPU spent on video | 25% of one core | 46% of one core |
| Best for | photos, gradients, animation | text, charts |

Both modes keep the framebuffer in internal SRAM and both need exactly 150 KB,
so switching between them has no memory cliff. **PSRAM is left entirely to you.**

### Why not 640x480 in full colour?

It needs 600 KB, which does not fit in the chip's 512 KB of internal SRAM. Moving
it to PSRAM does not work either: PSRAM reads measure ~60 MB/s on this board and
displaying 640x480@60 alone consumes 36 MB/s of that. Sharing the rest with
drawing means a quarter-screen redraw per frame already drops scan lines, and a
full-screen redraw breaks the picture - while the frame rate keeps reporting 60,
so fps tells you nothing.

Both supported modes measure **zero late scan lines even when redrawing the whole
screen three times per frame**, because their cost is constant CPU work rather
than contention for a shared bus.

The mode is fixed by `begin()` and cannot be changed at run time.

## API

| Call | Notes |
|---|---|
| `begin(mode)` | Once, from `setup()`. Default is `LB_VGA_320x240` |
| `waitVSync(timeout_ms)` | Returns at the start of a frame; you then have ~1.44 ms of blanking |
| `frameCount()` | Fields since boot; two readings one second apart give the refresh rate (59.5 Hz) |
| `mode()` | The mode passed to `begin()` |
| `setPaletteColor(i, r, g, b)` | 16-colour mode. Prefer this over the base-class versions |
| `syncPalette()` | Rebuilds the ISR's palette copy; `waitVSync()` already does this |
| `isrLoadPercent()` | Share of one core spent on video. **Reads are destructive** - each call reports the interval since the previous one |

Everything else comes from `LGFX_Sprite`.

## Examples

| | What it shows |
|---|---|
| `00_Hello` | Smallest possible sketch |
| `01_PinCheck` | Wiring self-test - run this first on new hardware |
| `02_Text` | 80x30 text in 16-colour mode |
| `03_Pixel` | Framebuffer, coordinate system, RGB565 |
| `04_Shapes` | Lines and shapes, `draw*` vs `fill*` |
| `05_Bounce` | Animation, tearing, and what `waitVSync()` buys you |
| `06_TextConsole` | Fonts, alignment, and a serial console on screen |
| `07_Photo` | Images as arrays, JPEG decoding, scaling and centring |
| `08_Palette` | Palette cycling: the picture animates with zero pixels redrawn |
| `09_Chart` | Bar and line charts drawn by hand, no chart library |
| `10_Dashboard` | Clock, live plot, photo and status in one panel |
| `11_Menu` | A menu driven entirely by the BOOT button |
| `12_DoubleBuffer` | A PSRAM back buffer and page flipping |

`07_Photo` and `12_DoubleBuffer` include `make_assets.py`, which converts any
image into the embedded header those sketches use.

## Things that will bite you

**Per-pixel loops in 16-colour mode are slow.** Changing one pixel in a 4bpp
buffer is a read-modify-write of half a byte; through LovyanGFX's `drawPixel`
that measures ~0.92 us per pixel, or 282 ms for a full screen. Use `fillRect`,
`drawString` and `drawJpg`, which take the byte-wide fast path.

**The video ISR runs on core 0** and takes 25% (320x240) or 46% (640x480x16) of
it. Pin heavy work of your own to core 1.

**There is no second framebuffer in internal SRAM.** After the 150 KB
framebuffer, the largest free internal block is about 135 KB, so classic double
buffering does not fit. Use `waitVSync()` and redraw only what changed - or see
`12_DoubleBuffer` for a back buffer in PSRAM, which is worth it when you replace
the whole screen at once (a photo, a page change) and not worth it per frame.

**Decoding a full-screen JPEG takes ~59 ms**, about 3.5 frames, so a photo drawn
straight to the screen paints in from the top. That is expected, not a bug.

**Custom types in an `.ino` belong in a `.h`.** The Arduino build inserts
generated function prototypes at the top of the file, ahead of any `enum` or
`struct` you declared there, and you get `error: '...' does not name a type`
pointing at a line that looks perfectly fine. The same code in a `.cpp` is fine.

## License

MIT. See [LICENSE](LICENSE).
