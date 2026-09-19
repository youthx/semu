# Persistent storage

Storage is implemented behind the VM's normal `read` and `write` methods. The
6502 does not know whether an address is RAM, SRAM, a mapper register, or a
device register. It simply performs a bus access.

The native storage backend keeps its files beside the running executable. In a
checkout, that normally means:

```text
bin/semu.sram
bin/semu.prg
bin/semu.chr
bin/semu.disk
```

## Battery-backed SRAM

| Address | Behavior |
| --- | --- |
| `$A000-$BFFF` | 8 KiB read/write SRAM |

Writes update the SRAM image and are flushed again during normal shutdown.
Missing files start as zero-filled memory. A simple program can use it without
any driver:

```asm
SRAM = $A000

        LDA #$42
        STA SRAM
        LDA SRAM
```

## Mapper

| Address | Name | Behavior |
| --- | --- | --- |
| `$5D00` | PRG bank | Selects one of four 8 KiB PRG banks |
| `$5D01` | CHR bank | Selects one of four 8 KiB CHR banks |
| `$5D03` | Mapper ID | Reads `$4D` |
| `$8000-$9FFF` | PRG window | Reads the selected PRG bank; writes are ignored |
| `$C000-$DFFF` | CHR window | Reads/writes the selected CHR bank |

Example:

```asm
PRG_BANK = $5D00
PRG       = $8000

        LDA #$02
        STA PRG_BANK
        LDA PRG
```

The bank latch changes immediately after the register write. PRG and CHR
backing files are fixed at four banks of 8 KiB each.

## Block device

The block device has a 24-bit block number and a 256-byte data window.

| Address | Name | Access |
| --- | --- | --- |
| `$5E00` | COMMAND | Write command; read last command |
| `$5E01` | STATUS | Read status |
| `$5E02` | BLOCK_LO | Read/write block ID low byte |
| `$5E03` | BLOCK_MI | Read/write block ID middle byte |
| `$5E04` | BLOCK_HI | Read/write block ID high byte |
| `$5E05` | DEVICE_ID | Reads `$A5` |
| `$5E06` | DEVICE_TYPE | Reads `$01` |
| `$5E07` | BLOCK_SIZE | Reads `$00`, meaning 256 bytes |
| `$5F00-$5FFF` | DATA | Read/write transfer buffer |

Commands:

| Value | Meaning | Delay |
| ---: | --- | ---: |
| `$01` | Read block into DATA | 2,000 CPU cycles |
| `$02` | Write DATA to block image | 4,000 CPU cycles |
| `$03` | Flush SRAM/CHR/disk images | 2,000 CPU cycles |
| `$04` | Identify device; writes `$A5,$01,$00` to DATA | 2,000 CPU cycles |

Status bits are:

```text
bit 0  READY
bit 1  BUSY
bit 7  ERROR
```

Issue a read by setting the block ID, writing `$01` to COMMAND, then polling
until READY is set:

```asm
CMD       = $5E00
STATUS    = $5E01
BLOCK_LO  = $5E02
BLOCK_MI  = $5E03
BLOCK_HI  = $5E04
DATA      = $5F00

        LDA #$00
        STA BLOCK_LO
        STA BLOCK_MI
        STA BLOCK_HI
        LDA #$01
        STA CMD

wait:
        LDA STATUS
        AND #$80
        BNE error
        LDA STATUS
        AND #$01
        BEQ wait

        LDY #$00
copy:
        LDA DATA,Y
        STA $0200,Y
        INY
        BNE copy
```

The disk image contains 1,024 blocks, for a total of 256 KiB. Device timing is
advanced by the same CPU cycle counter used for instruction timing; no polling
opcode or special CPU state is involved.

## Adding another device

Add address handling to `semu_storage_read` and `semu_storage_write`, and call
`semu_storage_tick` from the device's cycle accounting if it is delayed. Keep
the device range disjoint from video memory and document its registers here.
