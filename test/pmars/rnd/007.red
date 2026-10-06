;redcode-94
;name rnd007
;author generator
;assert CORESIZE==8000
    slt.ba $20, {-1
    jmz.x {20, $-20
    jmp.a #13, #-4
    sub.f $13, <10
    nop.f @17, @-15
    end 1
