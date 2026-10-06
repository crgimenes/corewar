;redcode-94
;name P-space test 1
;author test
;strategy Counts rounds in P-space cell 1 and dies on the third; after a
;strategy loss, cell 0 is zero and it dies again. Six rounds against a
;strategy sitter: tie, tie, loss, loss, loss, loss.
        ldp.ab #1, count
        add.ab #1, count
        stp.b  count, #1
        ldp.ab #0, res
        seq.ab #3, count
        jmp    look
        dat    #0, #0
look    jmz.b  die, res
loop    jmp    0
die     dat    #0, #0
count   dat    #0, #0
res     dat    #0, #0
        end
