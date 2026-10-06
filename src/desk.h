#ifndef COREWAR_DESK_H
#define COREWAR_DESK_H

#include <stdbool.h>

#include "app.h"
#include "arena.h"
#include "md.h"
#include "pager.h"

/* Core War on a desktop: the pick (corewar.fbb) as the app, the arena
   over it, the warriors from the current directory and the ones the
   binary carries, the manual in a pager, an editor of the person's own. */
typedef struct {
    app a;
    arena ar;
    pager pg;
    md render;
    bool direct; /* the fight was the command line's: leaving it ends the program */
} desk;

extern desk D;

/* The arena's host on a desktop, and the fight a command line names. */
void desk_init(void);

#endif
