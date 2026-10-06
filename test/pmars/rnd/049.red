;redcode-94
;name rnd049
;author generator
;assert CORESIZE==8000
    dat.a {7, $-6
    spl.ba >17, <17
    seq.a }13, {-3
    spl.x <-15, @2
    sne.f {-3, #7
    sne.ba #9, }18
    djn #-19, @14
    end 1
