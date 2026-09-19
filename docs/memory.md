# Memory and the bus

SEMU models a 6502 with a 16-bit address bus. Every address is masked to
`$0000-$FFFF`, so the address space contains 65,536 byte locations.

The VM keeps ordinary RAM in a 64 KiB byte buffer, but reads and writes pass
through the storage bus first. A mapped device can consume an access; ordinary
RAM is used only when no device claims the address.

```text
$0000-$00FF   zero page
$0100-$01FF   hardware stack
$0200-$03FF   commonly used by programs; main.s stores snake body data here
$4000-$7FFF   128 x 128 framebuffer, one RGB332 byte per pixel
$4016         controller input published by the host
$5D00-$5D03   cartridge mapper registers
$5E00-$5E07   block-device registers
$5F00-$5FFF   block-device data window
$8000-$9FFF   selected 8 KiB PRG bank
$A000-$BFFF   8 KiB battery-backed SRAM
$C000-$DFFF   selected 8 KiB CHR bank
$E000-$FFFF   ordinary RAM unless claimed by a future device
```

The ranges are conventions of the current machine profile. They are not extra
6502 features: a program accesses them with normal `LDA`, `STA`, indexed
loads, and indexed stores.

## Reset and program loading

The VM starts with:

```text
A  = $00
X  = $00
Y  = $00
SP = $FD
P  = I | U
PC = $1000        when SEMU loads a program directly
```

`$FFFA/$FFFB`, `$FFFC/$FFFD`, and `$FFFE/$FFFF` are reserved for the NMI,
reset, and IRQ vectors when code uses vector-based startup. SEMU's normal
loader starts an image at `$1000` unless `--org=` changes the origin.

Program loading writes bytes into the same VM bus used by the CPU. That means
an image intended for a ROM window should be treated as cartridge data rather
than ordinary RAM.

## Video memory

The framebuffer is 16,384 bytes:

```text
width  = 128 pixels
height = 128 pixels
base   = $4000
end    = $7FFF
```

Pixel `(x, y)` lives at:

```text
address = $4000 + y * 128 + x
```

Each byte is RGB332:

```text
76543210
RRRGGGBB
```

For example, `$E0` is bright red, `$1C` is green, and `$00` is black. The
host copies the framebuffer into an SDL texture before presenting a frame.

## Controller input

The host writes the current controller state to `$4016` before each running
frame. The bits are:

| Bit | Mask | Button |
| ---: | ---: | --- |
| 0 | `$01` | A / X key |
| 1 | `$02` | B / Z key |
| 2 | `$04` | Select / Right Shift |
| 3 | `$08` | Start / Enter |
| 4 | `$10` | Up |
| 5 | `$20` | Down |
| 6 | `$40` | Left |
| 7 | `$80` | Right |

Read it like any other I/O register:

```asm
PAD = $4016

        LDA PAD
        AND #$10
        BEQ up_not_pressed
```

## Cycle accounting

The processor handler adds each instruction's base cycles and addressing-mode
penalties to `VM.cycles`. Taken branches add their extra cycle, plus another
cycle when the branch crosses a page. Storage devices receive those cycle
increments so delayed hardware can move from BUSY to READY without changing
the CPU instruction set.
