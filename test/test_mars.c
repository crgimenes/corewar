#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../src/mars.h"

/* The MARS against pMARS: every fixture assembles to the oracle's own load
   listing, validate.red passes its self-test, and each fixed-position
   battle in battles.txt ends the way the oracle says. */

static int failures = 0;

#define CHECK(cond)                                                                                \
    do {                                                                                           \
        if (!(cond)) {                                                                             \
            printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);                                 \
            failures++;                                                                            \
        }                                                                                          \
    } while (0)

static uint8_t file_buf[65536];

static size_t read_file(const char *path, uint8_t *dst, size_t cap) {
    FILE *f = fopen(path, "rb");
    if (f == NULL) {
        printf("cannot open %s\n", path);
        return 0;
    }
    size_t n = fread(dst, 1, cap - 1, f);
    fclose(f);
    dst[n] = '\0';
    return n;
}

static mars_warrior W[4];
static mars M;

static uint32_t assemble_rounds = 1;

static bool assemble(const char *name, mars_warrior *w) {
    char path[256];
    snprintf(path, sizeof(path), "test/pmars/%s.red", name);
    size_t n = read_file(path, file_buf, sizeof(file_buf));
    mars_params p = mars_defaults();
    p.rounds = assemble_rounds;
    char err[MARS_ERR_MAX];
    if (!mars_assemble(file_buf, n, &p, w, err, sizeof(err))) {
        printf("%s: %s\n", name, err);
        return false;
    }
    return true;
}

static void test_listing_as(const char *name, const char *listing) {
    mars_warrior *w = &W[0];
    CHECK(assemble(name, w));
    char path[256];
    snprintf(path, sizeof(path), "test/pmars/%s.lst", listing);
    static uint8_t lst[65536];
    size_t n = read_file(path, lst, sizeof(lst));
    CHECK(n > 0);
    uint32_t line = 0;
    char *p = (char *)lst;
    while (*p != '\0') {
        char *eol = strchr(p, '\n');
        if (eol != NULL) {
            *eol = '\0';
        }
        while (*p == ' ') {
            p++;
        }
        if (*p != '\0') {
            char ours[64] = "";
            if (line < w->len) {
                mars_format(&w->code[line], 8000, ours, sizeof(ours));
            }
            if (strcmp(ours, p) != 0) {
                printf("%s line %u: pmars [%s] ours [%s]\n", name, line, p, ours);
                failures++;
            }
            line++;
        }
        if (eol == NULL) {
            break;
        }
        p = eol + 1;
    }
    CHECK(line == w->len);
}

static void test_listing(const char *name) {
    test_listing_as(name, name);
}

/* Twelve rounds at one position: P-space cell 0 tells each warrior how
   the last round went, and pspace.red acts on it. */
static void test_matches(void) {
    static uint8_t table[4096];
    size_t n = read_file("test/pmars/matches.txt", table, sizeof(table));
    CHECK(n > 0);
    char *p = (char *)table;
    int count = 0;
    while (*p != '\0') {
        char a[32];
        char b[32];
        unsigned pos = 0;
        unsigned rounds = 0;
        unsigned w1 = 0;
        unsigned w2 = 0;
        unsigned ties = 0;
        if (sscanf(p, "%31s %31s %u %u %u %u %u", a, b, &pos, &rounds, &w1, &w2, &ties) == 7) {
            assemble_rounds = rounds;
            CHECK(assemble(a, &W[0]));
            CHECK(assemble(b, &W[1]));
            assemble_rounds = 1;
            mars_params prm = mars_defaults();
            prm.rounds = rounds;
            const mars_warrior *ws[2] = {&W[0], &W[1]};
            uint32_t at[2] = {0, pos};
            unsigned got[3] = {0, 0, 0};
            mars_match_begin(&M, &prm);
            unsigned r = 0;
            while (r < rounds) {
                mars_load(&M, &prm, ws, at, 2);
                int win = mars_run(&M);
                got[win < 0 ? 2 : win]++;
                r++;
            }
            if (got[0] != w1 || got[1] != w2 || got[2] != ties) {
                printf("%s vs %s at %u, %u rounds: pmars %u %u %u, ours %u %u %u\n", a, b, pos,
                       rounds, w1, w2, ties, got[0], got[1], got[2]);
                failures++;
            }
            count++;
        }
        char *eol = strchr(p, '\n');
        if (eol == NULL) {
            break;
        }
        p = eol + 1;
    }
    CHECK(count == 2);
}

static void test_validate_alone(void) {
    CHECK(assemble("validate", &W[0]));
    mars_params p = mars_defaults();
    const mars_warrior *ws[1] = {&W[0]};
    uint32_t pos[1] = {0};
    mars_load(&M, &p, ws, pos, 1);
    int r = mars_run(&M);
    CHECK(r == 0); /* still alive at 80000 cycles: it self-ties when the MARS is right */
    CHECK(M.cycle == 80000);
}

/* One battle list: "a b pos 1|2|tie", warriors by name under dir. */
static int run_list(const char *dir, const char *list) {
    static uint8_t table[8192];
    size_t n = read_file(list, table, sizeof(table));
    CHECK(n > 0);
    char *p = (char *)table;
    int count = 0;
    while (*p != '\0') {
        char a[32];
        char b[32];
        unsigned pos = 0;
        char want[8];
        if (sscanf(p, "%31s %31s %u %7s", a, b, &pos, want) == 4) {
            char fa[64];
            char fb[64];
            snprintf(fa, sizeof(fa), "%s%s", dir, a);
            snprintf(fb, sizeof(fb), "%s%s", dir, b);
            CHECK(assemble(fa, &W[0]));
            CHECK(assemble(fb, &W[1]));
            mars_params prm = mars_defaults();
            const mars_warrior *ws[2] = {&W[0], &W[1]};
            uint32_t at[2] = {0, pos};
            mars_load(&M, &prm, ws, at, 2);
            int r = mars_run(&M);
            int expect = strcmp(want, "tie") == 0 ? -1 : atoi(want) - 1;
            if (r != expect) {
                printf("%s vs %s at %u: pmars says %s, ours %d (cycle %u)\n", a, b, pos, want, r,
                       M.cycle);
                failures++;
            }
            count++;
        }
        char *eol = strchr(p, '\n');
        if (eol == NULL) {
            break;
        }
        p = eol + 1;
    }
    return count;
}

static void test_battles(void) {
    CHECK(run_list("", "test/pmars/battles.txt") == 48);
    /* sixty random '94 warriors (every opcode, modifier and mode), thirty
       fixed-position pairs: the oracle's verdict for each */
    CHECK(run_list("rnd/", "test/pmars/rnd/results.txt") == 30);
}

/* A few things the standard is explicit about and warriors lean on. */
static void test_semantics(void) {
    mars_params p = mars_defaults();
    char err[MARS_ERR_MAX];
    /* default modifiers, one-operand forms, and the value folding */
    static const char src[] = ";redcode\n;name t\nmov #1, 2\nmov 1, #2\nadd 1, 2\nslt #1, 2\n"
                              "dat 5\njmp 3\ncmp #1,#1\nmov.x 1,2\n dat #-1, #8001\nend\n";
    CHECK(mars_assemble((const uint8_t *)src, sizeof(src) - 1, &p, &W[0], err, sizeof(err)));
    CHECK(W[0].len == 9);
    CHECK(W[0].code[0].mod == MOD_AB && W[0].code[1].mod == MOD_B && W[0].code[2].mod == MOD_F);
    CHECK(W[0].code[3].mod == MOD_AB && W[0].code[4].mod == MOD_F);
    CHECK(W[0].code[4].am == AM_IMM && W[0].code[4].a == 0 && W[0].code[4].b == 5);
    CHECK(W[0].code[5].a == 3 && W[0].code[5].b == 0 && W[0].code[5].mod == MOD_B);
    CHECK(W[0].code[6].op == OP_CMP && W[0].code[6].mod == MOD_AB);
    CHECK(W[0].code[7].mod == MOD_X);
    CHECK(W[0].code[8].a == 7999 && W[0].code[8].b == 1);
    /* errors say what and where */
    static const char bad[] = "mov x, 1\n";
    CHECK(!mars_assemble((const uint8_t *)bad, sizeof(bad) - 1, &p, &W[0], err, sizeof(err)));
    CHECK(strstr(err, "unknown label") != NULL);
    static const char twice[] = "a mov 0, 1\na jmp 0\n";
    CHECK(!mars_assemble((const uint8_t *)twice, sizeof(twice) - 1, &p, &W[0], err, sizeof(err)));
    CHECK(strstr(err, "declared twice") != NULL);
    static const char assert_no[] = ";assert CORESIZE == 8\nmov 0, 1\n";
    CHECK(!mars_assemble((const uint8_t *)assert_no, sizeof(assert_no) - 1, &p, &W[0], err,
                         sizeof(err)));
    CHECK(strstr(err, "assertion failed") != NULL);
    /* DIV by zero ends the process, but the other half of a .F still lands */
    static const char divz[] = "div.f #0, 1\ndat #6, #9\n";
    CHECK(mars_assemble((const uint8_t *)divz, sizeof(divz) - 1, &p, &W[0], err, sizeof(err)));
    /* A #0 means the instruction itself: a=0, b=1 (the operand) -> 6/0 dies, 9/1 = 9 */
    const mars_warrior *ws[1] = {&W[0]};
    uint32_t pos[1] = {10};
    mars_load(&M, &p, ws, pos, 1);
    CHECK(mars_step(&M) == false && M.nalive == 0);
    CHECK(M.core[11].a == 6 && M.core[11].b == 9);
    /* post-increment lands after the A operand is read, before B is: the
       classic "mov.i >x, x" reads the old target, bumps, then B sees it */
    static const char post[] = "mov.i >2, 2\ndat 0, 0\ndat #0, #1\ndat #7, #7\n";
    CHECK(mars_assemble((const uint8_t *)post, sizeof(post) - 1, &p, &W[0], err, sizeof(err)));
    mars_load(&M, &p, ws, pos, 1);
    (void)mars_step(&M);
    /* >2 points at cell 12 (dat #0, #1): reads via b=1 -> cell 13 (dat #7, #7), then 12.b = 2;
       B operand $2 is cell 12 itself: so cell 12 becomes a copy of cell 13 */
    CHECK(M.core[12].a == 7 && M.core[12].b == 7);
    /* SPL queues the next instruction first, then the split */
    static const char spl[] = "spl 2\njmp 0\njmp 0\n";
    CHECK(mars_assemble((const uint8_t *)spl, sizeof(spl) - 1, &p, &W[0], err, sizeof(err)));
    mars_load(&M, &p, ws, pos, 1);
    (void)mars_step(&M);
    CHECK(M.qlen[0] == 2 && M.q[0][M.qhead[0]] == 11 &&
          M.q[0][(M.qhead[0] + 1) % MARS_PROCS_MAX] == 12);
}

int main(void) {
    test_listing("imp");
    test_listing("dwarf");
    test_listing("validate");
    test_listing("rave");
    test_listing("aeka");
    test_listing("flashpaper");
    assemble_rounds = 2;
    test_listing("pspace"); /* two rounds: the strategy block is FOR'd out */
    assemble_rounds = 12;
    test_listing_as("pspace", "pspace12"); /* past ten it is in, LDP and STP with it */
    assemble_rounds = 1;
    test_listing("pstest1");
    test_listing("pstest2");
    test_semantics();
    test_matches();
    test_validate_alone();
    test_battles();
    if (failures > 0) {
        printf("%d failure(s)\n", failures);
        return 1;
    }
    printf("all mars tests passed\n");
    return 0;
}
