;redcode-94
;name P-space test 2
;author test
;strategy Dies whenever the last round was a tie (cell 0 is 2), lives
;strategy otherwise. Six rounds against a sitter: tie, loss, tie, loss,
;strategy tie, loss.
        ldp.ab #0, res
        sne.ab #2, res
        dat    #0, #0
        jmp    0
res     dat    #0, #0
        end
