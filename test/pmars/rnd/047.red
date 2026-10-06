;redcode-94
;name rnd047
;author generator
;assert CORESIZE==8000
    jmp #-4, >16
    dat.b #9, {13
    div.ba >16, *-9
    add.x *-18, >-11
    mul.ba >-16, $-14
    add.x {3, {-17
    mov.b $11, <-10
    sub @-13, #-19
    mod.ab >9, *-11
    djn @10, $-12
    end 2
