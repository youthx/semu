# Command line reference

The executable is `bin\semu.exe` in a normal project checkout.

## Invocation

```text
semu [PROGRAM] [OPTIONS]
```

The first non-option argument is the program path. If `SEMU_PROGRAM` is set,
it takes precedence over the command line program path.

With no arguments, SEMU prints the help page and exits. This keeps accidental
launches from opening a running demo when the user only wanted to inspect the
available commands.

## Options

| Option | Behavior |
| --- | --- |
| `--help`, `-h` | Print usage and examples |
| `--version` | Print the version |
| `--org=ADDR` | Load at an address such as `$8000`, `0x8000`, or `32768` |
| `--check` | Assemble source and report diagnostics without running it |
| `--compile` | Assemble and print a short hex preview |
| `--out=FILE` | Write the assembled image to a binary file |
| `--symbols` | Print the assembler symbol table |
| `--run=N` | Run headless for at most `N` instructions |
| `--peek=ADDR[:COUNT]` | Dump memory after a headless run |
| `--dump-cpu` | Print opcode-table and implementation information |

## Program formats

| Extension | Treatment |
| --- | --- |
| `.s`, `.asm`, `.a65` | Assemble as 6502 source |
| `.hex` | Load a hexadecimal byte dump |
| `.bin` | Load a raw byte image |
| `.nes` | Rejected until a NES bus, mapper, and PPU profile exists |

## Examples

Assemble and run a source file in the debugger:

```powershell
bin\semu.exe main.s
```

Check source without opening SDL:

```powershell
bin\semu.exe main.s --check
```

Run a deterministic headless sample and inspect memory:

```powershell
bin\semu.exe main.s --run=50000 --peek=$4000:64
```

Compile an image at a cartridge-like origin:

```powershell
bin\semu.exe game.s --org=$8000 --out=bin\game.bin
```

`--out` writes raw assembled bytes. The origin is not stored in the file, so
load it later with the same `--org=` value.

## Exit behavior

Headless execution stops when it reaches the instruction limit, when the VM is
halted, when an image ends, or when an unknown opcode is encountered. The
interactive window exits with `Esc` or its close button. Persistent storage is
flushed on normal emulator shutdown.
