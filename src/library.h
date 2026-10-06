#ifndef COREWAR_LIBRARY_H
#define COREWAR_LIBRARY_H

#include <stddef.h>
#include <stdint.h>

/* What the binary carries: the classic warriors and the Redcode manual
   (tools/library.sh writes them from warriors/ and redcode.md). */
typedef struct {
    const char *name; /* "imp.red" */
    const uint8_t *data;
    size_t len;
} cw_file;

extern const cw_file cw_library[];
extern const size_t cw_library_n;
extern const uint8_t cw_manual[];
extern const size_t cw_manual_len;

#endif
