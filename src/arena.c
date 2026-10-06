#include "arena.h"

#include <string.h>

#include "keys.h"
#include "paint.h"

static mars_warrior W[CW_FIGHTERS_MAX];
static mars_warrior TMP[CW_FIGHTERS_MAX]; /* a new core reassembles here first */
static mars M;                            /* the core: static, one fight at a time */
static char report[CW_REPORT_MAX];        /* the score as text, for a command */

/* Decimal digits of v into buf (at least 12 bytes); returns where they start. */
static const char *u32_str(char *buf, uint32_t v) {
    size_t d = 11;
    buf[d] = '\0';
    do {
        d--;
        buf[d] = (char)('0' + (v % 10));
        v /= 10;
    } while (v > 0);
    return buf + d;
}

static size_t cat(char *dst, size_t cap, size_t at, const char *s) {
    if (at >= cap) {
        return at;
    }
    size_t i = 0;
    while (s[i] != '\0' && at + 1 < cap) {
        dst[at] = s[i];
        at++;
        i++;
    }
    dst[at] = '\0';
    return at;
}

static size_t cat_u32(char *dst, size_t cap, size_t at, uint32_t v) {
    char buf[12];
    return cat(dst, cap, at, u32_str(buf, v));
}

/* A share of the core as tenths of a percent: "41.2%". */
static size_t cat_pct(char *dst, size_t cap, size_t at, uint32_t part, uint32_t whole) {
    uint32_t tenths = 0;
    if (whole > 0) {
        tenths = (part * 1000U) / whole;
    }
    at = cat_u32(dst, cap, at, tenths / 10);
    at = cat(dst, cap, at, ".");
    at = cat_u32(dst, cap, at, tenths % 10);
    return cat(dst, cap, at, "%");
}

static const char *base_name(const char *path) {
    const char *b = path;
    size_t i = 0;
    while (path[i] != '\0') {
        if (path[i] == '/') {
            b = path + i + 1;
        }
        i++;
    }
    return b;
}

/* Reads the file and assembles it. False with err saying why, in the
   system's words, so a command and the arena's footer can both say it. */
static bool assemble_path(arena *a, const char *path, mars_warrior *w, const mars_params *p,
                          char *err, size_t cap) {
    const uint8_t *data = NULL;
    size_t len = 0;
    err[0] = '\0';
    if (!a->host.read(a->host.user, path, &data, &len, err, cap)) {
        return false;
    }
    return mars_assemble(data, len, p, w, err, cap);
}

/* As assemble_path, with the path as it was given in front of why. */
static bool load_warrior(arena *a, const char *path, mars_warrior *w, const mars_params *p,
                         char *why, size_t cap) {
    char err[MARS_ERR_MAX];
    if (assemble_path(a, path, w, p, err, sizeof(err))) {
        return true;
    }
    size_t at = cat(why, cap, 0, path);
    at = cat(why, cap, at, ": ");
    (void)cat(why, cap, at, err);
    return false;
}

/* pMARS's arithmetic: the survivors of a round share (W*W - 1) points,
   so the fewer of them there are the more each one takes, and the
   division is the integer one — the two survivors of a two-warrior
   round get 1 each, not one and a half. */
static uint32_t round_points(uint32_t warriors, uint32_t survivors) {
    if (survivors == 0) {
        return 0;
    }
    return ((warriors * warriors) - 1) / survivors;
}

/* One warrior's share of the round that just ended: the points, and a
   mark in its row of how the rounds have gone — [0] took it alone, [k]
   shared it with k others, [n] out. */
static void record(corewar_state *s, uint32_t who, uint32_t alive) {
    if (who >= CW_FIGHTERS_MAX || s->n > CW_FIGHTERS_MAX || alive > CW_FIGHTERS_MAX) {
        return;
    }
    if (alive == 0 || !M.alive[who]) {
        s->res[who][s->n]++;
        return;
    }
    s->points[who] += round_points(s->n, alive);
    s->res[who][alive - 1]++;
}

/* The score the way pMARS prints it: two warriors get a line each and
   one tally at the end, more than two get the breakdown of their rounds
   — won alone, shared with one, with two, and so on, then lost. */
static void format_results(const corewar_state *s) {
    size_t at = 0;
    report[0] = '\0';
    uint32_t i = 0;
    while (i < s->n) {
        at = cat(report, sizeof(report), at, W[i].name);
        at = cat(report, sizeof(report), at, " by ");
        at = cat(report, sizeof(report), at, W[i].author);
        at = cat(report, sizeof(report), at, " scores ");
        at = cat_u32(report, sizeof(report), at, s->points[i]);
        at = cat(report, sizeof(report), at, "\r\n");
        if (s->n > 2) {
            at = cat(report, sizeof(report), at, "  Results:");
            uint32_t k = 0;
            while (k <= s->n) {
                at = cat(report, sizeof(report), at, " ");
                at = cat_u32(report, sizeof(report), at, s->res[i][k]);
                k++;
            }
            at = cat(report, sizeof(report), at, "\r\n");
        }
        i++;
    }
    if (s->n == 2) {
        at = cat(report, sizeof(report), at, "Results: ");
        at = cat_u32(report, sizeof(report), at, s->res[0][0]);
        at = cat(report, sizeof(report), at, " ");
        at = cat_u32(report, sizeof(report), at, s->res[1][0]);
        at = cat(report, sizeof(report), at, " ");
        at = cat_u32(report, sizeof(report), at, s->res[0][1]);
        (void)cat(report, sizeof(report), at, "\r\n");
    }
}

/* ---- where they start ---- */

static uint32_t roll(uint32_t *seed) {
    *seed = (*seed * 1103515245U) + 12345U;
    return *seed >> 8U;
}

/* The way round the core, whichever way is shorter. */
static uint32_t apart(uint32_t a, uint32_t b, uint32_t core) {
    uint32_t d = a > b ? a - b : b - a;
    return d < core - d ? d : core - d;
}

/* Somewhere for each warrior, none closer to another than the core's
   minimum distance. Drawn at random, as a tournament does it; if the
   core is too crowded for the draw to land, they are spread evenly. */
static void place(uint32_t *seed, const mars_params *p, uint32_t n, uint32_t *pos) {
    uint32_t tries = 0;
    while (tries < 200) {
        uint32_t i = 0;
        while (i < n) {
            pos[i] = roll(seed) % p->core;
            i++;
        }
        bool ok = true;
        i = 0;
        while (i < n) {
            uint32_t k = i + 1;
            while (k < n) {
                if (apart(pos[i], pos[k], p->core) < p->dist) {
                    ok = false;
                }
                k++;
            }
            i++;
        }
        if (ok) {
            return;
        }
        tries++;
    }
    uint32_t i = 0;
    while (i < n) {
        pos[i] = (i * p->core) / n;
        i++;
    }
}

/* ---- the fight's size ---- */

typedef struct {
    const char *name;
    uint32_t core;
    uint32_t procs;
    uint32_t cycles;
    uint32_t len;
    uint32_t dist;
} cw_preset;

/* The three cores the game is actually played on, biggest first: M walks
   down them, and a smaller core is a shorter fight — less to bomb, fewer
   cycles before the tie, and a warrior that no longer fits is refused. */
static const cw_preset presets[] = {
    {"standard", 8000, 8000, 80000, 100, 100},
    {"tiny", 800, 800, 8000, 20, 20},
    {"nano", 80, 80, 800, 5, 5},
};
enum { CW_PRESETS = 3, CW_PRESET_STD = 0, CW_LIMITS = 4 };

static mars_params preset_params(const corewar_state *s) {
    const cw_preset *p = &presets[s->size];
    mars_params r = mars_defaults();
    r.core = p->core;
    r.procs = p->procs;
    r.cycles = p->cycles >> s->limit;
    r.len = p->len;
    r.dist = p->dist;
    r.rounds = s->rounds;
    r.warriors = s->n > 0 ? s->n : 2;
    return r;
}

/* ---- who is fighting ---- */

/* green, red, blue, yellow, magenta, cyan, orange, violet — and a
   darker one each, for a cell the warrior only partly holds */
static const int16_t colour_lit[CW_FIGHTERS_MAX] = {10, 9, 12, 11, 13, 14, 208, 105};
static const int16_t colour_dim[CW_FIGHTERS_MAX] = {22, 52, 18, 58, 53, 23, 94, 60};

int16_t arena_colour(uint32_t slot) {
    return colour_lit[slot % CW_FIGHTERS_MAX];
}

uint32_t arena_count(arena *a) {
    corewar_state *s = &a->s;
    if (!s->seeded) {
        /* the host's two classics, ready to go on the first visit; after
           that an empty list is a choice and stays empty */
        s->seeded = true;
        s->path[0][0] = '\0';
        s->path[1][0] = '\0';
        s->n = 0;
        for (uint32_t i = 0; i < 2; i++) {
            if (a->host.first[i] != NULL) {
                (void)cat(s->path[s->n], CW_PATH_MAX, 0, a->host.first[i]);
                s->n++;
            }
        }
    }
    return s->n;
}

const char *arena_picked(arena *a, uint32_t slot) {
    (void)arena_count(a);
    if (slot >= a->s.n) {
        return "";
    }
    return a->s.path[slot];
}

bool arena_add(arena *a, const char *path) {
    corewar_state *s = &a->s;
    (void)arena_count(a);
    if (s->n >= CW_FIGHTERS_MAX) {
        return false;
    }
    s->path[s->n][0] = '\0';
    (void)cat(s->path[s->n], CW_PATH_MAX, 0, path);
    s->n++;
    return true;
}

void arena_drop(arena *a) {
    if (a->s.n > 0) {
        a->s.n--;
    }
}

void arena_clear(arena *a) {
    a->s.seeded = true; /* an empty list from here on is what was asked for */
    a->s.n = 0;
}

void arena_init(arena *a, const arena_host *host, term *t, canvas *target, canvas *shown) {
    memset(&a->s, 0, sizeof(a->s));
    a->host = *host;
    a->t = t;
    a->target = target;
    a->shown = shown;
}

/* ---- the door ---- */

static const uint32_t speeds[] = {1, 8, 64, 512, 4096};
enum {
    CW_SPEEDS = 5,
    CW_REST_MS = 1500,
    CW_NOTE_MS = 4000,
    CW_PANEL_W = 36, /* the right-hand panel: metrics and the listings */
    CW_PANEL_MIN = 76,
    CW_MIN_ROWS = 6,
    CW_MIN_COLS = 30,
    CW_LIST_MAX = 5,
    CW_LEAD_PCT = 90, /* holding this much of the core is worth saying */
};
enum {
    CW_PC = 15,    /* the cell about to be executed, whoever owns it */
    CW_DARK = 236, /* memory nobody has touched, and the footer's ground */
    CW_RULE = 238,
    CW_FAINT = 245,
    CW_TEXT = 252,
    CW_KEY = 214,
};

static void corewar_on_key(void *ctx, uint32_t cp);
static void corewar_on_resize(void *ctx);
static void corewar_on_tick(void *ctx, uint32_t ms);

const term_app arena_app = {
    .on_key = corewar_on_key,
    .on_resize = corewar_on_resize,
    .on_tick = corewar_on_tick,
};

/* Five digits, the way pMARS lists an address. */
static int32_t put_addr(canvas *c, int32_t row, int32_t col, uint32_t v) {
    char buf[6];
    size_t i = 5;
    buf[i] = '\0';
    while (i > 0) {
        i--;
        buf[i] = (char)('0' + (v % 10));
        v /= 10;
    }
    return cv_put(c, row, col, cv_cstr(buf));
}

/* A footer hint in the editor's colours: the key orange and bold, the
   word plain. Returns the column after it. */
static int32_t hint(canvas *c, int32_t row, int32_t col, const char *key, const char *word) {
    cv_pen(c, CW_KEY, CW_DARK, CV_A_BOLD);
    col += cv_put(c, row, col, cv_cstr(key));
    cv_pen(c, CW_TEXT, CW_DARK, 0);
    col += cv_put(c, row, col, cv_cstr(" "));
    col += cv_put(c, row, col, cv_cstr(word));
    return col + 2;
}

static void say(corewar_state *s, const char *a, const char *b) {
    size_t at = cat(s->note, sizeof(s->note), 0, a);
    if (b != NULL) {
        (void)cat(s->note, sizeof(s->note), at, b);
    }
    s->note_ms = CW_NOTE_MS;
}

/* How much of the core each warrior holds right now. */
static void count_cells(uint32_t *held) {
    uint32_t i = 0;
    while (i < CW_FIGHTERS_MAX) {
        held[i] = 0;
        i++;
    }
    i = 0;
    while (i < M.p.core) {
        if (M.owner[i] < M.nwarriors) {
            held[M.owner[i]]++;
        }
        i++;
    }
}

/* The colour of the core cells [from, to): whoever wrote there last,
   bright when all of them are his, white where a process is about to
   execute. */
static int16_t cell_colour(uint32_t from, uint32_t to) {
    uint32_t w = 0;
    while (w < M.nwarriors) {
        if (M.alive[w] && M.pc_of[w] >= from && M.pc_of[w] < to) {
            return CW_PC;
        }
        w++;
    }
    uint8_t owner = 0xFF;
    uint32_t best = 0;
    uint32_t i = from;
    while (i < to) {
        if (M.owner[i] != 0xFF && (owner == 0xFF || M.stamp[i] >= best)) {
            owner = M.owner[i];
            best = M.stamp[i];
        }
        i++;
    }
    if (owner == 0xFF) {
        return CW_DARK;
    }
    uint32_t held = 0;
    i = from;
    while (i < to) {
        if (M.owner[i] == owner) {
            held++;
        }
        i++;
    }
    if (held == to - from) {
        return colour_lit[owner % CW_FIGHTERS_MAX];
    }
    return colour_dim[owner % CW_FIGHTERS_MAX];
}

/* Laying the core out as cells wide by cells tall, as square as the
   screen allows: a sub-cell is about as wide as it is tall, so the two
   sides want the same ratio the map area has. */
static void grid_shape(uint32_t core, uint32_t w, uint32_t h, uint32_t *gw, uint32_t *gh) {
    uint32_t want = (core * w) / h;
    uint32_t c = 1;
    while ((c + 1) * (c + 1) <= want) {
        c++;
    }
    if (c > w) {
        c = w;
    }
    uint32_t r = (core + c - 1) / c;
    if (r > h) {
        c = (core + h - 1) / h;
        r = (core + c - 1) / c;
    }
    *gw = c;
    *gh = r;
}

/* The core as a map, scaled to whatever room there is. A terminal cell
   is two cells tall — "▀" painted in the upper half's colour over the
   lower half's — so a line of the map holds twice the memory, the trick
   pngtoansi uses to draw pictures. Where both halves come out the same
   colour a plain space carries it, and where only the lower half is
   memory the glyph is "▄" — the same four-glyph vocabulary. Bigger than
   the screen, the core folds two or three cells into each half;
   smaller, it is laid out as a block per cell. The arithmetic stays
   inside 32 bits: the largest canvas is 200x512, so the products top
   out near 1.6 billion. */
static void draw_grid(canvas *c, int32_t top, int32_t rows, int32_t cols) {
    uint32_t w = (uint32_t)cols;
    uint32_t h = (uint32_t)(rows * 2);
    uint32_t core = M.p.core;
    if (w == 0 || h == 0 || core == 0) {
        return;
    }
    uint32_t sub = w * h;
    uint32_t gw = 0;
    uint32_t gh = 0;
    if (core <= sub) {
        grid_shape(core, w, h, &gw, &gh);
    }
    int32_t r = 0;
    while (r < rows) {
        int32_t x = 0;
        while (x < cols) {
            int16_t half[2] = {CV_COLOR_DEFAULT, CV_COLOR_DEFAULT};
            uint32_t k = 0;
            while (k < 2) {
                uint32_t y = (uint32_t)(r * 2) + k;
                uint32_t from = 0;
                uint32_t to = 0;
                if (core > sub) {
                    uint32_t at = (y * w) + (uint32_t)x;
                    from = (at * core) / sub;
                    to = ((at + 1) * core) / sub;
                } else {
                    from = (((y * gh) / h) * gw) + (((uint32_t)x * gw) / w);
                    to = from + 1;
                }
                if (to > core) {
                    to = core;
                }
                if (from < to) {
                    half[k] = cell_colour(from, to);
                }
                k++;
            }
            if (half[0] == half[1]) {
                cv_pen(c, half[0], half[0], 0);
                (void)cv_put(c, top + r, x, cv_cstr(" ")); /* both halves: no glyph */
            } else if (half[0] != CV_COLOR_DEFAULT) {
                cv_pen(c, half[0], half[1], 0);
                (void)cv_put(c, top + r, x, cv_cstr("\xe2\x96\x80")); /* ▀ */
            } else {
                cv_pen(c, half[1], CV_COLOR_DEFAULT, 0);
                (void)cv_put(c, top + r, x, cv_cstr("\xe2\x96\x84")); /* ▄ */
            }
            x++;
        }
        r++;
    }
}

/* The round in the middle, and with two fighting their names in the
   corners; more than two and there is no room, so the panel names them.
   Under it a bar that fills as the round's cycles run out. */
static void draw_head(const corewar_state *s, canvas *c, int32_t cols) {
    if (s->n == 2) {
        cv_pen(c, arena_colour(0), CV_COLOR_DEFAULT, CV_A_BOLD);
        (void)cv_put(c, 0, 1, cv_cstr(W[0].name));
        int32_t wb = (int32_t)cv_text_width(cv_cstr(W[1].name));
        cv_pen(c, arena_colour(1), CV_COLOR_DEFAULT, CV_A_BOLD);
        (void)cv_put(c, 0, cols - 1 - wb, cv_cstr(W[1].name));
    } else {
        char many[32];
        size_t at = cat_u32(many, sizeof(many), 0, s->n);
        (void)cat(many, sizeof(many), at, " warriors");
        cv_pen(c, CW_FAINT, CV_COLOR_DEFAULT, 0);
        (void)cv_put(c, 0, 1, cv_cstr(many));
    }
    char mid[40];
    size_t at = cat(mid, sizeof(mid), 0, "round ");
    at = cat_u32(mid, sizeof(mid), at, s->round < s->rounds ? s->round + 1 : s->rounds);
    at = cat(mid, sizeof(mid), at, "/");
    at = cat_u32(mid, sizeof(mid), at, s->rounds);
    cv_pen(c, CW_TEXT, CV_COLOR_DEFAULT, 0);
    (void)cv_put(c, 0, (cols - (int32_t)at) / 2, cv_cstr(mid));
    uint32_t per_col = M.p.cycles / (uint32_t)cols;
    if (per_col == 0) {
        per_col = 1;
    }
    int32_t lit = (int32_t)(M.cycle / per_col);
    if (lit > cols) {
        lit = cols;
    }
    cv_pen(c, CW_TEXT, CV_COLOR_DEFAULT, 0);
    cv_fill(c, 1, 0, 1, lit, cv_cstr("\xe2\x94\x80")); /* ─ */
    cv_pen(c, CW_RULE, CV_COLOR_DEFAULT, 0);
    cv_fill(c, 1, lit, 1, cols - lit, cv_cstr("\xe2\x94\x80"));
}

/* The rounds as a row of blocks, each in the colour of whoever took it
   alone, grey where it was shared. */
static int32_t draw_rounds(const corewar_state *s, canvas *c, int32_t row, int32_t col,
                           int32_t end) {
    cv_pen(c, CW_FAINT, CV_COLOR_DEFAULT, 0);
    col += cv_put(c, row, col, cv_cstr("<"));
    uint32_t r = 0;
    while (r < s->rounds && col < end - 1) {
        int16_t colour = CW_RULE;
        if (r < s->round) {
            colour = s->won[r] < 0 ? CW_FAINT : arena_colour((uint32_t)s->won[r]);
        }
        cv_pen(c, colour, CV_COLOR_DEFAULT, 0);
        col += cv_put(c, row, col, cv_cstr("\xe2\x96\xa0")); /* ■ */
        r++;
    }
    cv_pen(c, CW_FAINT, CV_COLOR_DEFAULT, 0);
    return col + cv_put(c, row, col, cv_cstr(">"));
}

/* A label on the left, its value against the right edge. */
static void kv(canvas *c, int32_t row, int32_t col, int32_t w, const char *key, const char *val) {
    cv_pen(c, CW_FAINT, CV_COLOR_DEFAULT, 0);
    (void)cv_put(c, row, col, cv_cstr(key));
    cv_pen(c, CW_TEXT, CV_COLOR_DEFAULT, 0);
    (void)cv_put(c, row, col + w - (int32_t)cv_text_width(cv_cstr(val)), cv_cstr(val));
}

/* What the round has already settled, in one line. The rule is the
   processes: a warrior with none left is out, and whoever is still
   running when the cycles end shares the round. How much of the core
   each one holds settles nothing — it is only the clearest sign of
   where a running fight is going, which is why it sits in the bar above
   this line and not in it. */
static int16_t decision(const corewar_state *s, const uint32_t *held, char *buf, size_t cap) {
    buf[0] = '\0';
    uint32_t alive = M.nalive;
    if (alive == 0) {
        (void)cat(buf, cap, 0, "tie: nobody has a process left");
        return CW_FAINT;
    }
    if (alive == 1) {
        uint32_t win = 0;
        while (win < s->n && !M.alive[win]) {
            win++;
        }
        size_t at = cat(buf, cap, 0, W[win].name);
        (void)cat(buf, cap, at, s->n == 2 ? " wins: the other is out" : " wins: the rest are out");
        return arena_colour(win);
    }
    if (M.over) {
        size_t at = cat(buf, cap, 0, "tie: ");
        at = cat_u32(buf, cap, at, M.cycle);
        at = cat(buf, cap, at, " cycles, ");
        at = cat_u32(buf, cap, at, alive);
        (void)cat(buf, cap, at, " alive");
        return CW_FAINT;
    }
    uint32_t lead = 0;
    uint32_t i = 1;
    while (i < s->n) {
        if (held[i] > held[lead]) {
            lead = i;
        }
        i++;
    }
    if (held[lead] * 100 >= M.p.core * CW_LEAD_PCT) {
        size_t at = cat(buf, cap, 0, W[lead].name);
        (void)cat(buf, cap, at, " holds the core, all alive");
        return arena_colour(lead);
    }
    (void)cat(buf, cap, 0, "undecided: it ends on processes");
    return CW_FAINT;
}

/* How the core stands between them, one run of colour each. */
static void draw_split(const corewar_state *s, canvas *c, int32_t row, int32_t col, int32_t w,
                       const uint32_t *held) {
    int32_t at = col;
    uint32_t i = 0;
    while (i < s->n) {
        int32_t run = (int32_t)((held[i] * (uint32_t)w) / M.p.core);
        if (at + run > col + w) {
            run = col + w - at;
        }
        cv_pen(c, arena_colour(i), arena_colour(i), 0);
        cv_fill(c, row, at, 1, run, cv_cstr(" "));
        at += run;
        i++;
    }
    cv_pen(c, CW_DARK, CW_DARK, 0);
    cv_fill(c, row, at, 1, col + w - at, cv_cstr(" "));
}

/* One warrior: name and score, whether it is still running and how much
   of the core it has taken, then the instructions around the one it is
   about to execute — each line in the colour of whoever owns that cell,
   which is how you see a warrior walk into enemy code. */
static int32_t draw_warrior(const corewar_state *s, canvas *c, int32_t row, int32_t col, int32_t w,
                            uint32_t who, const uint32_t *held, int32_t lines) {
    char buf[64];
    cv_pen(c, arena_colour(who), CV_COLOR_DEFAULT, CV_A_BOLD);
    (void)cv_put(c, row, col, cv_cstr(W[who].name));
    size_t at = cat(buf, sizeof(buf), 0, "score ");
    at = cat_u32(buf, sizeof(buf), at, s->points[who]);
    cv_pen(c, CW_FAINT, CV_COLOR_DEFAULT, 0);
    (void)cv_put(c, row, col + w - (int32_t)at, cv_cstr(buf));
    row++;
    if (M.alive[who]) {
        at = cat(buf, sizeof(buf), 0, "procs ");
        at = cat_u32(buf, sizeof(buf), at, M.qlen[who]);
    } else {
        at = cat(buf, sizeof(buf), 0, "out");
    }
    at = cat(buf, sizeof(buf), at, "   core ");
    (void)cat_pct(buf, sizeof(buf), at, held[who], M.p.core);
    cv_pen(c, M.alive[who] ? CW_TEXT : CW_FAINT, CV_COLOR_DEFAULT, 0);
    (void)cv_put(c, row, col, cv_cstr(buf));
    row++;
    int32_t k = -(lines / 2);
    while (k <= lines / 2) {
        uint32_t addr = (uint32_t)((int32_t)M.pc_of[who] + k + (int32_t)M.p.core) % M.p.core;
        bool here = k == 0;
        int16_t colour = CW_FAINT;
        if (M.owner[addr] < M.nwarriors) {
            colour = arena_colour(M.owner[addr]);
        }
        cv_pen(c, here ? arena_colour(who) : CW_FAINT, CV_COLOR_DEFAULT, here ? CV_A_BOLD : 0);
        (void)cv_put(c, row, col, cv_cstr(here ? "\xe2\x96\xb8" : " ")); /* ▸ */
        cv_pen(c, colour, CV_COLOR_DEFAULT, here ? CV_A_BOLD : 0);
        int32_t x = col + 1;
        x += put_addr(c, row, x, addr);
        x += cv_put(c, row, x, cv_cstr(" "));
        (void)mars_format(&M.core[addr], M.p.core, buf, sizeof(buf));
        (void)cv_put(c, row, x, cv_cstr(buf));
        row++;
        k++;
    }
    return row;
}

/* The panel: what the fight costs in cycles and cells, what would end
   the round, and then every warrior in it. */
static void draw_panel(const corewar_state *s, canvas *c, int32_t rows, int32_t col, int32_t w,
                       const uint32_t *held) {
    char buf[48];
    int32_t row = 0;
    size_t at = cat_u32(buf, sizeof(buf), 0, M.cycle);
    at = cat(buf, sizeof(buf), at, "/");
    (void)cat_u32(buf, sizeof(buf), at, M.p.cycles);
    kv(c, row, col, w, "cycle", buf);
    row++;
    at = cat_u32(buf, sizeof(buf), 0, M.p.core);
    at = cat(buf, sizeof(buf), at, " cells  ");
    (void)cat(buf, sizeof(buf), at, presets[s->size].name);
    kv(c, row, col, w, "core", buf);
    row++;
    if (s->paused) {
        (void)cat(buf, sizeof(buf), 0, "paused");
    } else {
        at = cat(buf, sizeof(buf), 0, "x");
        (void)cat_u32(buf, sizeof(buf), at, speeds[s->speed]);
    }
    kv(c, row, col, w, "speed", buf);
    row++;
    (void)draw_rounds(s, c, row, col, col + w);
    row++;
    draw_split(s, c, row, col, w, held);
    row++;
    char text[96];
    int16_t colour = decision(s, held, text, sizeof(text));
    cv_pen(c, colour, CV_COLOR_DEFAULT, colour == CW_FAINT ? 0 : CV_A_BOLD);
    (void)cv_put(c, row, col, cv_cstr(text));
    row += 2;
    /* the rest of the panel, shared out: a name, a state and as much of
       the code as there is room for */
    int32_t each = (rows - row) / (int32_t)s->n;
    int32_t lines = each - 3;
    if (lines > CW_LIST_MAX) {
        lines = CW_LIST_MAX;
    }
    if (lines < 1) {
        lines = 1;
    }
    if (lines % 2 == 0) {
        lines--; /* odd, so the instruction being executed sits in the middle */
    }
    uint32_t i = 0;
    while (i < s->n && row < rows) {
        row = draw_warrior(s, c, row, col, w, i, held, lines);
        if (each > lines + 2) {
            row++;
        }
        i++;
    }
}

/* Without room for the panel, the numbers that matter go on one line. */
static void draw_compact(const corewar_state *s, canvas *c, int32_t row, int32_t cols,
                         const uint32_t *held) {
    int32_t col = draw_rounds(s, c, row, 1, cols - 28);
    char buf[48];
    size_t at = cat(buf, sizeof(buf), 0, "  cycle ");
    at = cat_u32(buf, sizeof(buf), at, M.cycle);
    at = cat(buf, sizeof(buf), at, "  alive ");
    at = cat_u32(buf, sizeof(buf), at, M.nalive);
    at = cat(buf, sizeof(buf), at, "/");
    (void)cat_u32(buf, sizeof(buf), at, s->n);
    cv_pen(c, CW_TEXT, CV_COLOR_DEFAULT, 0);
    col += cv_put(c, row, col, cv_cstr(buf));
    if (cols - col > 6) {
        draw_split(s, c, row, col + 2, cols - col - 3, held);
    }
}

static void draw_footer(const corewar_state *s, canvas *c, int32_t row, int32_t cols) {
    cv_pen(c, CW_TEXT, CW_DARK, 0);
    cv_fill(c, row, 0, 1, cols, cv_cstr(" "));
    if (s->note_ms > 0) {
        cv_pen(c, CW_KEY, CW_DARK, CV_A_BOLD);
        (void)cv_put(c, row, 1, cv_cstr(s->note));
        return;
    }
    int32_t col = 1;
    if (s->over) {
        col = hint(c, row, col, "R", "rematch");
        col = hint(c, row, col, "Esc", "leave");
        cv_pen(c, CW_TEXT, CW_DARK, 0);
        (void)cv_put(c, row, col, cv_cstr("match over"));
        return;
    }
    col = hint(c, row, col, "Space", s->paused ? "run" : "pause");
    col = hint(c, row, col, "+ -", "speed");
    col = hint(c, row, col, "M", "core");
    col = hint(c, row, col, "C", "cycles");
    col = hint(c, row, col, "N", "round");
    col = hint(c, row, col, "R", "rematch");
    (void)hint(c, row, col, "Esc", "leave");
}

static void corewar_draw(arena *a) {
    const corewar_state *s = &a->s;
    canvas *c = a->target;
    cv_reset(c, a->t->rows, a->t->cols);
    cv_pen_reset(c);
    cv_pen(c, CV_COLOR_DEFAULT, CV_COLOR_DEFAULT, 0);
    cv_fill(c, 0, 0, a->t->rows, a->t->cols, cv_cstr(" "));
    int32_t rows = (int32_t)a->t->rows;
    int32_t cols = (int32_t)a->t->cols;
    if (rows < CW_MIN_ROWS || cols < CW_MIN_COLS) {
        (void)cv_put(c, 0, 0, cv_cstr("the core needs a bigger window"));
        cv_flush(a->t, a->shown, a->target);
        return;
    }
    uint32_t held[CW_FIGHTERS_MAX];
    count_cells(held);
    int32_t body = rows - 1; /* everything above the footer */
    int32_t gw = cols;
    if (cols >= CW_PANEL_MIN) {
        gw = cols - CW_PANEL_W - 1;
        cv_pen(c, CW_RULE, CV_COLOR_DEFAULT, 0);
        cv_fill(c, 0, gw, body, 1, cv_cstr("\xe2\x94\x82")); /* │ */
        draw_panel(s, c, body, gw + 2, CW_PANEL_W - 2, held);
    }
    draw_head(s, c, gw);
    int32_t grid = body - 2;
    if (gw == cols) {
        grid--; /* the compact line takes the row the panel would have had */
        draw_compact(s, c, body - 1, cols, held);
    }
    draw_grid(c, 2, grid, gw);
    draw_footer(s, c, rows - 1, cols);
    cv_flush(a->t, a->shown, a->target);
}

static void corewar_round_begin(arena *a) {
    corewar_state *s = &a->s;
    mars_params prm = preset_params(s);
    uint32_t pos[CW_FIGHTERS_MAX];
    if (s->fixed && s->n == 2) {
        pos[0] = 0;
        pos[1] = s->at;
    } else {
        place(&s->seed, &prm, s->n, pos);
    }
    const mars_warrior *ws[CW_FIGHTERS_MAX];
    uint32_t i = 0;
    while (i < s->n) {
        ws[i] = &W[i];
        i++;
    }
    mars_load(&M, &prm, ws, pos, s->n);
    s->rest_ms = 0;
}

static void corewar_match_begin(arena *a) {
    corewar_state *s = &a->s;
    mars_params prm = preset_params(s);
    s->round = 0;
    memset(s->points, 0, sizeof(s->points));
    memset(s->res, 0, sizeof(s->res));
    memset(s->held, 0, sizeof(s->held));
    s->over = false;
    mars_match_begin(&M, &prm);
    corewar_round_begin(a);
}

/* Scores the round that just ended; true when it was the match's last. */
static bool corewar_round_end(arena *a) {
    corewar_state *s = &a->s;
    uint32_t alive = M.nalive;
    uint32_t i = 0;
    while (i < s->n) {
        record(s, i, alive);
        i++;
    }
    count_cells(s->held);
    s->won[s->round] = alive == 1 ? (int8_t)M.winner : -1;
    s->round++;
    if (s->round >= s->rounds) {
        s->over = true;
        return true;
    }
    s->rest_ms = CW_REST_MS; /* a breath, then the next round */
    return false;
}

/* A different core is a different fight: CORESIZE is in the warriors'
   own arithmetic, so all of them are assembled again before anything
   starts, and nothing changes if one no longer fits. */
static void corewar_resize_core(arena *a, uint32_t idx) {
    corewar_state *s = &a->s;
    uint32_t was = s->size;
    s->size = idx;
    mars_params prm = preset_params(s);
    char err[MARS_ERR_MAX];
    const char *who = s->path[0];
    bool ok = true;
    uint32_t i = 0;
    while (i < s->n && ok) {
        who = s->path[i];
        ok = assemble_path(a, who, &TMP[i], &prm, err, sizeof(err));
        i++;
    }
    if (!ok) {
        s->size = was;
        char msg[CW_NOTE_MAX];
        size_t at = cat(msg, sizeof(msg), 0, presets[idx].name);
        at = cat(msg, sizeof(msg), at, " core: ");
        at = cat(msg, sizeof(msg), at, base_name(who));
        at = cat(msg, sizeof(msg), at, ": ");
        (void)cat(msg, sizeof(msg), at, err);
        say(s, msg, NULL);
        return;
    }
    i = 0;
    while (i < s->n) {
        W[i] = TMP[i];
        i++;
    }
    s->fixed = false; /* the position was measured in the core that is gone */
    corewar_match_begin(a);
    say(s, presets[idx].name, " core: a new match");
}

static void corewar_set_limit(arena *a, uint32_t idx) {
    corewar_state *s = &a->s;
    s->limit = idx;
    corewar_match_begin(a);
    char msg[CW_NOTE_MAX];
    size_t at = cat(msg, sizeof(msg), 0, "round ends at ");
    at = cat_u32(msg, sizeof(msg), at, M.p.cycles);
    (void)cat(msg, sizeof(msg), at, " cycles: a new match");
    say(s, msg, NULL);
}

void arena_show(arena *a) {
    term_puts(a->t, "\x1b[?25l");
    a->shown->rows = 0; /* whatever was on the terminal is not ours */
    corewar_draw(a);
}

bool arena_ready_with(arena *a, uint32_t seed, uint32_t rounds, bool fixed, uint32_t at, char *why,
                      size_t cap) {
    corewar_state *s = &a->s;
    why[0] = '\0';
    uint32_t n = arena_count(a);
    if (n < 2) {
        (void)cat(why, cap, 0, "two warriors at least");
        return false;
    }
    if (rounds == 0 || rounds > CW_ROUNDS_MAX) {
        (void)cat(why, cap, 0, "rounds: Invalid argument");
        return false;
    }
    s->rounds = rounds;
    s->size = CW_PRESET_STD;
    s->limit = 0;
    mars_params prm = preset_params(s);
    uint32_t i = 0;
    while (i < n) {
        if (!load_warrior(a, s->path[i], &W[i], &prm, why, cap)) {
            return false;
        }
        i++;
    }
    s->fixed = false;
    if (fixed && n == 2) {
        uint32_t low = prm.dist > W[0].len ? prm.dist : W[0].len;
        if (at < low || at > prm.core - low) {
            (void)cat(why, cap, 0, "position: Invalid argument"); /* too close, or off the core */
            return false;
        }
        s->fixed = true; /* a position pinned in a two-warrior fight */
    }
    s->at = at;
    s->report = false;
    s->speed = 2;
    s->paused = false;
    s->note_ms = 0;
    s->note[0] = '\0';
    s->seed = seed;
    corewar_match_begin(a);
    return true;
}

bool arena_ready(arena *a, uint32_t seed, char *why, size_t cap) {
    return arena_ready_with(a, seed, CW_ROUNDS, false, 0, why, cap);
}

bool arena_batch(arena *a, uint32_t seed, uint32_t rounds, bool fixed, uint32_t at, char *out,
                 size_t cap, char *why, size_t whycap) {
    if (!arena_ready_with(a, seed, rounds, fixed, at, why, whycap)) {
        return false;
    }
    corewar_state *s = &a->s;
    uint32_t n = s->n;
    mars_params prm = preset_params(s);
    memset(s->points, 0, sizeof(s->points));
    memset(s->res, 0, sizeof(s->res));
    const mars_warrior *ws[CW_FIGHTERS_MAX];
    uint32_t i = 0;
    while (i < n) {
        ws[i] = &W[i];
        i++;
    }
    mars_match_begin(&M, &prm);
    uint32_t r = 0;
    while (r < rounds) {
        uint32_t pos[CW_FIGHTERS_MAX];
        if (s->fixed) {
            pos[0] = 0;
            pos[1] = at;
        } else {
            place(&s->seed, &prm, n, pos);
        }
        mars_load(&M, &prm, ws, pos, n);
        (void)mars_run(&M);
        i = 0;
        while (i < n) {
            record(s, i, M.nalive);
            i++;
        }
        r++;
    }
    format_results(s);
    out[0] = '\0';
    (void)cat(out, cap, 0, report);
    return true;
}

/* ---- the keys ---- */

static void corewar_on_key(void *ctx, uint32_t cp) {
    arena *a = ctx;
    corewar_state *s = &a->s;
    if (cp == FT_KEY_ESC || cp == 'q' || cp == 'Q') {
        const char *note = NULL;
        if (s->report) {
            format_results(s);
            note = report;
        }
        term_puts(a->t, "\x1b[?25h"); /* the arena hid it to draw: hand it back */
        a->host.leave(a->host.user, note);
        return;
    }
    if (cp == ' ') {
        if (s->paused) {
            s->paused = false;
        } else {
            s->paused = true;
        }
    } else if (cp == '+' || cp == '=') {
        if (s->speed + 1 < CW_SPEEDS) {
            s->speed++;
        }
    } else if (cp == '-') {
        if (s->speed > 0) {
            s->speed--;
        }
    } else if (cp == 'm' || cp == 'M') {
        corewar_resize_core(a, (s->size + 1) % CW_PRESETS);
    } else if (cp == 'c' || cp == 'C') {
        corewar_set_limit(a, (s->limit + 1) % CW_LIMITS);
    } else if ((cp == 'n' || cp == 'N') && !s->over) {
        bool done = false;
        if (M.over) {
            done = false; /* resting between rounds: skip the rest */
        } else {
            M.over = true; /* the round is called: whoever is alive shares it */
            if (M.nalive != 1) {
                M.winner = -1;
            }
            done = corewar_round_end(a);
        }
        if (!done) {
            corewar_round_begin(a);
        }
    } else if (cp == 'r' || cp == 'R') {
        corewar_match_begin(a);
    }
    corewar_draw(a);
}

static void corewar_on_resize(void *ctx) {
    arena *a = ctx;
    a->shown->rows = 0;
    corewar_draw(a);
}

static void corewar_on_tick(void *ctx, uint32_t ms) {
    arena *a = ctx;
    corewar_state *s = &a->s;
    bool note_went = false;
    if (s->note_ms > 0) {
        s->note_ms = ms < s->note_ms ? s->note_ms - ms : 0;
        note_went = s->note_ms == 0;
    }
    if (s->over || s->paused) {
        if (note_went) {
            corewar_draw(a);
        }
        return;
    }
    if (s->rest_ms > 0) {
        s->rest_ms = ms < s->rest_ms ? s->rest_ms - ms : 0;
        if (s->rest_ms == 0) {
            corewar_round_begin(a);
        }
        corewar_draw(a);
        return;
    }
    uint32_t n = speeds[s->speed];
    while (n > 0 && mars_step(&M)) {
        n--;
    }
    if (M.over) {
        corewar_round_end(a);
    }
    corewar_draw(a);
}

/* ---- what the pick screen calls ---- */

static arena *(*arena_of)(filo_ctx *ctx);

static int slot_arg(filo_ctx *ctx, const filo_value *v, const char *who, uint32_t *out) {
    if (v->kind != FILO_NUMBER || v->u.num < 0 || v->u.num >= (double)CW_FIGHTERS_MAX) {
        return filo_fail2(ctx, who, " expects the place of a warrior in the fight");
    }
    *out = (uint32_t)v->u.num;
    return FILO_OK;
}

static int path_arg(filo_ctx *ctx, const filo_value *v, char *dst, size_t cap) {
    filo_str str = {NULL, 0};
    dst[0] = '\0'; /* a refusal leaves an empty name behind, never a stale one */
    if (filo_arg_str(ctx, v, &str) != FILO_OK) {
        return FILO_ERR;
    }
    if (str.len + 1 > cap) {
        return filo_fail(ctx, "that warrior's name is too long");
    }
    size_t i = 0;
    while (i < str.len) {
        dst[i] = (char)str.ptr[i];
        i++;
    }
    dst[i] = '\0';
    return FILO_OK;
}

/* (cw-count): how many are in the fight. */
static int b_cw_count(filo_ctx *ctx, const filo_value *a, uint32_t n, filo_value *out) {
    (void)a;
    if (n != 0) {
        return filo_fail(ctx, "cw-count takes no argument");
    }
    *out = filo_num(arena_count(arena_of(ctx)));
    return FILO_OK;
}

/* (cw-picked i): the file the i-th one fights with. */
static int b_cw_picked(filo_ctx *ctx, const filo_value *a, uint32_t n, filo_value *out) {
    uint32_t slot = 0;
    if (n != 1 || slot_arg(ctx, &a[0], "cw-picked", &slot) != FILO_OK) {
        return FILO_ERR;
    }
    *out = filo_cstring(arena_picked(arena_of(ctx), slot));
    return FILO_OK;
}

/* (cw-colour i): the colour the arena paints that one in. */
static int b_cw_colour(filo_ctx *ctx, const filo_value *a, uint32_t n, filo_value *out) {
    uint32_t slot = 0;
    if (n != 1 || slot_arg(ctx, &a[0], "cw-colour", &slot) != FILO_OK) {
        return FILO_ERR;
    }
    *out = filo_num(arena_colour(slot));
    return FILO_OK;
}

/* (cw-add "path"): one more into the fight, false when it is full. */
static int b_cw_add(filo_ctx *ctx, const filo_value *a, uint32_t n, filo_value *out) {
    char path[CW_PATH_MAX];
    if (n != 1 || path_arg(ctx, &a[0], path, sizeof(path)) != FILO_OK) {
        return FILO_ERR;
    }
    *out = filo_bool(arena_add(arena_of(ctx), path));
    return FILO_OK;
}

/* (cw-drop): the last one out again. */
static int b_cw_drop(filo_ctx *ctx, const filo_value *a, uint32_t n, filo_value *out) {
    (void)a;
    if (n != 0) {
        return filo_fail(ctx, "cw-drop takes no argument");
    }
    arena_drop(arena_of(ctx));
    *out = filo_bool(true);
    return FILO_OK;
}

/* (cw-clear): an empty list. */
static int b_cw_clear(filo_ctx *ctx, const filo_value *a, uint32_t n, filo_value *out) {
    (void)a;
    if (n != 0) {
        return filo_fail(ctx, "cw-clear takes no argument");
    }
    arena_clear(arena_of(ctx));
    *out = filo_bool(true);
    return FILO_OK;
}

bool arena_register(filo_ctx *ctx, arena *(*arena_fn)(filo_ctx *ctx)) {
    arena_of = arena_fn;
    (void)filo_register_builtin(ctx, "cw-count", b_cw_count);
    (void)filo_register_builtin(ctx, "cw-picked", b_cw_picked);
    (void)filo_register_builtin(ctx, "cw-colour", b_cw_colour);
    (void)filo_register_builtin(ctx, "cw-add", b_cw_add);
    (void)filo_register_builtin(ctx, "cw-drop", b_cw_drop);
    /* a full table refuses the last ones first: this one standing means
       they all do */
    return filo_register_builtin(ctx, "cw-clear", b_cw_clear) == FILO_OK;
}
