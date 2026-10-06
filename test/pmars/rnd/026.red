;redcode-94
;name rnd026
;author generator
;assert CORESIZE==8000
    spl.b $1, $3
    jmn.ba *6, @0
    mov.b >-3, $-8
    dat.ab {3, {-13
    seq.a @7, #11
    jmz {5, @-6
    jmp.i @3, #-4
    jmz #-19, #4
    seq.f {-17, {8
    end 3
