;redcode
;name Paper
;author crg.eti.br
;strategy Copies itself a fifth of the core away and starts the copy
;strategy running, then does it again, further off. A bomb kills one
;strategy copy; the others never notice.
;assert CORESIZE > 200

step    equ (CORESIZE / 5)
size    equ (cnt - first + 1)

first   mov     #cnt+1-src, src     ; src just past my last instruction
        mov     #size, cnt          ; how many instructions to copy
loop    mov.i   <src, <dst          ; backwards: dst ends on the copy's first
        djn.b   loop, cnt
        spl     @dst                ; the copy runs too
        sub     #step, dst          ; the next one goes further off
        jmp     first
src     dat     #0, #0
dst     dat     #0, cnt+1+step-dst
cnt     dat     #0, #0
        end     first
