;redcode
;name Scanner
;author crg.eti.br
;strategy Walks the core looking for a cell that is not empty, drops a
;strategy DAT on it, and goes on looking. One bomb a find is not much
;strategy of an attack: making it dangerous is the exercise.
;assert CORESIZE > 20

step    equ 10

scan    seq     @ptr, #0        ; is the cell it points at still empty?
        jmp     hit             ; no: somebody lives there
        add     #step, ptr      ; yes: look a little further on
        jmp     scan
hit     mov     bomb, @ptr      ; drop a DAT on it
        add     #step, ptr
        jmp     scan
bomb    dat     #0, #0
ptr     dat     #0, 100
        end     scan
