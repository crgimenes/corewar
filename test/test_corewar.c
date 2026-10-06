/* The pick and the arena driven as a terminal drives them: a fight from
   the pick and back, the library, and a match with no screen. */
#include <stdio.h>
#include <string.h>

#include "desk.h"

extern const uint8_t corewar_fbb[];
extern const size_t corewar_fbb_len;

static int failures = 0;

#define CHECK(cond)                                                                                \
    do {                                                                                           \
        if (!(cond)) {                                                                             \
            printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);                                 \
            failures++;                                                                            \
        }                                                                                          \
    } while (0)

static void drain(void) {
    uint8_t out[TERM_OUT_CAP];
    while (term_out_read(&D.a.t, out, sizeof(out)) > 0) {
    }
}

static void type(const char *s) {
    app_input(&D.a, (const uint8_t *)s, strlen(s));
    app_tick(&D.a, 200); /* a lone Esc is told apart by time */
    drain();
}

static bool shows(const char *text) {
    size_t n = strlen(text);
    for (uint16_t r = 0; r < D.a.target.rows; r++) {
        char line[TERM_COLS_MAX + 1];
        uint16_t k = 0;
        for (uint16_t c = 0; c < D.a.target.cols; c++) {
            uint32_t cp = D.a.target.cells[r][c].cp;
            line[k++] = cp > 0 && cp < 0x80 ? (char)cp : ' ';
        }
        line[k] = '\0';
        if (n <= k && strstr(line, text) != NULL) {
            return true;
        }
    }
    return false;
}

static void test_pick_and_fight(void) {
    desk_init();
    char why[256];
    CHECK(app_start(&D.a, &app_program, corewar_fbb, corewar_fbb_len, 100, 30, why, sizeof(why)));
    drain();
    CHECK(shows("Core War"));
    CHECK(shows("imp.red")); /* the classics, first pick and library */
    CHECK(shows("sweeper.red"));
    type("f\r");
    CHECK(term_app_top(&D.a.t) == &arena_app);
    for (int i = 0; i < 50; i++) {
        app_tick(&D.a, 50);
        drain();
    }
    CHECK(D.ar.s.n == 2);
    type("\x1b");
    CHECK(term_app_top(&D.a.t) != &arena_app); /* back on the pick */
    CHECK(shows("Pick:"));
    type("c\r");
    type("f\r");
    CHECK(shows("two warriors at least"));
    type("\x1b");
    CHECK(app_done(&D.a));
}

static void test_batch(void) {
    desk_init();
    arena_clear(&D.ar);
    CHECK(arena_add(&D.ar, "imp.red"));
    CHECK(arena_add(&D.ar, "dwarf.red"));
    static char report[CW_REPORT_MAX];
    char why[256];
    CHECK(arena_batch(&D.ar, 1, 10, false, 0, report, sizeof(report), why, sizeof(why)));
    CHECK(strstr(report, "Results:") != NULL);
    arena_clear(&D.ar);
    CHECK(arena_add(&D.ar, "imp.red"));
    CHECK(arena_add(&D.ar, "nowhere.red"));
    CHECK(!arena_batch(&D.ar, 1, 10, false, 0, report, sizeof(report), why, sizeof(why)));
    CHECK(strstr(why, "nowhere.red: ") != NULL);
}

int main(void) {
    test_pick_and_fight();
    test_batch();
    if (failures > 0) {
        printf("%d corewar tests failed\n", failures);
        return 1;
    }
    puts("all corewar tests passed");
    return 0;
}
