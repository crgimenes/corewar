#ifndef MSH_MARS_H
#define MSH_MARS_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* Core War: a Redcode assembler and a MARS (Memory Array Redcode
   Simulator) after the ICWS'94 draft, the way pMARS reads it — the
   simulator every warrior on the net was written against. A leaf module:
   no terminal, no host, so the tests drive it directly and a native port
   takes it whole. Everything is static and bounded. */
enum {
    MARS_CORE_MAX = 8000,
    MARS_PROCS_MAX = 8000,
    MARS_LEN_MAX = 100,
    MARS_WARRIORS_MAX = 8, /* the queues are 16 KB each: a match, not a tournament */
    MARS_NAME_MAX = 64,
    MARS_ERR_MAX = 160,
    MARS_LABELS_MAX = 256,
    MARS_LABEL_LEN = 32,
    MARS_EQU_TEXT = 128,
    MARS_LINE_MAX = 256,
    MARS_PSPACE_MAX = 500, /* CORESIZE / 16 */
    MARS_LOOPS_MAX = 8,
};

typedef enum {
    OP_DAT,
    OP_MOV,
    OP_ADD,
    OP_SUB,
    OP_MUL,
    OP_DIV,
    OP_MOD,
    OP_JMP,
    OP_JMZ,
    OP_JMN,
    OP_DJN,
    OP_SPL,
    OP_SLT,
    OP_SEQ,
    OP_SNE,
    OP_NOP,
    OP_LDP, /* P-space: a warrior's private memory that outlives the round */
    OP_STP,
    OP_CMP, /* SEQ under its '88 name: the same instruction, listed as written */
    OP_COUNT,
} mars_op;

typedef enum { MOD_A, MOD_B, MOD_AB, MOD_BA, MOD_F, MOD_X, MOD_I } mars_mod;

/* # $ @ < > * { } in the order the standard lists them */
typedef enum { AM_IMM, AM_DIR, AM_BIND, AM_BPRE, AM_BPOST, AM_AIND, AM_APRE, AM_APOST } mars_mode;

typedef struct {
    uint8_t op;
    uint8_t mod;
    uint8_t am;
    uint8_t bm;
    uint16_t a;
    uint16_t b;
} mars_cell;

typedef struct {
    uint32_t core;     /* cells */
    uint32_t procs;    /* processes a warrior may have */
    uint32_t cycles;   /* until a tie */
    uint32_t len;      /* instructions a warrior may have */
    uint32_t dist;     /* least distance between warriors */
    uint32_t rounds;   /* ROUNDS, for the assembler's expressions */
    uint32_t warriors; /* WARRIORS, the same */
} mars_params;

/* The defaults every tournament assumes: 8000/8000/80000/100/100, one
   round, two warriors. */
mars_params mars_defaults(void);

typedef struct {
    char name[MARS_NAME_MAX];
    char author[MARS_NAME_MAX];
    mars_cell code[MARS_LEN_MAX];
    uint32_t len;
    uint32_t start;
} mars_warrior;

/* Assembles src into w. False on error, with err saying where and why.
   Labels, EQU, ORG, END, expressions, ;assert, ;name and ;author; the
   '88 forms (cmp, one operand) read as the '94 standard says they do. */
bool mars_assemble(const uint8_t *src, size_t n, const mars_params *p, mars_warrior *w, char *err,
                   size_t err_cap);

/* One cell as pMARS lists it: "MOV.I  $     0, $     1". */
size_t mars_format(const mars_cell *c, uint32_t core, char *buf, size_t cap);

typedef struct {
    mars_params p;
    mars_cell core[MARS_CORE_MAX];
    uint8_t owner[MARS_CORE_MAX];      /* who wrote the cell last, 0xFF nobody: for the screen */
    uint32_t stamp[MARS_CORE_MAX];     /* when (in steps), so the screen knows the freshest */
    uint32_t steps;                    /* instructions executed this round */
    uint16_t pc_of[MARS_WARRIORS_MAX]; /* where each warrior last executed */
    uint32_t nwarriors;
    const mars_warrior *w[MARS_WARRIORS_MAX];
    uint32_t pos[MARS_WARRIORS_MAX];
    /* one queue per warrior, a ring of addresses */
    uint16_t q[MARS_WARRIORS_MAX][MARS_PROCS_MAX];
    uint32_t qhead[MARS_WARRIORS_MAX];
    uint32_t qlen[MARS_WARRIORS_MAX];
    bool alive[MARS_WARRIORS_MAX];
    uint32_t nalive;
    uint32_t cycle;
    /* P-space: PSPACESIZE cells a warrior keeps from round to round; cell
       0 holds how the last round went (0 lost, else how many survived) */
    uint16_t ps[MARS_WARRIORS_MAX][MARS_PSPACE_MAX];
    uint32_t turn;    /* which warrior executes next */
    uint32_t last_pc; /* the address just executed */
    bool over;
    int winner; /* -1 while running or on a tie */
} mars;

/* Before the first round of a match: clears the P-spaces (cell 0 to -1,
   "no result yet"). mars_load keeps them, so rounds see each other. */
void mars_match_begin(mars *m, const mars_params *p);

/* Puts the warriors in an empty core (DAT 0, 0) at pos[i] and starts them. */
void mars_load(mars *m, const mars_params *p, const mars_warrior *const *w, const uint32_t *pos,
               uint32_t n);

/* One instruction of the next warrior; false once the round is over. */
bool mars_step(mars *m);

/* The whole round: winner index, or -1 for a tie. */
int mars_run(mars *m);

#endif
