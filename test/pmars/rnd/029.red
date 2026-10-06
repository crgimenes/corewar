;redcode-94
;name rnd029
;author generator
;assert CORESIZE==8000
    sne.a @15, >-2
    djn.ba #20, @16
    mod.f *4, #-12
    djn.i *-12, *15
    mul.ba *16, $6
    dat $-10, @-19
    jmn.x *-4, {2
    end 2
