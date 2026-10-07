/* corewar on a POSIX terminal: the pick, the arena, and pMARS's -b for a
   match with no screen at all. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "desk.h"
#include "tty.h"

extern const uint8_t corewar_fbb[];
extern const size_t corewar_fbb_len;

static const char usage[] = "usage: corewar [-b] [-r ROUNDS] [-F POSITION] [WARRIOR.red...]\n"
                            "\n"
                            "Core War: warriors written in Redcode fight in one core. With no\n"
                            "warrior, the pick opens: the .red files here and the classics this\n"
                            "program carries (imp, dwarf, paper, scanner, sweeper). With two to\n"
                            "eight, they fight at once. -b plays the match with no screen and\n"
                            "prints the score as pMARS does; -r is the rounds (default 10), -F\n"
                            "puts the second of two warriors at POSITION.\n"
                            "\n"
                            "Keys in the arena: space pause  + - speed  n next round  r rematch\n"
                            "  m core size  c round length  Esc leaves\n"
                            "\n"
                            "Example: corewar -b -r 100 imp.red dwarf.red\n";

static bool digits(const char *s) {
    if (s[0] == '\0') {
        return false;
    }
    for (const char *p = s; *p != '\0'; p++) {
        if (*p < '0' || *p > '9') {
            return false;
        }
    }
    return true;
}

static int refuse(const char *why) {
    fputs("corewar: ", stderr);
    fputs(why, stderr);
    fputs("\n", stderr);
    return 1;
}

/* The score as pMARS prints it, its \r\n for a terminal made \n. */
static void print_report(const char *text) {
    for (const char *p = text; *p != '\0'; p++) {
        if (*p != '\r') {
            putchar(*p);
        }
    }
}

int main(int argc, char **argv) {
    bool batch = false;
    bool fixed = false;
    uint32_t rounds = CW_ROUNDS;
    uint32_t at = 0;
    bool rounds_given = false;
    desk_init();
    uint32_t nf = 0;
    for (int i = 1; i < argc; i++) {
        const char *w = argv[i];
        if (strcmp(w, "-h") == 0 || strcmp(w, "--help") == 0) {
            fputs(usage, stdout);
            return 0;
        }
        if (strcmp(w, "--version") == 0) {
            fputs("corewar ", stdout);
            fputs(app_program.version, stdout);
            fputs("\n", stdout);
            return 0;
        }
        if (strcmp(w, "-b") == 0) {
            batch = true;
        } else if ((strcmp(w, "-r") == 0 || strcmp(w, "-F") == 0) && i + 1 < argc &&
                   digits(argv[i + 1])) {
            uint32_t v = (uint32_t)strtoul(argv[i + 1], NULL, 10);
            if (w[1] == 'r') {
                rounds = v;
                rounds_given = true;
            } else {
                fixed = true;
                at = v;
            }
            i++;
        } else if (w[0] == '-') {
            fputs(usage, stderr);
            return 2;
        } else {
            if (nf == 0) {
                arena_clear(&D.ar);
            }
            if (!arena_add(&D.ar, w)) {
                return refuse("eight warriors at most");
            }
            nf++;
        }
    }
    if (nf == 1 || (batch && nf == 0)) {
        fputs(usage, stderr);
        return 2;
    }
    if (fixed && !rounds_given) {
        rounds = 1; /* one position, one round, unless asked for more */
    }
    char why[CW_REPORT_MAX];
    if (batch) {
        static char report[CW_REPORT_MAX];
        if (!arena_batch(&D.ar, (uint32_t)tty_seed(), rounds, fixed, at, report, sizeof(report),
                         why, sizeof(why))) {
            return refuse(why);
        }
        print_report(report);
        return 0;
    }
    if (nf == 0) {
        return tty_main(1, argv, &D.a, &app_program, corewar_fbb, corewar_fbb_len, usage);
    }
    if (!arena_ready_with(&D.ar, (uint32_t)tty_seed(), rounds, fixed, at, why, sizeof(why))) {
        return refuse(why);
    }
    uint16_t cols = 0;
    uint16_t rows = 0;
    if (!tty_open(&cols, &rows, why, sizeof(why))) {
        return refuse(why);
    }
    if (!app_start(&D.a, &app_program, corewar_fbb, corewar_fbb_len, cols, rows, why,
                   sizeof(why)) ||
        !term_app_enter(&D.a.t, &arena_app, &D.ar)) {
        tty_close();
        return refuse(why);
    }
    D.direct = true;
    arena_show(&D.ar);
    bool finished = tty_run(&D.a);
    tty_close();
    return finished ? 0 : 1;
}
