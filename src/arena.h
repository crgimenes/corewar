#ifndef COREWAR_ARENA_H
#define COREWAR_ARENA_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "canvas.h"
#include "filo.h"
#include "mars.h"
#include "term.h"

/* Core War on a terminal: the fight shown as the core, the warriors'
   colours over it, the score beside. Two warriors or eight, all in the
   one core. The MARS is mars.c; this is the arena around it, and it knows
   no host: files come through arena_host.read, and the host says what
   leaving means. A shell and a desktop program both put it on screen. */

enum {
    CW_ROUNDS = 10, /* a fight's default */
    CW_ROUNDS_MAX = 100,
    CW_FIGHTERS_MAX = MARS_WARRIORS_MAX,
    CW_NOTE_MAX = 96,
    CW_PATH_MAX = 256,
    CW_REPORT_MAX = 1024,
};

typedef struct {
    uint32_t n;                              /* how many are fighting */
    bool seeded;                             /* the classics were offered once */
    char path[CW_FIGHTERS_MAX][CW_PATH_MAX]; /* their files: a new core reassembles them */
    uint32_t points[CW_FIGHTERS_MAX];        /* the score, as pMARS counts it */
    uint32_t held[CW_FIGHTERS_MAX];          /* cells each one held when the round ended */
    /* how the rounds went for each: [0] won it alone, [k] shared it with
       k others, [n] out. The same breakdown pMARS prints. */
    uint32_t res[CW_FIGHTERS_MAX][CW_FIGHTERS_MAX + 1];
    uint32_t speed; /* index into the steps-per-frame table */
    bool paused;
    bool over;                 /* the match: all rounds played */
    uint32_t rounds;           /* in this match */
    uint32_t round;            /* rounds finished */
    int8_t won[CW_ROUNDS_MAX]; /* each round's winner, -1 shared: the bar */
    bool fixed;                /* warrior 2 always at `at` (pMARS -F) */
    uint32_t at;
    bool report;    /* the score as text on leaving: a command wants it, a pick does not */
    uint32_t size;  /* index into the core presets: standard, tiny, nano */
    uint32_t limit; /* index into the cycle divisors: the round's length */
    uint32_t seed;
    uint32_t rest_ms; /* pause between rounds, counting down */
    uint32_t note_ms; /* how long the footer keeps the note */
    char note[CW_NOTE_MAX];
} corewar_state;

typedef struct {
    void *user;
    /* The warrior at path: bytes valid until the next call, or false with
       why (the system's words: "No such file or directory"). */
    bool (*read)(void *user, const char *path, const uint8_t **data, size_t *len, char *why,
                 size_t cap);
    /* The fight is over and the arena leaves the screen: note is the score
       as text when the fight was a command's (report), NULL otherwise. The
       host pops the arena off its terminal and repaints what is under it. */
    void (*leave)(void *user, const char *note);
    /* The two classics an empty list starts with, the first time. */
    const char *first[2];
} arena_host;

typedef struct {
    arena_host host;
    corewar_state s;
    term *t;
    canvas *target;
    canvas *shown;
} arena;

/* The arena as the term's full-screen app: its ctx is the arena. */
extern const term_app arena_app;

void arena_init(arena *a, const arena_host *host, term *t, canvas *target, canvas *shown);

/* The warriors the next fight uses: the pick reads and changes the list,
   so a trip through an editor does not lose it; an empty list answers
   with the host's two classics the first time it is asked. */
uint32_t arena_count(arena *a);
const char *arena_picked(arena *a, uint32_t slot);
int16_t arena_colour(uint32_t slot);
bool arena_add(arena *a, const char *path);
void arena_drop(arena *a);
void arena_clear(arena *a);

/* The list's warriors assembled for a fight of CW_ROUNDS in a standard
   core: false with why ("imp.red: ...") when one does not load or there
   are fewer than two. On true the host puts arena_app on its terminal
   and calls arena_show. */
bool arena_ready(arena *a, uint32_t seed, char *why, size_t cap);

/* As arena_ready, for a command's fight: rounds (1..CW_ROUNDS_MAX), and
   warrior 2 pinned at `at` when fixed (two warriors only). */
bool arena_ready_with(arena *a, uint32_t seed, uint32_t rounds, bool fixed, uint32_t at, char *why,
                      size_t cap);

/* The arena on the terminal, from scratch. */
void arena_show(arena *a);

/* A whole match with no screen, as pMARS -b plays it: the score as text
   in out. False with why, as arena_ready_with. */
bool arena_batch(arena *a, uint32_t seed, uint32_t rounds, bool fixed, uint32_t at, char *out,
                 size_t cap, char *why, size_t whycap);

/* The pick's builtins over the list, for any host: cw-count, cw-picked,
   cw-colour, cw-add, cw-drop, cw-clear. arena_fn gives the arena of the
   program running. False when one did not register. */
bool arena_register(filo_ctx *ctx, arena *(*arena_fn)(filo_ctx *ctx));

#endif
