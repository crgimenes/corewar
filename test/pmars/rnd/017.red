;redcode-94
;name rnd017
;author generator
;assert CORESIZE==8000
    jmp }0, $16
    jmn.ba <3, >5
    mul.x {-8, }-7
    jmz.ba *6, >10
    jmn.ba >5, >17
    jmz.f $-9, <-5
    djn *0, <-12
    sub.ba $11, @2
    end 3
