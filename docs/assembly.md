# Writing 6502 programs

SEMU's assembler is a two-pass 6502/Ricoh 2A03 assembler. The first pass
collects labels and determines addresses. The second pass emits bytes and
reports unresolved or invalid operands with source line numbers.

## Smallest useful program

```asm
        LDA #$2A
        STA $0200
        HLT
```

The default origin is `$1000`. A program can choose its own origin in source
or from the command line:

```asm
.org $8000
start:
        JMP start
```

```powershell
bin\semu.exe program.s --org=$8000
```

## Syntax

Comments begin with `;` and continue to the end of the line. Labels end with
`:` and may share a line with an instruction. Constants use `=` or `.equ`.

```asm
SCREEN = $4000
COUNT  .equ 16

start:
        LDX #$00
loop:   LDA #$1C
        STA SCREEN,X
        INX
        CPX #COUNT
        BNE loop
```

The assembler accepts hexadecimal (`$FF`, `0xFF`), binary (`%10101100`,
`0b10101100`), decimal, character literals such as `'A`, labels, the current
address `*`, and simple `+`/`-` expressions.

Low and high-byte prefixes are useful for pointers:

```asm
        LDA #<message
        STA $00
        LDA #>message
        STA $01
```

## Data directives

| Directive | Meaning |
| --- | --- |
| `.byte`, `.db`, `.by` | Emit one or more bytes |
| `.word`, `.dw` | Emit little-endian 16-bit values |
| `.text`, `.ascii` | Emit string bytes |
| `.res`, `.ds` | Reserve zero-filled bytes |
| `.align` | Pad to an alignment boundary |
| `.org` | Move the assembly address |
| `.equ` | Define a named constant |

Example:

```asm
message:
        .text "SEMU"
        .byte $00
table:
        .word start, message
        .res 8
```

## Addressing modes

The supported forms include implied, accumulator, immediate, zero page,
zero-page indexed, absolute, absolute indexed, indirect, indexed-indirect,
indirect-indexed, and relative branches.

```asm
        LDA #$10          ; immediate
        LDA $20           ; zero page
        LDA $20,X         ; zero page,X
        LDA $4000         ; absolute
        LDA $4000,X       ; absolute,X
        LDA ($00,X)       ; indexed indirect
        LDA ($00),Y       ; indirect indexed
        JMP ($FFFC)       ; indirect
        BNE loop          ; relative
```

Numeric operands at or below `$FF` use a zero-page form when the instruction
has one. Labels use the absolute form so both assembler passes agree about the
instruction size.

## Macros and conditional source

The assembler also contains support for conditional blocks, repeat blocks,
macros, procedures, exports, and symbol output. The exact directive spelling
is easiest to confirm in `libs/asm6502.sere`, where the parser and diagnostics
live; keeping examples close to that implementation avoids silently relying
on syntax from another assembler.

## Diagnostics

Use `--check` when editing a program:

```powershell
bin\semu.exe program.s --check
```

Errors are reported with a source line number. Common failures are undefined
labels, duplicate labels, invalid mnemonics, unsupported addressing modes,
branch targets outside the relative range, and images that exceed the 64 KiB
address space.

Useful inspection commands:

```powershell
bin\semu.exe program.s --symbols
bin\semu.exe program.s --compile
bin\semu.exe program.s --dump-cpu
```
