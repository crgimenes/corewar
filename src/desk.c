#include "desk.h"

#include <dirent.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

#include "library.h"
#include "tty.h"

#ifndef COREWAR_VERSION
#define COREWAR_VERSION "dev"
#endif

enum { FILE_CAP = 256 * 1024 };

desk D;

static const cw_file *carried(const char *name) {
    for (size_t i = 0; i < cw_library_n; i++) {
        if (strcmp(cw_library[i].name, name) == 0) {
            return &cw_library[i];
        }
    }
    return NULL;
}

static void copy(char *dst, size_t cap, const char *s) {
    size_t n = strlen(s);
    if (n >= cap) {
        n = cap - 1;
    }
    memcpy(dst, s, n);
    dst[n] = '\0';
}

/* A file of the directory first, so a warrior of one's own shadows a
   classic of the same name; then the ones the binary carries. */
static bool read_warrior(void *user, const char *path, const uint8_t **data, size_t *len, char *why,
                         size_t cap) {
    (void)user;
    static uint8_t buf[FILE_CAP];
    FILE *f = fopen(path, "rb");
    if (f == NULL) {
        const cw_file *c = carried(path);
        if (c != NULL) {
            *data = c->data;
            *len = c->len;
            return true;
        }
        copy(why, cap, strerror(errno));
        return false;
    }
    size_t n = fread(buf, 1, sizeof(buf), f);
    (void)fclose(f);
    if (n == sizeof(buf)) {
        copy(why, cap, "File too large");
        return false;
    }
    *data = buf;
    *len = n;
    return true;
}

static void leave(void *user, const char *note) {
    (void)user;
    (void)note;
    (void)term_app_leave(&D.a.t);
    if (D.direct) {
        D.a.done = true;
        (void)term_app_leave(&D.a.t); /* the pick under it, never shown */
        return;
    }
    app_repaint(&D.a);
}

/* ---- the pager over the pick ---- */

static void pager_on_key(void *ctx, uint32_t cp) {
    (void)ctx;
    if (pager_key(&D.pg, &D.a.t, cp)) {
        return;
    }
    pager_hide(&D.a.t);
    (void)term_app_leave(&D.a.t);
    app_repaint(&D.a);
}

static void pager_on_resize(void *ctx) {
    (void)ctx;
    pager_resize(&D.pg, &D.a.t);
}

static void pager_on_tick(void *ctx, uint32_t ms) {
    (void)ctx;
    (void)ms;
}

static const term_app pager_app = {
    .on_key = pager_on_key,
    .on_resize = pager_on_resize,
    .on_tick = pager_on_tick,
};

static void to_pager(void *user, const uint8_t *data, size_t n) {
    pager_load(user, data, n);
}

/* ---- the pick's builtins past the list ---- */

static arena *arena_of(filo_ctx *ctx) {
    (void)ctx;
    return &D.ar;
}

static bool red(const char *name) {
    size_t n = strlen(name);
    if (n <= 4) {
        return false;
    }
    return strcmp(name + n - 4, ".red") == 0;
}

static int entry(filo_ctx *ctx, const char *path, const char *tag, filo_value *out) {
    filo_value parts[2] = {filo_cstring(path), filo_cstring(tag)};
    return filo_tuple(ctx, parts, 2, out);
}

/* (cw-library): the .red files here, then the classics the binary
   carries that no file here shadows, as (path tag). */
static int b_cw_library(filo_ctx *ctx, const filo_value *a, uint32_t n, filo_value *out) {
    (void)a;
    if (n != 0) {
        return filo_fail(ctx, "cw-library takes no argument");
    }
    enum { MAX = 64 };
    static char names[MAX][256];
    filo_value items[MAX + 16];
    uint32_t k = 0;
    DIR *d = opendir(".");
    if (d != NULL) {
        const struct dirent *e = readdir(d);
        while (e != NULL && k < MAX) {
            if (red(e->d_name)) {
                copy(names[k], sizeof(names[k]), e->d_name);
                if (entry(ctx, names[k], "here", &items[k]) != FILO_OK) {
                    (void)closedir(d);
                    return FILO_ERR;
                }
                k++;
            }
            e = readdir(d);
        }
        (void)closedir(d);
    }
    uint32_t here = k;
    for (size_t i = 0; i < cw_library_n && k < MAX + 16; i++) {
        bool shadowed = false;
        for (uint32_t j = 0; j < here; j++) {
            if (strcmp(names[j], cw_library[i].name) == 0) {
                shadowed = true;
            }
        }
        if (shadowed) {
            continue;
        }
        if (entry(ctx, cw_library[i].name, "classic", &items[k]) != FILO_OK) {
            return FILO_ERR;
        }
        k++;
    }
    return filo_list(ctx, items, k, out);
}

/* (cw-fight): the arena over the pick; "" when it opened, else why. */
static int b_cw_fight(filo_ctx *ctx, const filo_value *a, uint32_t n, filo_value *out) {
    (void)a;
    if (n != 0) {
        return filo_fail(ctx, "cw-fight takes no argument");
    }
    static char why[CW_NOTE_MAX];
    if (!arena_ready(&D.ar, (uint32_t)tty_seed(), why, sizeof(why))) {
        *out = filo_cstring(why);
        return FILO_OK;
    }
    if (!term_app_enter(&D.a.t, &arena_app, &D.ar)) {
        *out = filo_cstring("no room for the arena on the terminal");
        return FILO_OK;
    }
    arena_show(&D.ar);
    *out = filo_cstring("");
    return FILO_OK;
}

/* (cw-manual): the Redcode manual in the pager; "" when it opened. */
static int b_cw_manual(filo_ctx *ctx, const filo_value *a, uint32_t n, filo_value *out) {
    (void)a;
    if (n != 0) {
        return filo_fail(ctx, "cw-manual takes no argument");
    }
    pager_reset(&D.pg, "redcode.md");
    D.render.emit = to_pager;
    D.render.user = &D.pg;
    D.render.base = "";
    D.render.slug = NULL;
    md_reset(&D.render, true);
    md_feed(&D.render, cw_manual, cw_manual_len);
    md_end(&D.render);
    if (!term_app_enter(&D.a.t, &pager_app, NULL)) {
        *out = filo_cstring("no room for the manual on the terminal");
        return FILO_OK;
    }
    pager_show(&D.pg, &D.a.t);
    *out = filo_cstring("");
    return FILO_OK;
}

/* A classic the binary carries, written out here so it can be edited. */
static bool write_out(const cw_file *c) {
    FILE *f = fopen(c->name, "wb");
    if (f == NULL) {
        return false;
    }
    bool ok = fwrite(c->data, 1, c->len, f) == c->len;
    if (fclose(f) != 0) {
        return false;
    }
    return ok;
}

/* (cw-edit path): the warrior in $VISUAL, $EDITOR or vi, the terminal
   lent to it until it ends; "" when it ran, else why. */
static int b_cw_edit(filo_ctx *ctx, const filo_value *a, uint32_t n, filo_value *out) {
    if (n != 1 || a[0].kind != FILO_STRING || a[0].u.str.len == 0 ||
        a[0].u.str.len >= CW_PATH_MAX) {
        return filo_fail(ctx, "cw-edit expects a path");
    }
    char path[CW_PATH_MAX];
    memcpy(path, a[0].u.str.ptr, a[0].u.str.len);
    path[a[0].u.str.len] = '\0';
    struct stat st;
    const cw_file *c = carried(path);
    if (stat(path, &st) != 0 && c != NULL && !write_out(c)) {
        *out = filo_cstring(strerror(errno));
        return FILO_OK;
    }
    const char *editor = getenv("VISUAL");
    if (editor == NULL || editor[0] == '\0') {
        editor = getenv("EDITOR");
    }
    if (editor == NULL || editor[0] == '\0') {
        editor = "vi";
    }
    tty_suspend();
    pid_t pid = fork();
    if (pid == 0) {
        /* the person's own $EDITOR, run as them: nothing to sanitize */
        // NOLINTNEXTLINE(clang-analyzer-optin.taint.GenericTaint)
        execlp(editor, editor, path, (char *)NULL);
        _exit(127);
    }
    int status = 0;
    if (pid > 0) {
        (void)waitpid(pid, &status, 0);
    }
    tty_resume(&D.a);
    if (pid < 0 || !WIFEXITED(status) || WEXITSTATUS(status) == 127) {
        *out = filo_cstring("the editor did not run");
        return FILO_OK;
    }
    *out = filo_cstring("");
    return FILO_OK;
}

static bool extend(app *a, filo_ctx *ctx) {
    (void)a;
    if (!arena_register(ctx, arena_of)) {
        return false;
    }
    (void)filo_register_builtin(ctx, "cw-library", b_cw_library);
    (void)filo_register_builtin(ctx, "cw-fight", b_cw_fight);
    (void)filo_register_builtin(ctx, "cw-manual", b_cw_manual);
    return filo_register_builtin(ctx, "cw-edit", b_cw_edit) == FILO_OK;
}

const app_spec app_program = {"corewar", COREWAR_VERSION, false, 0, 0, 0, extend};

void desk_init(void) {
    arena_host host = {NULL, read_warrior, leave, {"imp.red", "dwarf.red"}};
    arena_init(&D.ar, &host, &D.a.t, &D.a.target, &D.a.shown);
}
