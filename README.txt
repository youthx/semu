SEMU
====

SEMU is a small 6502 emulator written in Sere. It is mainly a place to try
out real 8-bit machine ideas: a flat address space, memory-mapped hardware,
cartridge-style banking, and a simple disk device.

The included `main.s` program is a playable Snake demo. The emulator opens an
SDL3 window for the framebuffer and debugger. Press `M` to switch between the
CPU and memory views. Press `L` to switch between the tabbed layout and the
split layout. `Space` pauses or resumes the CPU, `S` steps one instruction,
and `R` resets the program.

Building
--------

This project uses the pre-0.1.9 Sere toolchain and an MSVC/LLVM build on
Windows. SDL3 is loaded at runtime, so `SDL3.dll` must be available on `PATH`
or in the MSYS2 MinGW64 directory used by the native module.

From this directory:

	sere build
	bin\\semu.exe main.s

Useful command-line checks:

	bin\\semu.exe --help
	bin\\semu.exe main.s --check
	bin\\semu.exe main.s --run=50000

Storage hardware
----------------

Storage is exposed through the normal 6502 bus. No CPU instructions were
added.

	$A000-$BFFF   8 KiB battery-backed SRAM
	$5D00         PRG bank register
	$5D01         CHR bank register
	$8000-$9FFF   selected PRG bank
	$C000-$DFFF   selected CHR bank
	$5E00-$5E07   block-device registers
	$5F00-$5FFF   256-byte block buffer

The backing files are placed beside `semu.exe`: `semu.sram`, `semu.prg`,
`semu.chr`, and `semu.disk`. They are created when the emulator actually uses
the storage bus, not when it only prints help.

Repository layout
-----------------

`src/main.sere` contains the emulator loop and CLI. The 6502 VM and instruction
table live in `libs/vm.sere` and `libs/processor.sere`. SDL bindings and the
storage bus are in `libs/native`. The logo sources are in `assets/`; the SVG
is the editable version, while the PNG and ICO are generated copies used for
distribution and the Windows application icon.
