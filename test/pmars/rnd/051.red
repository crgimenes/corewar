;redcode-94
;name rnd051
;author generator
;assert CORESIZE==8000
    dat #-4, {4
    div *8, #9
    nop.ab $-12, {12
    mov.f }12, $17
    sne.ba *-12, >-14
    mov.ba }-9, #-7
    end 0
