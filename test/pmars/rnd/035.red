;redcode-94
;name rnd035
;author generator
;assert CORESIZE==8000
    spl.b $1, $-4
    spl.f $2, }-8
    seq.a @-12, <4
    seq >7, *19
    mul $17, {-12
    djn.b @19, {16
    seq.i >-5, $-7
    djn *-8, {-1
    spl @-6, <-5
    mov.f {1, {-15
    sne.ba }-5, *20
    sne.f $-20, {4
    end 2
