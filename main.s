; =========================================================================
;  SNAKE  --  for semu (6502)
;
;  Build:  bin\semu.exe main.s
;  Check:  bin\semu.exe main.s --check
;
;  Hardware:
;    $4000-$7FFF   128x128 framebuffer, one RGB332 byte per pixel
;    $4016         controller port, rewritten by the host every frame
;
;  Controls:
;    Arrow keys / D-pad   steer
;    Start or A           restart after dying
;
;  Design notes
;  ------------
;  The board is a 32x32 grid of 4x4 pixel cells, so one byte of framebuffer
;  is one pixel and a cell is four bytes on each of four scanlines.
;
;  The body lives in two 256-byte ring buffers (BODY_X / BODY_Y). HEADI is the
;  index of the head segment and TAIL the index of the oldest one; both are
;  plain bytes, so incrementing them wraps for free.
;
;  Only the cells that change are repainted each step (erase the tail, draw the
;  head), so a step costs a few hundred cycles instead of clearing 16K.
;
;  The host runs `speed` cycles per displayed frame (12000 by default) with
;  vsync on. `wait` burns about 15000 cycles for responsive movement.
;  Raise DELAY_OUT or DELAY_IN for a slower game.
; =========================================================================

; --- hardware -----------------------------------------------------------
SCREEN      = $4000
PAD         = $4016

BTN_A       = %00000001
BTN_B       = %00000010
BTN_SELECT  = %00000100
BTN_START   = %00001000
BTN_UP      = %00010000
BTN_DOWN    = %00100000
BTN_LEFT    = %01000000
BTN_RIGHT   = %10000000

BTN_RESTART = %00001001                ; Start or A

; --- colours (RGB332: rrrgggbb) -----------------------------------------
BLACK       = %00000000
SNAKE       = %00011100                ; green body
SNAKE_HEAD  = %00111110                ; brighter green head
FOOD        = %11100000                ; red
DEAD        = %10000000                ; dark red, the game over screen

; --- board --------------------------------------------------------------
CELL        = 4                        ; pixels per cell
GRID        = 32                       ; cells per side (32 * 4 = 128)

; --- zero page ----------------------------------------------------------
PTR_LO      = $00                      ; screen pointer used by the drawing code
PTR_HI      = $01
DIR_X       = $02                      ; $FF is -1, $01 is +1
DIR_Y       = $03
HEAD_X      = $04                      ; head cell
HEAD_Y      = $05
TAIL        = $06                      ; ring index of the oldest segment
HEADI       = $07                      ; ring index of the head
LEN         = $08                      ; segments currently alive
FOOD_X      = $09
FOOD_Y      = $0A
SCORE       = $0B
SEED        = $0C                      ; LFSR state, never zero
STATE       = $0D                      ; 0 playing, 1 game over
NX          = $0E                      ; candidate head cell
NY          = $0F
TMP         = $10
ATE         = $11
TMP_X       = $12                      ; cell handed to draw_cell
TMP_Y       = $13
COLOUR      = $14                      ; draw_cell parks the colour here

; --- snake body, two ring buffers of 256 cells each ---------------------
BODY_X      = $0200
BODY_Y      = $0300

; --- pacing -------------------------------------------------------------
DELAY_OUT   = $18                      ; 24 outer iterations
DELAY_IN    = $80                      ; 128 inner iterations, about 15K cycles


; =========================================================================
;  entry point -- execution starts at the first byte of the image ($1000)
; =========================================================================
start:
        JSR clear_screen
        JSR init_game
        JSR wait                       ; a beat of grace before the first move

main_loop:
        LDA STATE
        BEQ play_step
        ; --- dead: idle until the player asks for another run ------------
        JSR wait
        LDA PAD
        AND #BTN_RESTART
        BEQ main_loop
        JSR clear_screen
        JSR init_game
        JMP main_loop

play_step:
        JSR wait                       ; pace the game, then steer and step
        JSR read_input
        JSR move_snake
        JMP main_loop


; =========================================================================
;  input
; =========================================================================

; Steer with the D-pad. A reversal straight into the neck is ignored, and the
; first direction that matches wins.
read_input:
        LDA PAD
        STA TMP

        AND #BTN_LEFT
        BEQ input_right
        LDA DIR_X
        CMP #$01                       ; moving right? the neck is behind us
        BEQ input_right
        LDA #$FF
        STA DIR_X
        LDA #$00
        STA DIR_Y
        RTS

input_right:
        LDA TMP
        AND #BTN_RIGHT
        BEQ input_up
        LDA DIR_X
        CMP #$FF
        BEQ input_up
        LDA #$01
        STA DIR_X
        LDA #$00
        STA DIR_Y
        RTS

input_up:
        LDA TMP
        AND #BTN_UP
        BEQ input_down
        LDA DIR_Y
        CMP #$01
        BEQ input_down
        LDA #$00
        STA DIR_X
        LDA #$FF
        STA DIR_Y
        RTS

input_down:
        LDA TMP
        AND #BTN_DOWN
        BEQ input_done
        LDA DIR_Y
        CMP #$FF
        BEQ input_done
        LDA #$00
        STA DIR_X
        LDA #$01
        STA DIR_Y
input_done:
        RTS


; =========================================================================
;  one step of the game
; =========================================================================

; Move the head one cell, settle the tail, and repaint what changed.
move_snake:
        ; --- where the head wants to go --------------------------------
        LDA HEAD_X
        CLC
        ADC DIR_X                      ; $FF subtracts one, modulo 256
        STA NX
        LDA HEAD_Y
        CLC
        ADC DIR_Y
        STA NY

        ; --- walls ------------------------------------------------------
        LDA NX
        CMP #GRID                      ; unsigned: a step off the left edge
        BCS snake_die                  ; lands on $FF and fails here too
        LDA NY
        CMP #GRID
        BCS snake_die

        ; --- food -------------------------------------------------------
        LDA #$00
        STA ATE
        LDA NX
        CMP FOOD_X
        BNE snake_crawl
        LDA NY
        CMP FOOD_Y
        BNE snake_crawl

        LDA #$01
        STA ATE                        ; keep the tail: the snake grows
        INC SCORE
        INC LEN
        JMP snake_check

snake_crawl:
        JSR remove_tail                ; retire the tail before testing, so
                                       ; the cell it frees is not a collision
snake_check:
        JSR check_self
        BCS snake_die

        JSR push_head

        LDA ATE
        BEQ snake_done
        JSR place_food
snake_done:
        RTS

snake_die:
        JSR draw_game_over
        LDA #$01
        STA STATE
        RTS


; Erase the oldest segment and give its cell back to the board.
remove_tail:
        LDX TAIL
        LDA BODY_X,X
        STA TMP_X
        LDA BODY_Y,X
        STA TMP_Y
        LDA #BLACK
        JSR draw_cell
        INC TAIL                       ; wraps at $FF on its own
        RTS


; Commit the candidate head: store it, paint it bright and demote the segment
; behind it back to body colour.
push_head:
        INC HEADI
        LDX HEADI
        LDA NX
        STA BODY_X,X
        LDA NY
        STA BODY_Y,X

        LDA NX
        STA TMP_X
        LDA NY
        STA TMP_Y
        LDA #SNAKE_HEAD
        JSR draw_cell

        LDA HEAD_X                     ; still the previous head
        STA TMP_X
        LDA HEAD_Y
        STA TMP_Y
        LDA #SNAKE
        JSR draw_cell

        LDA NX
        STA HEAD_X
        LDA NY
        STA HEAD_Y
        RTS


; Carry set when the candidate head sits on any live segment. The scan runs
; from TAIL up to HEADI, so it also covers a ring buffer that has wrapped.
check_self:
        LDX TAIL
check_loop:
        LDA NX
        CMP BODY_X,X
        BNE check_next
        LDA NY
        CMP BODY_Y,X
        BEQ check_hit
check_next:
        CPX HEADI
        BEQ check_clear
        INX
        JMP check_loop
check_hit:
        SEC
        RTS
check_clear:
        CLC
        RTS


; =========================================================================
;  food
; =========================================================================

; Put the food on a free cell and paint it. A landing on the snake is re-rolled
; rather than accepted, so the fruit is always reachable.
place_food:
        JSR next_random
        LDA SEED
        AND #$1F
        STA FOOD_X
        JSR next_random                ; a second draw decorrelates y from x
        LDA SEED
        AND #$1F
        STA FOOD_Y

        JSR food_is_free
        BCC place_food

        LDA FOOD_X
        STA TMP_X
        LDA FOOD_Y
        STA TMP_Y
        LDA #FOOD
        JSR draw_cell
        RTS


; Carry set when FOOD_X/FOOD_Y is not occupied by the snake.
food_is_free:
        LDX TAIL
food_scan:
        LDA FOOD_X
        CMP BODY_X,X
        BNE food_next
        LDA FOOD_Y
        CMP BODY_Y,X
        BEQ food_taken
food_next:
        CPX HEADI
        BEQ food_free
        INX
        JMP food_scan
food_taken:
        CLC
        RTS
food_free:
        SEC
        RTS


; Eight bit maximal length LFSR (x^8 + x^6 + x^5 + x^4 + 1). Cheap, and good
; enough to scatter the food around the board.
next_random:
        LDA SEED
        LSR A
        BCC next_random_keep
        EOR #$B8
next_random_keep:
        STA SEED
        RTS


; =========================================================================
;  drawing
; =========================================================================

; A = colour, TMP_X/TMP_Y = cell. Paints one 4x4 cell.
;
; set_ptr computes its result in A, so the colour is parked in COLOUR first and
; reloaded for every scanline (the pointer arithmetic clobbers A as well).
draw_cell:
        STA COLOUR
        JSR set_ptr
        LDY #$00
        LDX #CELL
draw_row:
        LDA COLOUR
        STA (PTR_LO),Y
        INY
        STA (PTR_LO),Y
        INY
        STA (PTR_LO),Y
        INY
        STA (PTR_LO),Y
        CLC                            ; next scanline of the cell
        LDA PTR_LO
        ADC #$80
        STA PTR_LO
        LDA PTR_HI
        ADC #$00
        STA PTR_HI
        LDY #$00
        DEX
        BNE draw_row
        RTS


; TMP_X/TMP_Y = cell -> PTR points at its top left pixel.
;
;   address = $4000 + cell_y * 512 + cell_x * 4
;
; and because cell_x * 4 never spills past 255 the split is simply
;   low = cell_x * 4, high = $40 + cell_y * 2.
set_ptr:
        LDA TMP_X
        ASL A
        ASL A
        STA PTR_LO
        LDA TMP_Y
        ASL A
        CLC
        ADC #$40
        STA PTR_HI
        RTS


; Fill the framebuffer with the colour in A. Used for the opening clear and
; the game over screen, so it costs a quarter of a second and is only run
; when the picture genuinely changes.
fill_screen:
        STA TMP
        LDA #$00
        STA PTR_LO
        LDA #$40
        STA PTR_HI
        LDY #$00
        LDX #$40                       ; 64 pages of 256 pixels
fill_page:
        LDA TMP
        STA (PTR_LO),Y
        INY
        BNE fill_page
        INC PTR_HI
        DEX
        BNE fill_page
        RTS


clear_screen:
        LDA #BLACK
        JMP fill_screen


draw_game_over:
        LDA #DEAD
        JMP fill_screen


; =========================================================================
;  game set-up
; =========================================================================

; Three segments pointing right, head at (17,16), and the first piece of food.
init_game:
        LDA #$00
        STA STATE
        STA SCORE
        STA DIR_Y
        LDA #$01
        STA DIR_X

        LDA #$00
        STA TAIL
        LDA #$02
        STA HEADI
        LDA #$03
        STA LEN

        LDA #$0F                       ; segment i sits at x = 15 + i
        STA TMP_X
        LDA #$10
        STA TMP_Y
        LDX #$00
init_body:
        LDA TMP_X
        STA BODY_X,X
        LDA TMP_Y
        STA BODY_Y,X
        INC TMP_X
        INX
        CPX #$03
        BNE init_body

        LDA #$11
        STA HEAD_X
        LDA #$10
        STA HEAD_Y

        ; draw_cell uses X and Y for its own loops, so the counter lives in
        ; zero page instead of a register the call would trample.
        LDA #$00
        STA TMP
init_paint:
        LDX TMP
        LDA BODY_X,X
        STA TMP_X
        LDA BODY_Y,X
        STA TMP_Y
        LDA #SNAKE
        JSR draw_cell
        INC TMP
        LDA TMP
        CMP #$03
        BNE init_paint

        LDA HEAD_X
        STA TMP_X
        LDA HEAD_Y
        STA TMP_Y
        LDA #SNAKE_HEAD
        JSR draw_cell

        LDA #$A5                       ; a nonzero LFSR seed
        STA SEED
        JMP place_food


; =========================================================================
;  pacing
; =========================================================================

; Burn roughly DELAY_OUT * DELAY_IN * 5 cycles so the snake moves at a
; sensible rate against the host's cycles-per-frame budget.
wait:
        LDX #DELAY_OUT
wait_outer:
        LDY #DELAY_IN
wait_inner:
        DEY
        BNE wait_inner
        DEX
        BNE wait_outer
        RTS
