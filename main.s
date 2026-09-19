.export "Main"
.segment "CODE"

; ============================================================
; Snake Game for SEMU 6502 VM
; Framebuffer: $4000-$7FFF (128x128 RGB332)
; Input:      $4016
; Origin:     $1000 (set by host)
; ============================================================

; -------------------------------
; Zero‑page game state variables
; -------------------------------

snakeLen        = $00    ; number of body segments
snakeDir        = $01    ; current direction (bitmask)

appleX          = $02    ; apple cell X (0–31)
appleY          = $03    ; apple cell Y (0–31)

randSeed        = $04    ; pseudo‑random generator seed
frameCounter    = $05    ; timing

tmp0            = $06
tmp1            = $07
tmp2            = $08
tmp3            = $09
tmp4            = $0A
tmp5            = $0B

; head position
headX           = $10
headY           = $11

; body array: x,y pairs
bodyStart       = $12    ; x0
bodyEnd         = $4F    ; last byte of buffer

; rendering scratch
ptrLo           = $20
ptrHi           = $21

pixelX          = $22
pixelY          = $23

addrLo          = $24
addrHi          = $25

; drawing scratch
color           = $0C    ; colour of the block being drawn
cellX           = $0D    ; cell the block is drawn at
cellY           = $0E
idx             = $0F    ; index into the body pair buffer
pair            = $26    ; pointer to the current x/y pair
rowTmp          = $27    ; row counter inside DrawCell
len2            = $28    ; snakeLen * 2

; -------------------------------
; Hardware / constants
; -------------------------------

PORT            = $4016
SCREEN_BASE     = $4000

DIR_UP          = $01
DIR_RIGHT       = $02
DIR_DOWN        = $04
DIR_LEFT        = $08

COLOR_SNAKE     = $E0    ; green-ish
COLOR_APPLE     = $3F    ; red-ish
COLOR_BG        = $00    ; black

CELL_SIZE       = 4
GRID_W          = 32
GRID_H          = 32
MAX_SNAKE       = 31    ; the $12-$4F pair buffer holds 31 segments

; ============================================================
; Main entry
; ============================================================

.proc Main
    JSR Init

MainLoop:
    JSR ReadInput
    JSR UpdateSnake
    JSR CheckApple
    JSR DrawFrame
    JSR Delay
    JMP MainLoop
.endproc

; ============================================================
; Init: clear screen, set snake, spawn apple
; ============================================================

.proc Init
    JSR ClearScreen

    ; snake length = 3
    LDA #$03
    STA snakeLen

    ; head in center (16,16)
    LDA #$10
    STA headX
    STA headY

    ; body segments behind head
    LDA #$0F
    STA bodyStart       ; x0
    LDA #$10
    STA bodyStart+1     ; y0

    LDA #$0E
    STA bodyStart+2     ; x1
    LDA #$10
    STA bodyStart+3     ; y1

    LDA #$0D
    STA bodyStart+4     ; x2
    LDA #$10
    STA bodyStart+5     ; y2

    ; direction = right
    LDA #DIR_RIGHT
    STA snakeDir

    ; random seed
    LDA #$5A
    STA randSeed

    ; spawn apple
    JSR SpawnApple

    RTS
.endproc

; ============================================================
; ClearScreen: fill $4000-$7FFF with COLOR_BG
; ============================================================

.proc ClearScreen
    LDA #COLOR_BG
    LDY #$00

    LDA #<SCREEN_BASE
    STA ptrLo
    LDA #>SCREEN_BASE
    STA ptrHi

ClearLoop:
    LDA #COLOR_BG
    STA (ptrLo),Y

    INY
    BNE ClearLoop

    INC ptrHi
    LDA ptrHi
    CMP #$80          ; $4000 -> $7FFF
    BNE ClearLoop

    RTS
.endproc

; ============================================================
; SpawnApple: simple LFSR random, place apple in 0..31 grid
; ============================================================

.proc SpawnApple
    LDA randSeed
    ASL
    BCC no_xor
    EOR #$1D
no_xor:
    STA randSeed

    ; X coord
    LDA randSeed
    AND #$1F
    STA appleX

    ; Y coord
    ROL
    AND #$1F
    STA appleY

    RTS
.endproc

; ============================================================
; DrawCell: draw 4x4 block at (cellX=X, cellY=Y) with color=A
; ============================================================

.proc DrawCell
    ; the colour lives in `color`, the target cell in `cellX` / `cellY`
    LDA cellX
    ASL
    ASL
    STA pixelX

    ; pixelY = cellY * 4
    LDA cellY
    ASL
    ASL
    STA pixelY

    ; rowOffset = pixelY * 128, as a 16 bit value:
    ;   high byte = pixelY >> 1, low byte = (pixelY & 1) << 7
    LDA pixelY
    LSR
    STA addrHi
    LDA #$00
    ROR
    STA addrLo

    ; add pixelX
    LDA addrLo
    CLC
    ADC pixelX
    STA addrLo
    BCC no_carry
    INC addrHi
no_carry:

    ; add SCREEN_BASE
    LDA addrLo
    CLC
    ADC #<SCREEN_BASE
    STA addrLo
    LDA addrHi
    ADC #>SCREEN_BASE
    STA addrHi

    ; draw 4x4 block: Y walks the columns, rowTmp counts the rows
    LDA #$00
    STA rowTmp
row_loop:
    LDY #$00
col_loop:
    LDA color
    STA (addrLo),Y
    INY
    CPY #CELL_SIZE
    BNE col_loop

    ; next row: addr += 128
    CLC
    LDA addrLo
    ADC #$80
    STA addrLo
    LDA addrHi
    ADC #$00
    STA addrHi

    INC rowTmp
    LDA rowTmp
    CMP #CELL_SIZE
    BNE row_loop

    RTS
.endproc

; ============================================================
; DrawAt: DrawCell for the cell already in cellX/cellY
; ============================================================

.proc DrawAt
    LDX cellX
    LDY cellY
    JMP DrawCell
.endproc

; ============================================================
; ReadInput: read $4016, update snakeDir (no 180° turns)
; ============================================================

.proc ReadInput
    LDA PORT

    ; up
    AND #%00010000
    BEQ check_down
    LDA snakeDir
    CMP #DIR_DOWN
    BEQ check_down
    LDA #DIR_UP
    STA snakeDir

check_down:
    LDA PORT
    AND #%00100000
    BEQ check_left
    LDA snakeDir
    CMP #DIR_UP
    BEQ check_left
    LDA #DIR_DOWN
    STA snakeDir

check_left:
    LDA PORT
    AND #%01000000
    BEQ check_right
    LDA snakeDir
    CMP #DIR_RIGHT
    BEQ check_right
    LDA #DIR_LEFT
    STA snakeDir

check_right:
    LDA PORT
    AND #%10000000
    BEQ done_input
    LDA snakeDir
    CMP #DIR_LEFT
    BEQ done_input
    LDA #DIR_RIGHT
    STA snakeDir

done_input:
    RTS
.endproc

; ============================================================
; UpdateSnake: shift body, move head, wrap on 0..31
; ============================================================

.proc UpdateSnake
    ; shift body segments: from tail down to first
    LDA snakeLen
    CMP #$01
    BCC shift_done         ; a length of zero would index below the buffer

    ; X = 2 * (snakeLen - 1), the byte offset of the last x/y pair
    SEC
    SBC #$01
    ASL
    TAX
shift_loop:
    CPX #$00
    BEQ shift_done

    ; copy previous segment to current
    LDA bodyStart-2,X      ; x[i] = x[i-1]
    STA bodyStart,X
    LDA bodyStart-1,X      ; y[i] = y[i-1]
    STA bodyStart+1,X

    DEX
    DEX
    JMP shift_loop

shift_done:
    ; first segment takes old head
    LDA headX
    STA bodyStart
    LDA headY
    STA bodyStart+1

    ; move head by direction
    LDA snakeDir
    CMP #DIR_UP
    BEQ move_up
    CMP #DIR_RIGHT
    BEQ move_right
    CMP #DIR_DOWN
    BEQ move_down
    CMP #DIR_LEFT
    BEQ move_left
    RTS

move_up:
    DEC headY
    JMP wrap
move_down:
    INC headY
    JMP wrap
move_left:
    DEC headX
    JMP wrap
move_right:
    INC headX

wrap:
    ; wrap 0..31
    LDA headX
    AND #$1F
    STA headX
    LDA headY
    AND #$1F
    STA headY
    RTS
.endproc

; ============================================================
; CheckApple: grow snake if head hits apple, respawn apple
; ============================================================

.proc CheckApple
    LDA headX
    CMP appleX
    BNE no_hit
    LDA headY
    CMP appleY
    BNE no_hit

    ; hit: grow, but only while the pair buffer has room
    LDA snakeLen
    CMP #MAX_SNAKE
    BCS no_hit
    CLC
    ADC #$01
    STA snakeLen

    JSR SpawnApple

no_hit:
    RTS
.endproc

; ============================================================
; DrawSnake + DrawApple + DrawFrame
; ============================================================

.proc DrawSnake
    ; draw head
    LDA COLOR_SNAKE
    STA color
    LDA headX
    STA cellX
    LDA headY
    STA cellY
    JSR DrawAt

    ; draw body
    LDA snakeLen
    ASL
    STA len2
    LDA #$00
    STA idx
body_loop:
    LDA idx
    CMP len2
    BCS body_done

    ; pair = bodyStart + idx, so (pair),Y reads x first and y second
    CLC
    ADC #bodyStart
    STA pair

    LDY #$00
    LDA (pair),Y
    STA cellX
    INY
    LDA (pair),Y
    STA cellY
    JSR DrawAt

    LDA idx
    CLC
    ADC #$02
    STA idx
    JMP body_loop

body_done:
    RTS
.endproc

.proc DrawApple
    LDA COLOR_APPLE
    STA color
    LDA appleX
    STA cellX
    LDA appleY
    STA cellY
    JSR DrawAt
    RTS
.endproc

.proc DrawFrame
    ; optional: clear each frame or leave trail
    JSR ClearScreen
    JSR DrawSnake
    JSR DrawApple
    RTS
.endproc

; ============================================================
; Delay: crude frame delay
; ============================================================

.proc Delay
    ; short pause: the host already paces the emulator one frame at a time
    LDX #$40
outer:
    LDY #$FF
inner:
    DEY
    BNE inner
    DEX
    BNE outer
    RTS
.endproc
