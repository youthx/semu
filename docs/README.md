# SEMU documentation

These notes describe the machine SEMU actually runs. They are meant to be
read alongside the source, especially when writing a small operating system or
an assembly program for the emulator.

## Start with these

1. [Memory and the bus](memory.md) explains the 64 KiB address space and which
   addresses belong to hardware.
2. [Writing 6502 programs](assembly.md) covers assembler syntax, directives,
   labels, expressions, and diagnostics.
3. [Command line](cli.md) lists every current mode and option.
4. [Debugger and graphics](debugger.md) explains the SDL window and controls.
5. [Persistent storage](storage.md) documents SRAM, banking, and the block
   device at the register level.

## Source map

| Area | Source |
| --- | --- |
| Emulator loop and CLI | [`src/main.sere`](../src/main.sere) |
| CPU state and memory bus | [`libs/vm.sere`](../libs/vm.sere) |
| Instruction execution | [`libs/processor.sere`](../libs/processor.sere) |
| Two-pass assembler | [`libs/asm6502.sere`](../libs/asm6502.sere) |
| SDL and storage backend | [`libs/native/native.cpp`](../libs/native/native.cpp) |
| Sere/native declarations | [`libs/graphics.sere`](../libs/graphics.sere) |
| Example 6502 program | [`main.s`](../main.s) |

## One important distinction

Sere is the implementation language for the emulator. The files loaded by
SEMU, such as `main.s`, are 6502 assembly programs. They run inside the VM and
only see the VM's memory bus and devices.
