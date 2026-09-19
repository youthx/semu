; smoke test: LDA #$05 then halt
        LDA #$05
        LDX #$07
        STA $0300
        HLT
