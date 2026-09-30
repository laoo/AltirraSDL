# MARIA PBI device (MariaCEL sprite blitter)

`maria` is a Parallel Bus Interface device (PBI ID `$08`, fixed by the
hardware) that emulates the MariaCEL sprite blitter module of the MARIA
FPGA cartridge together with the parts of the host cartridge that the
blitter needs. It appears in **Configure System → Devices → Add Device →
Parallel Bus Interface (PBI) devices → MARIA** on both Windows and SDL3.

The hardware project (specification, C++ reference model, 6502 test
programs) is external; the reference model is vendored in
`src/Altirra/source/mariacel/` (see the README there for the origin
commit and the only local modifications). The device wrapper is
`src/Altirra/h/maria.h` / `src/Altirra/source/maria.cpp`.

## What is emulated

| block | address | notes |
|---|---|---|
| Blitter register `CTL`/`STATUS` | `$D1B2` | write `CTL`, read `STATUS`; only while the device is PBI-selected |
| `RAMMAP4000L/H`, `RAMMAP8000L/H` | `$D1A8–$D1AB` | 16 KB windows at `$4000` and `$8000`; value = SDRAM block (`0` = stock RAM), bit 7 of the high byte selects a blitter BRAM block (`SPRLIST`, `TEXTAB`, `DERIVED`, `STRIPMAP`, last strip) |
| `RAMMAPC000L/H` | `$D1AC–$D1AD` | latched for `.maria` only — the `$C000` window is not wired |
| SDRAM | 32 MB behind the windows | `Sdram` of the model; also the texture source of the rasterizer |
| `PBIRAMBANK` | `$D14F` | selects the page shown at `$DF00–$DFFF`; banks `$19`/`$1A`/`$1B` are the CEL palette R/G/B tables (256 entries each, read/write) |
| `MIRQ_CONTROL`, `MEXTBNL/H`, `BGEN` | `$D111`, `$D14C/D`, `$D14A` | latched only; the zero (reset) state is the only one the blitter subset defines |
| Video output | View → Video Outputs → MARIA | 320×240, 8 bpp through the CEL palette, square pixels |

Everything else on the `$D1xx` page (7800-style background, sound chips,
SD card, memory pointers, …) is not emulated; those registers are not
decoded (writes fall through, reads float).

## Timing

The blitter's video output is synchronous with the computer's video. The
device hooks the simulator's `VBLANK` event (end of scanline 248): the
previous frame is closed (`frame_done`), the prologue runs if the program
released the sprite list (`SNAP`), and ten strip events are scheduled 24
scanlines apart so that strip *s* is finished at the end of the scanline
before line `8 + 24·s`. The blitter cycle budget per strip/vblank window
is derived from the 135 MHz core clock and the machine rate, so PAL and
NTSC get the correct windows. 6502 accesses to SDRAM through the windows
are counted per window and fed to the model's bus contention injection
(docs/11 §5 of the hardware project).

## Save states, diagnostics

The device implements `IATDeviceSnapshot` (BRAM blocks, SDRAM, strip
buffers, registers, palette, frame sequencing). The PBI select register is
not part of the upstream simulator state, so the device restores its own
PDVS bit on load. `.maria` in the debugger dumps the full status
(registers, window mapping, per-strip cycle usage of the last frame,
model warnings).

## Screenshots

**Edit → Save Frame / Copy Frame** capture whatever the display area
shows, as on Windows: the MARIA picture while View → Video Outputs →
MARIA is selected, the computer picture otherwise.

Harnesses can capture the MARIA output explicitly, whether or not it is
the one on screen:

| interface | request |
|---|---|
| `--test-mode` socket | `screenshot /tmp/maria.png maria` (also `computer`, `display`); `list_video_outputs` |
| AltirraBridge | `SCREENSHOT path=/tmp/maria.png output=maria`, `RAWSCREEN inline=true output=maria`; `VIDEO_OUTPUTS` |
| Python bridge SDK | `a.screenshot(path=..., output="maria")`, `a.rawscreen(output="maria")`, `a.video_outputs()` |
| C bridge SDK | `atb_screenshot_output_path(c, "maria", path)`, `atb_screenshot_output_inline(...)`, `atb_rawscreen_output_inline(...)`, `atb_video_outputs(c)` |

The MARIA capture is the 320×240 frame buffer converted through the CEL
palette (pending palette writes are applied first). The bridge also
works in the headless `AltirraBridgeServer`.

## Testing

The hardware project's `out/apps/*.xex` programs run unmodified (they
select the device through `PDVREG` themselves). `d_square.xex` draws a
rotating 32×32 sprite; its palette entries `1`, `2`, `7` must be written
through banks `$19–$1B` before anything is visible, e.g. from the debugger:

```
e d14f 19 : e df01 c8 : e df07 dc
e d14f 1a : e df02 c8 : e df07 dc
e d14f 1b : e df07 dc
```

The sprite's `ANGLE` byte is at `$5000` while window `$4000` shows
`SPRLIST`; it advances by one per frame when the `SNAP` handshake works.
