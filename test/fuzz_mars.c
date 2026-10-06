#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "mars.h"

/* Redcode comes from the person: any bytes must either assemble or be
   refused, never crash the program. What assembles is put to fight against
   itself for a short round, which walks the simulator on whatever the
   assembler let through. */

static mars_warrior W;
static mars M;

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size);

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
    mars_params p = mars_defaults();
    p.cycles = 200;
    char err[MARS_ERR_MAX];
    if (!mars_assemble(data, size, &p, &W, err, sizeof(err))) {
        if (err[0] == '\0') {
            __builtin_trap(); /* a refusal always says why */
        }
        return 0;
    }
    if (W.len == 0 || W.len > MARS_LEN_MAX || W.start >= W.len) {
        __builtin_trap();
    }
    const mars_warrior *ws[2] = {&W, &W};
    uint32_t pos[2] = {0, 4000};
    mars_load(&M, &p, ws, pos, 2);
    (void)mars_run(&M);
    return 0;
}
