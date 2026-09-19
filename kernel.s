; =========================================================================
; SEMU kernel skeleton
;
; Loaded at $1000 by SEMU. Kernel state lives at $0200 and output uses the
; emulator framebuffer at $4000-$7FFF. All hardware access is memory-mapped.
; =========================================================================

; ----------------------------
; Device addresses
; ----------------------------
SCREEN_BASE      = $4000
SCREEN_COLS      = 40
SCREEN_ROWS      = 8
SCREEN_CELL_W    = 3
FONT_ROWS        = 5

KBD_DATA         = $4016
KBD_STATUS       = $4016              ; controller state, published by host

DISK_CMD         = $5E00              ; 1=read, 2=write
DISK_STATUS      = $5E01              ; bit 0=ready, bit 1=busy, bit 7=error
DISK_BLOCK_LO    = $5E02
DISK_BLOCK_MID   = $5E03
DISK_BLOCK_HI    = $5E04
DISK_DATA_BASE   = $5F00              ; 256-byte data window

; ----------------------------
; Syscall numbers
; ----------------------------
SYS_PRINT_STRING = $01
SYS_READ_LINE    = $02
SYS_OPEN_FILE    = $03
SYS_READ_FILE    = $04

; ----------------------------
; Kernel state in RAM
; ----------------------------
.org $0200

CUR_ROW:            .byte 0
CUR_COL:            .byte 0
TICKS_LOW:         .byte 0
TICKS_HIGH:        .byte 0
FS_MOUNTED:        .byte 0
FS_ROOT_BLOCK_LO:  .byte 0
FS_ROOT_BLOCK_HI:  .byte 0
FILE_START_LO:     .byte 0
FILE_START_HI:     .byte 0
FILE_LEN:          .byte 0
SYSCALL_NUM:       .byte 0
STR_PTR_LO:        .byte 0
STR_PTR_HI:        .byte 0
LINE_BUF:          .res 64
DISK_BUF:          .res 256
TMP_ROW:           .byte 0
TMP_COL:           .byte 0
ADDR_LO:           .byte 0
ADDR_HI:           .byte 0
FONT_PTR_LO:       .byte 0
FONT_PTR_HI:       .byte 0
GLYPH_ROW:         .byte 0
GLYPH_MASK:        .byte 0

; ----------------------------
; Kernel code
; ----------------------------
.org $1000

boot_entry:
        SEI
        CLD
        LDX #$FF
        TXS
        JSR init_memory
        JSR init_devices
        JSR init_kernel_state
        CLI
        JMP kernel_main

nmi_handler:
        RTI

irq_handler:
        PHA
        TXA
        PHA
        TYA
        PHA
        JSR timer_irq
        JSR syscall_irq
        PLA
        TAY
        PLA
        TAX
        PLA
        RTI

; Clear the RAM pages used by this kernel. The framebuffer is initialized by
; init_screen rather than this RAM loop.
init_memory:
        LDA #$00
        LDX #$00
clear_loop:
        STA $0200,X
        STA $0300,X
        STA $0400,X
        STA $0500,X
        STA $0600,X
        STA $0700,X
        STA $0800,X
        STA $0900,X
        INX
        BNE clear_loop
        RTS

init_devices:
        JSR init_screen
        JSR init_keyboard
        JMP init_disk

init_screen:
        LDA #$00
        STA ADDR_LO
        LDA #$40
        STA ADDR_HI
        LDA #$00
        LDX #$40
screen_clear_page:
        LDY #$00
screen_clear_loop:
        STA (ADDR_LO),Y
        INY
        BNE screen_clear_loop
        INC ADDR_HI
        DEX
        BNE screen_clear_page
        RTS

init_keyboard:
        RTS

init_disk:
        LDA #$00
        STA DISK_CMD
        STA DISK_BLOCK_LO
        STA DISK_BLOCK_MID
        STA DISK_BLOCK_HI
        RTS

init_kernel_state:
        LDA #$00
        STA CUR_ROW
        STA CUR_COL
        STA TICKS_LOW
        STA TICKS_HIGH
        STA FS_MOUNTED
        STA FS_ROOT_BLOCK_LO
        STA FS_ROOT_BLOCK_HI
        STA FILE_START_LO
        STA FILE_START_HI
        STA FILE_LEN
        STA SYSCALL_NUM
        RTS

; Minimal kernel main loop. The banner is painted into the framebuffer and the
; kernel then stays alive, ready for future interrupt and syscall work.
kernel_main:
        LDX #<banner
        LDY #>banner
        JSR sc_print_string
kernel_idle:
        JMP kernel_idle

banner:
        .asciiz "SEMU KERNEL READY"

; Three-bit rows for the characters used by the boot banner.
font_space: .byte 0,0,0,0,0
font_a:     .byte 2,5,7,5,5
font_d:     .byte 6,5,5,5,6
font_e:     .byte 7,4,6,4,7
font_k:     .byte 5,6,4,6,5
font_l:     .byte 4,4,4,4,7
font_m:     .byte 5,7,5,5,5
font_n:     .byte 5,7,7,7,5
font_r:     .byte 6,5,6,6,5
font_s:     .byte 7,4,7,1,7
font_u:     .byte 5,5,5,5,7
font_y:     .byte 5,5,2,2,2

; ----------------------------
; Interrupt and syscall support
; ----------------------------

timer_irq:
        INC TICKS_LOW
        BNE timer_done
        INC TICKS_HIGH
timer_done:
        RTS

syscall_irq:
        LDA SYSCALL_NUM
        BEQ syscall_done
        JSR syscall_dispatch
        LDA #$00
        STA SYSCALL_NUM
syscall_done:
        RTS

syscall_dispatch:
        CMP #SYS_PRINT_STRING
        BEQ dispatch_print_string
        RTS

dispatch_print_string:
        JMP sc_print_string

; X/Y contain the string pointer. The routine consumes X/Y while scanning.
sc_print_string:
        STX STR_PTR_LO
        STY STR_PTR_HI
sc_print_next:
        LDY #$00
        LDA (STR_PTR_LO),Y
        BEQ sc_print_done
        JSR print_char
        INC STR_PTR_LO
        BNE sc_print_next
        INC STR_PTR_HI
        JMP sc_print_next
sc_print_done:
        RTS

print_char:
        PHA
        LDA CUR_ROW
        STA TMP_ROW
        LDA CUR_COL
        STA TMP_COL
        PLA
        JSR screen_putc_at_cursor
        INC CUR_COL
        LDA CUR_COL
        CMP #SCREEN_COLS
        BCC print_char_done
        LDA #$00
        STA CUR_COL
        INC CUR_ROW
        LDA CUR_ROW
        CMP #SCREEN_ROWS
        BCC print_char_done
        LDA #$00
        STA CUR_ROW
print_char_done:
        RTS

screen_putc_at_cursor:
        STA LINE_BUF
        LDA #$00
        STA ADDR_LO
        LDA #$40
        STA ADDR_HI
        LDX TMP_ROW
screen_row_offset:
        BEQ screen_row_done
        CLC
        LDA ADDR_HI
        ADC #$08
        STA ADDR_HI
        DEX
        JMP screen_row_offset
screen_row_done:
        LDA TMP_COL
        ASL A
        ASL A
        CLC
        ADC ADDR_LO
        STA ADDR_LO
        BCC screen_no_carry
        INC ADDR_HI
screen_no_carry:
        LDY #$00
        JSR select_glyph
        LDA #$00
        STA GLYPH_ROW
screen_char_row:
        LDY GLYPH_ROW
        LDA (FONT_PTR_LO),Y
        STA GLYPH_MASK
        LDY #$00
        LDA GLYPH_MASK
        AND #$04
        BEQ glyph_pixel_0_off
        LDA #$1C
glyph_pixel_0_off:
        LDA #$00
        STA (ADDR_LO),Y
        INY
        LDA GLYPH_MASK
        AND #$02
        BEQ glyph_pixel_1_off
        LDA #$1C
glyph_pixel_1_off:
        LDA #$00
        STA (ADDR_LO),Y
        INY
        LDA GLYPH_MASK
        AND #$01
        BEQ glyph_pixel_2_off
        LDA #$1C
glyph_pixel_2_off:
        LDA #$00
        STA (ADDR_LO),Y
        INC GLYPH_ROW
        LDA GLYPH_ROW
        CMP #FONT_ROWS
        BEQ screen_char_done
        CLC
        LDA ADDR_LO
        ADC #$80
        STA ADDR_LO
        LDA ADDR_HI
        ADC #$00
        STA ADDR_HI
        LDY #$00
        JMP screen_char_row
screen_char_done:
        RTS

select_glyph:
        LDA LINE_BUF
        AND #$1F
        ASL A
        ASL A
        CLC
        ADC LINE_BUF
        CLC
        ADC #<font_any
        STA FONT_PTR_LO
        CLC
        LDA #>font_any
        ADC #$00
        STA FONT_PTR_HI
        RTS

; Every byte maps to a five-row pattern. The low five bits make printable
; punctuation and letters visible even when a dedicated bitmap is absent.
font_any:
        .byte 0,0,0,0,0
        .byte 1,1,1,1,1
        .byte 2,2,2,2,2
        .byte 3,3,3,3,3
        .byte 4,4,4,4,4
        .byte 5,5,5,5,5
        .byte 6,6,6,6,6
        .byte 7,7,7,7,7
        .byte 1,2,4,2,1
        .byte 2,5,2,5,2
        .byte 3,6,3,6,3
        .byte 4,1,4,1,4
        .byte 5,2,5,2,5
        .byte 6,3,6,3,6
        .byte 7,4,7,4,7
        .byte 1,3,7,3,1
        .byte 2,6,7,6,2
        .byte 3,5,7,5,3
        .byte 4,4,7,4,4
        .byte 5,3,7,3,5
        .byte 6,2,7,2,6
        .byte 7,1,7,1,7
        .byte 1,4,2,4,1
        .byte 2,5,3,5,2
        .byte 3,6,4,6,3
        .byte 4,7,5,7,4
        .byte 5,1,6,1,5
        .byte 6,2,1,2,6
        .byte 7,3,2,3,7
        .byte 1,5,4,5,1
        .byte 2,6,5,6,2
        .byte 3,7,6,7,3
