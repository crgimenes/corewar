;redcode-94
;name rnd032
;author generator
;assert CORESIZE==8000
    sub.b #1, #-15
    djn.a }10, >-16
    jmp.a <13, {10
    div.b #-8, }17
    sne.i {17, @9
    jmp.i #2, @-13
    djn.i }20, *-10
    djn <-17, {-3
    end 0
