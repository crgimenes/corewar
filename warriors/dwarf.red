;redcode
;name Dwarf
;author A. K. Dewdney
;strategy Bombs every fourth cell of the core with DAT, and never leaves home.
bomb    dat #0, #0
dwarf   add #4, bomb
        mov bomb, @bomb
        jmp dwarf
        end dwarf
