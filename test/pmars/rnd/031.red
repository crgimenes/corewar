;redcode-94
;name rnd031
;author generator
;assert CORESIZE==8000
    spl.b $1, $-5
    jmp.ba *6, <16
    mov.ba #-15, $-15
    mul.x @-16, *-4
    spl.ab @-4, *16
    mod.i $-1, *1
    spl $-4, #16
    spl >-4, #-16
    djn.ba <-14, {-5
    slt #2, {17
    end 2
