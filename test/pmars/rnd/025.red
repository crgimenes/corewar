;redcode-94
;name rnd025
;author generator
;assert CORESIZE==8000
    spl.b $1, $3
    mov $19, >1
    slt.x *5, $-8
    seq.ab $-15, #4
    dat.x }14, #-3
    mul.b }-15, $14
    end 0
