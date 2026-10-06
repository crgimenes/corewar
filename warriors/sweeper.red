;redcode
;name Sweeper
;author crg.eti.br
;strategy Fills the core with DAT, one cell at a time, sweeping away
;strategy from itself. Whatever the other one is doing, it ends up
;strategy under a DAT — if the sweep gets there in time.
;assert CORESIZE > 20

sweep   mov     bomb, >ptr      ; drop a DAT and move the pointer on
        jmp     sweep
bomb    dat     #0, #0
ptr     dat     #0, 10          ; the sweep starts ten cells ahead
        end     sweep
