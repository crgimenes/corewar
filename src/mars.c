#include "mars.h"

#include <string.h>

enum { SRC_MAX = 16384, LINES_MAX = 512, DEPTH_MAX = 16 };

mars_params mars_defaults(void) {
    mars_params p = {8000, 8000, 80000, 100, 100, 1, 2};
    return p;
}

/* ---- the assembler ---- */

static const char *const op_names[OP_COUNT] = {
    "DAT", "MOV", "ADD", "SUB", "MUL", "DIV", "MOD", "JMP", "JMZ", "JMN",
    "DJN", "SPL", "SLT", "SEQ", "SNE", "NOP", "LDP", "STP", "CMP",
};

static const char *const mod_names[] = {"A", "B", "AB", "BA", "F", "X", "I"};
static const char mode_chars[] = "#$@<>*{}";

typedef struct {
    char name[MARS_LABEL_LEN];
    int32_t line; /* instruction index, or -1 for an EQU */
    char text[MARS_EQU_TEXT];
} label;

/* A FOR/ROF being unrolled: where its body is, its index, where it stands. */
typedef struct {
    size_t body;
    size_t rof;
    char index[MARS_LABEL_LEN];
    int64_t i;
    int64_t count;
} loop;

typedef struct {
    const mars_params *p;
    mars_warrior *w;
    char *err;
    size_t err_cap;
    char src[SRC_MAX];
    char *lines[LINES_MAX];
    size_t nlines;
    label labels[MARS_LABELS_MAX];
    size_t nlabels;
    /* pass 1 keeps each instruction's text with its EQUs and FOR indices
       already spelled out; only address labels are left for pass 2 */
    char stmt[MARS_LEN_MAX][MARS_LINE_MAX];
    int32_t cur; /* the instruction being assembled, for CURLINE and labels */
    bool has_org;
    char org_text[MARS_EQU_TEXT];
    loop loops[MARS_LOOPS_MAX];
    size_t nloops;
    /* labels on a line of their own wait for the next instruction */
    char pending[8][MARS_LABEL_LEN];
    size_t npending;
} asm_state;

static bool fail(asm_state *s, const char *what, const char *detail) {
    size_t k = 0;
    const char *parts[] = {what, detail != NULL ? ": " : "", detail != NULL ? detail : ""};
    size_t i = 0;
    while (i < 3) {
        size_t n = strlen(parts[i]);
        if (n > s->err_cap - 1 - k) {
            n = s->err_cap - 1 - k;
        }
        memcpy(s->err + k, parts[i], n);
        k += n;
        i++;
    }
    s->err[k] = '\0';
    return false;
}

static bool is_alpha(char c) {
    if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_') {
        return true;
    }
    return false;
}

static bool is_digit(char c) {
    if (c >= '0' && c <= '9') {
        return true;
    }
    return false;
}

static bool is_space(char c) {
    if (c == ' ' || c == '\t' || c == '\r') {
        return true;
    }
    return false;
}

static char lower(char c) {
    return (c >= 'A' && c <= 'Z') ? (char)(c + 32) : c;
}

static bool same_word(const char *a, size_t n, const char *word) {
    size_t i = 0;
    while (i < n) {
        if (word[i] == '\0' || lower(a[i]) != lower(word[i])) {
            return false;
        }
        i++;
    }
    return word[n] == '\0';
}

static size_t ident_len(const char *p) {
    size_t n = 0;
    if (!is_alpha(p[0])) {
        return 0;
    }
    while (is_alpha(p[n]) || is_digit(p[n])) {
        n++;
    }
    return n;
}

static int find_op(const char *p, size_t n) {
    int i = 0;
    while (i < OP_COUNT) {
        if (same_word(p, n, op_names[i])) {
            return i;
        }
        i++;
    }
    return -1;
}

static bool is_pseudo(const char *p, size_t n) {
    static const char *const names[] = {"EQU", "ORG", "END", "FOR", "ROF", "PIN"};
    size_t i = 0;
    while (i < 6) {
        if (same_word(p, n, names[i])) {
            return true;
        }
        i++;
    }
    return false;
}

static label *find_label(asm_state *s, const char *name, size_t n) {
    size_t i = 0;
    while (i < s->nlabels) {
        if (strlen(s->labels[i].name) == n && memcmp(s->labels[i].name, name, n) == 0) {
            return &s->labels[i];
        }
        i++;
    }
    return NULL;
}

static bool add_label(asm_state *s, const char *name, size_t n, int32_t line, const char *text) {
    if (n >= MARS_LABEL_LEN) {
        return fail(s, "label too long", NULL);
    }
    if (find_label(s, name, n) != NULL) {
        char tmp[MARS_LABEL_LEN];
        memcpy(tmp, name, n);
        tmp[n] = '\0';
        return fail(s, "label declared twice", tmp);
    }
    if (s->nlabels >= MARS_LABELS_MAX) {
        return fail(s, "too many labels", NULL);
    }
    label *l = &s->labels[s->nlabels];
    memcpy(l->name, name, n);
    l->name[n] = '\0';
    l->line = line;
    l->text[0] = '\0';
    if (text != NULL) {
        size_t t = strlen(text);
        if (t >= sizeof(l->text)) {
            return fail(s, "EQU text too long", l->name);
        }
        memcpy(l->text, text, t + 1);
    }
    s->nlabels++;
    return true;
}

/* ---- expressions ---- */

typedef struct {
    asm_state *s;
    const char *p;
    int depth;
    bool ok;
} expr;

static int64_t parse_or(expr *e);

static void skip_ws(expr *e) {
    while (is_space(*e->p)) {
        e->p++;
    }
}

static bool predefined(const asm_state *s, const char *name, size_t n, int64_t *out) {
    const mars_params *p = s->p;
    if (same_word(name, n, "CORESIZE")) {
        *out = p->core;
    } else if (same_word(name, n, "MAXPROCESSES")) {
        *out = p->procs;
    } else if (same_word(name, n, "MAXCYCLES")) {
        *out = p->cycles;
    } else if (same_word(name, n, "MAXLENGTH")) {
        *out = p->len;
    } else if (same_word(name, n, "MINDISTANCE")) {
        *out = p->dist;
    } else if (same_word(name, n, "ROUNDS")) {
        *out = p->rounds;
    } else if (same_word(name, n, "PSPACESIZE")) {
        *out = p->core / 16;
    } else if (same_word(name, n, "CURLINE")) {
        *out = s->cur;
    } else if (same_word(name, n, "VERSION")) {
        *out = 92;
    } else if (same_word(name, n, "WARRIORS")) {
        *out = p->warriors;
    } else {
        return false;
    }
    return true;
}

static int64_t parse_primary(expr *e) {
    skip_ws(e);
    if (!e->ok || e->depth > DEPTH_MAX) {
        e->ok = false;
        return 0;
    }
    char c = *e->p;
    if (c == '(') {
        e->p++;
        e->depth++;
        int64_t v = parse_or(e);
        e->depth--;
        skip_ws(e);
        if (*e->p != ')') {
            (void)fail(e->s, "missing )", NULL);
            e->ok = false;
            return 0;
        }
        e->p++;
        return v;
    }
    if (c == '-') {
        e->p++;
        return -parse_primary(e);
    }
    if (c == '+') {
        e->p++;
        return parse_primary(e);
    }
    if (c == '!') {
        e->p++;
        return parse_primary(e) == 0 ? 1 : 0;
    }
    if (is_digit(c)) {
        int64_t v = 0;
        while (is_digit(*e->p)) {
            v = (v * 10) + (*e->p - '0');
            if (v > 100000000) {
                (void)fail(e->s, "number too large", NULL);
                e->ok = false;
                return 0;
            }
            e->p++;
        }
        return v;
    }
    size_t n = ident_len(e->p);
    if (n == 0) {
        (void)fail(e->s, "bad expression", e->p);
        e->ok = false;
        return 0;
    }
    const char *name = e->p;
    e->p += n;
    int64_t v = 0;
    if (predefined(e->s, name, n, &v)) {
        return v;
    }
    const label *l = find_label(e->s, name, n);
    if (l == NULL) {
        char tmp[MARS_LABEL_LEN];
        size_t k = n < sizeof(tmp) - 1 ? n : sizeof(tmp) - 1;
        memcpy(tmp, name, k);
        tmp[k] = '\0';
        (void)fail(e->s, "unknown label", tmp);
        e->ok = false;
        return 0;
    }
    if (l->line < 0) {
        /* an EQU: its text is an expression of its own */
        expr sub = {e->s, l->text, e->depth + 1, true};
        v = parse_or(&sub);
        skip_ws(&sub);
        if (!sub.ok || *sub.p != '\0') {
            (void)fail(e->s, "bad EQU expression", l->name);
            e->ok = false;
            return 0;
        }
        return v;
    }
    return (int64_t)l->line - e->s->cur; /* labels are offsets from here */
}

static int64_t parse_mul(expr *e) {
    int64_t v = parse_primary(e);
    for (;;) {
        skip_ws(e);
        char c = *e->p;
        if (c != '*' && c != '/' && c != '%') {
            return v;
        }
        e->p++;
        int64_t r = parse_primary(e);
        if (!e->ok) {
            return 0;
        }
        if (c == '*') {
            v *= r;
        } else if (r == 0) {
            (void)fail(e->s, "division by zero in expression", NULL);
            e->ok = false;
            return 0;
        } else if (c == '/') {
            v /= r;
        } else {
            v %= r;
        }
    }
}

static int64_t parse_add(expr *e) {
    int64_t v = parse_mul(e);
    for (;;) {
        skip_ws(e);
        char c = *e->p;
        if (c != '+' && c != '-') {
            return v;
        }
        e->p++;
        int64_t r = parse_mul(e);
        v = c == '+' ? v + r : v - r;
    }
}

static int64_t parse_cmp(expr *e) {
    int64_t v = parse_add(e);
    for (;;) {
        skip_ws(e);
        const char *p = e->p;
        if (p[0] == '=' && p[1] == '=') {
            e->p += 2;
            v = v == parse_add(e) ? 1 : 0;
        } else if (p[0] == '!' && p[1] == '=') {
            e->p += 2;
            v = v != parse_add(e) ? 1 : 0;
        } else if (p[0] == '<' && p[1] == '=') {
            e->p += 2;
            v = v <= parse_add(e) ? 1 : 0;
        } else if (p[0] == '>' && p[1] == '=') {
            e->p += 2;
            v = v >= parse_add(e) ? 1 : 0;
        } else if (p[0] == '<') {
            e->p += 1;
            v = v < parse_add(e) ? 1 : 0;
        } else if (p[0] == '>') {
            e->p += 1;
            v = v > parse_add(e) ? 1 : 0;
        } else {
            return v;
        }
    }
}

static int64_t parse_and(expr *e) {
    int64_t v = parse_cmp(e);
    for (;;) {
        skip_ws(e);
        if (e->p[0] != '&' || e->p[1] != '&') {
            return v;
        }
        e->p += 2;
        int64_t r = parse_cmp(e);
        v = (v != 0 && r != 0) ? 1 : 0;
    }
}

static int64_t parse_or(expr *e) {
    int64_t v = parse_and(e);
    for (;;) {
        skip_ws(e);
        if (e->p[0] != '|' || e->p[1] != '|') {
            return v;
        }
        e->p += 2;
        int64_t r = parse_and(e);
        v = (v != 0 || r != 0) ? 1 : 0;
    }
}

/* Evaluates text as one expression; false (with err) when it is not. */
static bool eval(asm_state *s, const char *text, int64_t *out) {
    expr e = {s, text, 0, true};
    int64_t v = parse_or(&e);
    if (!e.ok) {
        return false;
    }
    skip_ws(&e);
    if (*e.p != '\0') {
        return fail(s, "trailing text in expression", e.p);
    }
    *out = v;
    return true;
}

static uint16_t fold(int64_t v, uint32_t core) {
    int64_t m = (int64_t)core;
    v %= m;
    if (v < 0) {
        v += m;
    }
    return (uint16_t)v;
}

/* ---- statements ---- */

/* Cuts a comment, keeping the ;info comments the reader wants. */
static void strip_comment(char *line) {
    char *semi = strchr(line, ';');
    if (semi != NULL) {
        *semi = '\0';
    }
    size_t n = strlen(line);
    while (n > 0 && is_space(line[n - 1])) {
        n--;
        line[n] = '\0';
    }
}

static void copy_text(char *dst, size_t cap, const char *src) {
    while (is_space(*src)) {
        src++;
    }
    size_t n = strlen(src);
    while (n > 0 && (is_space(src[n - 1]) || src[n - 1] == '\n')) {
        n--;
    }
    if (n >= cap) {
        n = cap - 1;
    }
    memcpy(dst, src, n);
    dst[n] = '\0';
}

/* Splits one operand: an optional mode, then the expression up to the
   comma. Substitutes EQUs by text inside the expression as it goes. */
static bool parse_operand(asm_state *s, const char *text, size_t n, uint8_t *mode, uint16_t *val,
                          bool *given) {
    while (n > 0 && is_space(*text)) {
        text++;
        n--;
    }
    while (n > 0 && is_space(text[n - 1])) {
        n--;
    }
    *given = n > 0;
    *mode = AM_DIR;
    if (n == 0) {
        *val = 0;
        return true;
    }
    const char *mc = strchr(mode_chars, text[0]);
    if (mc != NULL && text[0] != '\0') {
        *mode = (uint8_t)(mc - mode_chars);
        text++;
        n--;
    }
    char buf[MARS_LINE_MAX];
    if (n >= sizeof(buf)) {
        return fail(s, "operand too long", NULL);
    }
    memcpy(buf, text, n);
    buf[n] = '\0';
    int64_t v = 0;
    if (!eval(s, buf, &v)) {
        return false;
    }
    *val = fold(v, s->p->core);
    return true;
}

static uint8_t default_mod(uint8_t op, uint8_t am, uint8_t bm) {
    switch (op) {
    case OP_DAT:
    case OP_NOP:
        return MOD_F;
    case OP_MOV:
    case OP_SEQ:
    case OP_CMP:
    case OP_SNE:
        if (am == AM_IMM) {
            return MOD_AB;
        }
        return bm == AM_IMM ? MOD_B : MOD_I;
    case OP_ADD:
    case OP_SUB:
    case OP_MUL:
    case OP_DIV:
    case OP_MOD:
        if (am == AM_IMM) {
            return MOD_AB;
        }
        return bm == AM_IMM ? MOD_B : MOD_F;
    case OP_SLT:
    case OP_LDP:
    case OP_STP:
        return am == AM_IMM ? MOD_AB : MOD_B;
    default: /* JMP JMZ JMN DJN SPL */
        return MOD_B;
    }
}

/* The instruction after its labels: opcode[.mod] A[, B]. */
static bool parse_instruction(asm_state *s, const char *text, mars_cell *c) {
    if (text == NULL) {
        return fail(s, "no statement", NULL);
    }
    size_t n = ident_len(text);
    int op = find_op(text, n);
    if (op < 0) {
        return fail(s, "unknown opcode", text);
    }
    const char *p = text + n;
    int mod = -1;
    if (*p == '.') {
        p++;
        size_t mn = ident_len(p);
        size_t i = 0;
        while (i < 7) {
            if (same_word(p, mn, mod_names[i])) {
                mod = (int)i;
            }
            i++;
        }
        if (mod < 0) {
            return fail(s, "unknown modifier", p);
        }
        p += mn;
    }
    if (!is_space(*p) && *p != '\0') {
        return fail(s, "bad opcode", text);
    }
    const char *comma = strchr(p, ',');
    uint8_t am = AM_DIR;
    uint8_t bm = AM_DIR;
    uint16_t a = 0;
    uint16_t b = 0;
    bool has_a = false;
    bool has_b = false;
    if (comma == NULL) {
        if (!parse_operand(s, p, strlen(p), &am, &a, &has_a)) {
            return false;
        }
    } else {
        if (!parse_operand(s, p, (size_t)(comma - p), &am, &a, &has_a) ||
            !parse_operand(s, comma + 1, strlen(comma + 1), &bm, &b, &has_b)) {
            return false;
        }
        if (!has_a || !has_b) {
            return fail(s, "missing operand", text);
        }
    }
    if (!has_a && op != OP_DAT && op != OP_NOP) {
        return fail(s, "missing operand", text);
    }
    if (!has_b) {
        /* one operand: it is the B of a DAT and the A of anything else */
        if (op == OP_DAT) {
            bm = has_a ? am : AM_IMM;
            b = a;
            am = AM_IMM;
            a = 0;
        } else {
            bm = AM_DIR;
            b = 0;
        }
    }
    c->op = (uint8_t)op;
    c->mod = mod >= 0 ? (uint8_t)mod : default_mod((uint8_t)op, am, bm);
    c->am = am;
    c->bm = bm;
    c->a = a;
    c->b = b;
    return true;
}

/* ---- pass 1: text becomes statements ---- */

/* The two-digit spelling of a FOR index, for label&index stringization. */
static void index_digits(int64_t i, char *out) {
    out[0] = (char)('0' + ((i / 10) % 10));
    out[1] = (char)('0' + (i % 10));
    out[2] = '\0';
}

static const loop *active_index(const asm_state *s, const char *name, size_t n) {
    size_t k = s->nloops;
    while (k > 0) {
        k--;
        const loop *l = &s->loops[k];
        if (l->index[0] != '\0' && strlen(l->index) == n && memcmp(l->index, name, n) == 0) {
            return l;
        }
    }
    return NULL;
}

static bool append(asm_state *s, char *dst, size_t *k, size_t cap, const char *text, size_t n) {
    if (n > cap - 1 - *k) {
        return fail(s, "statement too long after expansion", NULL);
    }
    memcpy(dst + *k, text, n);
    *k += n;
    dst[*k] = '\0';
    return true;
}

/* One expansion pass over text: label&index becomes labelNN, an index
   becomes its number, an EQU its text. True when something changed. */
static bool expand_once(asm_state *s, const char *text, char *out, size_t cap, bool *changed) {
    size_t k = 0;
    *changed = false;
    out[0] = '\0';
    const char *p = text;
    while (*p != '\0') {
        size_t n = ident_len(p);
        if (n == 0) {
            if (*p == '&') {
                return fail(s, "& without a FOR index", p);
            }
            if (!append(s, out, &k, cap, p, 1)) {
                return false;
            }
            p++;
            continue;
        }
        const char *name = p;
        p += n;
        /* stringization: name&index, possibly several */
        if (*p == '&') {
            if (!append(s, out, &k, cap, name, n)) {
                return false;
            }
            while (*p == '&') {
                p++;
                size_t in = ident_len(p);
                const loop *l = active_index(s, p, in);
                if (l == NULL) {
                    return fail(s, "& needs a FOR index", p);
                }
                char digits[3];
                index_digits(l->i, digits);
                if (!append(s, out, &k, cap, digits, 2)) {
                    return false;
                }
                p += in;
            }
            *changed = true;
            continue;
        }
        const loop *l = active_index(s, name, n);
        if (l != NULL) {
            char digits[24];
            size_t d = sizeof(digits);
            int64_t v = l->i;
            do {
                d--;
                digits[d] = (char)('0' + (v % 10));
                v /= 10;
            } while (v > 0);
            if (!append(s, out, &k, cap, digits + d, sizeof(digits) - d)) {
                return false;
            }
            *changed = true;
            continue;
        }
        int64_t dummy = 0;
        const label *e = find_label(s, name, n);
        if (e != NULL && e->line < 0 && !predefined(s, name, n, &dummy)) {
            if (!append(s, out, &k, cap, e->text, strlen(e->text))) {
                return false;
            }
            *changed = true;
            continue;
        }
        if (!append(s, out, &k, cap, name, n)) {
            return false;
        }
    }
    return true;
}

/* Expands text until nothing changes, the way pMARS substitutes: plainly. */
static bool expand(asm_state *s, const char *text, char *out, size_t cap) {
    char a[MARS_LINE_MAX];
    char b[MARS_LINE_MAX];
    if (strlen(text) >= sizeof(a)) {
        return fail(s, "line too long", NULL);
    }
    memcpy(a, text, strlen(text) + 1);
    int rounds = 0;
    for (;;) {
        bool changed = false;
        if (!expand_once(s, a, b, sizeof(b), &changed)) {
            return false;
        }
        if (!changed) {
            break;
        }
        rounds++;
        if (rounds > DEPTH_MAX) {
            return fail(s, "EQU refers to itself", text);
        }
        memcpy(a, b, strlen(b) + 1);
    }
    if (strlen(b) >= cap) {
        return fail(s, "statement too long after expansion", NULL);
    }
    memcpy(out, b, strlen(b) + 1);
    return true;
}

/* Where the ROF that closes the FOR at line at is. */
static bool find_rof(asm_state *s, size_t at, size_t *rof) {
    size_t depth = 1;
    size_t i = at + 1;
    while (i < s->nlines) {
        const char *p = s->lines[i];
        while (is_space(*p)) {
            p++;
        }
        /* skip labels to the opcode */
        for (;;) {
            size_t n = ident_len(p);
            if (n == 0) {
                break;
            }
            if (same_word(p, n, "FOR")) {
                depth++;
                break;
            }
            if (same_word(p, n, "ROF")) {
                depth--;
                if (depth == 0) {
                    *rof = i;
                    return true;
                }
                break;
            }
            if (find_op(p, n) >= 0 || is_pseudo(p, n)) {
                break;
            }
            p += n;
            while (is_space(*p)) {
                p++;
            }
        }
        i++;
    }
    return fail(s, "FOR without ROF", NULL);
}

static bool take_pending(asm_state *s, int32_t line) {
    size_t k = 0;
    while (k < s->npending) {
        if (!add_label(s, s->pending[k], strlen(s->pending[k]), line, NULL)) {
            return false;
        }
        k++;
    }
    s->npending = 0;
    return true;
}

static bool push_pending(asm_state *s, const char *name, size_t n) {
    if (s->npending >= 8 || n >= MARS_LABEL_LEN) {
        return fail(s, "too many labels on one instruction", NULL);
    }
    memcpy(s->pending[s->npending], name, n);
    s->pending[s->npending][n] = '\0';
    s->npending++;
    return true;
}

static bool pass1(asm_state *s) {
    mars_warrior *w = s->w;
    w->len = 0;
    size_t li = 0;
    while (li < s->nlines) {
        char line[MARS_LINE_MAX];
        line[0] = '\0';
        /* an EQU definition keeps its text as written, & and all, unless a
           FOR index is active and may be spelled into it; every other line
           is expanded first */
        const char *raw = s->lines[li];
        while (is_space(*raw)) {
            raw++;
        }
        bool equ_def = false;
        {
            size_t rn = ident_len(raw);
            const char *after = raw + rn;
            while (is_space(*after)) {
                after++;
            }
            if (rn > 0 && same_word(after, ident_len(after), "EQU")) {
                equ_def = true;
            }
        }
        if (equ_def && s->nloops == 0) {
            if (strlen(raw) >= sizeof(line)) {
                return fail(s, "line too long", NULL);
            }
            memcpy(line, raw, strlen(raw) + 1);
        } else if (!expand(s, raw, line, sizeof(line))) {
            return false;
        }
        const char *p = line;
        while (is_space(*p)) {
            p++;
        }
        if (*p == '\0') {
            li++;
            continue;
        }
        const char *labels[8];
        size_t label_len[8];
        size_t nl = 0;
        for (;;) {
            size_t n = ident_len(p);
            if (n == 0 || find_op(p, n) >= 0 || is_pseudo(p, n)) {
                break;
            }
            if (nl < 8) {
                labels[nl] = p;
                label_len[nl] = n;
                nl++;
            }
            p += n;
            while (is_space(*p)) {
                p++;
            }
        }
        size_t n = ident_len(p);
        if (n == 0) {
            if (*p != '\0') {
                return fail(s, "bad statement", line);
            }
            /* labels alone: they name the next instruction */
            size_t k = 0;
            while (k < nl) {
                if (!push_pending(s, labels[k], label_len[k])) {
                    return false;
                }
                k++;
            }
            li++;
            continue;
        }
        if (same_word(p, n, "EQU")) {
            if (nl != 1) {
                return fail(s, "EQU needs one label", line);
            }
            char text[MARS_EQU_TEXT];
            copy_text(text, sizeof(text), p + n);
            if (!add_label(s, labels[0], label_len[0], -1, text)) {
                return false;
            }
            li++;
            continue;
        }
        if (same_word(p, n, "FOR")) {
            /* labels before the last name the loop's first instruction; the
               last is the index. The count is an expression; zero or less
               skips the body whole, which is how pMARS comments blocks. */
            size_t k = 0;
            while (nl > 0 && k + 1 < nl) {
                if (!push_pending(s, labels[k], label_len[k])) {
                    return false;
                }
                k++;
            }
            char text[MARS_EQU_TEXT];
            copy_text(text, sizeof(text), p + n);
            int64_t count = 0;
            s->cur = (int32_t)w->len;
            if (!eval(s, text, &count)) {
                return false;
            }
            size_t rof = 0;
            if (!find_rof(s, li, &rof)) {
                return false;
            }
            if (count <= 0) {
                li = rof + 1;
                continue;
            }
            if (s->nloops >= MARS_LOOPS_MAX) {
                return fail(s, "FOR nested too deep", NULL);
            }
            loop *l = &s->loops[s->nloops];
            l->body = li + 1;
            l->rof = rof;
            l->i = 1;
            l->count = count;
            l->index[0] = '\0';
            if (nl > 0) {
                size_t in = label_len[nl - 1];
                if (in >= MARS_LABEL_LEN) {
                    return fail(s, "label too long", NULL);
                }
                memcpy(l->index, labels[nl - 1], in);
                l->index[in] = '\0';
            }
            s->nloops++;
            li++;
            continue;
        }
        if (same_word(p, n, "ROF")) {
            if (s->nloops == 0) {
                return fail(s, "ROF without FOR", NULL);
            }
            loop *l = &s->loops[s->nloops - 1];
            l->i++;
            if (l->i <= l->count) {
                li = l->body;
                continue;
            }
            s->nloops--;
            li++;
            continue;
        }
        if (same_word(p, n, "ORG")) {
            s->has_org = true;
            copy_text(s->org_text, sizeof(s->org_text), p + n);
            li++;
            continue;
        }
        if (same_word(p, n, "END")) {
            char text[MARS_EQU_TEXT];
            copy_text(text, sizeof(text), p + n);
            if (text[0] != '\0') {
                /* END start names the entry too; a bare END keeps ORG's */
                memcpy(s->org_text, text, strlen(text) + 1);
                s->has_org = true;
            }
            size_t k = 0;
            while (k < nl) {
                if (!push_pending(s, labels[k], label_len[k])) {
                    return false;
                }
                k++;
            }
            break;
        }
        if (same_word(p, n, "PIN")) {
            li++;
            continue; /* shared P-space between warriors: not here yet */
        }
        if (w->len >= s->p->len || w->len >= MARS_LEN_MAX) {
            return fail(s, "warrior too long", NULL);
        }
        if (!take_pending(s, (int32_t)w->len)) {
            return false;
        }
        size_t k = 0;
        while (k < nl) {
            if (!add_label(s, labels[k], label_len[k], (int32_t)w->len, NULL)) {
                return false;
            }
            k++;
        }
        memcpy(s->stmt[w->len], p, strlen(p) + 1);
        w->len++;
        li++;
    }
    if (s->nloops > 0) {
        return fail(s, "FOR without ROF", NULL);
    }
    /* labels after the last instruction point just past it */
    if (!take_pending(s, (int32_t)w->len)) {
        return false;
    }
    if (w->len == 0) {
        return fail(s, "no instructions", NULL);
    }
    return true;
}

static bool pass2(asm_state *s) {
    mars_warrior *w = s->w;
    uint32_t i = 0;
    while (i < w->len) {
        s->cur = (int32_t)i;
        if (!parse_instruction(s, s->stmt[i], &w->code[i])) {
            /* say which line: the message so far, then the statement */
            char why[MARS_ERR_MAX];
            memcpy(why, s->err, sizeof(why));
            char where[16];
            size_t k = 0;
            uint32_t v = i + 1;
            char digits[8];
            size_t d = sizeof(digits);
            do {
                d--;
                digits[d] = (char)('0' + (v % 10));
                v /= 10;
            } while (v > 0);
            memcpy(where + k, "line ", 5);
            k += 5;
            memcpy(where + k, digits + d, sizeof(digits) - d);
            k += sizeof(digits) - d;
            where[k] = '\0';
            (void)fail(s, where, why);
            return false;
        }
        i++;
    }
    s->cur = 0;
    w->start = 0;
    if (s->has_org) {
        int64_t v = 0;
        if (!eval(s, s->org_text, &v)) {
            return false;
        }
        if (v < 0 || v >= (int64_t)w->len) {
            return fail(s, "start outside the warrior", s->org_text);
        }
        w->start = (uint32_t)v;
    }
    return true;
}

static asm_state A; /* one assembly at a time; far too big for the stack */

bool mars_assemble(const uint8_t *src, size_t n, const mars_params *p, mars_warrior *w, char *err,
                   size_t err_cap) {
    asm_state *s = &A;
    memset(s, 0, sizeof(*s));
    s->p = p;
    s->w = w;
    s->err = err;
    s->err_cap = err_cap;
    err[0] = '\0';
    memset(w, 0, sizeof(*w));
    if (n >= sizeof(s->src)) {
        return fail(s, "source too long", NULL);
    }
    memcpy(s->src, src, n);
    s->src[n] = '\0';
    /* lines: info comments read here, everything before ;redcode dropped */
    char *cur = s->src;
    while (*cur != '\0') {
        char *eol = strchr(cur, '\n');
        if (eol != NULL) {
            *eol = '\0';
        }
        const char *t = cur;
        while (is_space(*t)) {
            t++;
        }
        if (t[0] == ';') {
            if (strncmp(t, ";redcode", 8) == 0) {
                s->nlines = 0;
                w->name[0] = '\0';
                w->author[0] = '\0';
            } else if (strncmp(t, ";name", 5) == 0) {
                copy_text(w->name, sizeof(w->name), t + 5);
            } else if (strncmp(t, ";author", 7) == 0) {
                copy_text(w->author, sizeof(w->author), t + 7);
            } else if (strncmp(t, ";assert", 7) == 0) {
                int64_t v = 0;
                char text[MARS_EQU_TEXT];
                copy_text(text, sizeof(text), t + 7);
                strip_comment(text); /* ;assert x > 1    ;why */
                if (!eval(s, text, &v)) {
                    return false;
                }
                if (v == 0) {
                    return fail(s, "assertion failed", text);
                }
            }
        } else {
            strip_comment(cur);
            if (s->nlines >= LINES_MAX) {
                return fail(s, "too many lines", NULL);
            }
            s->lines[s->nlines] = cur;
            s->nlines++;
        }
        if (eol == NULL) {
            break;
        }
        cur = eol + 1;
    }
    if (w->name[0] == '\0') {
        memcpy(w->name, "Unknown", 8);
    }
    if (w->author[0] == '\0') {
        memcpy(w->author, "Anonymous", 10);
    }
    if (!pass1(s)) {
        return false;
    }
    return pass2(s);
}

size_t mars_format(const mars_cell *c, uint32_t core, char *buf, size_t cap) {
    /* "MOV.I  $   -82, $     3": values past half the core read negative */
    char tmp[48];
    size_t k = 0;
    const char *op = c->op < OP_COUNT ? op_names[c->op] : "???";
    const char *mod = c->mod < 7 ? mod_names[c->mod] : "?";
    memcpy(tmp + k, op, 3);
    k += 3;
    tmp[k++] = '.';
    size_t ml = strlen(mod);
    memcpy(tmp + k, mod, ml);
    k += ml;
    while (k < 7) {
        tmp[k++] = ' ';
    }
    const int32_t vals[2] = {c->a, c->b};
    const uint8_t modes[2] = {c->am, c->bm};
    size_t i = 0;
    while (i < 2) {
        int32_t v = vals[i];
        if (v > (int32_t)(core / 2)) {
            v -= (int32_t)core;
        }
        tmp[k++] = mode_chars[modes[i] < 8 ? modes[i] : 1];
        tmp[k++] = ' ';
        char digits[12];
        size_t d = sizeof(digits);
        uint32_t u = v < 0 ? (uint32_t)(-v) : (uint32_t)v;
        do {
            d--;
            digits[d] = (char)('0' + (u % 10));
            u /= 10;
        } while (u > 0);
        if (v < 0) {
            d--;
            digits[d] = '-';
        }
        size_t dn = sizeof(digits) - d;
        size_t pad = dn < 5 ? 5 - dn : 0;
        while (pad > 0) {
            tmp[k++] = ' ';
            pad--;
        }
        memcpy(tmp + k, digits + d, dn);
        k += dn;
        if (i == 0) {
            tmp[k++] = ',';
            tmp[k++] = ' ';
        }
        i++;
    }
    if (k >= cap) {
        k = cap - 1;
    }
    memcpy(buf, tmp, k);
    buf[k] = '\0';
    return k;
}

/* ---- the simulator: ICWS'94, in-register evaluation ---- */

static uint16_t wrap(const mars *m, int64_t v) {
    int64_t c = m->p.core;
    v %= c;
    if (v < 0) {
        v += c;
    }
    return (uint16_t)v;
}

static void queue_push(mars *m, uint32_t w, uint16_t pc) {
    if (m->qlen[w] >= m->p.procs) {
        return; /* the standard drops the split, the process count is the limit */
    }
    uint32_t at = (m->qhead[w] + m->qlen[w]) % MARS_PROCS_MAX;
    m->q[w][at] = pc;
    m->qlen[w]++;
}

static uint16_t queue_pop(mars *m, uint32_t w) {
    uint16_t pc = m->q[w][m->qhead[w]];
    m->qhead[w] = (m->qhead[w] + 1) % MARS_PROCS_MAX;
    m->qlen[w]--;
    return pc;
}

void mars_match_begin(mars *m, const mars_params *p) {
    memset(m->ps, 0, sizeof(m->ps));
    uint32_t i = 0;
    while (i < MARS_WARRIORS_MAX) {
        m->ps[i][0] = (uint16_t)(p->core - 1); /* -1: no round played yet */
        i++;
    }
}

void mars_load(mars *m, const mars_params *p, const mars_warrior *const *w, const uint32_t *pos,
               uint32_t n) {
    m->p = *p;
    if (m->p.core > MARS_CORE_MAX) {
        m->p.core = MARS_CORE_MAX;
    }
    if (m->p.procs > MARS_PROCS_MAX) {
        m->p.procs = MARS_PROCS_MAX;
    }
    memset(m->core, 0, sizeof(m->core[0]) * m->p.core); /* DAT.F #0, #0 */
    memset(m->owner, 0xFF, sizeof(m->owner));
    memset(m->stamp, 0, sizeof(m->stamp));
    m->steps = 0;
    m->nwarriors = n > MARS_WARRIORS_MAX ? MARS_WARRIORS_MAX : n;
    m->nalive = 0;
    m->cycle = 0;
    m->turn = 0;
    m->over = false;
    m->winner = -1;
    m->last_pc = 0;
    uint32_t i = 0;
    while (i < m->nwarriors) {
        m->w[i] = w[i];
        m->pos[i] = pos[i] % m->p.core;
        uint32_t k = 0;
        while (k < w[i]->len) {
            uint32_t at = (m->pos[i] + k) % m->p.core;
            m->core[at] = w[i]->code[k];
            m->owner[at] = (uint8_t)i;
            k++;
        }
        m->pc_of[i] = (uint16_t)((m->pos[i] + w[i]->start) % m->p.core);
        m->qhead[i] = 0;
        m->qlen[i] = 0;
        queue_push(m, i, (uint16_t)((m->pos[i] + w[i]->start) % m->p.core));
        m->alive[i] = true;
        m->nalive++;
        i++;
    }
}

static void touch(mars *m, uint32_t at, uint32_t who) {
    m->owner[at] = (uint8_t)who;
    m->stamp[at] = m->steps;
}

/* A field of a cell, chosen by which of the two an addressing mode names. */
static uint16_t *field(mars_cell *c, bool a_field) {
    return a_field ? &c->a : &c->b;
}

/* Resolves one operand the '94 way: the read pointer, the write pointer,
   and the instruction it points at copied before anything else moves.
   Pre-decrement happens before the copy, post-increment after it. */
static void resolve(mars *m, uint16_t pc, uint8_t mode, uint16_t number, uint32_t who, uint16_t *rp,
                    uint16_t *wp, mars_cell *ir) {
    uint32_t core = m->p.core;
    if (mode == AM_IMM) {
        *rp = 0;
        *wp = 0;
        *ir = m->core[pc];
        return;
    }
    uint16_t r = number;
    uint16_t w = number;
    uint16_t pip = 0;
    bool post = false;
    if (mode != AM_DIR) {
        bool a_side = false;
        if (mode == AM_AIND || mode == AM_APRE || mode == AM_APOST) {
            a_side = true;
        }
        uint16_t at = (uint16_t)((pc + r) % core);
        mars_cell *via = &m->core[at];
        if (mode == AM_BPRE || mode == AM_APRE) {
            uint16_t *f = field(via, a_side);
            *f = (uint16_t)((*f + core - 1) % core);
            touch(m, at, who);
        }
        if (mode == AM_BPOST || mode == AM_APOST) {
            pip = at;
            post = true;
        }
        r = (uint16_t)((r + *field(via, a_side)) % core);
        w = r;
    }
    *rp = r;
    *wp = w;
    *ir = m->core[(pc + r) % core];
    if (post) {
        bool a_side = mode == AM_APOST;
        uint16_t *f = field(&m->core[pip], a_side);
        *f = (uint16_t)((*f + 1) % core);
        touch(m, pip, who);
    }
}

typedef struct {
    uint16_t a;
    uint16_t b;
} pair;

/* The operands an arithmetic instruction combines, by modifier: what is
   read from A and B, and which fields of the target take the result. */
static void arith(mars *m, uint8_t op, uint8_t mod, const mars_cell *ira, const mars_cell *irb,
                  mars_cell *dst, uint32_t who, uint16_t at, bool *died) {
    pair src = {0, 0};
    pair cur = {0, 0};
    uint16_t *out_a = NULL;
    uint16_t *out_b = NULL;
    switch (mod) {
    case MOD_A:
        src.a = ira->a;
        cur.a = irb->a;
        out_a = &dst->a;
        break;
    case MOD_B:
        src.a = ira->b;
        cur.a = irb->b;
        out_a = &dst->b;
        break;
    case MOD_AB:
        src.a = ira->a;
        cur.a = irb->b;
        out_a = &dst->b;
        break;
    case MOD_BA:
        src.a = ira->b;
        cur.a = irb->a;
        out_a = &dst->a;
        break;
    case MOD_X:
        src.a = ira->b;
        cur.a = irb->a;
        out_a = &dst->a;
        src.b = ira->a;
        cur.b = irb->b;
        out_b = &dst->b;
        break;
    default: /* F and I */
        src.a = ira->a;
        cur.a = irb->a;
        out_a = &dst->a;
        src.b = ira->b;
        cur.b = irb->b;
        out_b = &dst->b;
        break;
    }
    uint16_t *outs[2] = {out_a, out_b};
    const uint16_t srcs[2] = {src.a, src.b};
    const uint16_t curs[2] = {cur.a, cur.b};
    size_t i = 0;
    while (i < 2) {
        if (outs[i] == NULL) {
            i++;
            continue;
        }
        int64_t s = srcs[i];
        int64_t c = curs[i];
        int64_t r = 0;
        switch (op) {
        case OP_ADD:
            r = c + s;
            break;
        case OP_SUB:
            r = c - s;
            break;
        case OP_MUL:
            r = c * s;
            break;
        case OP_DIV:
        case OP_MOD:
            if (s == 0) {
                *died = true; /* the other half still goes, then the process ends */
                i++;
                continue;
            }
            r = op == OP_DIV ? c / s : c % s;
            break;
        default:
            break;
        }
        *outs[i] = wrap(m, r);
        i++;
    }
    touch(m, at, who);
}

/* Compares by modifier: true when equal (SEQ), the same test SLT and SNE
   read their own way. */
static bool same(uint8_t mod, const mars_cell *ira, const mars_cell *irb) {
    switch (mod) {
    case MOD_A:
        return ira->a == irb->a;
    case MOD_B:
        return ira->b == irb->b;
    case MOD_AB:
        return ira->a == irb->b;
    case MOD_BA:
        return ira->b == irb->a;
    case MOD_F:
        if (ira->a == irb->a && ira->b == irb->b) {
            return true;
        }
        return false;
    case MOD_X:
        if (ira->a == irb->b && ira->b == irb->a) {
            return true;
        }
        return false;
    default: /* I */
        if (ira->op == irb->op && ira->mod == irb->mod && ira->am == irb->am &&
            ira->bm == irb->bm && ira->a == irb->a && ira->b == irb->b) {
            return true;
        }
        return false;
    }
}

static bool less(uint8_t mod, const mars_cell *ira, const mars_cell *irb) {
    switch (mod) {
    case MOD_A:
        return ira->a < irb->a;
    case MOD_B:
        return ira->b < irb->b;
    case MOD_AB:
        return ira->a < irb->b;
    case MOD_BA:
        return ira->b < irb->a;
    case MOD_X:
        if (ira->a < irb->b && ira->b < irb->a) {
            return true;
        }
        return false;
    default: /* F and I */
        if (ira->a < irb->a && ira->b < irb->b) {
            return true;
        }
        return false;
    }
}

/* Is the B operand zero, by modifier: both halves for F, X and I. */
static bool zero_b(uint8_t mod, const mars_cell *irb) {
    switch (mod) {
    case MOD_A:
    case MOD_BA:
        return irb->a == 0;
    case MOD_B:
    case MOD_AB:
        return irb->b == 0;
    default:
        if (irb->a == 0 && irb->b == 0) {
            return true;
        }
        return false;
    }
}

static void execute(mars *m, uint32_t who, uint16_t pc) {
    uint32_t core = m->p.core;
    mars_cell ir = m->core[pc];
    uint16_t rpa = 0;
    uint16_t wpa = 0;
    uint16_t rpb = 0;
    uint16_t wpb = 0;
    mars_cell ira;
    mars_cell irb;
    resolve(m, pc, ir.am, ir.a, who, &rpa, &wpa, &ira);
    resolve(m, pc, ir.bm, ir.b, who, &rpb, &wpb, &irb);
    (void)wpa;
    uint16_t next = (uint16_t)((pc + 1) % core);
    uint16_t at_b = (uint16_t)((pc + wpb) % core);
    uint16_t to_a = (uint16_t)((pc + rpa) % core);
    mars_cell *dst = &m->core[at_b];
    switch (ir.op) {
    case OP_DAT:
        return; /* the process ends */
    case OP_MOV:
        switch (ir.mod) {
        case MOD_A:
            dst->a = ira.a;
            break;
        case MOD_B:
            dst->b = ira.b;
            break;
        case MOD_AB:
            dst->b = ira.a;
            break;
        case MOD_BA:
            dst->a = ira.b;
            break;
        case MOD_F:
            dst->a = ira.a;
            dst->b = ira.b;
            break;
        case MOD_X:
            dst->a = ira.b;
            dst->b = ira.a;
            break;
        default:
            *dst = ira;
            break;
        }
        touch(m, at_b, who);
        queue_push(m, who, next);
        return;
    case OP_ADD:
    case OP_SUB:
    case OP_MUL:
    case OP_DIV:
    case OP_MOD: {
        bool died = false;
        arith(m, ir.op, ir.mod, &ira, &irb, dst, who, at_b, &died);
        if (!died) {
            queue_push(m, who, next);
        }
        return;
    }
    case OP_JMP:
        queue_push(m, who, to_a);
        return;
    case OP_JMZ:
        queue_push(m, who, zero_b(ir.mod, &irb) ? to_a : next);
        return;
    case OP_JMN:
        queue_push(m, who, zero_b(ir.mod, &irb) ? next : to_a);
        return;
    case OP_DJN: {
        /* decrement in the core and in the copy, then test the copy */
        bool a_half = false;
        bool b_half = false;
        if (ir.mod == MOD_A || ir.mod == MOD_BA) {
            a_half = true;
        }
        if (ir.mod == MOD_B || ir.mod == MOD_AB) {
            b_half = true;
        }
        if (a_half || !b_half) {
            dst->a = (uint16_t)((dst->a + core - 1) % core);
            irb.a = (uint16_t)((irb.a + core - 1) % core);
        }
        if (b_half || !a_half) {
            dst->b = (uint16_t)((dst->b + core - 1) % core);
            irb.b = (uint16_t)((irb.b + core - 1) % core);
        }
        touch(m, at_b, who);
        queue_push(m, who, zero_b(ir.mod, &irb) ? next : to_a);
        return;
    }
    case OP_SPL:
        queue_push(m, who, next);
        queue_push(m, who, to_a);
        return;
    case OP_SLT:
        queue_push(m, who, less(ir.mod, &ira, &irb) ? (uint16_t)((pc + 2) % core) : next);
        return;
    case OP_SEQ:
    case OP_CMP:
        queue_push(m, who, same(ir.mod, &ira, &irb) ? (uint16_t)((pc + 2) % core) : next);
        return;
    case OP_SNE:
        queue_push(m, who, same(ir.mod, &ira, &irb) ? next : (uint16_t)((pc + 2) % core));
        return;
    case OP_LDP:
    case OP_STP: {
        /* P-space: A names the value (or the cell to read), B the target;
           .A and .AB take A's A-field, the rest its B-field, and .F .X .I
           behave as .B, the way pMARS has it */
        uint32_t size = core / 16;
        if (size > MARS_PSPACE_MAX) {
            size = MARS_PSPACE_MAX;
        }
        if (size == 0) {
            size = 1;
        }
        bool a_from_a = false;
        if (ir.mod == MOD_A || ir.mod == MOD_AB) {
            a_from_a = true;
        }
        bool b_to_a = false;
        if (ir.mod == MOD_A || ir.mod == MOD_BA) {
            b_to_a = true;
        }
        uint16_t aval = a_from_a ? ira.a : ira.b;
        if (ir.op == OP_LDP) {
            uint16_t v = m->ps[who][aval % size];
            if (b_to_a) {
                dst->a = v;
            } else {
                dst->b = v;
            }
            touch(m, at_b, who);
        } else {
            uint16_t idx = b_to_a ? irb.a : irb.b;
            m->ps[who][idx % size] = aval;
        }
        queue_push(m, who, next);
        return;
    }
    default: /* NOP */
        queue_push(m, who, next);
        return;
    }
}

bool mars_step(mars *m) {
    uint32_t n = m->nwarriors;
    if (m->over || n == 0 || n > MARS_WARRIORS_MAX) {
        m->over = true; /* nothing loaded: nothing to run */
        return false;
    }
    if (m->turn >= n) {
        m->turn = 0;
    }
    /* the next living warrior in turn */
    uint32_t tries = 0;
    while (tries < n && !m->alive[m->turn]) {
        m->turn = (m->turn + 1) % n;
        if (m->turn == 0) {
            m->cycle++; /* past a dead last warrior is a cycle too, or a tie never ends */
        }
        tries++;
    }
    uint32_t who = m->turn;
    uint16_t pc = queue_pop(m, who);
    m->last_pc = pc;
    m->pc_of[who] = pc;
    m->steps++;
    execute(m, who, pc);
    if (m->qlen[who] == 0) {
        m->alive[who] = false;
        m->nalive--;
    }
    m->turn = (m->turn + 1) % n;
    if (m->turn == 0) {
        m->cycle++;
    }
    if (n > 1 && m->nalive <= 1) {
        m->over = true;
        uint32_t i = 0;
        while (i < n) {
            if (m->alive[i]) {
                m->winner = (int)i;
            }
            i++;
        }
    } else if (n == 1 && m->nalive == 0) {
        m->over = true;
    } else if (m->cycle >= m->p.cycles) {
        m->over = true; /* a tie; alone, the survivor is its own winner */
        if (n == 1) {
            m->winner = 0;
        }
    }
    if (!m->over) {
        return true;
    }
    /* what each warrior will find in P-space cell 0 next round */
    uint32_t i = 0;
    while (i < n) {
        m->ps[i][0] = m->alive[i] ? (uint16_t)m->nalive : 0;
        i++;
    }
    return false;
}

int mars_run(mars *m) {
    while (mars_step(m)) {
    }
    return m->winner;
}
