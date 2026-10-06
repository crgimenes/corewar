;redcode-94
;name rnd008
;author generator
;assert CORESIZE==8000
    spl.b $3, $2
    djn $-1, {20
    jmz.x }10, }-18
    sne }-17, >0
    jmn.ba $5, {-1
    slt.b #-15, @11
    jmp.i *10, $-6
    add #-4, *16
    djn >3, @-16
    slt #17, >-9
    slt }13, #17
    dat @6, #4
    dat.ab <6, {-7
    end 1
