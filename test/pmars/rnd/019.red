;redcode-94
;name rnd019
;author generator
;assert CORESIZE==8000
    div.b #-18, {14
    seq.x #10, {-8
    jmp.f @-15, >-7
    sub.a @19, *-13
    slt.x }-7, $2
    mod.b >18, {10
    slt <4, <-6
    slt.a >16, $-15
    djn *-20, @-2
    sub.f #1, $18
    nop #-2, <-17
    end 3
