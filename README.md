# SEMU

![SEMU logo](assets/semu-logo.png)

**SEMU** is a small 6502 emulator written in [Sere](https://github.com/serelang/sere).
It is a hands-on machine: flat memory, memory-mapped hardware, cartridge-style
banking, persistent SRAM, and a block device instead of emulator-only shortcuts.

The included `main.s` is a playable Snake program. It is also a useful example
of writing directly to the emulator's framebuffer and reading the controller
port.

## Start here

SEMU currently targets Windows with the pre-0.1.9 Sere toolchain, LLVM/MSVC,
and SDL3 available at runtime.

```powershell
sere build
bin\semu.exe main.s
```

SDL3 is loaded dynamically. Put `SDL3.dll` on `PATH`, or keep it in the MSYS2
MinGW64 location used by the native module.

## Useful commands

```powershell
bin\semu.exe --help
bin\semu.exe main.s --check
bin\semu.exe main.s --run=50000
```

Running without a program shows the CLI help. Passing an assembly source opens
the interactive emulator; `--run=N` executes without opening a window.

## Debugger controls

| Key | Action |
| --- | --- |
| `Space` | Run or pause the CPU |
| `S` | Execute one instruction |
| `R` | Reset the program |
| `M` | Switch between CPU and memory views |
| `L` | Switch between tabbed and split layouts |
| `F` | Give keyboard focus to the emulated screen |
| `Esc` | Quit, or release screen focus |

The split layout puts memory on the left, the emulated screen in the middle,
and CPU state on the right.

## Memory-mapped storage

Storage is ordinary 6502 bus traffic. No instructions were added to the CPU.

| Range | Hardware |
| --- | --- |
| `$A000-$BFFF` | 8 KiB battery-backed SRAM |
| `$5D00` | PRG bank register |
| `$5D01` | CHR bank register |
| `$8000-$9FFF` | Selected PRG bank |
| `$C000-$DFFF` | Selected CHR bank |
| `$5E00-$5E07` | Block-device registers |
| `$5F00-$5FFF` | 256-byte block buffer |

Backing files are stored beside `semu.exe`, not in the caller's working
directory:

```text
bin/semu.sram
bin/semu.prg
bin/semu.chr
bin/semu.disk
```

The block device uses a READY/BUSY status bit and completes commands after a
cycle delay, so software has to poll it like a real peripheral.

## Project map

```text
src/main.sere       emulator loop, CLI, and window integration
libs/vm.sere        6502 registers, memory bus, stack, and cycle accounting
libs/processor.sere instruction table and opcode handlers
libs/native/        SDL3 debugger and persistent storage backend
assets/             SVG source plus PNG, ICO, and runtime bitmap assets
main.s              6502 Snake demo
```

The Windows application icon and the SDL window icon use the SEMU artwork in
`assets/`. The SVG is the editable source; the PNG is convenient for README
and release pages; the ICO is used by Windows.

## Detailed documentation

The longer technical notes live in [`docs/`](docs/README.md). They cover the
memory map, bus behavior, assembler syntax, command-line modes, debugger
layouts, and persistent storage registers.

## Why this exists

The interesting part is keeping the boundary honest. A 6502 program should see
devices through addresses and bus timing, just as it would on a cartridge or a
disk controller. The emulator provides the machine around that program, then
gets out of the way.
