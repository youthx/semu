# Debugger and graphics

The native module owns the SDL3 window. Sere sends it CPU registers, the
framebuffer, memory-view bytes, and the current execution state. The CPU and
the window are separate: closing or pausing the window does not add any new
instruction semantics to the 6502.

## Window contents

The emulated screen is a 128x128 RGB332 texture displayed at 2x logical zoom.
The debugger panel is 168 logical units wide and shows one of two layouts.

### Tabbed layout

The left panel has `CPU` and `MEM` tabs. Press `M` or click a tab to switch.
The screen sits to the right of that panel.

### Split layout

Press `L` to switch layouts. The arrangement becomes:

```text
MEM panel | 128x128 screen | CPU panel
```

Press `L` again to return to the tabbed layout. The window is resized when the
layout changes.

## Controls

| Key | Action |
| --- | --- |
| `Space` | Run or pause |
| `S`, `N`, `F10` | Step one instruction |
| `R`, `F5` | Reset |
| `[` / `-` | Slow down |
| `]` / `=` | Speed up |
| `M` | Toggle CPU/memory tab |
| `L` | Toggle layout |
| `F` | Focus the emulated screen |
| `Esc` | Quit, or release screen focus |

When the screen has focus, keyboard input is sent to the emulated controller
and debugger shortcuts are ignored except `Esc` and `F`.

## Execution states

The panel can display READY, RUNNING, PAUSED, HALTED, ERROR, and ENDED. The
state is calculated in `src/main.sere` and passed to `gfx_frame` each frame.

## Adding graphics behavior

The Sere declarations live in `libs/graphics.sere`. Their C implementations
live in `libs/native/native.cpp`. Keep this boundary explicit:

1. Add an `extern "C"` declaration in the Sere library.
2. Add the matching native function.
3. Register boxed calls in `sere_mod_init` when Sere calls the function through
   the module ABI.
4. Rebuild the native target before rebuilding the Sere application.

The framebuffer upload is intentionally explicit and little-endian. Any new
bulk upload path should be checked against the existing eight-byte path before
being used for visible pixels.
