/* harness_daily.h - the shared harness behind test_daily.c and test_cli.c.
 *
 * Both suites include RANK.C as a translation unit and then include this file,
 * so everything here may use RankTime, RankRow and the test seams.  It is a
 * header rather than a third .c file because run-daily.sh builds each suite as
 * one translation unit; it is shared rather than copied because the two suites
 * need the same environment, the same snapshot set and the same stdout capture.
 *
 * It does NOT duplicate test_rank.c's harness by choice: test_rank.c is frozen
 * (its 61 cases are the green baseline this work must not disturb), so the file
 * helpers are restated here rather than lifted out of it.
 *
 * Every helper is static, and not every suite uses every helper, so each one
 * carries the unused attribution: -Wall includes -Wunused-function and the
 * build is -Werror.
 *
 * Case names come from .work/plans/nx93-daily-reset/test-design.md, which is
 * the sole authority for every oracle.  No oracle here was read back out of an
 * implementation.
 */
#ifndef NX93RANK_HARNESS_DAILY_H
#define NX93RANK_HARNESS_DAILY_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <dirent.h>
#include <unistd.h>

#if defined(__GNUC__)
#define HUNUSED __attribute__((unused))
#else
#define HUNUSED
#endif

#define TPATH 1024

/* ------------------------------------------------------------------ 表明 */

static const char *g_case = "(none)";
static const char *g_filter;
static char g_scratch[TPATH];
static char g_snapdir[TPATH];
static char g_io[TPATH];          /* reusable scratch file for FILE* fixtures */
static char g_cap_path[TPATH];    /* where captured stdout lands */
static int g_case_failed;
static int g_ran;
static int g_failed;
static long g_asserts;

static HUNUSED void note(const char *file, int line, const char *what)
{
    fprintf(stderr, "FAIL  %s  [%s]  (%s:%d)\n", g_case, what, file, line);
    g_case_failed = 1;
}

static HUNUSED int is_true(const char *file, int line, const char *what, int cond)
{
    g_asserts++;
    if (cond) {
        return 1;
    }
    note(file, line, what);
    return 0;
}

static HUNUSED int eq_long(const char *file, int line, const char *what,
                           long got, long want)
{
    g_asserts++;
    if (got == want) {
        return 1;
    }
    fprintf(stderr, "FAIL  %s  [%s]  (%s:%d) got %ld want %ld\n",
            g_case, what, file, line, got, want);
    g_case_failed = 1;
    return 0;
}

/* Print a byte string readably: printable ASCII as-is, everything else \xXX. */
static HUNUSED void show(const char *p, size_t n)
{
    size_t i;
    fputc('"', stderr);
    for (i = 0; i < n; i++) {
        unsigned char c = (unsigned char)p[i];
        if (c >= 0x20 && c < 0x7f && c != '"' && c != '\\') {
            fputc((int)c, stderr);
        } else {
            fprintf(stderr, "\\x%02x", (unsigned)c);
        }
    }
    fputc('"', stderr);
}

static HUNUSED int eq_mem(const char *file, int line, const char *what,
                          const void *got, size_t glen,
                          const void *want, size_t wlen)
{
    g_asserts++;
    if (glen == wlen && (glen == 0 || memcmp(got, want, glen) == 0)) {
        return 1;
    }
    fprintf(stderr, "FAIL  %s  [%s]  (%s:%d)\n      got  (%lu) ",
            g_case, what, file, line, (unsigned long)glen);
    show((const char *)got, glen > 96 ? 96 : glen);
    fprintf(stderr, "\n      want (%lu) ", (unsigned long)wlen);
    show((const char *)want, wlen > 96 ? 96 : wlen);
    fputc('\n', stderr);
    g_case_failed = 1;
    return 0;
}

static HUNUSED int eq_str(const char *file, int line, const char *what,
                          const char *got, const char *want)
{
    return eq_mem(file, line, what, got, strlen(got), want, strlen(want));
}

#define OK(what, cond)   is_true(__FILE__, __LINE__, (what), (cond))
#define EQL(what, g, w)  eq_long(__FILE__, __LINE__, (what), (long)(g), (long)(w))
#define EQS(what, g, w)  eq_str(__FILE__, __LINE__, (what), (g), (w))
#define EQM(what, g, gl, w, wl) \
    eq_mem(__FILE__, __LINE__, (what), (g), (gl), (w), (wl))

/* A label with an index in it, for table-driven cases. */
static HUNUSED const char *lbl(const char *stem, long i)
{
    static char buf[4][192];
    static int turn;
    char *p = buf[turn++ & 3];
    snprintf(p, sizeof buf[0], "%s #%ld", stem, i);
    return p;
}

/* A label built from two strings, for helpers that loop over file names. */
static HUNUSED const char *lbl_s(const char *stem, const char *tail)
{
    static char buf[4][192];
    static int turn;
    char *p = buf[turn++ & 3];
    snprintf(p, sizeof buf[0], "%s %s", stem, tail);
    return p;
}

/* ------------------------------------------------------------------ ファイル */

static HUNUSED void die(const char *what)
{
    fprintf(stderr, "harness failure: %s: %s: %s\n", g_case, what, strerror(errno));
    exit(2);
}

static HUNUSED void write_bytes(const char *path, const void *data, size_t len)
{
    FILE *fp = fopen(path, "wb");
    if (fp == NULL) {
        die(path);
    }
    if (len != 0 && fwrite(data, 1, len, fp) != len) {
        die(path);
    }
    if (fclose(fp) != 0) {
        die(path);
    }
}

static HUNUSED void write_text(const char *path, const char *s)
{
    write_bytes(path, s, strlen(s));
}

/* Read a whole file into a fresh malloc'd buffer, NUL-terminated for
 * convenience.  The test may use the heap freely; S4 constrains RANK.C only. */
static HUNUSED char *read_file(const char *path, size_t *len)
{
    FILE *fp;
    char *buf;
    long n;
    size_t got;

    fp = fopen(path, "rb");
    if (fp == NULL) {
        return NULL;
    }
    if (fseek(fp, 0L, SEEK_END) != 0) {
        die(path);
    }
    n = ftell(fp);
    if (n < 0) {
        die(path);
    }
    rewind(fp);
    buf = (char *)malloc((size_t)n + 1u);
    if (buf == NULL) {
        die("malloc");
    }
    got = fread(buf, 1, (size_t)n, fp);
    if (got != (size_t)n) {
        die(path);
    }
    buf[n] = '\0';
    if (fclose(fp) != 0) {
        die(path);
    }
    *len = (size_t)n;
    return buf;
}

static HUNUSED int file_exists(const char *path)
{
    struct stat st;
    return stat(path, &st) == 0;
}

static HUNUSED long file_size(const char *path)
{
    struct stat st;
    if (stat(path, &st) != 0) {
        return -1L;
    }
    return (long)st.st_size;
}

/* Byte comparison that streams, so a 2 MiB ledger costs nothing.  Two absent
 * files count as equal; one absent does not. */
static HUNUSED int files_equal(const char *a, const char *b)
{
    FILE *fa = fopen(a, "rb");
    FILE *fb = fopen(b, "rb");
    int same = 1;

    if (fa == NULL || fb == NULL) {
        same = (fa == NULL && fb == NULL);
    } else {
        for (;;) {
            char ba[8192];
            char bb[8192];
            size_t na = fread(ba, 1, sizeof ba, fa);
            size_t nb = fread(bb, 1, sizeof bb, fb);
            if (na != nb || (na != 0 && memcmp(ba, bb, na) != 0)) {
                same = 0;
                break;
            }
            if (na == 0) {
                break;
            }
        }
    }
    if (fa != NULL) {
        (void)fclose(fa);
    }
    if (fb != NULL) {
        (void)fclose(fb);
    }
    return same;
}

static HUNUSED void copy_file(const char *from, const char *to)
{
    FILE *fi = fopen(from, "rb");
    FILE *fo;

    if (fi == NULL) {
        (void)unlink(to);   /* absent stays absent, so snapshots round-trip */
        return;
    }
    fo = fopen(to, "wb");
    if (fo == NULL) {
        die(to);
    }
    for (;;) {
        char b[8192];
        size_t n = fread(b, 1, sizeof b, fi);
        if (n == 0) {
            break;
        }
        if (fwrite(b, 1, n, fo) != n) {
            die(to);
        }
    }
    if (fclose(fi) != 0 || fclose(fo) != 0) {
        die(to);
    }
}

static HUNUSED void rm_tree(const char *path)
{
    struct stat st;

    if (lstat(path, &st) != 0) {
        return;
    }
    if (S_ISDIR(st.st_mode)) {
        DIR *d;
        (void)chmod(path, 0700);
        d = opendir(path);
        if (d != NULL) {
            struct dirent *e;
            while ((e = readdir(d)) != NULL) {
                char child[TPATH];
                if (strcmp(e->d_name, ".") == 0 || strcmp(e->d_name, "..") == 0) {
                    continue;
                }
                snprintf(child, sizeof child, "%s/%s", path, e->d_name);
                rm_tree(child);
            }
            (void)closedir(d);
        }
        (void)rmdir(path);
    } else {
        (void)chmod(path, 0600);
        (void)unlink(path);
    }
}

/* A fresh, empty directory for one case. */
static HUNUSED void case_dir(const char *tag, char *out)
{
    snprintf(out, TPATH, "%s/%s", g_scratch, tag);
    rm_tree(out);
    if (mkdir(out, 0755) != 0) {
        die(out);
    }
}

static HUNUSED void snap(const char *path, const char *tag)
{
    char to[TPATH];
    snprintf(to, sizeof to, "%s/%s", g_snapdir, tag);
    copy_file(path, to);
}

static HUNUSED int snap_same(const char *path, const char *tag)
{
    char to[TPATH];
    snprintf(to, sizeof to, "%s/%s", g_snapdir, tag);
    return files_equal(path, to);
}

/* --------------------------------------------------------------- stdout 採取
 *
 * Every message oracle in the design depends on this: the mode lexeme, the
 * elapsed-day count, usage, W1's raw bytes, W2's retained name, and the
 * differential that keeps the eleven error texts apart.  Both directions are
 * needed - "contains" and "does not contain" - plus an ASCII-only check and an
 * occurrence count, because "W2 is reported once per file actually discarded"
 * is a count and not a membership.
 *
 * fflush before and after is not optional: stdout is a FILE* with its own
 * buffer, and dup2 moves the file descriptor underneath it.
 */

static int g_cap_saved = -1;
static char *g_cap_buf;
static size_t g_cap_len;

static HUNUSED void cap_begin(void)
{
    int fd;

    if (g_cap_saved != -1) {
        die("cap_begin while already capturing");
    }
    free(g_cap_buf);
    g_cap_buf = NULL;
    g_cap_len = 0;
    if (fflush(stdout) != 0) {
        die("fflush stdout");
    }
    g_cap_saved = dup(STDOUT_FILENO);
    if (g_cap_saved < 0) {
        die("dup stdout");
    }
    fd = open(g_cap_path, O_RDWR | O_CREAT | O_TRUNC, 0600);
    if (fd < 0) {
        die(g_cap_path);
    }
    if (dup2(fd, STDOUT_FILENO) < 0) {
        die("dup2 capture");
    }
    (void)close(fd);
}

static HUNUSED void cap_end(void)
{
    if (g_cap_saved == -1) {
        die("cap_end without cap_begin");
    }
    if (fflush(stdout) != 0) {
        die("fflush stdout");
    }
    if (dup2(g_cap_saved, STDOUT_FILENO) < 0) {
        die("dup2 restore");
    }
    (void)close(g_cap_saved);
    g_cap_saved = -1;
    g_cap_buf = read_file(g_cap_path, &g_cap_len);
    if (g_cap_buf == NULL) {
        die(g_cap_path);
    }
}

static HUNUSED const char *cap_text(void)
{
    return (g_cap_buf != NULL) ? g_cap_buf : "";
}

static HUNUSED size_t cap_len(void)
{
    return g_cap_len;
}

/* Non-overlapping occurrences of a byte string.  Searched over the captured
 * length rather than with strstr, so a NUL in the output cannot hide the rest
 * of it. */
static HUNUSED int mem_count(const char *hay, size_t hlen, const char *needle)
{
    size_t nlen = strlen(needle);
    size_t i;
    int n = 0;

    if (nlen == 0u || hlen < nlen) {
        return 0;
    }
    i = 0;
    while (i + nlen <= hlen) {
        if (memcmp(hay + i, needle, nlen) == 0) {
            n++;
            i += nlen;
        } else {
            i++;
        }
    }
    return n;
}

static HUNUSED int cap_count(const char *needle)
{
    return mem_count(cap_text(), cap_len(), needle);
}

static HUNUSED int cap_has(const char *needle)
{
    return cap_count(needle) > 0;
}

/* All of stdout below 0x80.  Japanese belongs in GAME.BAT's ECHO, never in
 * RANK.EXE's own output (spec.md section 3.2). */
static HUNUSED int cap_ascii_only(void)
{
    size_t i;
    for (i = 0; i < g_cap_len; i++) {
        if ((unsigned char)g_cap_buf[i] >= 0x80u) {
            return 0;
        }
    }
    return 1;
}

/* ------------------------------------------------------------------ 時刻注入 */

static HUNUSED void set_clock(const char *s)
{
    RankTime t;
    if (rank_time_parse(s, &t) != 0) {
        fprintf(stderr, "harness failure: %s: bad clock %s\n", g_case, s);
        exit(2);
    }
    rank_test_now = t;
}

/* ------------------------------------------------------------------ 環境
 *
 * Thirteen paths: the five files that outlive a run, the four temporaries and
 * the four retained copies (spec.md section 3.3).
 */

typedef struct {
    char dir[TPATH];
    /* the five the snapshot set covers */
    char scr[TPATH];        /* NX93.SCR    */
    char txt[TPATH];        /* NX93.TXT    */
    char r30[TPATH];        /* RANK30.TXT  */
    char who[TPATH];        /* RANKWHO.TXT */
    char oldledger[TPATH];  /* NX93OLD.TXT - migration's retained ledger */
    /* the four temporaries */
    char tmpscr[TPATH];     /* RANKTMP.SCR */
    char tmptxt[TPATH];     /* RANKTMP.TXT */
    char tmp30[TPATH];      /* RANKTMP.30  */
    char tmpwho[TPATH];     /* RANKTMP.WHO */
    /* the four the commit step parks the outgoing file in */
    char oldscr[TPATH];     /* RANKOLD.SCR */
    char oldtxt[TPATH];     /* RANKOLD.TXT */
    char old30[TPATH];      /* RANKOLD.30  */
    char oldwho[TPATH];     /* RANKOLD.WHO */
} Env;

static HUNUSED void env_make(Env *e, const char *tag)
{
    case_dir(tag, e->dir);
    snprintf(e->scr,       sizeof e->scr,       "%s/NX93.SCR",    e->dir);
    snprintf(e->txt,       sizeof e->txt,       "%s/NX93.TXT",    e->dir);
    snprintf(e->r30,       sizeof e->r30,       "%s/RANK30.TXT",  e->dir);
    snprintf(e->who,       sizeof e->who,       "%s/RANKWHO.TXT", e->dir);
    snprintf(e->oldledger, sizeof e->oldledger, "%s/NX93OLD.TXT", e->dir);
    snprintf(e->tmpscr,    sizeof e->tmpscr,    "%s/RANKTMP.SCR", e->dir);
    snprintf(e->tmptxt,    sizeof e->tmptxt,    "%s/RANKTMP.TXT", e->dir);
    snprintf(e->tmp30,     sizeof e->tmp30,     "%s/RANKTMP.30",  e->dir);
    snprintf(e->tmpwho,    sizeof e->tmpwho,    "%s/RANKTMP.WHO", e->dir);
    snprintf(e->oldscr,    sizeof e->oldscr,    "%s/RANKOLD.SCR", e->dir);
    snprintf(e->oldtxt,    sizeof e->oldtxt,    "%s/RANKOLD.TXT", e->dir);
    snprintf(e->old30,     sizeof e->old30,     "%s/RANKOLD.30",  e->dir);
    snprintf(e->oldwho,    sizeof e->oldwho,    "%s/RANKOLD.WHO", e->dir);
}

/* The canonical 45 byte state file of spec.md section 3.4, written by hand so
 * that a test never depends on the writer it is there to check. */
static HUNUSED void env_state(const Env *e, const char *last_update)
{
    char buf[64];
    snprintf(buf, sizeof buf, "{\r\n  \"lastUpdate\": \"%s\"\r\n}\r\n", last_update);
    write_text(e->txt, buf);
}

/* ------------------------------------------------------- 5 枚スナップショット
 *
 * The five files a run may legitimately leave behind.  Everything else in Env
 * is the implementation's own scaffolding and expect_no_temps covers it.
 */

#define ENV_SNAP_FILES 5

static HUNUSED void env_snapshot(const Env *e)
{
    snap(e->scr,       "scr");
    snap(e->txt,       "txt");
    snap(e->r30,       "r30");
    snap(e->who,       "who");
    snap(e->oldledger, "oldledger");
}

static HUNUSED void expect_unchanged(const Env *e)
{
    OK("NX93.SCR is byte for byte what it was",    snap_same(e->scr, "scr"));
    OK("NX93.TXT is byte for byte what it was",    snap_same(e->txt, "txt"));
    OK("RANK30.TXT is byte for byte what it was",  snap_same(e->r30, "r30"));
    OK("RANKWHO.TXT is byte for byte what it was", snap_same(e->who, "who"));
    OK("NX93OLD.TXT is byte for byte what it was",
       snap_same(e->oldledger, "oldledger"));
}

/* Nothing of the implementation's own making may outlive a run, on any path.
 * RANKOLD.* is where the commit step parks the outgoing file, so a leftover
 * there is as much a defect as a leftover RANKTMP.*.
 *
 * The one deliberate exception is a stranded commit (E15), where the retained
 * copy is the only surviving one and the program says so.  `keep` names the
 * single path allowed to survive - pass one of e->oldscr/oldtxt/old30/oldwho,
 * or NULL when nothing may.  Without that parameter every *_STRAND case would
 * fail here for the wrong reason.
 *
 * NX93OLD.TXT is deliberately NOT in this set: migration keeps it on success
 * (spec.md section 3.7), so it belongs to the snapshot set instead.
 */
static HUNUSED void expect_no_temps(const Env *e, const char *keep)
{
    static const char *const names[8] = {
        "RANKTMP.SCR", "RANKTMP.TXT", "RANKTMP.30", "RANKTMP.WHO",
        "RANKOLD.SCR", "RANKOLD.TXT", "RANKOLD.30", "RANKOLD.WHO"
    };
    const char *paths[8];
    int i;

    paths[0] = e->tmpscr;
    paths[1] = e->tmptxt;
    paths[2] = e->tmp30;
    paths[3] = e->tmpwho;
    paths[4] = e->oldscr;
    paths[5] = e->oldtxt;
    paths[6] = e->old30;
    paths[7] = e->oldwho;

    for (i = 0; i < 8; i++) {
        if (keep != NULL && strcmp(paths[i], keep) == 0) {
            continue;
        }
        OK(lbl_s(names[i], "was cleaned up"), !file_exists(paths[i]));
    }
}

/* --------------------------------------------------------------------- main */

typedef struct {
    const char *name;
    void (*fn)(void);
} Case;

/* The four fixed seeds of the design (test-design.md section 15 item 6) are
 * set by the cases that use them, at the top of each case.  This neutral
 * reseed before every case is what stops a case order from carrying a stream
 * position into a case that forgot to seed. */
#define HARNESS_NEUTRAL_SEED 1u

static HUNUSED int harness_main(const char *prog, int argc, char **argv,
                                const Case *cases, size_t ncases)
{
    size_t i;
    const char *tz = getenv("TZ");

    if (argc < 2 || argc > 3) {
        fprintf(stderr, "usage: %s <scratch-dir> [name-substring]\n", prog);
        return 2;
    }
    if (strlen(argv[1]) + 64u >= sizeof g_scratch) {
        fprintf(stderr, "%s: scratch directory name is too long\n", prog);
        return 2;
    }
    strcpy(g_scratch, argv[1]);
    g_filter = (argc == 3) ? argv[2] : NULL;

    if (mkdir(g_scratch, 0755) != 0 && errno != EEXIST) {
        die(g_scratch);
    }
    snprintf(g_snapdir, sizeof g_snapdir, "%s/.snap", g_scratch);
    rm_tree(g_snapdir);
    if (mkdir(g_snapdir, 0755) != 0) {
        die(g_snapdir);
    }
    snprintf(g_io, sizeof g_io, "%s/io.bin", g_scratch);
    snprintf(g_cap_path, sizeof g_cap_path, "%s/stdout.cap", g_scratch);

    for (i = 0; i < ncases; i++) {
        if (g_filter != NULL && strstr(cases[i].name, g_filter) == NULL) {
            continue;
        }
        g_case = cases[i].name;
        g_case_failed = 0;
        /* Both ends, because the current suite resets at one end only and
         * that is an order dependence waiting to happen. */
        rank_test_fault = RANK_FAULT_NONE;
        srand(HARNESS_NEUTRAL_SEED);
        set_clock("2026-10-26T02:00:00");
        cases[i].fn();
        rank_test_fault = RANK_FAULT_NONE;
        if (g_cap_saved != -1) {
            cap_end();      /* a case that returned mid-capture must not take
                             * the rest of the suite's stdout with it */
            fprintf(stderr, "FAIL  %s  [left stdout captured]\n", g_case);
            g_case_failed = 1;
        }
        g_ran++;
        if (g_case_failed) {
            g_failed++;
        }
    }

    free(g_cap_buf);
    g_cap_buf = NULL;

    /* Leave nothing behind that the shell cannot delete: a case that made a
     * directory unwritable has already restored it, but be certain. */
    rm_tree(g_scratch);

    if (g_ran == 0) {
        fprintf(stderr, "%s: no case matched %s\n", prog,
                g_filter != NULL ? g_filter : "(no filter)");
        return 2;
    }
    fprintf(g_failed ? stderr : stdout,
            "%s[TZ=%s]: %d case(s), %ld assertion(s), %d failure(s)\n",
            prog, tz != NULL ? tz : "(unset)", g_ran, g_asserts, g_failed);
    return g_failed != 0;
}

#endif /* NX93RANK_HARNESS_DAILY_H */
