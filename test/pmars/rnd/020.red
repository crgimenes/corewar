;redcode-94
;name rnd020
;author generator
;assert CORESIZE==8000
    spl.b $3, $3
    spl.ba <-11, *0
    spl.f }20, {20
    seq.ba $-10, *-7
    jmz <20, @-19
    spl.x >12, @9
    add.a {14, <19
    sne.x #15, <20
    sub.ba <-7, #-16
    spl.ba *-4, }-8
    jmp.b @-3, >-11
    end 1
