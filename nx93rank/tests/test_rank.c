/*
 * test_rank.c - Mac-side regression suite for nx93rank/dist/RANK.C.
 *
 *   test_rank <scratch-dir> [name-substring]
 *
 * Exits 0 only when every selected case passed and at least one case ran.
 * Cases are named after the case table in tests/README.md, which is
 * the authority for every oracle here.  Oracles were derived from plan.md's
 * "更新の規則" by hand; none of them was read back out of an implementation.
 *
 * The suite includes RANK.C as a translation unit so it can reach the internal
 * functions.  RANK_NO_MAIN drops RANK.C's own main; RANK_TEST_CLOCK and
 * RANK_TEST_FAULT enable seams that the DOS build never compiles (run.sh
 * asserts their absence from the DOS object, cases S5 / S2).
 */

#ifndef RANK_NO_MAIN
#define RANK_NO_MAIN 1
#endif
#ifndef RANK_TEST_CLOCK
#define RANK_TEST_CLOCK 1
#endif
#ifndef RANK_TEST_FAULT
#define RANK_TEST_FAULT 1
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <dirent.h>
#include <unistd.h>

#include "../dist/RANK.C"
#include "fixtures.h"

/* ------------------------------------------------------------------ 契約表明
 *
 * These are the parts of the contract a test can pin at compile time.  The
 * `long` requirement matters because the PC-9801 build has a 16 bit int and
 * every score in the real data overflows it.
 */
typedef char chk_score_is_long[(sizeof(((RankRow *)0)->score) == sizeof(long)) ? 1 : -1];
typedef char chk_rows[(RANK_ROWS == 10) ? 1 : -1];
typedef char chk_name_len[(RANK_NAME_LEN == 8) ? 1 : -1];
typedef char chk_max_line[(RANK_MAX_LINE == 63) ? 1 : -1];
typedef char chk_path_max[(RANK_PATH_MAX == 128) ? 1 : -1];
typedef char chk_raw[(sizeof(((RankRow *)0)->raw) == RANK_MAX_LINE + 1) ? 1 : -1];
typedef char chk_name[(sizeof(((RankRow *)0)->name) == RANK_NAME_LEN + 1) ? 1 : -1];
typedef char chk_key[(sizeof(((RankRow *)0)->key) == RANK_NAME_LEN + 1) ? 1 : -1];
typedef char chk_table[(sizeof(((RankTable *)0)->row) / sizeof(RankRow) == RANK_ROWS * 2) ? 1 : -1];
typedef char chk_r30_rows[(RANK30_ROWS == 30) ? 1 : -1];
typedef char chk_who_rows[(RANKWHO_ROWS == 100) ? 1 : -1];
typedef char chk_mode_weekly[(RANK_MODE_WEEKLY == 0) ? 1 : -1];
typedef char chk_mode_daily[(RANK_MODE_DAILY == 1) ? 1 : -1];
typedef char chk_r30_slack[(sizeof(((Rank30Table *)0)->row) / sizeof(RankRow)
                            == RANK30_ROWS + 1) ? 1 : -1];
typedef char chk_who_slack[(sizeof(((RankWhoTable *)0)->row) / sizeof(RankRow)
                            == RANKWHO_ROWS + 1) ? 1 : -1];

/* The two constants the calendar rule retired.  Nothing measures a period in
 * whole days any more, and nothing writes a JSON array that needs its leading
 * whitespace buffered, so a build that still defines either of these is one
 * where part of the old rule survived.  The two functions that went with them
 * cannot be checked here - absence of a function is not something the
 * preprocessor can see - and are covered by run-daily.sh's S11 instead. */
#ifdef RANK_PERIOD_DAYS
#error "RANK_PERIOD_DAYS outlived the elapsed-days rule"
#endif
#ifdef RANK_WS_MAX
#error "RANK_WS_MAX outlived the ledger writer"
#endif

#define TPATH 1024

/* ------------------------------------------------------------------ ハーネス */

static const char *g_case = "(none)";
static const char *g_filter;
static char g_scratch[TPATH];
static char g_snapdir[TPATH];
static char g_io[TPATH];          /* reusable scratch file for FILE* fixtures */
static int g_case_failed;
static int g_ran;
static int g_failed;
static int g_asserts;

static void note(int line, const char *what)
{
    fprintf(stderr, "FAIL  %s  [%s]  (test_rank.c:%d)\n", g_case, what, line);
    g_case_failed = 1;
}

static int is_true(int line, const char *what, int cond)
{
    g_asserts++;
    if (cond) {
        return 1;
    }
    note(line, what);
    return 0;
}

static int eq_long(int line, const char *what, long got, long want)
{
    g_asserts++;
    if (got == want) {
        return 1;
    }
    fprintf(stderr, "FAIL  %s  [%s]  (test_rank.c:%d) got %ld want %ld\n",
            g_case, what, line, got, want);
    g_case_failed = 1;
    return 0;
}

/* Print a byte string readably: printable ASCII as-is, everything else \xXX. */
static void show(const char *p, size_t n)
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

static int eq_mem(int line, const char *what,
                  const void *got, size_t glen, const void *want, size_t wlen)
{
    g_asserts++;
    if (glen == wlen && (glen == 0 || memcmp(got, want, glen) == 0)) {
        return 1;
    }
    fprintf(stderr, "FAIL  %s  [%s]  (test_rank.c:%d)\n      got  (%lu) ",
            g_case, what, line, (unsigned long)glen);
    show((const char *)got, glen > 96 ? 96 : glen);
    fprintf(stderr, "\n      want (%lu) ", (unsigned long)wlen);
    show((const char *)want, wlen > 96 ? 96 : wlen);
    fputc('\n', stderr);
    g_case_failed = 1;
    return 0;
}

static int eq_str(int line, const char *what, const char *got, const char *want)
{
    return eq_mem(line, what, got, strlen(got), want, strlen(want));
}

#define OK(what, cond)   is_true(__LINE__, (what), (cond))
#define EQL(what, g, w)  eq_long(__LINE__, (what), (long)(g), (long)(w))
#define EQS(what, g, w)  eq_str(__LINE__, (what), (g), (w))
#define EQM(what, g, gl, w, wl) eq_mem(__LINE__, (what), (g), (gl), (w), (wl))

/* A label with an index in it, for table-driven cases. */
static const char *lbl(const char *stem, long i)
{
    static char buf[4][160];
    static int turn;
    char *p = buf[turn++ & 3];
    snprintf(p, sizeof buf[0], "%s #%ld", stem, i);
    return p;
}

/* ------------------------------------------------------------------ ファイル */

static void die(const char *what)
{
    fprintf(stderr, "test_rank: harness failure: %s: %s\n", what, strerror(errno));
    exit(2);
}

static void write_bytes(const char *path, const void *data, size_t len)
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

static void write_text(const char *path, const char *s)
{
    write_bytes(path, s, strlen(s));
}

/* Read a whole file into a fresh malloc'd buffer, NUL-terminated for
 * convenience.  The test may use the heap freely; S4 constrains RANK.C only. */
static char *read_file(const char *path, size_t *len)
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

static int file_exists(const char *path)
{
    struct stat st;
    return stat(path, &st) == 0;
}

static long file_size(const char *path)
{
    struct stat st;
    if (stat(path, &st) != 0) {
        return -1L;
    }
    return (long)st.st_size;
}

/* Byte comparison that streams, so a 2 MiB ledger costs nothing.  Two absent
 * files count as equal; one absent does not. */
static int files_equal(const char *a, const char *b)
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

static void copy_file(const char *from, const char *to)
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

static void rm_tree(const char *path)
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

/* A fresh, empty directory for one integration case. */
static void case_dir(const char *tag, char *out)
{
    snprintf(out, TPATH, "%s/%s", g_scratch, tag);
    rm_tree(out);
    if (mkdir(out, 0755) != 0) {
        die(out);
    }
}

static void snap(const char *path, const char *tag)
{
    char to[TPATH];
    snprintf(to, sizeof to, "%s/%s", g_snapdir, tag);
    copy_file(path, to);
}

static int snap_same(const char *path, const char *tag)
{
    char to[TPATH];
    snprintf(to, sizeof to, "%s/%s", g_snapdir, tag);
    return files_equal(path, to);
}

/* ------------------------------------------------------------- RANK.C 呼び出し */

/* Hand a byte image to a FILE*-taking entry point.  A real file is used rather
 * than fmemopen so the reader meets exactly the stream it meets on DOS,
 * including the zero-length case. */
static int read_table_bytes(const void *data, size_t len, RankTable *t)
{
    FILE *fp;
    int rc;

    write_bytes(g_io, data, len);
    fp = fopen(g_io, "rb");
    if (fp == NULL) {
        die(g_io);
    }
    memset(t, 0, sizeof *t);
    rc = rank_read_table(fp, t);
    if (fclose(fp) != 0) {
        die(g_io);
    }
    return rc;
}

/* Build a table by round-tripping text through the reader, so raw/score/name/
 * key always agree with whatever the implementation computes for real input. */
static void table_of(const char *const *lines, int n, RankTable *t)
{
    char buf[2048];
    size_t at = 0;
    int i;

    for (i = 0; i < n; i++) {
        size_t l = strlen(lines[i]);
        memcpy(buf + at, lines[i], l);
        at += l;
        buf[at++] = '\r';
        buf[at++] = '\n';
    }
    if (read_table_bytes(buf, at, t) != n) {
        fprintf(stderr, "test_rank: harness failure: %s: fixture table of %d "
                        "line(s) did not read back\n", g_case, n);
        exit(2);
    }
}

static void expect_lines(const RankTable *t, const char *const *want, int n, const char *what)
{
    int i;
    if (!EQL(what, t->count, n)) {
        return;
    }
    for (i = 0; i < n; i++) {
        eq_str(__LINE__, lbl(what, i), t->row[i].raw, want[i]);
    }
}

/* Write a table out through rank_write_table and return its bytes. */
static char *written_bytes(const RankTable *t, size_t *len)
{
    FILE *fp = fopen(g_io, "wb");
    int rc;

    if (fp == NULL) {
        die(g_io);
    }
    rc = rank_write_table(fp, t);
    if (fclose(fp) != 0) {
        die(g_io);
    }
    if (rc != 0) {
        return NULL;
    }
    return read_file(g_io, len);
}

/* -------------------------------------------------------------- 期待ランキング
 *
 * Derived by hand for the two real inputs, under the rule that a trailing
 * run of non-alphanumeric ASCII in a name is a mark the player put on their
 * own name and so does not identify anybody.  nx93.scr: every row folds to
 * the key OMZN, OMZN@@@@ included, so one survivor is left (327400) and the
 * nine remaining seats go to the defaults, highest first.
 */
static const char *const UM1_WANT[RANK_ROWS] = {
    "327400 30 01 OMZN     26/09/15 A",
    "200000 29 00 Bio_100% 93/10/01 A",
    "180000 28 01 Bio_100% 93/10/01 A",
    "160000 27 02 Bio_100% 93/10/01 A",
    "140000 25 03 Bio_100% 93/10/01 A",
    "120000 22 04 Bio_100% 93/10/01 A",
    "100000 21 05 Bio_100% 93/10/01 A",
    "080000 19 06 Bio_100% 93/10/01 A",
    "060000 16 07 Bio_100% 93/10/01 A",
    "040000 13 08 Bio_100% 93/10/01 A"
};

/* The live nx93.scr as ten rows.  None of them is a shipped row and all ten
 * are already in descending order, so this is also what RANK30.TXT has to hold
 * after one reset of that table: every row leaving NX93.SCR goes into the
 * all-time table, which dedupes on whole lines and so folds nothing away. */
static const char *const SCR_ROWS[RANK_ROWS] = {
    "327400 30 01 OMZN     26/09/15 A",
    "316770 30 02 OMZN     26/09/17 A",
    "315870 30 01 OMZN     26/09/16 A",
    "304890 30 00 omzn     26/09/25 A",
    "299370 30 00 OMZN@@@@ 61/11/17 A",
    "291290 30 02 omzn     26/09/18 A",
    "286340 30 01 omzn     26/09/15 A",
    "285680 30 05 omzn     26/09/15 A",
    "265680 29 06 omzn     26/09/25 A",
    "261740 29 05 omzn     26/09/14 A"
};

/* nx93.bak holds two players, not seven: OMZN@@@@, OMZN++++, OMZN$$$$,
 * OMZN~~~~ and omzn all fold to the key OMZN, whose best is 299370, and omzo
 * is the only other one, whose 15030 loses to the shipped 020000.  So the
 * nine seats below the survivor go to the defaults.
 *
 * This case no longer decides whether scores are compared as numbers or as
 * text - the five digit rows it used to keep are folded away.  That net is
 * now U-G10 in test_daily.c, whose ten names all end in an alphanumeric and
 * so survive the fold. */
static const char *const UM2_WANT[RANK_ROWS] = {
    "299370 30 00 OMZN@@@@ 61/11/17 A",
    "200000 29 00 Bio_100% 93/10/01 A",
    "180000 28 01 Bio_100% 93/10/01 A",
    "160000 27 02 Bio_100% 93/10/01 A",
    "140000 25 03 Bio_100% 93/10/01 A",
    "120000 22 04 Bio_100% 93/10/01 A",
    "100000 21 05 Bio_100% 93/10/01 A",
    "080000 19 06 Bio_100% 93/10/01 A",
    "060000 16 07 Bio_100% 93/10/01 A",
    "040000 13 08 Bio_100% 93/10/01 A"
};

/* One player, which is what the ten rows above come to once a mark on the end
 * of a name stops identifying anybody: all ten fold to the key OMZN and the
 * best of them is 327400.  This is RANKWHO.TXT after one reset. */
static const char *const WHO_WANT[1] = {
    "327400 30 01 OMZN     26/09/15 A"
};

/* ------------------------------------------------------------------- S8 契約 */

static void case_contract(void)
{
    /* The typedefs above already failed the build if any of this were wrong;
     * restating it at run time keeps the contract visible in the case list. */
    EQL("RANK_ROWS", RANK_ROWS, 10);
    EQL("RANK_NAME_LEN", RANK_NAME_LEN, 8);
    EQL("RANK_MAX_LINE", RANK_MAX_LINE, 63);
    EQL("RANK_PATH_MAX", RANK_PATH_MAX, 128);
    EQL("sizeof score == sizeof long", sizeof(((RankRow *)0)->score), sizeof(long));
    EQL("merge buffer holds current + defaults",
        sizeof(((RankTable *)0)->row) / sizeof(RankRow), RANK_ROWS * 2);

    /* The daily reset's own surface. */
    EQL("RANK30_ROWS", RANK30_ROWS, 30);
    EQL("RANKWHO_ROWS", RANKWHO_ROWS, 100);
    EQL("RANK_MODE_WEEKLY", RANK_MODE_WEEKLY, 0);
    EQL("RANK_MODE_DAILY", RANK_MODE_DAILY, 1);
    EQL("the all-time table carries one row of slack",
        sizeof(((Rank30Table *)0)->row) / sizeof(RankRow), RANK30_ROWS + 1);
    EQL("and so does the per-user table",
        sizeof(((RankWhoTable *)0)->row) / sizeof(RankRow), RANKWHO_ROWS + 1);
    EQL("the state file is forty five bytes", RANK_STATE_LEN, 45);
    EQL("whose timestamp starts at offset twenty", RANK_STATE_TS_AT, 20);
}

/* --------------------------------------------------------------------- U-T */

static void case_ut1(void)
{
    static const struct { int y, m, d; long want; } tab[] = {
        { 1970,  1,  1,     0L },
        { 1970,  1,  2,     1L },
        { 1969, 12, 31,    -1L },
        { 2000,  2, 29, 11016L },
        { 2000,  3,  1, 11017L },
        { 2024,  2, 29, 19782L },
        { 1980,  1,  1,  3652L },
        { 2026,  9, 28, 20724L }
    };
    size_t i;

    for (i = 0; i < sizeof tab / sizeof tab[0]; i++) {
        eq_long(__LINE__, lbl("civil days", (long)i),
                rank_days_from_civil(tab[i].y, tab[i].m, tab[i].d), tab[i].want);
    }
}

static int days_in(int y, int m)
{
    static const int len[12] = { 31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31 };
    if (m == 2 && ((y % 4 == 0 && y % 100 != 0) || y % 400 == 0)) {
        return 29;
    }
    return len[m - 1];
}

/* Walking every day from 1970 to 2100 catches a leap rule that is only wrong
 * at a century, which a spot-check table cannot reach. */
static void case_ut2(void)
{
    long expect = 0;
    int y;
    int bad = 0;

    for (y = 1970; y <= 2100; y++) {
        int m;
        for (m = 1; m <= 12; m++) {
            int d;
            for (d = 1; d <= days_in(y, m); d++) {
                if (rank_days_from_civil(y, m, d) != expect) {
                    if (bad == 0) {
                        fprintf(stderr, "FAIL  %s  [%04d-%02d-%02d] got %ld want %ld\n",
                                g_case, y, m, d,
                                rank_days_from_civil(y, m, d), expect);
                        g_case_failed = 1;
                    }
                    bad++;
                }
                expect++;
            }
        }
    }
    g_asserts++;
    EQL("days in 1970-01-01..2100-12-31", expect, 47847L);
    EQL("days that did not advance by exactly one", bad, 0);
}

static void case_ut3(void)
{
    static const char *const good[] = {
        "2026-09-28T21:40:00",
        "2000-02-29T00:00:00",
        "2024-02-29T23:59:59",
        "1970-01-01T00:00:00"
    };
    static const char *const bad[] = {
        "2026-09-28T21:40:0",       /* 18 bytes */
        "2026-09-28T21:40:000",     /* 20 bytes */
        "2026-00-28T21:40:00",      /* month 00 */
        "2026-13-28T21:40:00",      /* month 13 */
        "2026-09-00T21:40:00",      /* day 00 */
        "2026-09-32T21:40:00",      /* day 32 */
        "2026-02-29T21:40:00",      /* not a leap year */
        "1900-02-29T21:40:00",      /* century, not a leap year */
        "2026-09-28T24:40:00",      /* hour 24 */
        "2026-09-28T21:60:00",      /* minute 60 */
        "2026-09-28T21:40:60",      /* second 60 */
        "2026-09-28 21:40:00",      /* no T */
        "2026-09-2xT21:40:00",      /* non-digit */
        "",
        "x"
    };
    size_t i;
    RankTime t;

    for (i = 0; i < sizeof good / sizeof good[0]; i++) {
        memset(&t, 0, sizeof t);
        if (eq_long(__LINE__, lbl("accepts", (long)i), rank_time_parse(good[i], &t), 0)) {
            char buf[20];
            rank_time_format(&t, buf);
            eq_str(__LINE__, lbl("accepts, round trip", (long)i), buf, good[i]);
        }
    }
    /* Fields must land in the right slots, not merely parse. */
    memset(&t, 0, sizeof t);
    if (EQL("parse 2026-09-28T21:40:05", rank_time_parse("2026-09-28T21:40:05", &t), 0)) {
        EQL("year", t.year, 2026);
        EQL("month", t.month, 9);
        EQL("day", t.day, 28);
        EQL("hour", t.hour, 21);
        EQL("minute", t.minute, 40);
        EQL("second", t.second, 5);
    }
    for (i = 0; i < sizeof bad / sizeof bad[0]; i++) {
        memset(&t, 0, sizeof t);
        eq_long(__LINE__, lbl("rejects", (long)i), rank_time_parse(bad[i], &t), -1);
    }
}

static void case_ut4(void)
{
    static const struct { const char *to; long want; } tab[] = {
        { "2026-09-27T21:40:00",  6L },   /* one second short of seven days */
        { "2026-09-28T21:39:59",  6L },
        { "2026-09-28T21:40:00",  7L },   /* exactly seven days */
        { "2026-09-29T21:40:00",  8L },
        { "2026-09-21T21:40:00",  0L },
        { "2026-09-21T21:39:59", -1L },   /* clock went backwards */
        { "2026-09-14T21:40:00", -7L }
    };
    RankTime from;
    size_t i;

    if (!EQL("base parses", rank_time_parse("2026-09-21T21:40:00", &from), 0)) {
        return;
    }
    for (i = 0; i < sizeof tab / sizeof tab[0]; i++) {
        RankTime to;
        if (rank_time_parse(tab[i].to, &to) != 0) {
            note(__LINE__, lbl("target parses", (long)i));
            continue;
        }
        eq_long(__LINE__, lbl("elapsed days", (long)i),
                rank_elapsed_days(&from, &to), tab[i].want);
    }
}

/* In America/Los_Angeles a mktime()-based difference over the spring forward
 * gives 6.9583 days, which truncates to 6 and silently skips a reset.  run.sh
 * repeats the whole suite under three zones; this case is the one that would
 * differ between them. */
static void case_ut5(void)
{
    static const struct { const char *a; const char *b; long want; } tab[] = {
        { "2026-03-07T12:00:00", "2026-03-14T12:00:00", 7L },   /* spring forward */
        { "2026-10-31T12:00:00", "2026-11-07T12:00:00", 7L },   /* fall back */
        { "2026-03-07T12:00:00", "2026-03-13T12:00:00", 6L },
        { "2026-03-07T12:00:00", "2026-03-14T11:59:59", 6L }
    };
    size_t i;

    for (i = 0; i < sizeof tab / sizeof tab[0]; i++) {
        RankTime a;
        RankTime b;
        if (rank_time_parse(tab[i].a, &a) != 0 || rank_time_parse(tab[i].b, &b) != 0) {
            note(__LINE__, lbl("dst pair parses", (long)i));
            continue;
        }
        eq_long(__LINE__, lbl("elapsed across a DST change", (long)i),
                rank_elapsed_days(&a, &b), tab[i].want);
    }
}

static void case_ut6(void)
{
    static const int years[] = { 1970, 1999, 2000, 2024, 2026, 2099 };
    size_t yi;

    for (yi = 0; yi < sizeof years / sizeof years[0]; yi++) {
        int m;
        for (m = 1; m <= 12; m++) {
            static const int hh[] = { 0, 9, 23 };
            int di;
            int dd[3];
            size_t hi;
            dd[0] = 1;
            dd[1] = 15;
            dd[2] = days_in(years[yi], m);
            for (di = 0; di < 3; di++) {
                for (hi = 0; hi < sizeof hh / sizeof hh[0]; hi++) {
                    char s[20];
                    char out[20];
                    RankTime t;
                    snprintf(s, sizeof s, "%04d-%02d-%02dT%02d:%02d:%02d",
                            years[yi], m, dd[di], hh[hi], (int)(hi * 7), (int)(di * 29));
                    memset(&t, 0, sizeof t);
                    if (rank_time_parse(s, &t) != 0) {
                        note(__LINE__, s);
                        return;
                    }
                    memset(out, 0, sizeof out);
                    rank_time_format(&t, out);
                    if (strcmp(out, s) != 0) {
                        eq_str(__LINE__, "format(parse(s)) == s", out, s);
                        return;
                    }
                }
            }
        }
    }
    g_asserts++;
}

/* --------------------------------------------------------------------- U-P */

/* score, 8-byte name, fixed 26 byte tail.  32 bytes for a 6 digit score. */
static void make_line(char *out, size_t cap, const char *score, const char *name)
{
    snprintf(out, cap, "%s 30 01 %s 26/09/15 A", score, name);
}

/* The four widths that matter: the shortest row, the width the game writes,
 * the width the distribution file uses, and the longest row. */
static const char *const SCORES[4] = { "9", "98840", "327400", "123456789" };

static void case_up1(void)
{
    static const struct { long score; const char *name; } want[30] = {
        { 327400L, "OMZN    " }, { 316770L, "OMZN    " }, { 315870L, "OMZN    " },
        { 304890L, "omzn    " }, { 299370L, "OMZN@@@@" }, { 291290L, "omzn    " },
        { 286340L, "omzn    " }, { 285680L, "omzn    " }, { 265680L, "omzn    " },
        { 261740L, "omzn    " },
        { 299370L, "OMZN@@@@" }, {  98840L, "OMZN@@@@" }, {  80000L, "OMZN@@@@" },
        {  75170L, "OMZN++++" }, {  64830L, "omzn    " }, {  54600L, "OMZN$$$$" },
        {  40520L, "OMZN~~~~" }, {  38400L, "omzn    " }, {  26010L, "omzn    " },
        {  15030L, "omzo    " },
        { 200000L, "Bio_100%" }, { 180000L, "Bio_100%" }, { 160000L, "Bio_100%" },
        { 140000L, "Bio_100%" }, { 120000L, "Bio_100%" }, { 100000L, "Bio_100%" },
        {  80000L, "Bio_100%" }, {  60000L, "Bio_100%" }, {  40000L, "Bio_100%" },
        {  20000L, "Bio_100%" }
    };
    static const struct { const char *data; size_t len; } src[3] = {
        { fix_scr, FIX_SCR_LEN }, { fix_bak, FIX_BAK_LEN }, { fix_def, FIX_DEF_LEN }
    };
    int at = 0;
    int f;

    for (f = 0; f < 3; f++) {
        RankTable t;
        int i;
        if (!eq_long(__LINE__, lbl("real file reads ten rows", f),
                     read_table_bytes(src[f].data, src[f].len, &t), RANK_ROWS)) {
            at += RANK_ROWS;
            continue;
        }
        for (i = 0; i < RANK_ROWS; i++, at++) {
            eq_long(__LINE__, lbl("score", at), t.row[i].score, want[at].score);
            eq_mem(__LINE__, lbl("name", at),
                   t.row[i].name, strlen(t.row[i].name),
                   want[at].name, (size_t)RANK_NAME_LEN);
        }
    }
    EQL("all thirty real rows visited", at, 30);
}

static void case_up2(void)
{
    RankRow r;

    /* A row is score digits plus a fixed 26 byte tail, so 27 and 35 bracket
     * the one and nine digit scores. */
    OK("len 27 accepted",
       rank_parse_line("9 30 01 OMZN     26/09/15 A", &r) == 0);
    EQL("len 27 score", r.score, 9L);
    OK("len 35 accepted",
       rank_parse_line("123456789 30 01 OMZN     26/09/15 A", &r) == 0);
    EQL("len 35 score", r.score, 123456789L);
    OK("len 26 rejected",
       rank_parse_line(" 30 01 OMZN     26/09/15 A", &r) == -1);
    OK("len 36 rejected",
       rank_parse_line("1234567890 30 01 OMZN     26/09/15 A", &r) == -1);
    OK("empty line rejected", rank_parse_line("", &r) == -1);
    OK("one byte line rejected", rank_parse_line("A", &r) == -1);
    OK("long line rejected",
       rank_parse_line("327400 30 01 OMZN     26/09/15 AA", &r) == -1);
}

/* The five separators are counted from the end because the score is variable
 * width.  Reading from the front with fixed offsets survives the 6 digit rows
 * and breaks on the 5 digit ones, so each separator gets its own mutation. */
static void case_up3(void)
{
    static const int off[5] = { 2, 11, 20, 23, 26 };
    static const char *const base[2] = {
        "327400 30 01 OMZN     26/09/15 A",     /* 32 bytes, 6 digit score */
        "98840 30 01 OMZN@@@@ 26/09/15 A"       /* 31 bytes, 5 digit score */
    };
    int b;
    int i;
    RankRow r;

    for (b = 0; b < 2; b++) {
        size_t len = strlen(base[b]);
        OK(lbl("unmutated base parses", b), rank_parse_line(base[b], &r) == 0);
        for (i = 0; i < 5; i++) {
            char line[64];
            strcpy(line, base[b]);
            line[len - (size_t)off[i]] = 'X';
            eq_long(__LINE__, lbl(b == 0 ? "6 digit, separator clobbered"
                                         : "5 digit, separator clobbered", off[i]),
                    rank_parse_line(line, &r), -1);
        }
    }
}

static void case_up4(void)
{
    static const char *const bad[] = {
        "32740X 30 01 OMZN     26/09/15 A",     /* non-digit inside the score */
        "X27400 30 01 OMZN     26/09/15 A",
        "+32740 30 01 OMZN     26/09/15 A",     /* leading sign */
        "-32740 30 01 OMZN     26/09/15 A",
        " 27400 30 01 OMZN     26/09/15 A",     /* leading space, no digits */
        "3274.0 30 01 OMZN     26/09/15 A"
    };
    size_t i;
    RankRow r;

    for (i = 0; i < sizeof bad / sizeof bad[0]; i++) {
        eq_long(__LINE__, lbl("non-numeric score rejected", (long)i),
                rank_parse_line(bad[i], &r), -1);
    }
}

static void case_up5(void)
{
    static const char *const names[] = {
        "OMZN    ",
        "Bio_100%",             /* exactly eight, and it carries a % */
        "A B     ",             /* an interior space */
        "  AB    ",             /* leading spaces */
        "        ",             /* all spaces */
        "%n%s%%%%",             /* would be fatal if a line reached printf */
        "AB\"CD\\EF",           /* 0x22 and 0x5C, which the ledger must escape */
        "\340\300\340\300\340\300\340\300"      /* high bytes, as SJIS would be */
    };
    size_t i;
    size_t s;

    /* Every score width, because at six digits an offset counted from the
     * front lands on the same byte as one counted from the end and the whole
     * point of the case disappears. */
    for (s = 0; s < sizeof SCORES / sizeof SCORES[0]; s++) {
        for (i = 0; i < sizeof names / sizeof names[0]; i++) {
            char line[64];
            RankRow r;
            make_line(line, sizeof line, SCORES[s], names[i]);
            memset(&r, 0, sizeof r);
            if (!eq_long(__LINE__, lbl("name row parses", (long)(s * 100 + i)),
                         rank_parse_line(line, &r), 0)) {
                continue;
            }
            eq_mem(__LINE__, lbl("name taken verbatim", (long)(s * 100 + i)),
                   r.name, strlen(r.name), names[i], (size_t)RANK_NAME_LEN);
            eq_long(__LINE__, lbl("name is NUL terminated", (long)(s * 100 + i)),
                    r.name[RANK_NAME_LEN], 0L);
        }
    }
}

static void case_up6(void)
{
    static const struct { const char *name; const char *key; } tab[] = {
        { "omzn    ", "OMZN"     },
        { "OMZN    ", "OMZN"     },
        { "OMZN@@@@", "OMZN"     },     /* trailing marks are the same player */
        { "  AB    ", "AB"       },     /* both ends trimmed */
        { "A B     ", "A B"      },     /* interior space kept */
        { "        ", ""         },     /* nothing left at all */
        { "Bio_100%", "BIO_100"  },     /* the trailing % goes too */
        { "\340abc\300def", "\340ABC\300DEF" },  /* high bytes untouched */
        { "\340\300\340\300\340\300\340\300",
          "\340\300\340\300\340\300\340\300" }
    };
    size_t i;
    size_t s;

    for (s = 0; s < sizeof SCORES / sizeof SCORES[0]; s++) {
        for (i = 0; i < sizeof tab / sizeof tab[0]; i++) {
            char line[64];
            RankRow r;
            make_line(line, sizeof line, SCORES[s], tab[i].name);
            memset(&r, 0, sizeof r);
            if (!eq_long(__LINE__, lbl("key row parses", (long)(s * 100 + i)),
                         rank_parse_line(line, &r), 0)) {
                continue;
            }
            eq_str(__LINE__, lbl("folded key", (long)(s * 100 + i)),
                   r.key, tab[i].key);
        }
    }
}

static void case_up7(void)
{
    static const char *const lines[] = {
        "327400 30 01 OMZN     26/09/15 A",
        "98840 30 01 OMZN@@@@ 63/10/16 A",
        "9 30 01 %n%s%%%% 26/09/15 A",
        "123456789 30 01 AB\"CD\\EF 26/09/15 A"
    };
    size_t i;

    for (i = 0; i < sizeof lines / sizeof lines[0]; i++) {
        RankRow r;
        memset(&r, 0, sizeof r);
        if (!eq_long(__LINE__, lbl("raw row parses", (long)i),
                     rank_parse_line(lines[i], &r), 0)) {
            continue;
        }
        eq_mem(__LINE__, lbl("raw is the input byte for byte", (long)i),
               r.raw, strlen(r.raw), lines[i], strlen(lines[i]));
    }
}

/* --------------------------------------------------------------------- U-R */

static void case_ur1(void)
{
    RankTable t;
    EQL("nx93.bak with its trailing 0x1A reads ten rows",
        read_table_bytes(fix_bak, FIX_BAK_LEN, &t), RANK_ROWS);
    EQL("count matches", t.count, RANK_ROWS);
    EQS("first row", t.row[0].raw, "299370 30 00 OMZN@@@@ 61/11/17 A");
    EQS("last row", t.row[9].raw, "15030 30 06 omzo     26/09/14 A");
}

static void case_ur2(void)
{
    RankTable t;
    EQL("nx93.scr without its final CRLF reads ten rows",
        read_table_bytes(fix_scr, FIX_SCR_LEN - 2u, &t), RANK_ROWS);
    EQS("last row survives", t.row[9].raw, "261740 29 05 omzn     26/09/14 A");
}

static void case_ur3(void)
{
    char buf[512];
    size_t n = 0;
    size_t i;
    RankTable t;

    for (i = 0; i < FIX_SCR_LEN; i++) {
        if (fix_scr[i] != '\r') {
            buf[n++] = fix_scr[i];
        }
    }
    EQL("LF-only line endings read ten rows", read_table_bytes(buf, n, &t), RANK_ROWS);
    EQS("first row has no stray CR", t.row[0].raw, "327400 30 01 OMZN     26/09/15 A");
}

static void case_ur4(void)
{
    RankTable t;
    EQL("empty file is not an error", read_table_bytes("", 0, &t), 0L);
    EQL("empty file has no rows", t.count, 0L);
    EQL("0x1A only is not an error", read_table_bytes("\032", 1, &t), 0L);
    EQL("0x1A only has no rows", t.count, 0L);
    EQL("bare CRLF is not an error", read_table_bytes("\r\n", 2, &t), 0L);
    EQL("bare CRLF has no rows", t.count, 0L);
}

static void case_ur5(void)
{
    char buf[1024];
    RankTable t;

    memcpy(buf, fix_scr, FIX_SCR_LEN);
    memcpy(buf + FIX_SCR_LEN, "060000 16 07 Bio_100% 93/10/01 A\r\n", 34);
    EQL("eleven rows are refused, not truncated",
        read_table_bytes(buf, FIX_SCR_LEN + 34u, &t), -1L);
}

static void case_ur6(void)
{
    char buf[512];
    RankTable t;
    int i;

    for (i = 0; i < 200; i++) {
        buf[i] = (char)('0' + (i % 10));
    }
    buf[200] = '\r';
    buf[201] = '\n';
    EQL("a 200 byte line is refused", read_table_bytes(buf, 202, &t), -1L);

    /* And it must not resynchronise onto the next line as if nothing happened. */
    memcpy(buf + 202, "327400 30 01 OMZN     26/09/15 A\r\n", 34);
    EQL("no resynchronisation after an overlong line",
        read_table_bytes(buf, 236, &t), -1L);
}

/* Bit flips, truncations and insertions over the real file.  The bar is only
 * that the reader answers rather than misbehaves; ASan and UBSan supply the
 * rest of the oracle by aborting the process. */
static void case_ur7(void)
{
    char buf[512];
    int iter;
    int bad = 0;

    srand(20260928u);
    for (iter = 0; iter < 20000; iter++) {
        size_t n = FIX_SCR_LEN;
        RankTable t;
        int rc;
        int kind;

        memcpy(buf, fix_scr, FIX_SCR_LEN);
        kind = rand() % 3;
        if (kind == 0) {
            size_t at = (size_t)(rand() % (int)n);
            buf[at] = (char)(buf[at] ^ (1 << (rand() % 8)));
        } else if (kind == 1) {
            n = (size_t)(rand() % (int)(FIX_SCR_LEN + 1));
        } else {
            size_t at = (size_t)(rand() % (int)(n + 1));
            memmove(buf + at + 1, buf + at, n - at);
            buf[at] = (char)(rand() % 256);
            n++;
        }

        rc = read_table_bytes(buf, n, &t);
        if (rc < -1 || rc > RANK_ROWS || (rc >= 0 && t.count != rc)) {
            if (bad == 0) {
                fprintf(stderr, "FAIL  %s  [iteration %d kind %d] rc=%d count=%d\n",
                        g_case, iter, kind, rc, t.count);
                g_case_failed = 1;
            }
            bad++;
        }
        if (rc > 0) {
            int i;
            for (i = 0; i < rc; i++) {
                if (memchr(t.row[i].raw, '\0', sizeof t.row[i].raw) == NULL) {
                    if (bad == 0) {
                        fprintf(stderr, "FAIL  %s  [iteration %d] row %d not "
                                        "terminated\n", g_case, iter, i);
                        g_case_failed = 1;
                    }
                    bad++;
                }
            }
        }
    }
    g_asserts++;
    EQL("mutated inputs that produced neither a row count nor -1", bad, 0);
}

/* --------------------------------------------------------------------- U-M */

static void rebuild_of(const void *data, size_t len, RankTable *out)
{
    RankTable cur;
    if (read_table_bytes(data, len, &cur) < 0) {
        fprintf(stderr, "test_rank: harness failure: %s: fixture did not read\n", g_case);
        exit(2);
    }
    memset(out, 0, sizeof *out);
    rank_rebuild(&cur, out);
}

static void case_um1(void)
{
    RankTable out;
    char *bytes;
    size_t len;

    rebuild_of(fix_scr, FIX_SCR_LEN, &out);
    expect_lines(&out, UM1_WANT, RANK_ROWS, "rebuild of the live nx93.scr");

    bytes = written_bytes(&out, &len);
    if (OK("write succeeded", bytes != NULL)) {
        EQL("written size", (long)len, 340L);
        free(bytes);
    }
}

static void case_um2(void)
{
    RankTable out;
    char *bytes;
    size_t len;

    rebuild_of(fix_bak, FIX_BAK_LEN, &out);
    expect_lines(&out, UM2_WANT, RANK_ROWS, "rebuild of nx93.bak");

    bytes = written_bytes(&out, &len);
    if (OK("write succeeded", bytes != NULL)) {
        EQL("written size", (long)len, 340L);
        free(bytes);
    }
}

/* Both directions of the key rule, seen through rank_rebuild: a trailing mark
 * is a mark of the same player, and a changed letter is somebody else. */
static void case_um3(void)
{
    static const char *const mixed[4] = {
        "327400 30 01 OMZN     26/09/15 A",
        "299370 30 00 OMZN@@@@ 61/11/17 A",
        "286340 30 01 omzn     26/09/15 A",
        "250000 30 01 omzo     26/09/15 A"
    };
    RankTable cur;
    RankTable out;
    int i;
    int omzn = 0;
    int omzo = 0;

    rebuild_of(fix_scr, FIX_SCR_LEN, &out);
    if (!EQL("ten rows out", out.count, RANK_ROWS)) {
        return;
    }
    EQL("the OMZN survivor keeps the best score", out.row[0].score, 327400L);
    EQS("and it is the row that scored it", out.row[0].name, "OMZN    ");
    EQL("OMZN@@@@ is the same player, so a default follows instead",
        out.row[1].score, 200000L);
    for (i = 0; i < out.count; i++) {
        if (strcmp(out.row[i].key, "OMZN") == 0) {
            omzn++;
        }
    }
    EQL("exactly one row folds to the key OMZN", omzn, 1);

    /* The live nx93.scr has no second player scoring above the shipped
     * floor, so the "somebody else" half is shown on a table built for it. */
    table_of(mixed, 4, &cur);
    memset(&out, 0, sizeof out);
    rank_rebuild(&cur, &out);
    if (!EQL("ten rows out of the mixed table", out.count, RANK_ROWS)) {
        return;
    }
    omzn = 0;
    for (i = 0; i < out.count; i++) {
        if (strcmp(out.row[i].key, "OMZN") == 0) {
            omzn++;
        }
        if (strcmp(out.row[i].key, "OMZO") == 0) {
            omzo++;
        }
    }
    EQL("three spellings of OMZN leave one row", omzn, 1);
    EQL("omzo was not folded into it", omzo, 1);
    EQS("and omzo keeps its own row", out.row[1].raw, mixed[3]);
}

static void case_um4(void)
{
    RankTable cur;
    RankTable rev;
    RankTable out;
    int i;

    if (read_table_bytes(fix_scr, FIX_SCR_LEN, &cur) != RANK_ROWS) {
        note(__LINE__, "fixture did not read");
        return;
    }
    memset(&rev, 0, sizeof rev);
    rev.count = cur.count;
    for (i = 0; i < cur.count; i++) {
        rev.row[i] = cur.row[cur.count - 1 - i];
    }
    memset(&out, 0, sizeof out);
    rank_rebuild(&rev, &out);

    expect_lines(&out, UM1_WANT, RANK_ROWS, "ascending input still yields the same table");
    for (i = 1; i < out.count; i++) {
        eq_long(__LINE__, lbl("output is non-increasing", i),
                out.row[i - 1].score >= out.row[i].score, 1L);
    }
}

/* Two current rows and one default row all score 200000.  plan.md fixes the
 * order: current rows first, in their original order, then the default. */
static void case_um5(void)
{
    static const char *const lines[2] = {
        "200000 30 01 AAA      26/09/15 A",
        "200000 30 01 BBB      26/09/15 A"
    };
    RankTable cur;
    RankTable out;

    table_of(lines, 2, &cur);
    memset(&out, 0, sizeof out);
    rank_rebuild(&cur, &out);

    if (!EQL("ten rows out", out.count, RANK_ROWS)) {
        return;
    }
    EQS("first current row keeps its place", out.row[0].raw, lines[0]);
    EQS("second current row follows it", out.row[1].raw, lines[1]);
    EQS("the tied default comes last", out.row[2].raw, rank_default_line(0));
}

static void case_um6(void)
{
    RankTable cur;
    RankTable out;
    char *bytes;
    size_t len;
    int i;

    memset(&cur, 0, sizeof cur);
    cur.count = 0;
    memset(&out, 0, sizeof out);
    rank_rebuild(&cur, &out);

    if (!EQL("ten rows out", out.count, RANK_ROWS)) {
        return;
    }
    for (i = 0; i < RANK_ROWS; i++) {
        /* The ten Bio_100% rows share a name on purpose: they are there to
         * hold seats, so the defaults must not be deduplicated. */
        eq_str(__LINE__, lbl("default row", i), out.row[i].raw, rank_default_line(i));
    }
    bytes = written_bytes(&out, &len);
    if (OK("write succeeded", bytes != NULL)) {
        EQM("an empty table reproduces the distribution file byte for byte",
            bytes, len, fix_def, FIX_DEF_LEN);
        free(bytes);
    }
}

static void case_um7(void)
{
    static const char *const lines[3] = {
        "900000 30 01 AAA      26/09/15 A",
        "500000 30 01 BBB      26/09/15 A",
        "1 30 01 CCC      26/09/15 A"
    };
    RankTable cur;
    RankTable out;

    table_of(lines, 3, &cur);
    memset(&out, 0, sizeof out);
    rank_rebuild(&cur, &out);
    EQL("three current rows still fill ten seats", out.count, RANK_ROWS);
    EQS("top row", out.row[0].raw, lines[0]);
    EQS("second row", out.row[1].raw, lines[1]);
    /* 900000 and 500000 take two seats, the eight best defaults take the rest,
     * and the row scoring 1 is pushed out below 060000. */
    EQS("last row is the eighth default", out.row[9].raw, rank_default_line(7));
    {
        int i;
        int found = 0;
        for (i = 0; i < out.count; i++) {
            if (strcmp(out.row[i].raw, lines[2]) == 0) {
                found = 1;
            }
        }
        EQL("the row scoring 1 did not make the table", found, 0);
    }
}

static void case_um8(void)
{
    const char *lines[RANK_ROWS];
    char store[RANK_ROWS][64];
    RankTable cur;
    RankTable out;
    int i;

    for (i = 0; i < RANK_ROWS; i++) {
        char name[16];
        snprintf(name, sizeof name, "N%d      ", i);
        snprintf(store[i], sizeof store[0], "%ld 30 01 %s 26/09/15 A", 909000L - (long)i * 1000L, name);
        lines[i] = store[i];
    }
    /* The cast only drops a nested const; C will not add one implicitly. */
    table_of((const char *const *)lines, RANK_ROWS, &cur);
    memset(&out, 0, sizeof out);
    rank_rebuild(&cur, &out);

    if (!EQL("ten rows out", out.count, RANK_ROWS)) {
        return;
    }
    for (i = 0; i < RANK_ROWS; i++) {
        eq_str(__LINE__, lbl("ten distinct high scores push every default out", i),
               out.row[i].raw, lines[i]);
    }
}

/* D7: the survivor of a deduplicated name keeps the index where its best score
 * appeared, which is what makes the tie between two different names decidable. */
static void case_um9(void)
{
    static const char *const lines[3] = {
        "100000 30 01 AAA      26/09/15 A",
        "500000 30 01 BBB      26/09/15 A",
        "500000 30 01 AAA      26/09/15 A"
    };
    RankTable cur;
    RankTable out;

    table_of(lines, 3, &cur);
    memset(&out, 0, sizeof out);
    rank_rebuild(&cur, &out);

    if (!EQL("ten rows out", out.count, RANK_ROWS)) {
        return;
    }
    /* AAA's survivor is its row 2, so BBB at row 1 sorts ahead of it. */
    EQS("BBB, whose only row is index 1", out.row[0].raw, lines[1]);
    EQS("AAA, whose survivor is index 2", out.row[1].raw, lines[2]);
}

static void case_um10(void)
{
    RankTable out;
    char *bytes;
    size_t len;
    char want[512];
    size_t at = 0;
    int i;

    rebuild_of(fix_scr, FIX_SCR_LEN, &out);
    bytes = written_bytes(&out, &len);
    if (!OK("write succeeded", bytes != NULL)) {
        return;
    }
    for (i = 0; i < RANK_ROWS; i++) {
        size_t l = strlen(UM1_WANT[i]);
        memcpy(want + at, UM1_WANT[i], l);
        at += l;
        want[at++] = '\r';
        want[at++] = '\n';
    }
    EQM("every row, CRLF terminated, and nothing else", bytes, len, want, at);
    if (len > 0) {
        EQL("no trailing 0x1A", (long)(unsigned char)bytes[len - 1], 10L);
    }
    free(bytes);
}

/* --------------------------------------------------------------------- U-L */

static int ledger_last_update(const char *path, RankTime *out)
{
    FILE *fp = fopen(path, "rb");
    int rc;

    if (fp == NULL) {
        return -1;
    }
    memset(out, 0, sizeof *out);
    rc = rank_ledger_read_last_update(fp, out);
    if (fclose(fp) != 0) {
        die(path);
    }
    return rc;
}

static int ledger_last_update_text(const char *text, RankTime *out)
{
    write_text(g_io, text);
    return ledger_last_update(g_io, out);
}

static void case_ul1(void)
{
    static const char *const forms[] = {
        "{\"lastUpdate\":\"2026-09-28T21:40:00\",\"backups\":[]}",
        "{\n  \"lastUpdate\" : \"2026-09-28T21:40:00\" ,\n  \"backups\" : [ ]\n}\n",
        "{\"schema\":1,\"note\":\"x\",\"lastUpdate\":\"2026-09-28T21:40:00\",\"backups\":[]}"
    };
    size_t i;

    for (i = 0; i < sizeof forms / sizeof forms[0]; i++) {
        RankTime t;
        if (!eq_long(__LINE__, lbl("lastUpdate read", (long)i),
                     ledger_last_update_text(forms[i], &t), 0)) {
            continue;
        }
        eq_long(__LINE__, lbl("year", (long)i), t.year, 2026);
        eq_long(__LINE__, lbl("month", (long)i), t.month, 9);
        eq_long(__LINE__, lbl("day", (long)i), t.day, 28);
        eq_long(__LINE__, lbl("hour", (long)i), t.hour, 21);
        eq_long(__LINE__, lbl("minute", (long)i), t.minute, 40);
        eq_long(__LINE__, lbl("second", (long)i), t.second, 0);
    }
}

static void case_ul2(void)
{
    static const char *const bad[] = {
        "{\"backups\":[]}",                                 /* key absent */
        "{\"lastUpdate\":\"2026-09-28T21:40:0\"}",          /* 18 bytes */
        "{\"lastUpdate\":\"2026-09-28T21:40:000\"}",        /* 20 bytes */
        "{\"lastUpdate\":\"2026-09-2xT21:40:00\"}",         /* non-digit */
        "{\"lastUpdate\":\"2026-09-28T21:40:00}",           /* unterminated */
        "{\"lastUpdate\":20260928214000}",                  /* not a string */
        "{\"lastUpdate\":null}",
        "",
        "{"
    };
    size_t i;

    for (i = 0; i < sizeof bad / sizeof bad[0]; i++) {
        RankTime t;
        eq_long(__LINE__, lbl("corrupt lastUpdate refused", (long)i),
                ledger_last_update_text(bad[i], &t), -1);
    }
}
/* ----------------------------------------------------------------------- I
 *
 * Thirteen names a run may open: the five that outlive it, the four
 * temporaries and the four copies the commit step parks the outgoing file in.
 */

typedef struct {
    char dir[TPATH];
    char scr[TPATH];        /* NX93.SCR    */
    char txt[TPATH];        /* NX93.TXT    */
    char r30[TPATH];        /* RANK30.TXT  */
    char who[TPATH];        /* RANKWHO.TXT */
    char retired[TPATH];    /* NX93OLD.TXT */
    char tmpscr[TPATH];
    char tmptxt[TPATH];
    char tmp30[TPATH];
    char tmpwho[TPATH];
    char oldscr[TPATH];
    char oldtxt[TPATH];
    char old30[TPATH];
    char oldwho[TPATH];
} Env;

static void env_make(Env *e, const char *tag)
{
    case_dir(tag, e->dir);
    snprintf(e->scr,     sizeof e->scr,     "%s/NX93.SCR",    e->dir);
    snprintf(e->txt,     sizeof e->txt,     "%s/NX93.TXT",    e->dir);
    snprintf(e->r30,     sizeof e->r30,     "%s/RANK30.TXT",  e->dir);
    snprintf(e->who,     sizeof e->who,     "%s/RANKWHO.TXT", e->dir);
    snprintf(e->retired, sizeof e->retired, "%s/NX93OLD.TXT", e->dir);
    snprintf(e->tmpscr,  sizeof e->tmpscr,  "%s/RANKTMP.SCR", e->dir);
    snprintf(e->tmptxt,  sizeof e->tmptxt,  "%s/RANKTMP.TXT", e->dir);
    snprintf(e->tmp30,   sizeof e->tmp30,   "%s/RANKTMP.30",  e->dir);
    snprintf(e->tmpwho,  sizeof e->tmpwho,  "%s/RANKTMP.WHO", e->dir);
    snprintf(e->oldscr,  sizeof e->oldscr,  "%s/RANKOLD.SCR", e->dir);
    snprintf(e->oldtxt,  sizeof e->oldtxt,  "%s/RANKOLD.TXT", e->dir);
    snprintf(e->old30,   sizeof e->old30,   "%s/RANKOLD.30",  e->dir);
    snprintf(e->oldwho,  sizeof e->oldwho,  "%s/RANKOLD.WHO", e->dir);
}

/* The state file, written by hand so that no case depends on the writer it is
 * there to check.  Forty five bytes, of which nineteen are the instant of the
 * last reset. */
static void env_state(const Env *e, const char *last_update)
{
    char buf[80];
    snprintf(buf, sizeof buf, "{\r\n  \"lastUpdate\": \"%s\"\r\n}\r\n", last_update);
    write_text(e->txt, buf);
}

static void set_clock(const char *s)
{
    RankTime t;
    if (rank_time_parse(s, &t) != 0) {
        fprintf(stderr, "test_rank: harness failure: bad clock %s\n", s);
        exit(2);
    }
    rank_test_now = t;
}

/* The five files a run may legitimately leave behind. */
static void env_snapshot(const Env *e)
{
    snap(e->scr, "scr");
    snap(e->txt, "txt");
    snap(e->r30, "r30");
    snap(e->who, "who");
    snap(e->retired, "retired");
}

static void expect_unchanged(const Env *e)
{
    OK("NX93.SCR is byte for byte what it was",    snap_same(e->scr, "scr"));
    OK("NX93.TXT is byte for byte what it was",    snap_same(e->txt, "txt"));
    OK("RANK30.TXT is byte for byte what it was",  snap_same(e->r30, "r30"));
    OK("RANKWHO.TXT is byte for byte what it was", snap_same(e->who, "who"));
    OK("NX93OLD.TXT is byte for byte what it was", snap_same(e->retired, "retired"));
}

/* Nothing of the implementation's own making may outlive a run, on any path.
 * RANKOLD.* is where the commit step parks the outgoing file, so a leftover
 * there is as much a defect as a leftover RANKTMP.*: the run before it did not
 * finish tidying up.  (The one deliberate exception is a stranded commit, where
 * RANKOLD.* is the only surviving copy and the program says so; no case here
 * provokes that, because it needs the rename back to fail too - test_daily.c's
 * I-43 and I-53 do, with an injection point of their own.) */
static void expect_no_temps(const Env *e)
{
    OK("RANKTMP.SCR was cleaned up", !file_exists(e->tmpscr));
    OK("RANKTMP.TXT was cleaned up", !file_exists(e->tmptxt));
    OK("RANKTMP.30 was cleaned up",  !file_exists(e->tmp30));
    OK("RANKTMP.WHO was cleaned up", !file_exists(e->tmpwho));
    OK("RANKOLD.SCR was cleaned up", !file_exists(e->oldscr));
    OK("RANKOLD.TXT was cleaned up", !file_exists(e->oldtxt));
    OK("RANKOLD.30 was cleaned up",  !file_exists(e->old30));
    OK("RANKOLD.WHO was cleaned up", !file_exists(e->oldwho));
}

/* A file of rows: each line verbatim plus CR LF, no trailing 0x1A. */
static void expect_rows(const char *path, const char *const *want, int n,
                        const char *what)
{
    char buf[2048];
    size_t at = 0;
    char *got;
    size_t len;
    int i;

    for (i = 0; i < n; i++) {
        size_t l = strlen(want[i]);
        memcpy(buf + at, want[i], l);
        at += l;
        buf[at++] = '\r';
        buf[at++] = '\n';
    }
    got = read_file(path, &len);
    if (got == NULL) {
        note(__LINE__, what);
        return;
    }
    eq_mem(__LINE__, what, got, len, buf, at);
    free(got);
}

static void expect_scr(const Env *e, const char *const *want, int n)
{
    expect_rows(e->scr, want, n, "NX93.SCR holds the rebuilt table");
}

/* The canonical state file, spelled out here rather than taken from the writer
 * it is checking: forty five bytes, nineteen of which are the instant. */
static void expect_state(const char *path, const char *ts, const char *what)
{
    char want[80];
    char *got;
    size_t len;

    snprintf(want, sizeof want, "{\r\n  \"lastUpdate\": \"%s\"\r\n}\r\n", ts);
    got = read_file(path, &len);
    if (got == NULL) {
        note(__LINE__, what);
        return;
    }
    eq_mem(__LINE__, what, got, len, want, strlen(want));
    free(got);
}

/* The four files a reset of the live table produces, asserted together: the
 * two records files first, because the point of the order is that nothing
 * leaves NX93.SCR before it has arrived in them. */
static void expect_reset(const Env *e, const char *ts)
{
    expect_rows(e->r30, SCR_ROWS, RANK_ROWS, "RANK30.TXT holds all ten records");
    expect_rows(e->who, WHO_WANT, 1, "RANKWHO.TXT holds the one player");
    expect_scr(e, UM1_WANT, RANK_ROWS);
    expect_state(e->txt, ts, "NX93.TXT holds the instant of this reset");
}

/* The calendar anchors every integration case below is built on.  2026-09-21
 * and 2026-09-28 are Mondays, so the Sunday between them closes the first
 * one's week; that pair is where the weekly rule says "no" and the day after
 * says "yes", with no reference to how many days have gone by. */
#define R_PREVWK "2026-09-14T00:00:00"   /* a Monday, two weeks back */
#define R_LAST   "2026-09-21T00:00:00"   /* a Monday */
#define R_MIDWK  "2026-09-24T09:00:00"   /* the Thursday of its week */
#define R_SAMEWK "2026-09-27T23:59:59"   /* the Sunday that closes it */
#define R_NEXTWK "2026-09-28T21:40:00"   /* the next Monday: the rule crosses */

/* Inside one week nothing happens, however near the boundary the clock gets.
 * The second instant is one second short of the Monday, which under the rule
 * this replaced - seven whole days - was also "not yet", and under the new one
 * is "not yet" for an entirely different reason. */
static void case_i1(void)
{
    static const char *const when[2] = { R_MIDWK, R_SAMEWK };
    int i;

    for (i = 0; i < 2; i++) {
        Env e;
        env_make(&e, lbl("i1", i));
        write_bytes(e.scr, fix_scr, FIX_SCR_LEN);
        env_state(&e, R_LAST);
        env_snapshot(&e);
        set_clock(when[i]);

        eq_long(__LINE__, lbl("a run inside one week does nothing", i),
                rank_run(e.dir, RANK_MODE_WEEKLY), 0);
        expect_unchanged(&e);
        expect_no_temps(&e);
    }
}

/* The Monday itself.  Six days and twenty three hours separate these two
 * instants, which the retired rule would have called "not yet". */
static void case_i2(void)
{
    Env e;

    env_make(&e, "i2");
    write_bytes(e.scr, fix_scr, FIX_SCR_LEN);
    env_state(&e, R_LAST);
    set_clock(R_NEXTWK);

    EQL("crossing a Monday resets", rank_run(e.dir, RANK_MODE_WEEKLY), 0);
    expect_reset(&e, R_NEXTWK);
    OK("no ledger was retired, because there was none", !file_exists(e.retired));
    expect_no_temps(&e);
}

/* Two Mondays in the gap, and still exactly one reset. */
static void case_i3(void)
{
    Env e;

    env_make(&e, "i3");
    write_bytes(e.scr, fix_scr, FIX_SCR_LEN);
    env_state(&e, R_PREVWK);
    set_clock(R_NEXTWK);

    EQL("two boundaries in the gap still reset once",
        rank_run(e.dir, RANK_MODE_WEEKLY), 0);
    expect_reset(&e, R_NEXTWK);
    env_snapshot(&e);
    EQL("and the next run has nothing left to do",
        rank_run(e.dir, RANK_MODE_WEEKLY), 0);
    expect_unchanged(&e);
    expect_no_temps(&e);
}

/* No NX93.TXT at all means nothing records a reset, so this run is the first
 * one.  The two tables are written before NX93.SCR is touched, so the records
 * are never lost to this path. */
static void case_i4(void)
{
    Env e;

    env_make(&e, "i4");
    write_bytes(e.scr, fix_scr, FIX_SCR_LEN);
    set_clock(R_NEXTWK);

    EQL("a missing state file resets on the spot",
        rank_run(e.dir, RANK_MODE_WEEKLY), 0);
    OK("NX93.TXT was created", file_exists(e.txt));
    expect_reset(&e, R_NEXTWK);
    expect_no_temps(&e);
}

/* An NX93.TXT we cannot make sense of is not a licence to overwrite it.  Fail
 * closed: the first four cannot even be read, the fifth is readable but is
 * neither this program's state file nor an old ledger, and the sixth carries
 * the backups key and then breaks the grammar behind it. */
static void case_i5(void)
{
    static const char *const bad[] = {
        "{\"backups\":[]}",                                 /* no lastUpdate */
        "{\"lastUpdate\":\"2026-09-2xT21:40:00\",\"backups\":[]}",
        "{\"lastUpdate\":\"2026-09-28T21:40:0\",\"backups\":[]}",
        "",
        "not json at all",
        "{\"lastUpdate\":\"" R_LAST "\"}",
        "{\"lastUpdate\":\"" R_LAST "\",\"backups\":null}"
    };
    size_t i;

    for (i = 0; i < sizeof bad / sizeof bad[0]; i++) {
        Env e;
        env_make(&e, lbl("i5", (long)i));
        write_bytes(e.scr, fix_scr, FIX_SCR_LEN);
        write_text(e.txt, bad[i]);
        env_snapshot(&e);
        set_clock(R_NEXTWK);

        eq_long(__LINE__, lbl("an unusable state file fails without writing",
                              (long)i),
                rank_run(e.dir, RANK_MODE_WEEKLY), 1);
        expect_unchanged(&e);
        OK(lbl("no all-time table was created", (long)i), !file_exists(e.r30));
        OK(lbl("no per-user table either", (long)i), !file_exists(e.who));
        expect_no_temps(&e);
    }
}

/* The two records files have to be on disk before NX93.SCR is touched.  Break
 * the NX93.SCR write and the rows it is about to lose must already be in them,
 * or a crash at that moment loses them for good. */
static void case_i6(void)
{
    static const RankFault faults[2] = { RANK_FAULT_SCR_WRITE, RANK_FAULT_SCR_COMMIT };
    int i;

    for (i = 0; i < 2; i++) {
        Env e;
        env_make(&e, lbl("i6", i));
        write_bytes(e.scr, fix_scr, FIX_SCR_LEN);
        env_state(&e, R_LAST);
        snap(e.scr, "scr");
        snap(e.txt, "txt");
        set_clock(R_NEXTWK);

        rank_test_fault = faults[i];
        eq_long(__LINE__, lbl("a failed NX93.SCR write is reported", i),
                rank_run(e.dir, RANK_MODE_WEEKLY), 1);
        rank_test_fault = RANK_FAULT_NONE;

        OK(lbl("NX93.SCR is untouched", i), snap_same(e.scr, "scr"));
        OK(lbl("and so is NX93.TXT, the stage after it", i),
           snap_same(e.txt, "txt"));
        expect_rows(e.r30, SCR_ROWS, RANK_ROWS,
                    "RANK30.TXT already holds every record");
        expect_rows(e.who, WHO_WANT, 1, "and RANKWHO.TXT the one player");
        expect_no_temps(&e);
    }
}

/* The mirror image, with the order the other way round from the program this
 * replaced: NX93.TXT is the last file written, so a failure there leaves the
 * other three new and the reset simply happens again next time.  Recording the
 * reset first would be the unrecoverable order - a run that then failed to
 * rewrite NX93.SCR would never reset again. */
static void case_i7(void)
{
    static const RankFault faults[2] = { RANK_FAULT_TXT_WRITE, RANK_FAULT_TXT_COMMIT };
    int i;

    for (i = 0; i < 2; i++) {
        Env e;
        env_make(&e, lbl("i7", i));
        write_bytes(e.scr, fix_scr, FIX_SCR_LEN);
        env_state(&e, R_LAST);
        snap(e.txt, "txt");
        set_clock(R_NEXTWK);

        rank_test_fault = faults[i];
        eq_long(__LINE__, lbl("a failed state write is reported", i),
                rank_run(e.dir, RANK_MODE_WEEKLY), 1);
        rank_test_fault = RANK_FAULT_NONE;

        OK(lbl("NX93.TXT still holds the old instant", i),
           snap_same(e.txt, "txt"));
        expect_rows(e.r30, SCR_ROWS, RANK_ROWS, "RANK30.TXT is new");
        expect_rows(e.who, WHO_WANT, 1, "RANKWHO.TXT is new");
        expect_scr(&e, UM1_WANT, RANK_ROWS);
        expect_no_temps(&e);

        eq_long(__LINE__, lbl("and the retry finishes the job", i),
                rank_run(e.dir, RANK_MODE_WEEKLY), 0);
        expect_reset(&e, R_NEXTWK);
        expect_no_temps(&e);
    }
}

/* A fresh install has no NX93.SCR.  That is zero rows, not an error, so the
 * run repairs it with the defaults - and the two tables are created empty
 * rather than left absent, because nothing left NX93.SCR to put in them. */
static void case_i8(void)
{
    Env e;
    char *buf;
    size_t len;

    env_make(&e, "i8");
    env_state(&e, R_LAST);
    set_clock(R_NEXTWK);

    EQL("a missing NX93.SCR is not an error",
        rank_run(e.dir, RANK_MODE_WEEKLY), 0);
    expect_no_temps(&e);

    buf = read_file(e.scr, &len);
    if (OK("NX93.SCR was created", buf != NULL)) {
        EQM("and it is the distribution table", buf, len, fix_def, FIX_DEF_LEN);
        free(buf);
    }
    OK("RANK30.TXT exists", file_exists(e.r30));
    EQL("and is empty rather than absent", file_size(e.r30), 0L);
    OK("RANKWHO.TXT exists", file_exists(e.who));
    EQL("and is empty rather than absent", file_size(e.who), 0L);
    expect_state(e.txt, R_NEXTWK, "NX93.TXT holds the instant of the reset");
}

static void case_i11(void)
{
    Env e;
    int i;

    env_make(&e, "i11");
    write_bytes(e.scr, fix_scr, FIX_SCR_LEN);
    env_state(&e, R_LAST);
    env_snapshot(&e);
    set_clock(R_MIDWK);

    for (i = 0; i < 5; i++) {
        eq_long(__LINE__, lbl("run", i), rank_run(e.dir, RANK_MODE_WEEKLY), 0);
        expect_unchanged(&e);
    }
    expect_no_temps(&e);
}

/* If lastUpdate is not moved forward, the very next launch resets again and
 * the table never survives a session. */
static void case_i12(void)
{
    Env e;

    env_make(&e, "i12");
    write_bytes(e.scr, fix_scr, FIX_SCR_LEN);
    env_state(&e, R_LAST);
    set_clock(R_NEXTWK);

    EQL("the reset runs", rank_run(e.dir, RANK_MODE_WEEKLY), 0);
    expect_reset(&e, R_NEXTWK);
    env_snapshot(&e);

    EQL("running again at the same instant does nothing",
        rank_run(e.dir, RANK_MODE_WEEKLY), 0);
    expect_unchanged(&e);
    expect_no_temps(&e);
}

/* A directory we cannot write is the honest "write is impossible" case: the
 * first temporary file cannot even be created. */
static void case_i13a(void)
{
    Env e;
    int rc;

    env_make(&e, "i13a");
    write_bytes(e.scr, fix_scr, FIX_SCR_LEN);
    env_state(&e, R_LAST);
    env_snapshot(&e);
    set_clock(R_NEXTWK);

    if (chmod(e.dir, 0555) != 0) {
        die(e.dir);
    }
    rc = rank_run(e.dir, RANK_MODE_WEEKLY);
    if (chmod(e.dir, 0755) != 0) {
        die(e.dir);
    }

    EQL("a read-only directory is reported, not worked around", rc, 1);
    expect_unchanged(&e);
    expect_no_temps(&e);
}

/* A read-only NX93.SCR in a writable directory is not a write barrier on
 * POSIX: rename(2) replaces it regardless of its mode, and MS-DOS behaves the
 * same way once the file is deleted first.  So the oracle here is the
 * all-or-nothing one, not "must fail".  See the note in README.md. */
static void case_i13b(void)
{
    Env e;
    int rc;

    env_make(&e, "i13b");
    write_bytes(e.scr, fix_scr, FIX_SCR_LEN);
    env_state(&e, R_LAST);
    env_snapshot(&e);
    set_clock(R_NEXTWK);

    if (chmod(e.scr, 0444) != 0) {
        die(e.scr);
    }
    rc = rank_run(e.dir, RANK_MODE_WEEKLY);
    (void)chmod(e.scr, 0644);

    if (rc == 1) {
        expect_unchanged(&e);
    } else if (EQL("the run either failed or succeeded, nothing else", rc, 0)) {
        expect_reset(&e, R_NEXTWK);
    }
    expect_no_temps(&e);
}

static void case_i14(void)
{
    char dir[TPATH];
    int i;

    for (i = 0; i < RANK_PATH_MAX + 120; i++) {
        dir[i] = 'x';
    }
    dir[RANK_PATH_MAX + 120] = '\0';
    set_clock(R_NEXTWK);
    EQL("a directory name that cannot fit the path buffer is refused",
        rank_run(dir, RANK_MODE_WEEKLY), 1);
}

/* D5: more rows than the game keeps is a signal that something else wrote the
 * file.  Truncating it silently would throw records away. */
static void case_i15(void)
{
    Env e;
    char buf[1024];

    env_make(&e, "i15");
    memcpy(buf, fix_scr, FIX_SCR_LEN);
    memcpy(buf + FIX_SCR_LEN, "060000 16 07 Bio_100% 93/10/01 A\r\n", 34);
    write_bytes(e.scr, buf, FIX_SCR_LEN + 34u);
    env_state(&e, R_LAST);
    env_snapshot(&e);
    set_clock(R_NEXTWK);

    EQL("eleven rows stop the run", rank_run(e.dir, RANK_MODE_WEEKLY), 1);
    expect_unchanged(&e);
    OK("no all-time table was created", !file_exists(e.r30));
    OK("no per-user table either", !file_exists(e.who));
    expect_no_temps(&e);
}

/* D4: one unreadable row and the whole run stops.  Dropping the row would
 * destroy a record with no way back. */
static void case_i16(void)
{
    static const char *const junk[] = {
        "garbage",
        "32740X 30 01 OMZN     26/09/15 A",
        "327400 30 01 OMZN    X26/09/15 A"
    };
    size_t i;

    for (i = 0; i < sizeof junk / sizeof junk[0]; i++) {
        Env e;
        char buf[1024];
        size_t at;

        env_make(&e, lbl("i16", (long)i));
        memcpy(buf, fix_scr, FIX_SCR_LEN - 34u);   /* nine good rows */
        at = FIX_SCR_LEN - 34u;
        memcpy(buf + at, junk[i], strlen(junk[i]));
        at += strlen(junk[i]);
        buf[at++] = '\r';
        buf[at++] = '\n';
        write_bytes(e.scr, buf, at);
        env_state(&e, R_LAST);
        env_snapshot(&e);
        set_clock(R_NEXTWK);

        eq_long(__LINE__, lbl("an unreadable row stops the run", (long)i),
                rank_run(e.dir, RANK_MODE_WEEKLY), 1);
        expect_unchanged(&e);
        OK(lbl("no all-time table was created", (long)i), !file_exists(e.r30));
        OK(lbl("no per-user table either", (long)i), !file_exists(e.who));
        expect_no_temps(&e);
    }
}

/* The DOS main calls rank_run with no directory at all when DOS 2.x leaves
 * argv[0] empty: the 98 side never builds a path. */
static void case_i17(void)
{
    Env e;
    char cwd[TPATH];
    int i;

    if (getcwd(cwd, sizeof cwd) == NULL) {
        die("getcwd");
    }
    for (i = 0; i < 2; i++) {
        env_make(&e, lbl("i17", i));
        write_bytes(e.scr, fix_scr, FIX_SCR_LEN);
        env_state(&e, R_LAST);
        set_clock(R_NEXTWK);

        if (chdir(e.dir) != 0) {
            die(e.dir);
        }
        eq_long(__LINE__, lbl(i == 0 ? "rank_run(NULL)" : "rank_run(\"\")", i),
                rank_run(i == 0 ? NULL : "", RANK_MODE_WEEKLY), 0);
        if (chdir(cwd) != 0) {
            die(cwd);
        }
        expect_reset(&e, R_NEXTWK);
        expect_no_temps(&e);
    }
}

/* DOS has no rename that replaces, so committing means moving the outgoing file
 * aside to RANKOLD.* first and only then putting the new one in its place.  The
 * two cases below break that second rename, which is the step that leaves the
 * real name briefly unoccupied, and check that the recovery path really runs
 * rather than merely being written down. */

/* NX93.TXT is the last file committed.  If its rename fails, the recovery
 * rename has to put it back - the target holding its old bytes afterwards is
 * the only evidence on this machine that the recovery ran at all, since a
 * commit that never started leaves exactly the same file behind. */
static void case_i18(void)
{
    Env e;

    env_make(&e, "i18");
    write_bytes(e.scr, fix_scr, FIX_SCR_LEN);
    env_state(&e, R_LAST);
    snap(e.txt, "txt");
    set_clock(R_NEXTWK);

    rank_test_fault = RANK_FAULT_TXT_RENAME;
    EQL("a failed state rename is reported", rank_run(e.dir, RANK_MODE_WEEKLY), 1);
    rank_test_fault = RANK_FAULT_NONE;

    OK("NX93.TXT still holds its old bytes", snap_same(e.txt, "txt"));
    OK("and the copy it was parked in is gone", !file_exists(e.oldtxt));
    expect_no_temps(&e);

    /* Nothing was committed by halves: retrying at the same instant succeeds.
     * If the failed attempt had left NX93.TXT in an in-between state, this
     * second run would fail or produce something other than the expected
     * four files. */
    EQL("retrying straight away succeeds", rank_run(e.dir, RANK_MODE_WEEKLY), 0);
    expect_reset(&e, R_NEXTWK);
    expect_no_temps(&e);
}

/* NX93.SCR is committed third, after both records files are already in place.
 * If its rename fails, NX93.SCR must roll back but the two tables must NOT:
 * they hold the only copy of the rows a reset takes out of it, and throwing
 * them away to make the files agree would be the one unrecoverable move. */
static void case_i19(void)
{
    Env e;

    env_make(&e, "i19");
    write_bytes(e.scr, fix_scr, FIX_SCR_LEN);
    env_state(&e, R_LAST);
    snap(e.scr, "scr");
    snap(e.txt, "txt");
    set_clock(R_NEXTWK);

    rank_test_fault = RANK_FAULT_SCR_RENAME;
    EQL("a failed table rename is reported", rank_run(e.dir, RANK_MODE_WEEKLY), 1);
    rank_test_fault = RANK_FAULT_NONE;

    OK("NX93.SCR rolled back to the byte", snap_same(e.scr, "scr"));
    OK("and the copy it was parked in is gone", !file_exists(e.oldscr));
    OK("NX93.TXT was never reached", snap_same(e.txt, "txt"));
    expect_rows(e.r30, SCR_ROWS, RANK_ROWS,
                "RANK30.TXT did not roll back with it");
    expect_rows(e.who, WHO_WANT, 1, "nor did RANKWHO.TXT");
    expect_no_temps(&e);
}

/* A run killed between the two renames leaves RANKOLD.* behind.  The commit
 * step frees the name before it needs it, so the next run must sail past the
 * debris and clear it; otherwise every later launch trips over it.
 *
 * Only the two names whose target already exists are planted.  On a first
 * reset RANK30.TXT and RANKWHO.TXT are absent, so their commits never move
 * anything aside and never reach the discard - a copy planted under those two
 * names would correctly survive.  The four-name version of this, from a
 * baseline where all four files exist, is test_daily.c's I-46. */
static void case_i20(void)
{
    Env e;

    env_make(&e, "i20");
    write_bytes(e.scr, fix_scr, FIX_SCR_LEN);
    env_state(&e, R_LAST);
    write_text(e.oldscr, "stale table left by an interrupted run\r\n");
    write_text(e.oldtxt, "stale state left by an interrupted run\r\n");
    OK("the debris is really in place", file_exists(e.oldscr) && file_exists(e.oldtxt));
    set_clock(R_NEXTWK);

    EQL("leftover RANKOLD files do not block the run",
        rank_run(e.dir, RANK_MODE_WEEKLY), 0);
    /* The real files must hold the new table and state, not the debris. */
    expect_reset(&e, R_NEXTWK);
    expect_no_temps(&e);

    /* The step being pinned is that the commit frees the RANKOLD name *before*
     * it renames onto it.  A leftover plain file cannot show that on this
     * machine: POSIX rename() replaces an existing target, so dropping the
     * remove() changes nothing here, while on DOS -- where rename fails if the
     * target exists -- it jams every run after an interrupted one.  Occupying
     * the name with an empty directory, which rename() will not overwrite on
     * either system, makes the missing remove() observable.  Verified: with the
     * remove() deleted this variant fails and the plain-file one above does not. */
    env_make(&e, "i20-occupied");
    write_bytes(e.scr, fix_scr, FIX_SCR_LEN);
    env_state(&e, R_LAST);
    if (mkdir(e.oldscr, 0755) != 0 || mkdir(e.oldtxt, 0755) != 0) {
        die(e.oldscr);
    }
    set_clock(R_NEXTWK);

    EQL("an occupied RANKOLD name is freed before it is needed",
        rank_run(e.dir, RANK_MODE_WEEKLY), 0);
    expect_reset(&e, R_NEXTWK);
    expect_no_temps(&e);
}

/* ----------------------------------------------------------------------- X */

/* A PC-9801 with a flat RTC battery comes up in 1980.  Sailing past that would
 * park lastUpdate in the future and the reset would never come round again, so
 * the run stops and GAME.BAT tells the player to fix the clock with DATE.
 *
 * The comparison is over all six fields, which is what makes a clock nudged
 * back inside one day visible as well; test_daily.c's X-2 takes that boundary
 * down to the second, and the two variants here stay at a day and a year. */
#define X1_LAST "2026-09-28T21:40:00"

static void case_x1(void)
{
    static const char *const when[2] = {
        "2026-09-27T21:40:00",      /* one day behind lastUpdate */
        "2025-09-28T21:40:00"       /* a year behind it */
    };
    int i;

    for (i = 0; i < 2; i++) {
        Env e;
        RankTime last;
        RankTime now;

        env_make(&e, lbl("x1", i));
        write_bytes(e.scr, fix_scr, FIX_SCR_LEN);
        env_state(&e, X1_LAST);
        env_snapshot(&e);
        set_clock(when[i]);

        /* The premise, stated rather than assumed: the instant the run reads
         * off the clock really does sit before the one NX93.TXT records. */
        if (rank_time_parse(X1_LAST, &last) != 0
         || rank_time_parse(when[i], &now) != 0) {
            die("x1");
        }
        eq_long(__LINE__, lbl("the clock is behind the last reset", i),
                rank_time_before(&now, &last), 1);
        eq_long(__LINE__, lbl("a clock that moved backwards stops the run", i),
                rank_run(e.dir, RANK_MODE_WEEKLY), 1);
        expect_unchanged(&e);
        expect_no_temps(&e);
    }
}

/* -------------------------------------------------------------------- main */

typedef struct {
    const char *name;
    void (*fn)(void);
} Case;

static const Case CASES[] = {
    { "S8 contract-shape",                  case_contract },

    { "U-T1 civil-days-table",              case_ut1 },
    { "U-T2 civil-days-monotone",           case_ut2 },
    { "U-T3 time-parse-validation",         case_ut3 },
    { "U-T4 elapsed-days-boundary",         case_ut4 },
    { "U-T5 elapsed-days-dst-free",         case_ut5 },
    { "U-T6 time-format-roundtrip",         case_ut6 },

    { "U-P1 parse-real-data",               case_up1 },
    { "U-P2 parse-length-bounds",           case_up2 },
    { "U-P3 parse-separator-positions",     case_up3 },
    { "U-P4 parse-score-digits",            case_up4 },
    { "U-P5 parse-name-field",              case_up5 },
    { "U-P6 parse-key-folding",             case_up6 },
    { "U-P7 parse-raw-byte-identical",      case_up7 },

    { "U-R1 read-trailing-eof-marker",      case_ur1 },
    { "U-R2 read-missing-final-crlf",       case_ur2 },
    { "U-R3 read-lf-only",                  case_ur3 },
    { "U-R4 read-empty",                    case_ur4 },
    { "U-R5 read-eleven-rows",              case_ur5 },
    { "U-R6 read-overlong-line",            case_ur6 },
    { "U-R7 read-fuzz",                     case_ur7 },

    { "U-M1 rebuild-live-scr",              case_um1 },
    { "U-M2 rebuild-bak-numeric-order",     case_um2 },
    { "U-M3 rebuild-case-insensitive-dedup", case_um3 },
    { "U-M4 rebuild-sorts-descending",      case_um4 },
    { "U-M5 rebuild-stable-on-ties",        case_um5 },
    { "U-M6 rebuild-empty-gives-defaults",  case_um6 },
    { "U-M7 rebuild-always-ten",            case_um7 },
    { "U-M8 rebuild-defaults-pushed-out",   case_um8 },
    { "U-M9 rebuild-survivor-index",        case_um9 },
    { "U-M10 write-table-shape",            case_um10 },

    { "U-L1 ledger-read-last-update",       case_ul1 },
    { "U-L2 ledger-reject-corrupt",         case_ul2 },

    { "I-1 run-inside-one-week",            case_i1 },
    { "I-2 run-crosses-one-monday",         case_i2 },
    { "I-3 run-crosses-two-mondays",        case_i3 },
    { "I-4 run-first-time-no-state",        case_i4 },
    { "I-5 run-unusable-state-fails-closed", case_i5 },
    { "I-6 run-tables-precede-scr-write",   case_i6 },
    { "I-7 run-state-failure-leaves-tables", case_i7 },
    { "I-8 run-missing-scr",                case_i8 },
    { "I-11 run-idempotent",                case_i11 },
    { "I-12 run-updates-last-update",       case_i12 },
    { "I-13a run-readonly-dir",             case_i13a },
    { "I-13b run-readonly-scr",             case_i13b },
    { "I-14 run-path-too-long",             case_i14 },
    { "I-15 run-eleven-row-scr",            case_i15 },
    { "I-16 run-unparsable-line",           case_i16 },
    { "I-17 run-bare-filenames",            case_i17 },
    { "I-18 run-state-rename-rolls-back",   case_i18 },
    { "I-19 run-scr-rename-keeps-tables",   case_i19 },
    { "I-20 run-clears-stale-retained",     case_i20 },

    { "X-1 run-clock-moved-backwards",      case_x1 }
};

int main(int argc, char **argv)
{
    size_t i;
    const char *tz = getenv("TZ");

    if (argc < 2 || argc > 3) {
        fprintf(stderr, "usage: test_rank <scratch-dir> [name-substring]\n");
        return 2;
    }
    if (strlen(argv[1]) + 64u >= sizeof g_scratch) {
        fprintf(stderr, "test_rank: scratch directory name is too long\n");
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

    for (i = 0; i < sizeof CASES / sizeof CASES[0]; i++) {
        if (g_filter != NULL && strstr(CASES[i].name, g_filter) == NULL) {
            continue;
        }
        g_case = CASES[i].name;
        g_case_failed = 0;
        rank_test_fault = RANK_FAULT_NONE;
        set_clock("2026-09-28T21:40:00");
        CASES[i].fn();
        g_ran++;
        if (g_case_failed) {
            g_failed++;
        }
    }

    /* Leave nothing behind that the shell cannot delete: a case that made a
     * directory unwritable has already restored it, but be certain. */
    rm_tree(g_scratch);

    if (g_ran == 0) {
        fprintf(stderr, "test_rank: no case matched %s\n",
                g_filter != NULL ? g_filter : "(no filter)");
        return 2;
    }
    fprintf(g_failed ? stderr : stdout,
            "test_rank[TZ=%s]: %d case(s), %d assertion(s), %d failure(s)\n",
            tz != NULL ? tz : "(unset)", g_ran, g_asserts, g_failed);
    return g_failed != 0;
}
