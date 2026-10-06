;redcode-94
;name rnd058
;author generator
;assert CORESIZE==8000
    spl.b $1, $0
    slt $-14, >9
    jmz }-20, >17
    jmz.i #2, $-10
    div $8, {-8
    add <20, {0
    mul.a <3, >6
    jmz @18, #-16
    jmn.f #12, {-20
    slt.ab {11, {15
    slt.ba #9, >10
    sne @-3, *15
    end 1
