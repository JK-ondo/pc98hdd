/*
 * test_daily.c - Mac-side suite for the daily reset of nx93rank/dist/RANK.C.
 *
 *   test_daily <scratch-dir> [name-substring]
 *
 * Exits 0 only when every selected case passed and at least one case ran.
 * Cases are named after .work/plans/nx93-daily-reset/test-design.md, which is
 * the sole authority for every oracle here.  Oracles were derived from spec.md
 * by hand; none of them was read back out of an implementation.  This suite is
 * written before the implementation exists, so it is expected to be red: the
 * compiler's undeclared-symbol list is the implementation's work list.
 *
 * Covered here: U-A, U-C, U-K, U-F, U-G, U-V, I-21..I-53, X-2, X-3.
 * The entry point cases (A-1..A-7) live in test_cli.c, and the static checks
 * (S10..S12, S6', H2..H4) live in run-daily.sh.
 *
 * RANK_NO_MAIN drops RANK.C's own main; RANK_TEST_CLOCK and RANK_TEST_FAULT
 * enable seams the DOS build never compiles (run.sh asserts their absence from
 * the DOS object).  RANK_C_PATH lets run-daily.sh point the suite at a throwaway
 * copy of the source, which is how fault attribution and mutation testing run
 * without touching the repository.
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

#ifndef RANK_C_PATH
#define RANK_C_PATH "../dist/RANK.C"
#endif
#include RANK_C_PATH

#include "fixtures.h"
#include "fixtures_daily.h"
#include "harness_daily.h"

/* ------------------------------------------------------------------ 契約表明
 *
 * The parts of spec.md section 3.9 a compiler can pin.  The fault count is here
 * because adding a seventeenth injection point and forgetting to test it is
 * exactly the accident this suite cannot otherwise see: the enumerator list and
 * the case list are written in different files.
 */
typedef char chk_r30_rows[(RANK30_ROWS == 30) ? 1 : -1];
typedef char chk_who_rows[(RANKWHO_ROWS == 100) ? 1 : -1];
typedef char chk_mode_weekly[(RANK_MODE_WEEKLY == 0) ? 1 : -1];
typedef char chk_mode_daily[(RANK_MODE_DAILY == 1) ? 1 : -1];
typedef char chk_r30_slack[(sizeof(((Rank30Table *)0)->row) / sizeof(RankRow)
                            == RANK30_ROWS + 1) ? 1 : -1];
typedef char chk_who_slack[(sizeof(((RankWhoTable *)0)->row) / sizeof(RankRow)
                            == RANKWHO_ROWS + 1) ? 1 : -1];
typedef char chk_faults[(RANK_FAULT_RETIRE_RENAME == 17) ? 1 : -1];

/* ------------------------------------------------------------- 行の組み立て */

/* score, 8 byte name, fixed 26 byte tail.  32 bytes for a 6 digit score. */
static void make_line(char *out, size_t cap, const char *score, const char *name)
{
    snprintf(out, cap, "%s 30 01 %s 26/09/15 A", score, name);
}

/* The four widths that matter: the shortest row, the width the game writes, the
 * width the distribution file uses, and the longest row.  Every name-field case
 * runs all four, because at six digits an offset counted from the front lands on
 * the same byte as one counted from the end and the case stops proving anything. */
static const char *const SCORES[4] = { "9", "98840", "327400", "123456789" };

/* rank_fold_key is static, so the key is only observable as RankRow.key through
 * rank_parse_line (spec.md section 3.9).  Four slots so that two keys can be
 * compared in one expression. */
static HUNUSED const char *key_for(const char *score, const char *name)
{
    static char buf[4][RANK_NAME_LEN + 1];
    static int turn;
    char *p = buf[turn++ & 3];
    char line[80];
    RankRow r;

    make_line(line, sizeof line, score, name);
    memset(&r, 0, sizeof r);
    if (rank_parse_line(line, &r) != 0) {
        fprintf(stderr, "harness failure: %s: name fixture did not parse\n", g_case);
        exit(2);
    }
    memcpy(p, r.key, sizeof r.key);
    return p;
}

static HUNUSED int row_of(const char *line, RankRow *out)
{
    memset(out, 0, sizeof *out);
    return rank_parse_line(line, out);
}

/* Parse n lines into rows[], aborting the suite if a fixture is malformed: a
 * broken fixture is a harness defect, not a finding about RANK.C. */
static HUNUSED void rows_of(const char *const *lines, int n, RankRow *rows)
{
    int i;
    for (i = 0; i < n; i++) {
        if (row_of(lines[i], &rows[i]) != 0) {
            fprintf(stderr, "harness failure: %s: fixture row %d did not parse\n",
                    g_case, i);
            exit(2);
        }
    }
}

/* ------------------------------------------------------------ FILE* の継ぎ手
 *
 * A real file rather than fmemopen, so each reader meets exactly the stream it
 * meets on DOS, including the zero length case.
 */

static HUNUSED int read_table_bytes(const void *data, size_t len, RankTable *t)
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

static HUNUSED int rows_read_bytes(const void *data, size_t len,
                                   RankRow *rows, int maxrows)
{
    FILE *fp;
    int rc;

    write_bytes(g_io, data, len);
    fp = fopen(g_io, "rb");
    if (fp == NULL) {
        die(g_io);
    }
    rc = rank_rows_read(fp, rows, maxrows);
    if (fclose(fp) != 0) {
        die(g_io);
    }
    return rc;
}

static HUNUSED int r30_load_bytes(const void *data, size_t len, Rank30Table *t)
{
    FILE *fp;
    int rc;

    write_bytes(g_io, data, len);
    fp = fopen(g_io, "rb");
    if (fp == NULL) {
        die(g_io);
    }
    memset(t, 0, sizeof *t);
    rc = rank_r30_load(fp, t);
    if (fclose(fp) != 0) {
        die(g_io);
    }
    return rc;
}

static HUNUSED int who_load_bytes(const void *data, size_t len, RankWhoTable *t)
{
    FILE *fp;
    int rc;

    write_bytes(g_io, data, len);
    fp = fopen(g_io, "rb");
    if (fp == NULL) {
        die(g_io);
    }
    memset(t, 0, sizeof *t);
    rc = rank_who_load(fp, t);
    if (fclose(fp) != 0) {
        die(g_io);
    }
    return rc;
}

static HUNUSED int state_check_bytes(const void *data, size_t len)
{
    FILE *fp;
    int rc;

    write_bytes(g_io, data, len);
    fp = fopen(g_io, "rb");
    if (fp == NULL) {
        die(g_io);
    }
    rc = rank_state_check(fp);
    if (fclose(fp) != 0) {
        die(g_io);
    }
    return rc;
}

static HUNUSED int has_backups_bytes(const void *data, size_t len)
{
    FILE *fp;
    int rc;

    write_bytes(g_io, data, len);
    fp = fopen(g_io, "rb");
    if (fp == NULL) {
        die(g_io);
    }
    rc = rank_has_backups(fp);
    if (fclose(fp) != 0) {
        die(g_io);
    }
    return rc;
}

/* rank_ledger_string starts just past the opening quote, so `data` is the string
 * body plus its closing quote and nothing else (spec.md section 3.9). */
static HUNUSED int ledger_string_bytes(const void *data, size_t len,
                                       char *buf, int bufsize)
{
    FILE *fp;
    int rc;

    write_bytes(g_io, data, len);
    fp = fopen(g_io, "rb");
    if (fp == NULL) {
        die(g_io);
    }
    rc = rank_ledger_string(fp, buf, bufsize);
    if (fclose(fp) != 0) {
        die(g_io);
    }
    return rc;
}

static HUNUSED int import_file(const char *path, Rank30Table *t30, RankWhoTable *who)
{
    FILE *fp;
    int rc;

    fp = fopen(path, "rb");
    if (fp == NULL) {
        die(path);
    }
    rc = rank_import_ledger(fp, t30, who);
    if (fclose(fp) != 0) {
        die(path);
    }
    return rc;
}

static HUNUSED int import_bytes(const void *data, size_t len,
                                Rank30Table *t30, RankWhoTable *who)
{
    write_bytes(g_io, data, len);
    return import_file(g_io, t30, who);
}

static HUNUSED char *rows_written_bytes(const RankRow *rows, int count, size_t *len)
{
    FILE *fp = fopen(g_io, "wb");
    int rc;

    if (fp == NULL) {
        die(g_io);
    }
    rc = rank_rows_write(fp, rows, count);
    if (fclose(fp) != 0) {
        die(g_io);
    }
    if (rc != 0) {
        return NULL;
    }
    return read_file(g_io, len);
}

static HUNUSED char *state_written_bytes(const RankTime *now, size_t *len)
{
    FILE *fp = fopen(g_io, "wb");
    int rc;

    if (fp == NULL) {
        die(g_io);
    }
    rc = rank_state_write(fp, now);
    if (fclose(fp) != 0) {
        die(g_io);
    }
    if (rc != 0) {
        return NULL;
    }
    return read_file(g_io, len);
}

/* ------------------------------------------------------------ 行表のファイル */

/* 101 rows of 34 bytes is 3434, so one buffer covers every table this suite
 * writes or expects. */
#define ROWSBUF 8192

static HUNUSED size_t rows_image(const char *const *lines, int n, char *out)
{
    size_t at = 0;
    int i;

    for (i = 0; i < n; i++) {
        size_t l = strlen(lines[i]);
        memcpy(out + at, lines[i], l);
        at += l;
        out[at++] = '\r';
        out[at++] = '\n';
    }
    return at;
}

static HUNUSED void write_rows_file(const char *path, const char *const *lines, int n)
{
    static char buf[ROWSBUF];
    size_t at = rows_image(lines, n, buf);
    write_bytes(path, buf, at);
}

static HUNUSED void expect_rows_file(const char *path, const char *const *lines,
                                     int n, const char *what)
{
    static char buf[ROWSBUF];
    size_t at = rows_image(lines, n, buf);
    char *got;
    size_t len;

    got = read_file(path, &len);
    if (!is_true(__FILE__, __LINE__, lbl_s(what, "exists"), got != NULL)) {
        return;
    }
    eq_mem(__FILE__, __LINE__, what, got, len, buf, at);
    free(got);
}

static HUNUSED void expect_file_bytes(const char *path, const void *want, size_t wlen,
                                      const char *what)
{
    char *got;
    size_t len;

    got = read_file(path, &len);
    if (!is_true(__FILE__, __LINE__, lbl_s(what, "exists"), got != NULL)) {
        return;
    }
    eq_mem(__FILE__, __LINE__, what, got, len, want, wlen);
    free(got);
}

/* How many rows a file on disk holds, by the reader's own conventions.  -1 when
 * the file is absent or unreadable. */
static HUNUSED int rows_in_file(const char *path, RankRow *rows, int maxrows)
{
    FILE *fp = fopen(path, "rb");
    int rc;

    if (fp == NULL) {
        return -1;
    }
    rc = rank_rows_read(fp, rows, maxrows);
    (void)fclose(fp);
    return rc;
}

/* ------------------------------------------------------------- 状態ファイル */

#define CANON_TS  "2026-10-26T02:00:00"
#define CANON_LEN 45u

static HUNUSED size_t canon_state(char *out, size_t cap, const char *ts)
{
    snprintf(out, cap, "{\r\n  \"lastUpdate\": \"%s\"\r\n}\r\n", ts);
    return strlen(out);
}

static HUNUSED void expect_state(const char *path, const char *ts, const char *what)
{
    char want[80];
    size_t n = canon_state(want, sizeof want, ts);
    expect_file_bytes(path, want, n, what);
}

/* The canonical file is byte-identical apart from its timestamp field, which is
 * offsets 20..38 (spec.md section 3.4).  I-27 and I-44 compare across runs whose
 * clocks differ, so they need this and not a plain byte compare. */
static HUNUSED int state_same_but_time(const char *path, const char *ref, size_t reflen)
{
    char *got;
    size_t len;
    int same;

    got = read_file(path, &len);
    if (got == NULL) {
        return 0;
    }
    same = (len == reflen && len == CANON_LEN
            && memcmp(got, ref, 20u) == 0
            && memcmp(got + 39, ref + 39, 6u) == 0);
    free(got);
    return same;
}

/* ------------------------------------------------------------- rank_run 呼び */

/* Every rank_run in this suite goes through the capture, so that the screen
 * oracles are always available and the suite's own output stays readable. */
static HUNUSED int rr_dir(const char *dir, int mode)
{
    int rc;

    cap_begin();
    rc = rank_run(dir, mode);
    cap_end();
    return rc;
}

static HUNUSED int rr(const Env *e, int mode)
{
    return rr_dir(e->dir, mode);
}

static HUNUSED int cap_has_any(const char *const *words, int n)
{
    int i;
    for (i = 0; i < n; i++) {
        if (cap_has(words[i])) {
            return 1;
        }
    }
    return 0;
}

/* W2's wording is the implementation's to choose, so the design fixes a floor
 * rather than a spelling (test-design.md section 10.7): the retained name has to
 * appear verbatim, and the line has to say that the copy goes away and that
 * saving it first is an option.  The two vocabulary sets are lexical proxies,
 * not a check that the sentence reads well; that stays with audit. */
static const char *const W2_GONE[7] = {
    "discard", "discarded", "delete", "deleted", "remove", "removed", "removing"
};
static const char *const W2_SAVE[5] = { "save", "rename", "copy", "keep", "before" };

/* The four retained names, which is what distinguishes a W2 report from any
 * other line.  The negative variants assert the absence of these rather than the
 * absence of the vocabulary above, because words like "save" and "before" can
 * legitimately appear in a success or error sentence. */
static const char *const KEEP_NAMES[4] = {
    "RANKOLD.30", "RANKOLD.WHO", "RANKOLD.SCR", "RANKOLD.TXT"
};

static HUNUSED int cap_has_any_keep_name(void)
{
    int i;
    for (i = 0; i < 4; i++) {
        if (cap_has(KEEP_NAMES[i])) {
            return 1;
        }
    }
    return 0;
}

/* --------------------------------------------------------------- 期待する表 */

/* U-G8: the live 2026-10-13 table reset under decisions 7 and 8.  One real row
 * survives (OMZN 327400, with OMZN@@@@ folded into it) and nine shipped rows
 * follow; 200000 appears once and 020000 is pushed out.  Ten rows, 340 bytes. */
static const char *const G8_WANT[RANK_ROWS] = {
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

/* I-26: RANK30.TXT after one reset of the live table.  The six non-shipped rows
 * of fix_scr13, in the order they already stood (descending).  204 bytes. */
static const char *const I26_R30[6] = {
    "327400 30 01 OMZN     26/09/15 A",
    "315970 30 02 omzn     26/10/13 A",
    "300820 30 01 omzn     26/10/08 A",
    "300280 30 04 omzn     26/10/13 A",
    "299370 30 00 OMZN@@@@ 61/11/17 A",
    "286530 30 01 omzn     26/10/09 A"
};

/* I-26: RANKWHO.TXT after the same reset.  All six rows fold to the key OMZN
 * under decision 8, so one row survives.  34 bytes. */
static const char *const I26_WHO[1] = {
    "327400 30 01 OMZN     26/09/15 A"
};

/* I-37 section 10.8: RANK30.TXT after migrating the live ledger.  Apply order is
 * existing table (empty), then the six SCR candidates, then the ten ledger rows;
 * two rows appear in both and dedupe by byte.  Fourteen rows, 476 bytes, and
 * unchanged by decision 8 because the all-time table dedupes on bytes, not keys. */
static const char *const I37_R30[14] = {
    "327400 30 01 OMZN     26/09/15 A",
    "316770 30 02 OMZN     26/09/17 A",
    "315970 30 02 omzn     26/10/13 A",
    "315870 30 01 OMZN     26/09/16 A",
    "304890 30 00 omzn     26/09/25 A",
    "300820 30 01 omzn     26/10/08 A",
    "300280 30 04 omzn     26/10/13 A",
    "299370 30 00 OMZN@@@@ 61/11/17 A",
    "291290 30 02 omzn     26/09/18 A",
    "286530 30 01 omzn     26/10/09 A",
    "286340 30 01 omzn     26/09/15 A",
    "285680 30 05 omzn     26/09/15 A",
    "265680 29 06 omzn     26/09/25 A",
    "261740 29 05 omzn     26/09/14 A"
};

/* U-V3: the ten rows of the live ledger, which already stand in descending
 * order, as rank_import_ledger must leave them in the all-time table. */
static const char *const LEDGER_ROWS[10] = {
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

/* U-G10 section 8.1: ten synthetic rows whose only variable is the score, so the
 * only thing the case can be deciding is how scores compare.  Every name ends in
 * an alphanumeric, so decision 8 drops nothing and the keys P1..PA are all
 * distinct; the case is therefore immune to the key rule.  All twenty score
 * values (these ten plus the shipped ten) are distinct, so no tie is involved. */
static const char *const G10_IN[RANK_ROWS] = {
    "900000 30 01 P1       26/09/15 A",
    "98840 30 01 P2       26/09/15 A",
    "81000 30 01 P3       26/09/15 A",
    "75170 30 01 P4       26/09/15 A",
    "64830 30 01 P5       26/09/15 A",
    "54600 30 01 P6       26/09/15 A",
    "40520 30 01 P7       26/09/15 A",
    "38400 30 01 P8       26/09/15 A",
    "26010 30 01 P9       26/09/15 A",
    "15030 30 01 PA       26/09/15 A"
};

/* Ten rows, 338 bytes: eight of 32 bytes and two of 31.  A lexicographic
 * comparison floats "98840" above "200000" and differs on every one of the ten
 * rows, starting with the first. */
static const char *const G10_WANT[RANK_ROWS] = {
    "900000 30 01 P1       26/09/15 A",
    "200000 29 00 Bio_100% 93/10/01 A",
    "180000 28 01 Bio_100% 93/10/01 A",
    "160000 27 02 Bio_100% 93/10/01 A",
    "140000 25 03 Bio_100% 93/10/01 A",
    "120000 22 04 Bio_100% 93/10/01 A",
    "100000 21 05 Bio_100% 93/10/01 A",
    "98840 30 01 P2       26/09/15 A",
    "81000 30 01 P3       26/09/15 A",
    "080000 19 06 Bio_100% 93/10/01 A"
};

/* The seven inputs that lose their seat to a shipped row.  Asserted explicitly:
 * "a low score does not take a seat" is half of what the ordering means. */
static const char *const G10_OUT[7] = {
    "75170 30 01 P4       26/09/15 A",
    "64830 30 01 P5       26/09/15 A",
    "54600 30 01 P6       26/09/15 A",
    "40520 30 01 P7       26/09/15 A",
    "38400 30 01 P8       26/09/15 A",
    "26010 30 01 P9       26/09/15 A",
    "15030 30 01 PA       26/09/15 A"
};

/* The shipped table as ten lines, for the rebuild cases that feed it back in. */
static HUNUSED const char *const *default_lines(void)
{
    static const char *ptr[RANK_ROWS];
    int i;
    for (i = 0; i < RANK_ROWS; i++) {
        ptr[i] = rank_default_line(i);
    }
    return ptr;
}

/* ------------------------------------------------------------------- U-A */

static void case_ua1(void)
{
    static const struct { const char *arg; int want; } tab[5] = {
        { NULL, RANK_MODE_WEEKLY },
        { "/W", RANK_MODE_WEEKLY },
        { "/w", RANK_MODE_WEEKLY },
        { "/D", RANK_MODE_DAILY  },
        { "/d", RANK_MODE_DAILY  }
    };
    size_t i;

    for (i = 0; i < sizeof tab / sizeof tab[0]; i++) {
        int mode = 0x5A;
        if (eq_long(__FILE__, __LINE__, lbl("switch accepted", (long)i),
                    rank_mode_parse(tab[i].arg, &mode), 0)) {
            eq_long(__FILE__, __LINE__, lbl("switch selects the mode", (long)i),
                    mode, tab[i].want);
        }
    }
}

/* A mistyped switch must not fall back to the default: on a machine whose
 * GAME.BAT says RANK /D, a silent fallback turns a typo into a weekly reset, and
 * on one that says RANK /W it turns a typo into a daily one. */
static void case_ua2(void)
{
    static const char *const bad[15] = {
        "/X", "/x", "FOO", "-D", "-d", "/DD", "/dd", "/D ", " /D",
        "/", "", "D", "d", "/Dd", "//D"
    };
    size_t i;

    for (i = 0; i < sizeof bad / sizeof bad[0]; i++) {
        int mode = 0x5A;
        eq_long(__FILE__, __LINE__, lbl("unknown switch refused", (long)i),
                rank_mode_parse(bad[i], &mode), -1);
        eq_long(__FILE__, __LINE__, lbl("and the mode is left alone", (long)i),
                mode, 0x5A);
    }
}

/* ------------------------------------------------------------------- U-C */

/* Monday from a day number, by a formula that is not the one under test:
 * 1970-01-01 is a Thursday and day 0, so day 4 is the first Monday. */
static long mod7(long d)
{
    long m = d % 7L;
    return (m < 0L) ? m + 7L : m;
}

static int is_monday(long days)
{
    return mod7(days) == 4L;
}

static void case_uc1(void)
{
    static const struct { int y, m, d; long days; long w; } tab[15] = {
        { 1969, 12, 28,    -4L,   -1L },
        { 1969, 12, 29,    -3L,    0L },
        { 1970,  1,  1,     0L,    0L },
        { 1970,  1,  5,     4L,    1L },
        { 2026,  9, 28, 20724L, 2961L },
        { 2026, 10, 18, 20744L, 2963L },
        { 2026, 10, 19, 20745L, 2964L },
        { 2026, 10, 25, 20751L, 2964L },
        { 2026, 10, 26, 20752L, 2965L },
        { 2026, 12, 31, 20818L, 2974L },
        { 2027,  1,  1, 20819L, 2974L },
        { 2027,  1,  4, 20822L, 2975L },
        { 2028,  2, 28, 21242L, 3035L },
        { 2028,  2, 29, 21243L, 3035L },
        { 2028,  3,  6, 21249L, 3036L }
    };
    size_t i;

    for (i = 0; i < sizeof tab / sizeof tab[0]; i++) {
        long days = rank_days_from_civil(tab[i].y, tab[i].m, tab[i].d);
        if (eq_long(__FILE__, __LINE__, lbl("anchor day number", (long)i),
                    days, tab[i].days)) {
            eq_long(__FILE__, __LINE__, lbl("anchor week index", (long)i),
                    rank_week_index(days), tab[i].w);
        }
    }
}

/* C89 leaves a negative quotient's rounding to the implementation, so -1/7 may
 * be 0.  Truncating here moves the Monday of 1969-12-29 into the week before it
 * and makes two adjacent Mondays share a week index.
 *
 * The week containing day -10 (a Monday) runs from -10 to -4, so every day from
 * -4 down to -10 has index -1 and -11 is the first with -2.  A truncating
 * implementation answers 0 for all of -4..-8, so each of those rows still
 * separates the two. */
static void case_uc2(void)
{
    static const struct { long days; long w; } tab[10] = {
        { -1L,  0L }, { -2L,  0L }, { -3L,  0L }, { -4L, -1L },
        { -5L, -1L }, { -6L, -1L }, { -7L, -1L }, { -8L, -1L },
        { -10L, -1L }, { -11L, -2L }
    };
    size_t i;

    for (i = 0; i < sizeof tab / sizeof tab[0]; i++) {
        eq_long(__FILE__, __LINE__, lbl("floor division", (long)i),
                rank_week_index(tab[i].days), tab[i].w);
    }
}

/* The staircase: it may only ever step by one, it must step exactly once every
 * seven days, and the step must land on a Monday.  The Monday test comes from
 * mod7, which is a different expression from (days + 3) / 7, so this is not the
 * implementation compared with itself. */
static void case_uc3(void)
{
    long d;
    long bad_step = 0;
    long bad_period = 0;
    long bad_monday = 0;

    for (d = -3000L; d < 24000L; d++) {
        long w0 = rank_week_index(d);
        long w1 = rank_week_index(d + 1L);
        long delta = w1 - w0;

        if (delta != 0L && delta != 1L) {
            bad_step++;
        }
        if (rank_week_index(d + 7L) != w0 + 1L) {
            bad_period++;
        }
        if ((delta != 0L) != is_monday(d + 1L)) {
            if (bad_monday == 0L) {
                fprintf(stderr, "FAIL  %s  [day %ld] w(%ld)=%ld w(%ld)=%ld "
                                "monday=%d\n",
                        g_case, d, d, w0, d + 1L, w1, is_monday(d + 1L));
                g_case_failed = 1;
            }
            bad_monday++;
        }
    }
    EQL("days where the week index stepped by something other than 0 or 1",
        bad_step, 0L);
    EQL("days where seven days did not advance the week by one", bad_period, 0L);
    EQL("days where the step did not coincide with a Monday", bad_monday, 0L);
}

/* A comparison that stops at the date cannot see a clock that went back a few
 * hours within one day, which is the RTC failure the second granularity is for. */
static void case_uc4(void)
{
    static const char *const base = "2026-10-20T12:30:45";
    static const char *const lower[6] = {
        "2025-10-20T12:30:45",
        "2026-09-20T12:30:45",
        "2026-10-19T12:30:45",
        "2026-10-20T11:30:45",
        "2026-10-20T12:29:45",
        "2026-10-20T12:30:44"
    };
    static const char *const higher[6] = {
        "2027-10-20T12:30:45",
        "2026-11-20T12:30:45",
        "2026-10-21T12:30:45",
        "2026-10-20T13:30:45",
        "2026-10-20T12:31:45",
        "2026-10-20T12:30:46"
    };
    RankTime b;
    RankTime v;
    RankTime p;
    RankTime q;
    int i;

    if (rank_time_parse(base, &b) != 0) {
        die("base");
    }
    for (i = 0; i < 6; i++) {
        if (rank_time_parse(lower[i], &v) != 0) {
            die(lower[i]);
        }
        eq_long(__FILE__, __LINE__, lbl("one field smaller is earlier", i),
                rank_time_before(&v, &b), 1);
        eq_long(__FILE__, __LINE__, lbl("and not the other way round", i),
                rank_time_before(&b, &v), 0);

        if (rank_time_parse(higher[i], &v) != 0) {
            die(higher[i]);
        }
        eq_long(__FILE__, __LINE__, lbl("one field larger is not earlier", i),
                rank_time_before(&v, &b), 0);
        eq_long(__FILE__, __LINE__, lbl("but the base is earlier than it", i),
                rank_time_before(&b, &v), 1);
    }

    EQL("equal times are not before each other", rank_time_before(&b, &b), 0);

    /* The year has to outweigh the second, or a New Year's Eve session reads as
     * a clock that went backwards. */
    if (rank_time_parse("2025-12-31T23:59:59", &p) != 0
     || rank_time_parse("2026-01-01T00:00:00", &q) != 0) {
        die("new year");
    }
    EQL("a smaller year wins over a larger second", rank_time_before(&p, &q), 1);
    EQL("and the reverse is false", rank_time_before(&q, &p), 0);

    EQL("NULL on the left is not before anything", rank_time_before(NULL, &b), 0);
    EQL("nothing is before NULL", rank_time_before(&b, NULL), 0);
    EQL("NULL on both sides", rank_time_before(NULL, NULL), 0);
}

/* The crossing decision table.  The "old" column is the >= 7 days rule this
 * change retires; the rows marked as discriminators are the ones where the new
 * weekly answer differs from it, and row 15 is the one where they agree, kept so
 * that a regression which flattens the weekly rule still trips an old net. */
typedef struct {
    const char *last;
    const char *now;
    int daily;
    int weekly;
    int discriminator;
} CrossRow;

static const CrossRow CROSS[18] = {
    { "2026-10-18T23:59:59", "2026-10-19T00:00:00", 1, 1, 1 },
    { "2026-10-18T23:59:59", "2026-10-19T00:01:00", 1, 1, 1 },
    { "2026-10-19T00:00:00", "2026-10-19T23:59:59", 0, 0, 0 },
    { "2026-10-19T00:00:00", "2026-10-25T23:59:59", 1, 0, 0 },
    { "2026-10-19T03:00:00", "2026-10-26T02:00:00", 1, 1, 1 },
    { "2026-10-20T23:59:59", "2026-10-21T00:01:00", 1, 0, 0 },
    { "2026-12-31T23:59:59", "2027-01-01T00:00:00", 1, 0, 0 },
    { "2026-12-31T12:00:00", "2027-01-04T00:00:00", 1, 1, 1 },
    { "2028-02-28T23:59:59", "2028-02-29T00:00:00", 1, 0, 0 },
    { "2028-02-29T23:59:59", "2028-03-01T00:00:00", 1, 0, 0 },
    { "2028-02-28T12:00:00", "2028-03-06T00:00:00", 1, 1, 1 },
    { "2026-10-20T12:00:00", "2026-10-20T12:00:00", 0, 0, 0 },
    { "2026-10-18T00:00:00", "2026-10-19T00:00:00", 1, 1, 1 },
    { "2026-10-19T23:59:59", "2026-10-26T00:00:00", 1, 1, 1 },
    { "2026-10-19T00:00:00", "2026-10-26T00:00:00", 1, 1, 0 },
    { "2028-02-29T12:00:00", "2028-03-06T00:00:00", 1, 1, 1 },
    { "1969-12-28T00:00:00", "1969-12-29T00:00:00", 1, 1, 1 },
    { "1969-12-29T00:00:00", "1970-01-01T00:00:00", 1, 0, 0 }
};

#define CROSS_N (sizeof CROSS / sizeof CROSS[0])

static void case_uc5(void)
{
    size_t i;

    for (i = 0; i < CROSS_N; i++) {
        RankTime last;
        RankTime now;

        if (rank_time_parse(CROSS[i].last, &last) != 0
         || rank_time_parse(CROSS[i].now, &now) != 0) {
            die(CROSS[i].last);
        }
        eq_long(__FILE__, __LINE__, lbl("daily crossing", (long)i + 1L),
                rank_crossed(&last, &now, RANK_MODE_DAILY), CROSS[i].daily);
        eq_long(__FILE__, __LINE__, lbl("weekly crossing", (long)i + 1L),
                rank_crossed(&last, &now, RANK_MODE_WEEKLY), CROSS[i].weekly);
    }
}

typedef struct { int y, m, d; } Ymd;

static int days_in_month_h(int y, int m)
{
    static const int len[12] = { 31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31 };
    if (m == 2 && ((y % 4 == 0 && y % 100 != 0) || y % 400 == 0)) {
        return 29;
    }
    return len[m - 1];
}

static void ymd_next(Ymd *p)
{
    p->d++;
    if (p->d > days_in_month_h(p->y, p->m)) {
        p->d = 1;
        p->m++;
        if (p->m > 12) {
            p->m = 1;
            p->y++;
        }
    }
}

/* Three calendar years of starting days against ten following days each, so the
 * leap rule, the month ends and the year ends all interact.  The weekly oracle
 * counts Mondays in the half-open interval one day at a time, which is a
 * different computation from a week index. */
#define SWEEP_SPAN  1096        /* 2026-01-01 .. 2028-12-31 */
#define SWEEP_AHEAD 10
#define SWEEP_N     (SWEEP_SPAN + SWEEP_AHEAD + 1)

static void case_uc6(void)
{
    static Ymd date[SWEEP_N];
    static long days[SWEEP_N];
    Ymd cur;
    int i;
    int k;
    long bad_daily = 0;
    long bad_weekly = 0;

    cur.y = 2026;
    cur.m = 1;
    cur.d = 1;
    for (i = 0; i < SWEEP_N; i++) {
        date[i] = cur;
        days[i] = rank_days_from_civil(cur.y, cur.m, cur.d);
        ymd_next(&cur);
    }
    EQL("the sweep really spans 2026-01-01 to 2028-12-31",
        days[SWEEP_SPAN - 1] - days[0], (long)SWEEP_SPAN - 1L);

    for (i = 0; i < SWEEP_SPAN; i++) {
        RankTime last;

        last.year = date[i].y;
        last.month = date[i].m;
        last.day = date[i].d;
        last.hour = 0;
        last.minute = 0;
        last.second = 0;

        for (k = 1; k <= SWEEP_AHEAD; k++) {
            RankTime now;
            int want_weekly = 0;
            int j;

            now.year = date[i + k].y;
            now.month = date[i + k].m;
            now.day = date[i + k].d;
            now.hour = 12;
            now.minute = 0;
            now.second = 0;

            for (j = i + 1; j <= i + k; j++) {
                if (is_monday(days[j])) {
                    want_weekly = 1;
                }
            }

            if (rank_crossed(&last, &now, RANK_MODE_DAILY) != 1) {
                if (bad_daily == 0L) {
                    fprintf(stderr, "FAIL  %s  [daily %04d-%02d-%02d +%d]\n",
                            g_case, date[i].y, date[i].m, date[i].d, k);
                    g_case_failed = 1;
                }
                bad_daily++;
            }
            if (rank_crossed(&last, &now, RANK_MODE_WEEKLY) != want_weekly) {
                if (bad_weekly == 0L) {
                    fprintf(stderr, "FAIL  %s  [weekly %04d-%02d-%02d +%d] "
                                    "want %d\n",
                            g_case, date[i].y, date[i].m, date[i].d, k,
                            want_weekly);
                    g_case_failed = 1;
                }
                bad_weekly++;
            }
        }
    }
    EQL("day pairs where the daily rule disagreed with the calendar", bad_daily, 0L);
    EQL("day pairs where the weekly rule disagreed with a Monday count",
        bad_weekly, 0L);
}

/* An invalid mode must be refused rather than treated as the default, and the
 * two times must come back untouched. */
static void case_uc7(void)
{
    static const int bad[3] = { 2, -1, 99 };
    RankTime last;
    RankTime now;
    RankTime keep_last;
    RankTime keep_now;
    int i;

    memset(&last, 0, sizeof last);
    memset(&now, 0, sizeof now);
    if (rank_time_parse("2026-10-19T00:00:00", &last) != 0
     || rank_time_parse("2026-10-26T02:00:00", &now) != 0) {
        die("uc7");
    }
    keep_last = last;
    keep_now = now;

    for (i = 0; i < 3; i++) {
        eq_long(__FILE__, __LINE__, lbl("invalid mode refused", bad[i]),
                rank_crossed(&last, &now, bad[i]), -1);
    }
    EQL("NULL last is refused", rank_crossed(NULL, &now, RANK_MODE_DAILY), -1);
    EQL("NULL now is refused", rank_crossed(&last, NULL, RANK_MODE_DAILY), -1);
    EQL("both NULL is refused", rank_crossed(NULL, NULL, RANK_MODE_WEEKLY), -1);

    EQM("last was not written to", &last, sizeof last, &keep_last, sizeof keep_last);
    EQM("now was not written to", &now, sizeof now, &keep_now, sizeof keep_now);
}

/* ------------------------------------------------------------------- U-K
 *
 * Decision 8 (spec.md section 3.8): a mark on the end of a name is the same
 * player's mark, so it is not part of the identity.  Every case here runs all
 * four score widths, because the name field is cut out at an offset counted from
 * the end of the line and a width-dependent slip would otherwise hide.
 */

/* Spec 3.8 rule 1 on its own: 0x20 off both ends of the eight byte field.  Used
 * as the independent oracle for U-K7 properties 3 and 5. */
static int trim_name(const char *name, char *out)
{
    int b = 0;
    int e = RANK_NAME_LEN;
    int n;

    while (b < e && name[b] == ' ') {
        b++;
    }
    while (e > b && name[e - 1] == ' ') {
        e--;
    }
    n = e - b;
    memcpy(out, name + b, (size_t)n);
    out[n] = '\0';
    return n;
}

static void case_uk1(void)
{
    static const struct { const char *name; const char *key; } tab[10] = {
        { "omzn    ", "OMZN"   },
        { "OMZN    ", "OMZN"   },
        { "OMZN@@@@", "OMZN"   },   /* decision 8: was a different player */
        { "omzn++++", "OMZN"   },
        { "omzn-   ", "OMZN"   },   /* the adjacent pair, first half */
        { "omzn-2  ", "OMZN-2" },   /* and the second: one byte apart */
        { "TARO2   ", "TARO2"  },
        { "Bio_100%", "BIO_100" },  /* decision 8: the % falls off */
        { "@@@@    ", "@@@@"   },   /* rule 3 puts it back */
        { "        ", ""       }
    };
    size_t i;
    size_t s;

    for (s = 0; s < sizeof SCORES / sizeof SCORES[0]; s++) {
        for (i = 0; i < sizeof tab / sizeof tab[0]; i++) {
            eq_str(__FILE__, __LINE__,
                   lbl("spec key", (long)(s * 100u + i)),
                   key_for(SCORES[s], tab[i].name), tab[i].key);
        }
        /* Stated outright rather than left to two table rows that happen to
         * pass: one trailing byte decides same player from different player. */
        OK(lbl("omzn- and omzn-2 are not the same player", (long)s),
           strcmp(key_for(SCORES[s], "omzn-   "),
                  key_for(SCORES[s], "omzn-2  ")) != 0);
    }
}

/* Both directions.  Folding too little loses a player's records to a second
 * row; folding too much merges two people's records into one. */
static void case_uk2(void)
{
    static const char *const same[7] = {
        "OMZN    ", "omzn    ", "OMZN@@@@", "omzn++++",
        "OMZN$$$$", "OMZN~~~~", "omzn-   "
    };
    static const char *const diff[8] = {
        "OMZN    ", "OMZO    ", "OMZN-2  ", "@@@@OMZN",
        "TARO    ", "TARO2   ", "@@@@    ", "        "
    };
    int i;
    int j;

    for (i = 0; i < 7; i++) {
        for (j = i + 1; j < 7; j++) {
            OK(lbl("same player", (long)(i * 10 + j)),
               strcmp(key_for(SCORES[2], same[i]),
                      key_for(SCORES[2], same[j])) == 0);
        }
    }
    for (i = 0; i < 8; i++) {
        for (j = i + 1; j < 8; j++) {
            OK(lbl("different players", (long)(i * 10 + j)),
               strcmp(key_for(SCORES[2], diff[i]),
                      key_for(SCORES[2], diff[j])) != 0);
        }
    }
}

/* The required half of rule 2, and the only case that can see it.  Dropping
 * trailing non-alphanumerics without stopping at 0x80 empties a Shift-JIS name,
 * rule 3 then restores it, and the answer comes out right anyway - so a name
 * made only of Shift-JIS bytes proves nothing.  Only a name with alphanumerics
 * in front of the high bytes escapes the rescue and separates the two
 * implementations (test-design.md section 6.1).
 *
 * The same two forms also kill a comparison written on a signed char, where
 * c >= 0x80 is never true.  The two defects are indistinguishable by
 * observation, and there is no value in telling them apart. */
static void case_uk3(void)
{
    static const struct { const char *name; const char *key; } must[2] = {
        { "omzn\202\240  ", "OMZN\202\240"  },  /* nothing falls off at all */
        { "omzn\202\240@ ", "OMZN\202\240"  }   /* the ASCII mark goes, 0x80 stops */
    };
    /* Present for completeness, and not a net on their own: the first is all
     * high bytes, so rule 3 rescues a wrong implementation into the right
     * answer, and the second only differs in how far the drop runs. */
    static const struct { const char *name; const char *key; } aux[2] = {
        { "\202\240\202\242    ", "\202\240\202\242" },
        { "\200------ ",          "\200"             }
    };
    size_t i;
    size_t s;

    for (s = 0; s < sizeof SCORES / sizeof SCORES[0]; s++) {
        for (i = 0; i < 2; i++) {
            eq_str(__FILE__, __LINE__,
                   lbl("high byte stops the drop", (long)(s * 100u + i)),
                   key_for(SCORES[s], must[i].name), must[i].key);
        }
        for (i = 0; i < 2; i++) {
            eq_str(__FILE__, __LINE__,
                   lbl("high byte, auxiliary form", (long)(s * 100u + i)),
                   key_for(SCORES[s], aux[i].name), aux[i].key);
        }
        /* This is the body of it: two different people stay two people. */
        OK(lbl("a Shift-JIS name is not the ASCII name it starts with", (long)s),
           strcmp(key_for(SCORES[s], "omzn\202\240  "),
                  key_for(SCORES[s], "omzn    ")) != 0);
        OK(lbl("and the marked Shift-JIS form is the same person", (long)s),
           strcmp(key_for(SCORES[s], "omzn\202\240  "),
                  key_for(SCORES[s], "omzn\202\240@ ")) == 0);
    }
}

/* Rule 3.  Without it every name made only of marks folds to an empty key and
 * all of them become one player, together with the blank name. */
static void case_uk4(void)
{
    static const struct { const char *name; const char *key; } tab[4] = {
        { "@@@@    ", "@@@@"     },
        { "--------", "--------" },
        { "-       ", "-"        },
        { "%%%%%%%%", "%%%%%%%%" }
    };
    static const char *const apart[3] = { "@@@@    ", "--------", "        " };
    size_t i;
    int a;
    int b;

    for (i = 0; i < sizeof tab / sizeof tab[0]; i++) {
        eq_str(__FILE__, __LINE__, lbl("rescued key", (long)i),
               key_for(SCORES[2], tab[i].name), tab[i].key);
    }
    for (a = 0; a < 3; a++) {
        for (b = a + 1; b < 3; b++) {
            OK(lbl("marks-only names stay apart", (long)(a * 10 + b)),
               strcmp(key_for(SCORES[2], apart[a]),
                      key_for(SCORES[2], apart[b])) != 0);
        }
    }
}

/* The one byte boundary of "stop when the last byte is alphanumeric". */
static void case_uk5(void)
{
    static const struct { const char *name; const char *key; } tab[6] = {
        { "omzn-   ", "OMZN"   },
        { "omzn-2  ", "OMZN-2" },
        { "TARO    ", "TARO"   },
        { "TARO2   ", "TARO2"  },
        { "A       ", "A"      },
        { "1       ", "1"      }
    };
    size_t i;
    size_t s;

    for (s = 0; s < sizeof SCORES / sizeof SCORES[0]; s++) {
        for (i = 0; i < sizeof tab / sizeof tab[0]; i++) {
            eq_str(__FILE__, __LINE__, lbl("boundary key", (long)(s * 100u + i)),
                   key_for(SCORES[s], tab[i].name), tab[i].key);
        }
        OK(lbl("TARO and TARO2 are different players", (long)s),
           strcmp(key_for(SCORES[s], "TARO    "),
                  key_for(SCORES[s], "TARO2   ")) != 0);
        OK(lbl("omzn and omzo are different players", (long)s),
           strcmp(key_for(SCORES[s], "omzn    "),
                  key_for(SCORES[s], "omzo    ")) != 0);
    }
}

/* The key buffer is RANK_NAME_LEN + 1 bytes, and rule 3 can leave all eight
 * bytes in place, so the full width case has to land exactly.  The blank name is
 * the other end: a drop loop that reads the byte before an empty trimmed name
 * walks off the front. */
static void case_uk6(void)
{
    static const char *const full[3] = {
        "--------", "@@@@@@@@", "\200\200\200\200\200\200\200\200"
    };
    /* A guard behind the row, so a key written past its ninth byte shows up as
     * a changed pattern rather than as nothing at all: ASan does not police
     * writes that stay inside one object. */
    static struct { RankRow row; char guard[32]; } g;
    static char want_guard[32];
    char line[80];
    int i;

    memset(want_guard, 0xAA, sizeof want_guard);

    for (i = 0; i < 3; i++) {
        memset(&g, 0xAA, sizeof g);
        make_line(line, sizeof line, SCORES[2], full[i]);
        if (!eq_long(__FILE__, __LINE__, lbl("full width name parses", i),
                     rank_parse_line(line, &g.row), 0)) {
            continue;
        }
        eq_long(__FILE__, __LINE__, lbl("eight bytes of key survive", i),
                (long)strlen(g.row.key), (long)RANK_NAME_LEN);
        eq_long(__FILE__, __LINE__, lbl("and the terminator is the ninth", i),
                g.row.key[RANK_NAME_LEN], 0L);
        eq_mem(__FILE__, __LINE__, lbl("nothing was written past the row", i),
               g.guard, sizeof g.guard, want_guard, sizeof want_guard);
    }

    memset(&g, 0xAA, sizeof g);
    make_line(line, sizeof line, SCORES[2], "        ");
    if (EQL("a blank name parses", rank_parse_line(line, &g.row), 0)) {
        EQL("and its key is empty", g.row.key[0], 0L);
        EQM("with nothing written past the row",
            g.guard, sizeof g.guard, want_guard, sizeof want_guard);
    }
}

/* Properties of the rule rather than instances of it, over random names.  0x00
 * is excluded from the random bytes because a NUL inside a NUL terminated line
 * is not a name byte the parser can ever be handed; it is covered separately
 * below, where the line simply fails to parse. */
static void case_uk7(void)
{
    char name[RANK_NAME_LEN + 1];
    char trimmed[RANK_NAME_LEN + 1];
    char line[80];
    RankRow r;
    int iter;
    long bad_term = 0;
    long bad_lower = 0;
    long bad_prefix = 0;
    long bad_empty = 0;
    long bad_full = 0;

    srand(20261023u);
    for (iter = 0; iter < 20000; iter++) {
        int i;
        int tn;
        size_t kl;
        unsigned char last;

        for (i = 0; i < RANK_NAME_LEN; i++) {
            name[i] = (char)(1 + (rand() % 255));
        }
        name[RANK_NAME_LEN] = '\0';
        make_line(line, sizeof line, SCORES[2], name);
        memset(&r, 0, sizeof r);
        if (rank_parse_line(line, &r) != 0) {
            continue;   /* a shape the game never writes; U-P covers refusal */
        }
        tn = trim_name(name, trimmed);
        kl = strlen(r.key);

        if (kl > (size_t)RANK_NAME_LEN || r.key[kl] != '\0') {
            bad_term++;
        }
        for (i = 0; i < (int)kl; i++) {
            unsigned char c = (unsigned char)r.key[i];
            if (c >= 'a' && c <= 'z') {
                bad_lower++;
                break;
            }
        }
        if (kl > (size_t)tn) {
            bad_prefix++;
        } else {
            for (i = 0; i < (int)kl; i++) {
                unsigned char c = (unsigned char)trimmed[i];
                if (c >= 'a' && c <= 'z') {
                    c = (unsigned char)(c - 'a' + 'A');
                }
                if ((unsigned char)r.key[i] != c) {
                    bad_prefix++;
                    break;
                }
            }
        }
        if (tn > 0 && kl == 0u) {
            bad_empty++;
        }
        if (tn > 0) {
            last = (unsigned char)trimmed[tn - 1];
            if (last >= 0x80u
             || (last >= '0' && last <= '9')
             || (last >= 'A' && last <= 'Z')
             || (last >= 'a' && last <= 'z')) {
                if (kl != (size_t)tn) {
                    bad_full++;
                }
            }
        }
    }
    EQL("keys that were not terminated inside the buffer", bad_term, 0L);
    EQL("keys that still held an ASCII lowercase byte", bad_lower, 0L);
    EQL("keys that were not an upper-cased prefix of the trimmed name",
        bad_prefix, 0L);
    EQL("non-empty names that folded to an empty key", bad_empty, 0L);
    EQL("names ending in an alphanumeric or a high byte that lost bytes",
        bad_full, 0L);

    /* The excluded byte, stated rather than assumed away: a NUL inside the name
     * field ends the line early, so the shape check refuses it and no key is
     * ever produced from it. */
    make_line(line, sizeof line, SCORES[2], "omXzn   ");
    line[15] = '\0';            /* third byte of the eight byte name field */
    EQL("a name holding 0x00 never reaches the folding at all",
        rank_parse_line(line, &r), -1);
}

/* The key has to arrive the same way through the table reader as through
 * rank_parse_line; a reader that builds rows by hand would skip the folding. */
static void case_uk8(void)
{
    RankTable t;
    int i;
    int omzn = 0;
    int omzo = 0;

    if (!EQL("nx93.bak reads ten rows",
             read_table_bytes(fix_bak, FIX_BAK_LEN, &t), RANK_ROWS)) {
        return;
    }
    for (i = 0; i < t.count; i++) {
        RankRow direct;
        if (strcmp(t.row[i].key, "OMZN") == 0) {
            omzn++;
        } else if (strcmp(t.row[i].key, "OMZO") == 0) {
            omzo++;
        }
        if (row_of(t.row[i].raw, &direct) == 0) {
            eq_str(__FILE__, __LINE__, lbl("same key through either path", i),
                   t.row[i].key, direct.key);
        }
    }
    /* OMZN@@@@ three times, OMZN++++, OMZN$$$$, OMZN~~~~ and omzn three times
     * all join OMZN under decision 8; omzo stays its own player. */
    EQL("nine rows fold to OMZN", omzn, 9);
    EQL("and one to OMZO", omzo, 1);
}

/* ------------------------------------------------------------ 行表の生成器 */

/* Rows with distinct keys U000..U127 and a stepped score, for the capacity
 * boundaries.  A negative step gives an ascending file.  The pointer table is
 * static, so one call's result is valid until the next call. */
static HUNUSED const char *const *gen_rows(int n, long top, long step)
{
    static char buf[128][RANK_MAX_LINE + 1];
    static const char *ptr[128];
    int i;

    if (n < 0 || n > 128) {
        fprintf(stderr, "harness failure: %s: gen_rows(%d)\n", g_case, n);
        exit(2);
    }
    for (i = 0; i < n; i++) {
        snprintf(buf[i], sizeof buf[0], "%06ld 30 01 U%03d     26/09/15 A",
                 top - step * (long)i, i);
        ptr[i] = buf[i];
    }
    return ptr;
}

/* The hundred row RANKWHO.TXT the overflow cases start from: keys U00..U99,
 * scores 500000 - 100*i, so the last row is U99 at 490100 (test-design.md
 * section 8, U-G5). */
#define WHO100_LAST_SCORE 490100L

static HUNUSED const char *const *gen_who100(void)
{
    static char buf[RANKWHO_ROWS][RANK_MAX_LINE + 1];
    static const char *ptr[RANKWHO_ROWS];
    int i;

    for (i = 0; i < RANKWHO_ROWS; i++) {
        snprintf(buf[i], sizeof buf[0], "%06ld 30 01 U%02d      26/09/15 A",
                 500000L - 100L * (long)i, i);
        ptr[i] = buf[i];
    }
    return ptr;
}

/* ------------------------------------------------------------------- U-F */

static void case_uf1(void)
{
    static char lfonly[512];
    static char eleven[1024];
    static char overlong[512];
    static RankRow rows[RANK_ROWS + 4];
    size_t lfn = 0;
    size_t i;
    size_t at;

    /* U-R3's input: every CR taken out of the live table. */
    for (i = 0; i < FIX_SCR_LEN; i++) {
        if (fix_scr[i] != '\r') {
            lfonly[lfn++] = fix_scr[i];
        }
    }
    /* U-R5's: an eleventh row. */
    memcpy(eleven, fix_scr, FIX_SCR_LEN);
    memcpy(eleven + FIX_SCR_LEN, "060000 16 07 Bio_100% 93/10/01 A\r\n", 34u);
    /* U-R6's: a 200 byte line, and no resynchronising onto the next one. */
    for (i = 0; i < 200u; i++) {
        overlong[i] = (char)('0' + (int)(i % 10u));
    }
    at = 200u;
    overlong[at++] = '\r';
    overlong[at++] = '\n';
    memcpy(overlong + at, "327400 30 01 OMZN     26/09/15 A\r\n", 34u);
    at += 34u;

    {
        static const struct { const char *what; long want; } tab[6] = {
            { "nx93.bak, trailing 0x1A", (long)RANK_ROWS },
            { "nx93.scr, no final CRLF", (long)RANK_ROWS },
            { "nx93.scr, LF only",       (long)RANK_ROWS },
            { "empty file",              0L },
            { "eleven rows",             -1L },
            { "a 200 byte line",         -1L }
        };
        const char *data[6];
        size_t len[6];
        int k;

        data[0] = fix_bak;   len[0] = FIX_BAK_LEN;
        data[1] = fix_scr;   len[1] = FIX_SCR_LEN - 2u;
        data[2] = lfonly;    len[2] = lfn;
        data[3] = "";        len[3] = 0u;
        data[4] = eleven;    len[4] = FIX_SCR_LEN + 34u;
        data[5] = overlong;  len[5] = at;

        for (k = 0; k < 6; k++) {
            RankTable t;
            long a = (long)rows_read_bytes(data[k], len[k], rows, RANK_ROWS);
            long b = (long)read_table_bytes(data[k], len[k], &t);
            eq_long(__FILE__, __LINE__, lbl_s("rows_read", tab[k].what),
                    a, tab[k].want);
            eq_long(__FILE__, __LINE__, lbl_s("same answer as rank_read_table",
                                              tab[k].what), a, b);
        }
    }

    /* The remaining two of U-R4: a lone 0x1A and a lone blank line. */
    EQL("a lone 0x1A carries no row",
        rows_read_bytes("\032", 1u, rows, RANK_ROWS), 0L);
    EQL("a lone CRLF carries no row",
        rows_read_bytes("\r\n", 2u, rows, RANK_ROWS), 0L);

    /* maxrows is the caller's limit, not RANK_ROWS: the all-time table asks for
     * thirty and the per-user one for a hundred. */
    EQL("more rows than maxrows is refused",
        rows_read_bytes(fix_scr, FIX_SCR_LEN, rows, 5), -1L);
    EQL("exactly maxrows is accepted",
        rows_read_bytes(fix_scr, FIX_SCR_LEN, rows, RANK_ROWS), (long)RANK_ROWS);
}

static void case_uf2(void)
{
    static RankRow rows[64];
    const char *const *lines;
    char *bytes;
    size_t len;
    int i;

    bytes = rows_written_bytes(rows, 0, &len);
    if (OK("an empty table writes", bytes != NULL)) {
        EQL("and it is zero bytes, not a blank line", (long)len, 0L);
        free(bytes);
    }

    lines = gen_rows(1, 500000L, 100L);
    rows_of(lines, 1, rows);
    bytes = rows_written_bytes(rows, 1, &len);
    if (OK("one row writes", bytes != NULL)) {
        EQL("32 bytes plus CRLF", (long)len, 34L);
        EQM("row and terminator only", bytes, len,
            "500000 30 01 U000     26/09/15 A\r\n", 34u);
        free(bytes);
    }

    lines = gen_rows(RANK30_ROWS, 500000L, 100L);
    rows_of(lines, RANK30_ROWS, rows);
    bytes = rows_written_bytes(rows, RANK30_ROWS, &len);
    if (OK("thirty rows write", bytes != NULL)) {
        EQL("thirty times thirty four", (long)len, 1020L);
        OK("no 0x1A anywhere", memchr(bytes, 0x1A, len) == NULL);
        for (i = 0; i < RANK30_ROWS; i++) {
            const char *at = bytes + (size_t)i * 34u;
            eq_mem(__FILE__, __LINE__, lbl("row written verbatim", i),
                   at, 32u, rows[i].raw, 32u);
            eq_mem(__FILE__, __LINE__, lbl("and terminated with CRLF", i),
                   at + 32, 2u, "\r\n", 2u);
        }
        free(bytes);
    }

    lines = gen_rows(2, 500000L, 100L);
    rows_of(lines, 2, rows);
    bytes = rows_written_bytes(rows, 2, &len);
    if (OK("two rows write", bytes != NULL)) {
        EQL("no blank line between them", (long)len, 68L);
        OK("the final row is terminated too",
           len >= 2u && bytes[len - 2] == '\r' && bytes[len - 1] == '\n');
        free(bytes);
    }
}

static void case_uf3(void)
{
    static Rank30Table t;
    static const char *const ties[3] = {
        "300000 30 01 AAA      26/09/15 A",
        "300000 30 02 BBB      26/09/16 A",
        "300000 30 03 CCC      26/09/17 A"
    };
    static char buf[ROWSBUF];
    const char *const *lines;
    size_t n;
    int i;

    lines = gen_rows(RANK30_ROWS, 500000L, 100L);
    n = rows_image(lines, RANK30_ROWS, buf);
    EQL("thirty rows load", r30_load_bytes(buf, n, &t), (long)RANK30_ROWS);
    EQL("and the count agrees", t.count, (long)RANK30_ROWS);

    lines = gen_rows(RANK30_ROWS + 1, 500000L, 100L);
    n = rows_image(lines, RANK30_ROWS + 1, buf);
    EQL("thirty one rows are refused, not truncated",
        r30_load_bytes(buf, n, &t), -1L);

    EQL("an absent table is zero rows, not an error",
        r30_load_bytes("", 0u, &t), 0L);
    EQL("and the count agrees", t.count, 0L);

    /* The file's order is not trusted; the loader normalises it. */
    lines = gen_rows(5, 100000L, -1000L);
    n = rows_image(lines, 5, buf);
    if (EQL("an ascending file loads", r30_load_bytes(buf, n, &t), 5L)) {
        for (i = 0; i < 5; i++) {
            eq_long(__FILE__, __LINE__, lbl("normalised to descending", i),
                    t.row[i].score, 104000L - 1000L * (long)i);
        }
    }

    n = rows_image(ties, 3, buf);
    if (EQL("three tied rows load", r30_load_bytes(buf, n, &t), 3L)) {
        for (i = 0; i < 3; i++) {
            eq_str(__FILE__, __LINE__, lbl("ties keep their file order", i),
                   t.row[i].raw, ties[i]);
        }
    }
}

static void case_uf4(void)
{
    static RankWhoTable t;
    static char buf[ROWSBUF];
    static const struct { const char *what; const char *a; const char *b; long want; }
    pairs[4] = {
        { "OMZN and omzn",     "OMZN    ", "omzn    ", -1L },
        { "OMZN and OMZN@@@@", "OMZN    ", "OMZN@@@@", -1L },
        { "OMZN and OMZO",     "OMZN    ", "OMZO    ",  2L },
        { "OMZN and @@@@OMZN", "OMZN    ", "@@@@OMZN",  2L }
    };
    const char *const *lines;
    size_t n;
    int i;

    lines = gen_who100();
    n = rows_image(lines, RANKWHO_ROWS, buf);
    EQL("a hundred rows load", who_load_bytes(buf, n, &t), (long)RANKWHO_ROWS);
    EQL("and the count agrees", t.count, (long)RANKWHO_ROWS);

    lines = gen_rows(RANKWHO_ROWS + 1, 500000L, 100L);
    n = rows_image(lines, RANKWHO_ROWS + 1, buf);
    EQL("a hundred and one rows are refused",
        who_load_bytes(buf, n, &t), -1L);

    lines = gen_rows(5, 100000L, -1000L);
    n = rows_image(lines, 5, buf);
    if (EQL("an ascending file loads", who_load_bytes(buf, n, &t), 5L)) {
        for (i = 0; i < 5; i++) {
            eq_long(__FILE__, __LINE__, lbl("normalised to descending", i),
                    t.row[i].score, 104000L - 1000L * (long)i);
        }
    }

    /* One row per key is the file's invariant, so a file that breaks it is
     * damaged and the whole table is refused.  The second pair is the one
     * decision 8 newly makes a duplicate, which is how a hand edit can produce
     * it by accident. */
    for (i = 0; i < 4; i++) {
        char two[2][RANK_MAX_LINE + 1];
        const char *ptr[2];
        make_line(two[0], sizeof two[0], "500000", pairs[i].a);
        make_line(two[1], sizeof two[1], "400000", pairs[i].b);
        ptr[0] = two[0];
        ptr[1] = two[1];
        n = rows_image(ptr, 2, buf);
        eq_long(__FILE__, __LINE__, lbl_s("key pair", pairs[i].what),
                who_load_bytes(buf, n, &t), pairs[i].want);
    }
}

static void case_uf5(void)
{
    RankTime now;
    char *bytes;
    size_t len;
    char ts[20];
    char want[80];
    size_t wn;

    if (rank_time_parse(CANON_TS, &now) != 0) {
        die(CANON_TS);
    }
    wn = canon_state(want, sizeof want, CANON_TS);
    EQL("the canonical form is forty five bytes", (long)wn, (long)CANON_LEN);

    bytes = state_written_bytes(&now, &len);
    if (!OK("the state file writes", bytes != NULL)) {
        return;
    }
    EQM("byte for byte the canonical form", bytes, len, want, wn);
    rank_time_format(&now, ts);
    if (len == CANON_LEN) {
        EQM("offsets 20..38 are the formatted timestamp",
            bytes + 20, 19u, ts, 19u);
    }
    free(bytes);
}

/* The eleven non-canonical variants, built around a chosen timestamp so that
 * I-34 can put the same bytes either side of a calendar boundary.  Index 8 is
 * the empty file and index 9 is the real old ledger, both of which ignore `ts`. */
#define STATE_VARIANTS 11

static const char *const STATE_VARIANT_NAME[STATE_VARIANTS] = {
    "one extra trailing byte",
    "the final CRLF removed",
    "every CRLF turned into LF",
    "one more space before lastUpdate",
    "a comma where offset 39's quote belongs",
    "an out of range month in the timestamp",
    "a bracket instead of the closing brace",
    "three bytes prepended",
    "an empty file",
    "the old ledger itself",
    "the canonical form with a trailing 0x1A"
};

/* Variant 4 replaces the quote that closes the timestamp, so lastUpdate stops
 * being readable at all and E4 settles the run before the canonical check is
 * ever consulted.  I-34's "no crossing gives 0" half cannot hold for it. */
#define STATE_VARIANT_UNREADABLE 4

static size_t state_variant(int i, const char *ts, char *out, size_t cap)
{
    switch (i) {
    case 0:  snprintf(out, cap, "{\r\n  \"lastUpdate\": \"%s\"\r\n}\r\nX", ts); break;
    case 1:  snprintf(out, cap, "{\r\n  \"lastUpdate\": \"%s\"\r\n}", ts); break;
    case 2:  snprintf(out, cap, "{\n  \"lastUpdate\": \"%s\"\n}\n", ts); break;
    case 3:  snprintf(out, cap, "{\r\n   \"lastUpdate\": \"%s\"\r\n}\r\n", ts); break;
    case 4:  snprintf(out, cap, "{\r\n  \"lastUpdate\": \"%s,\r\n}\r\n", ts); break;
    case 5:  snprintf(out, cap, "{\r\n  \"lastUpdate\": \"2026-13-01T00:00:00\"\r\n}\r\n"); break;
    case 6:  snprintf(out, cap, "{\r\n  \"lastUpdate\": \"%s\"\r\n]\r\n", ts); break;
    case 7:  snprintf(out, cap, "XYZ{\r\n  \"lastUpdate\": \"%s\"\r\n}\r\n", ts); break;
    case 8:  out[0] = '\0'; break;
    case 9:  memcpy(out, fix_ledger_lf, FIX_LEDGER_LF_LEN + 1u); break;
    case 10: snprintf(out, cap, "{\r\n  \"lastUpdate\": \"%s\"\r\n}\r\n\032", ts); break;
    default:
        fprintf(stderr, "harness failure: %s: state_variant(%d)\n", g_case, i);
        exit(2);
    }
    return strlen(out);
}

/* The canonical check has to be an exact match, because the path it guards
 * throws the old ledger away (E10).  A check that settles for "lastUpdate is
 * readable" would read a ledger whose backups key had been damaged as a state
 * file and discard every record in it. */
static void case_uf6(void)
{
    char buf[800];
    char canon[80];
    size_t n;
    int i;

    n = canon_state(canon, sizeof canon, CANON_TS);
    EQL("the canonical form is canonical", state_check_bytes(canon, n), 1L);

    for (i = 0; i < STATE_VARIANTS; i++) {
        n = state_variant(i, CANON_TS, buf, sizeof buf);
        eq_long(__FILE__, __LINE__,
                lbl_s("not canonical:", STATE_VARIANT_NAME[i]),
                state_check_bytes(buf, n), 0L);
    }
    /* Variant 4 keeps the length right, so a check that only measured the file
     * would pass it.  Say so, since that is the point of the variant. */
    n = state_variant(4, CANON_TS, buf, sizeof buf);
    EQL("and that variant is still forty five bytes long", (long)n, (long)CANON_LEN);
}

static void case_uf7(void)
{
    char canon[80];
    char *big;
    size_t n;
    size_t big_len = 1024u * 1024u;

    EQL("the old ledger has a backups key",
        has_backups_bytes(fix_ledger_lf, FIX_LEDGER_LF_LEN), 1L);
    EQL("the CRLF ledger too",
        has_backups_bytes(fix_ledger_crlf, FIX_LEDGER_CRLF_LEN), 1L);
    n = canon_state(canon, sizeof canon, CANON_TS);
    EQL("the state file has none", has_backups_bytes(canon, n), 0L);
    EQL("an empty file has none", has_backups_bytes("", 0u), 0L);
    EQL("the bare word is not the key", has_backups_bytes("backups", 7u), 0L);
    EQL("a shorter key is not the key",
        has_backups_bytes("\"backup\"", 8u), 0L);
    /* The key is the nine byte sequence with both its quotes, so a name that
     * merely starts with it does not contain it: the closing quote is one byte
     * further on.  What does count is the sequence appearing anywhere in a
     * longer file, which is the next assertion. */
    EQL("a longer name does not contain the key",
        has_backups_bytes("\"backupsX\"", 10u), 0L);
    EQL("but the key found anywhere in a file is the key",
        has_backups_bytes("xx\"backups\"yy", 13u), 1L);

    /* The key is nine bytes including both quotes, so a scan written for eight
     * misses it when it sits exactly at the end of a long file. */
    big = (char *)malloc(big_len);
    if (big == NULL) {
        die("malloc");
    }
    memset(big, 'x', big_len);
    memcpy(big + big_len - 9u, "\"backups\"", 9u);
    EQL("a key in the last nine bytes of a megabyte is found",
        has_backups_bytes(big, big_len), 1L);
    memset(big + big_len - 9u, 'x', 9u);
    memcpy(big + big_len - 8u, "backups\"", 8u);
    EQL("the same bytes without the opening quote are not the key",
        has_backups_bytes(big, big_len), 0L);
    free(big);
}

static void case_uf8(void)
{
    static const char *const when[6] = {
        "1969-12-28T00:00:00",
        "2028-02-29T12:00:00",
        "2026-10-26T00:00:00",
        "2026-10-26T23:59:59",
        "9999-12-31T23:59:59",
        "0001-01-01T00:00:00"
    };
    size_t i;

    for (i = 0; i < sizeof when / sizeof when[0]; i++) {
        RankTime now;
        RankTime back;
        char *bytes;
        size_t len;
        FILE *fp;

        if (rank_time_parse(when[i], &now) != 0) {
            die(when[i]);
        }
        bytes = state_written_bytes(&now, &len);
        if (!is_true(__FILE__, __LINE__, lbl("state writes", (long)i),
                     bytes != NULL)) {
            continue;
        }
        eq_long(__FILE__, __LINE__, lbl("forty five bytes", (long)i),
                (long)len, (long)CANON_LEN);
        free(bytes);

        /* state_written_bytes left the file in g_io, so both readers below meet
         * exactly the bytes the writer produced. */
        fp = fopen(g_io, "rb");
        if (fp == NULL) {
            die(g_io);
        }
        memset(&back, 0, sizeof back);
        eq_long(__FILE__, __LINE__, lbl("lastUpdate reads back", (long)i),
                rank_ledger_read_last_update(fp, &back), 0);
        if (fclose(fp) != 0) {
            die(g_io);
        }
        eq_mem(__FILE__, __LINE__, lbl("the same instant came back", (long)i),
               &back, sizeof back, &now, sizeof now);

        fp = fopen(g_io, "rb");
        if (fp == NULL) {
            die(g_io);
        }
        eq_long(__FILE__, __LINE__, lbl("the writer's output is canonical",
                                        (long)i),
                rank_state_check(fp), 1);
        if (fclose(fp) != 0) {
            die(g_io);
        }
    }
}

/* Bit flips, truncations and insertions over the real table.  The bar is only
 * that each reader answers rather than misbehaves, with ASan and UBSan aborting
 * the process to supply the rest of the oracle. */
static void case_uf9(void)
{
    static RankRow rows[RANK_ROWS + 4];
    static Rank30Table t30;
    static RankWhoTable who;
    char buf[512];
    int iter;
    long bad = 0;

    srand(20261020u);
    for (iter = 0; iter < 20000; iter++) {
        size_t n = FIX_SCR13_LEN;
        int kind;
        int a;
        int b;
        int c;
        int i;

        memcpy(buf, fix_scr13, FIX_SCR13_LEN);
        kind = rand() % 3;
        if (kind == 0) {
            size_t at = (size_t)(rand() % (int)n);
            buf[at] = (char)(buf[at] ^ (1 << (rand() % 8)));
        } else if (kind == 1) {
            n = (size_t)(rand() % (int)(FIX_SCR13_LEN + 1));
        } else {
            size_t at = (size_t)(rand() % (int)(n + 1));
            memmove(buf + at + 1, buf + at, n - at);
            buf[at] = (char)(rand() % 256);
            n++;
        }

        a = rows_read_bytes(buf, n, rows, RANK_ROWS);
        b = r30_load_bytes(buf, n, &t30);
        c = who_load_bytes(buf, n, &who);

        if (a < -1 || a > RANK_ROWS
         || b < -1 || b > RANK30_ROWS || (b >= 0 && t30.count != b)
         || c < -1 || c > RANKWHO_ROWS || (c >= 0 && who.count != c)) {
            if (bad == 0L) {
                fprintf(stderr, "FAIL  %s  [iteration %d kind %d] rows=%d "
                                "r30=%d who=%d\n", g_case, iter, kind, a, b, c);
                g_case_failed = 1;
            }
            bad++;
        }
        for (i = 0; i < a; i++) {
            if (memchr(rows[i].raw, '\0', sizeof rows[i].raw) == NULL) {
                if (bad == 0L) {
                    fprintf(stderr, "FAIL  %s  [iteration %d] row %d not "
                                    "terminated\n", g_case, iter, i);
                    g_case_failed = 1;
                }
                bad++;
            }
        }
    }
    EQL("mutated tables that produced neither a row count nor -1", bad, 0L);
}

/* ------------------------------------------------------------- 併合の継ぎ手 */

static HUNUSED void table_of(const char *const *lines, int n, RankTable *t)
{
    static char buf[ROWSBUF];
    size_t at = rows_image(lines, n, buf);

    if (read_table_bytes(buf, at, t) != n) {
        fprintf(stderr, "harness failure: %s: fixture table of %d row(s) did "
                        "not read back\n", g_case, n);
        exit(2);
    }
}

static HUNUSED char *written_table_bytes(const RankTable *t, size_t *len)
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

static HUNUSED void expect_table_lines(const RankTable *t, const char *const *want,
                                       int n, const char *what)
{
    int i;

    if (!eq_long(__FILE__, __LINE__, lbl_s(what, "row count"), t->count, n)) {
        return;
    }
    for (i = 0; i < n; i++) {
        eq_str(__FILE__, __LINE__, lbl(what, i), t->row[i].raw, want[i]);
    }
}

static HUNUSED void expect_r30_lines(const Rank30Table *t, const char *const *want,
                                     int n, const char *what)
{
    int i;

    if (!eq_long(__FILE__, __LINE__, lbl_s(what, "row count"), t->count, n)) {
        return;
    }
    for (i = 0; i < n; i++) {
        eq_str(__FILE__, __LINE__, lbl(what, i), t->row[i].raw, want[i]);
    }
}

static HUNUSED void expect_who_lines(const RankWhoTable *t, const char *const *want,
                                     int n, const char *what)
{
    int i;

    if (!eq_long(__FILE__, __LINE__, lbl_s(what, "row count"), t->count, n)) {
        return;
    }
    for (i = 0; i < n; i++) {
        eq_str(__FILE__, __LINE__, lbl(what, i), t->row[i].raw, want[i]);
    }
}

static HUNUSED int merge30_list(Rank30Table *t, const char *const *lines, int n)
{
    RankRow r;
    int i;
    int worst = 0;

    for (i = 0; i < n; i++) {
        int rc;
        if (row_of(lines[i], &r) != 0) {
            fprintf(stderr, "harness failure: %s: candidate %d did not parse\n",
                    g_case, i);
            exit(2);
        }
        rc = rank_merge30(t, &r);
        if (rc != 0) {
            worst = rc;
        }
    }
    return worst;
}

/* Returns how many rows the merges reported dropping.  The rows themselves are
 * the business of the cases that offer one candidate at a time. */
static HUNUSED int mergewho_list(RankWhoTable *t, const char *const *lines, int n)
{
    RankRow r;
    RankRow dropped;
    int i;
    int drops = 0;

    for (i = 0; i < n; i++) {
        if (row_of(lines[i], &r) != 0) {
            fprintf(stderr, "harness failure: %s: candidate %d did not parse\n",
                    g_case, i);
            exit(2);
        }
        memset(&dropped, 0, sizeof dropped);
        if (rank_mergewho(t, &r, &dropped) == 1) {
            drops++;
        }
    }
    return drops;
}

/* Two tables hold the same rows.  The comparison stops at `count`, because the
 * slack row past it (RANK30_ROWS + 1, RANKWHO_ROWS + 1) exists precisely so a
 * row can be inserted and then dropped (spec.md section 3.9): a merge that goes
 * over capacity writes it and leaves it written, which is correct and is why the
 * whole-struct compare cannot be the oracle where capacity is exceeded.  The
 * idempotence cases do compare whole structs, because there the same candidate
 * sequence writes the slack the same way both times. */
static HUNUSED int r30_same_rows(const Rank30Table *a, const Rank30Table *b)
{
    int i;

    if (a->count != b->count) {
        return 0;
    }
    for (i = 0; i < a->count; i++) {
        if (strcmp(a->row[i].raw, b->row[i].raw) != 0
         || a->row[i].score != b->row[i].score) {
            return 0;
        }
    }
    return 1;
}

static HUNUSED int who_same_rows(const RankWhoTable *a, const RankWhoTable *b)
{
    int i;

    if (a->count != b->count) {
        return 0;
    }
    for (i = 0; i < a->count; i++) {
        if (strcmp(a->row[i].raw, b->row[i].raw) != 0
         || a->row[i].score != b->row[i].score
         || strcmp(a->row[i].key, b->row[i].key) != 0) {
            return 0;
        }
    }
    return 1;
}

static int key_cmp9(const void *a, const void *b)
{
    return memcmp(a, b, (size_t)RANK_NAME_LEN + 1u);
}

/* One row per key is RANKWHO.TXT's shape invariant.  Sorted rather than
 * compared pairwise so that the property sweep stays cheap. */
static HUNUSED int who_keys_distinct(const RankWhoTable *t)
{
    static char keys[RANKWHO_ROWS + 1][RANK_NAME_LEN + 1];
    int i;

    for (i = 0; i < t->count; i++) {
        memset(keys[i], 0, sizeof keys[0]);
        strcpy(keys[i], t->row[i].key);
    }
    if (t->count > 1) {
        qsort(keys, (size_t)t->count, sizeof keys[0], key_cmp9);
        for (i = 1; i < t->count; i++) {
            if (memcmp(keys[i - 1], keys[i], sizeof keys[0]) == 0) {
                return 0;
            }
        }
    }
    return 1;
}

/* ------------------------------------------------------------------- U-G */

static void case_ug1(void)
{
    const char *base = rank_default_line(0);
    char line[80];
    size_t len;
    int i;

    for (i = 0; i < RANK_ROWS; i++) {
        eq_long(__FILE__, __LINE__, lbl("shipped row recognised", i),
                rank_is_default_line(rank_default_line(i)), i);
    }

    len = strlen(base);
    EQL("the shipped row is thirty two bytes", (long)len, 32L);
    for (i = 0; i < (int)len; i++) {
        strcpy(line, base);
        line[i] = (line[i] == 'X') ? 'Y' : 'X';
        eq_long(__FILE__, __LINE__, lbl("one byte different is not shipped", i),
                rank_is_default_line(line), -1);
    }

    snprintf(line, sizeof line, " %s", base);
    EQL("a leading space is not shipped", rank_is_default_line(line), -1);
    snprintf(line, sizeof line, "%s ", base);
    EQL("a trailing space is not shipped", rank_is_default_line(line), -1);
    EQL("NULL is not shipped", rank_is_default_line(NULL), -1);
    EQL("an empty line is not shipped", rank_is_default_line(""), -1);

    /* Byte equality, not key equality: decision 8 folds Bio_100% to BIO_100 and
     * a real player called Bio_100 would share that key, so a key based test
     * would start treating their records as filler. */
    EQL("the same name and score on another date is not shipped",
        rank_is_default_line("200000 29 00 Bio_100% 26/09/15 A"), -1);
}

static void case_ug2(void)
{
    static Rank30Table t;
    static const char *const five[5] = {
        "500000 30 01 AAA      26/09/15 A",
        "400000 30 01 BBB      26/09/15 A",
        "300000 30 01 CCC      26/09/15 A",
        "200000 30 01 DDD      26/09/15 A",
        "100000 30 01 EEE      26/09/15 A"
    };
    static const char *const between[5] = {
        "450000 30 01 N01      26/09/15 A",
        "350000 30 01 N02      26/09/15 A",
        "250000 30 01 N03      26/09/15 A",
        "150000 30 01 N04      26/09/15 A",
        "050000 30 01 N05      26/09/15 A"
    };
    static const char *const samekey[2] = {
        "327400 30 01 OMZN     26/09/15 A",
        "299370 30 00 OMZN@@@@ 61/11/17 A"
    };
    const char *const *lines;
    RankRow r;
    int i;

    /* 1. The filler rows are not records, so none of them enters. */
    memset(&t, 0, sizeof t);
    EQL("the shipped rows merge cleanly",
        merge30_list(&t, default_lines(), RANK_ROWS), 0);
    EQL("and none of them is in the table", t.count, 0);

    /* 2. One row offered three times is one row: after a reset each player's
     * best is still in NX93.SCR, so without this the table fills in a day. */
    memset(&t, 0, sizeof t);
    (void)merge30_list(&t, five, 1);
    (void)merge30_list(&t, five, 1);
    (void)merge30_list(&t, five, 1);
    EQL("the same row three times is one row", t.count, 1);

    /* 3. Insertion keeps the order, at each of the five gaps. */
    for (i = 0; i < 5; i++) {
        memset(&t, 0, sizeof t);
        (void)merge30_list(&t, five, 5);
        (void)merge30_list(&t, &between[i], 1);
        if (eq_long(__FILE__, __LINE__, lbl("six rows after inserting", i),
                    t.count, 6)) {
            eq_str(__FILE__, __LINE__, lbl("landed in its descending slot", i),
                   t.row[i + 1].raw, between[i]);
        }
    }

    /* 4. A tie goes behind the row already there. */
    memset(&t, 0, sizeof t);
    (void)merge30_list(&t, five, 5);
    {
        static const char *const tie[1] = {
            "300000 30 09 ZZZ      26/10/01 A"
        };
        (void)merge30_list(&t, tie, 1);
        if (EQL("six rows after the tie", t.count, 6)) {
            EQS("the row already there stays in front", t.row[2].raw, five[2]);
            EQS("and the tie goes behind it", t.row[3].raw, tie[0]);
        }
    }

    /* 5. The thirty first row.  Breaking the floor of the all-time table is the
     * specified behaviour; which row falls through it is not negotiable. */
    lines = gen_rows(RANK30_ROWS, 500000L, 100L);
    {
        static char keep[RANK30_ROWS][RANK_MAX_LINE + 1];
        static const char *ptr[RANK30_ROWS];
        static Rank30Table full;
        static Rank30Table after;

        for (i = 0; i < RANK30_ROWS; i++) {
            strcpy(keep[i], lines[i]);
            ptr[i] = keep[i];
        }
        memset(&full, 0, sizeof full);
        (void)merge30_list(&full, ptr, RANK30_ROWS);
        EQL("thirty rows fit", full.count, (long)RANK30_ROWS);

        after = full;
        if (row_of("000001 30 01 LOW      26/09/15 A", &r) == 0) {
            EQL("the lowest new row merges", rank_merge30(&after, &r), 0);
            EQL("the table is still thirty rows", after.count, (long)RANK30_ROWS);
            OK("and the row that fell through was the new one",
               r30_same_rows(&after, &full));
        }

        after = full;
        if (row_of("999999 30 01 TOP      26/09/15 A", &r) == 0) {
            EQL("the highest new row merges", rank_merge30(&after, &r), 0);
            EQL("the table is still thirty rows", after.count, (long)RANK30_ROWS);
            EQS("the new row is at the top", after.row[0].raw,
                "999999 30 01 TOP      26/09/15 A");
            EQS("and the old last row is gone",
                after.row[RANK30_ROWS - 1].raw, keep[RANK30_ROWS - 2]);
        }

        after = full;
        if (row_of("497100 30 09 TIE      26/10/01 A", &r) == 0) {
            EQL("a row tying the last row merges", rank_merge30(&after, &r), 0);
            EQL("the table is still thirty rows", after.count, (long)RANK30_ROWS);
            OK("and the newcomer is the one that fell through",
               r30_same_rows(&after, &full));
        }
    }

    /* 6. A NULL argument is refused rather than ignored. */
    memset(&t, 0, sizeof t);
    EQL("NULL table is refused", rank_merge30(NULL, &r), -1);
    EQL("NULL row is refused", rank_merge30(&t, NULL), -1);

    /* 7. The all-time table dedupes on bytes, so two rows of the same player
     * both belong in it.  That asymmetry with RANKWHO.TXT is the division of
     * labour between the two files. */
    memset(&t, 0, sizeof t);
    (void)merge30_list(&t, samekey, 2);
    if (EQL("two rows of one player both stay", t.count, 2)) {
        EQS("in descending order, first", t.row[0].raw, samekey[0]);
        EQS("and second", t.row[1].raw, samekey[1]);
    }
}

#define MERGE_N 22

/* Twenty-two candidates: five duplicates, three shipped rows, two rows that
 * share a key but not their bytes, and LLL and MMM, who share a score but not a
 * key.  Running the list twice has to leave the table byte-identical, or an
 * interrupted run that redoes the merge changes the file.
 *
 * LLL and MMM are the pair that makes the ordering rule reach this list at all:
 * with every score distinct, "equal scores keep the order they came in" has
 * nothing here to say.  What they do not do is separate a merge that leaves a
 * tied incumbent alone from one that re-seats it within its own score group.
 * Re-seating every tie is itself idempotent - the order it produces depends on
 * the candidate list and not on the table it started from - so no candidate list
 * replayed against itself can tell the two apart.  That separation belongs to
 * U-G4 and I-50, which name the row that survives instead of comparing two runs
 * with each other. */
static const char *const MERGE22[MERGE_N] = {
    "500000 30 01 AAA      26/09/15 A",
    "400000 30 01 BBB      26/09/15 A",
    "500000 30 01 AAA      26/09/15 A",
    "327400 30 01 OMZN     26/09/15 A",
    "200000 29 00 Bio_100% 93/10/01 A",
    "300000 30 01 CCC      26/09/15 A",
    "325000 30 01 LLL      26/09/15 A",
    "299370 30 00 OMZN@@@@ 61/11/17 A",
    "400000 30 01 BBB      26/09/15 A",
    "180000 28 01 Bio_100% 93/10/01 A",
    "250000 30 01 DDD      26/09/15 A",
    "300000 30 01 CCC      26/09/15 A",
    "150000 30 01 EEE      26/09/15 A",
    "100000 21 05 Bio_100% 93/10/01 A",
    "450000 30 01 FFF      26/09/15 A",
    "325000 30 02 MMM      26/09/16 A",
    "250000 30 01 DDD      26/09/15 A",
    "350000 30 01 GGG      26/09/15 A",
    "120000 30 01 HHH      26/09/15 A",
    "050000 30 01 III      26/09/15 A",
    "275000 30 01 JJJ      26/09/15 A",
    "225000 30 01 KKK      26/09/15 A"
};

static void case_ug3(void)
{
    static Rank30Table once;
    static Rank30Table twice;
    int i;

    memset(&once, 0, sizeof once);
    memset(&twice, 0, sizeof twice);
    (void)merge30_list(&once, MERGE22, MERGE_N);
    (void)merge30_list(&twice, MERGE22, MERGE_N);
    (void)merge30_list(&twice, MERGE22, MERGE_N);

    EQL("the same count both times", twice.count, once.count);
    for (i = 0; i < once.count && i < twice.count; i++) {
        eq_str(__FILE__, __LINE__, lbl("the same row both times", i),
               twice.row[i].raw, once.row[i].raw);
    }
    EQM("byte identical across the whole table",
        &twice, sizeof twice, &once, sizeof once);
}

static void case_ug4(void)
{
    static RankWhoTable t;
    static const char *const three[3] = {
        "500000 30 01 AAA      26/09/15 A",
        "300000 30 01 OMZN     26/09/15 A",
        "200000 30 01 BBB      26/09/15 A"
    };
    static const char *const marked[4] = {
        "100000 30 01 omzn     26/09/15 A",
        "327400 30 01 OMZN     26/09/15 A",
        "299370 30 00 OMZN@@@@ 61/11/17 A",
        "150000 30 01 omzn++++ 26/09/15 A"
    };
    static const char *const apart[5] = {
        "327400 30 01 OMZN     26/09/15 A",
        "310000 30 01 omzo     26/09/15 A",
        "305000 30 01 omzn-2   26/09/15 A",
        "304000 30 01 TARO2    26/09/15 A",
        "303000 30 01 @@@@omzn 26/09/15 A"
    };
    RankRow r;
    RankRow dropped;
    RankRow sentinel;

    memset(&sentinel, 0x5A, sizeof sentinel);

    /* 1. A strictly better score replaces the row and moves it. */
    memset(&t, 0, sizeof t);
    (void)mergewho_list(&t, three, 3);
    dropped = sentinel;
    if (row_of("600000 30 01 omzn     26/10/01 A", &r) == 0) {
        EQL("a better score reports no drop", rank_mergewho(&t, &r, &dropped), 0);
        if (EQL("the table is still three rows", t.count, 3)) {
            EQS("and the replacement moved to the top", t.row[0].raw,
                "600000 30 01 omzn     26/10/01 A");
            EQS("behind which the others keep their order", t.row[1].raw, three[0]);
            EQS("and their order", t.row[2].raw, three[2]);
        }
    }

    /* 2. A tie is not better.  The row that goes is the player's own previous
     * one, so nothing is reported either way. */
    memset(&t, 0, sizeof t);
    (void)mergewho_list(&t, three, 3);
    dropped = sentinel;
    if (row_of("300000 30 09 omzn     26/10/01 A", &r) == 0) {
        EQL("a tie reports no drop", rank_mergewho(&t, &r, &dropped), 0);
        EQS("and the row already there is kept", t.row[1].raw, three[1]);
        EQM("with the caller's dropped row left alone",
            &dropped, sizeof dropped, &sentinel, sizeof sentinel);
    }

    /* 3. A worse score changes nothing. */
    memset(&t, 0, sizeof t);
    (void)mergewho_list(&t, three, 3);
    if (row_of("100000 30 09 omzn     26/10/01 A", &r) == 0) {
        EQL("a worse score reports no drop", rank_mergewho(&t, &r, &dropped), 0);
        EQS("and the row already there is kept", t.row[1].raw, three[1]);
    }

    /* 4. Decision 8: the marked spellings are one player, keeping their best. */
    memset(&t, 0, sizeof t);
    (void)mergewho_list(&t, marked, 4);
    if (EQL("four spellings of one name are one row", t.count, 1)) {
        EQS("holding that player's best", t.row[0].raw, marked[1]);
    }

    /* And the names that only look similar stay apart. */
    memset(&t, 0, sizeof t);
    (void)mergewho_list(&t, apart, 5);
    EQL("five names that are five people are five rows", t.count, 5);
    memset(&t, 0, sizeof t);
    {
        static const char *const taro[2] = {
            "400000 30 01 TARO     26/09/15 A",
            "300000 30 01 TARO2    26/09/15 A"
        };
        (void)mergewho_list(&t, taro, 2);
        EQL("TARO and TARO2 are two people", t.count, 2);
    }

    /* 5. A filler row is not a record. */
    memset(&t, 0, sizeof t);
    (void)mergewho_list(&t, default_lines(), RANK_ROWS);
    EQL("the shipped rows do not enter", t.count, 0);

    /* 6. NULL is refused. */
    EQL("NULL table is refused", rank_mergewho(NULL, &r, &dropped), -1);
    EQL("NULL row is refused", rank_mergewho(&t, NULL, &dropped), -1);
}

static void case_ug5(void)
{
    static RankWhoTable full;
    static RankWhoTable after;
    const char *const *lines;
    static char keep[RANKWHO_ROWS][RANK_MAX_LINE + 1];
    static const char *ptr[RANKWHO_ROWS];
    RankRow r;
    RankRow dropped;
    int i;

    lines = gen_who100();
    for (i = 0; i < RANKWHO_ROWS; i++) {
        strcpy(keep[i], lines[i]);
        ptr[i] = keep[i];
    }
    memset(&full, 0, sizeof full);
    (void)mergewho_list(&full, ptr, RANKWHO_ROWS);
    if (!EQL("a hundred players fit", full.count, (long)RANKWHO_ROWS)) {
        return;
    }

    /* 1. A new player in the middle pushes the last one out, and the row that
     * goes is reported rather than quietly dropped. */
    after = full;
    memset(&dropped, 0, sizeof dropped);
    if (row_of("495000 30 01 ZZZ      26/09/15 A", &r) == 0) {
        int seated = 0;
        int ordered = 1;
        EQL("the hundred and first player reports a drop",
            rank_mergewho(&after, &r, &dropped), 1);
        EQL("the table is still a hundred rows", after.count, (long)RANKWHO_ROWS);
        EQS("and the row reported is the old last one",
            dropped.raw, keep[RANKWHO_ROWS - 1]);
        for (i = 0; i < after.count; i++) {
            if (strcmp(after.row[i].raw, "495000 30 01 ZZZ      26/09/15 A") == 0) {
                seated = 1;
            }
            if (i > 0 && after.row[i - 1].score < after.row[i].score) {
                ordered = 0;
            }
        }
        OK("the newcomer took a seat", seated);
        OK("and the table is still descending", ordered);
        OK("one row per key still holds", who_keys_distinct(&after));
    }

    /* 2. A new player below everyone loses their own seat, which is still a
     * drop and still reported. */
    after = full;
    memset(&dropped, 0, sizeof dropped);
    if (row_of("1 30 01 ZZZ      26/09/15 A", &r) == 0) {
        EQL("a newcomer below the floor reports a drop",
            rank_mergewho(&after, &r, &dropped), 1);
        EQS("and the row reported is the newcomer itself",
            dropped.raw, "1 30 01 ZZZ      26/09/15 A");
        OK("so the table holds exactly the rows it held before",
           who_same_rows(&after, &full));
    }

    /* 3. A tie with the floor is not better than it: the incumbent keeps the
     * seat, which is what makes the merge idempotent. */
    after = full;
    memset(&dropped, 0, sizeof dropped);
    {
        char line[80];
        snprintf(line, sizeof line, "%06ld 30 09 ZZZ      26/10/01 A",
                 WHO100_LAST_SCORE);
        if (row_of(line, &r) == 0) {
            EQL("a newcomer tying the floor reports a drop",
                rank_mergewho(&after, &r, &dropped), 1);
            EQS("and it is the newcomer that falls", dropped.raw, line);
            OK("leaving the table holding exactly what it held",
               who_same_rows(&after, &full));
        }
    }
}

static void case_ug6(void)
{
    static RankWhoTable once;
    static RankWhoTable twice;
    static const char *const marked[3] = {
        "100000 30 01 omzn     26/09/15 A",
        "327400 30 01 OMZN@@@@ 26/09/16 A",
        "150000 30 01 omzn++++ 26/09/17 A"
    };
    const char *const *lines;
    static char keep[RANKWHO_ROWS][RANK_MAX_LINE + 1];
    static const char *ptr[RANKWHO_ROWS];
    int i;

    memset(&once, 0, sizeof once);
    memset(&twice, 0, sizeof twice);
    (void)mergewho_list(&once, MERGE22, MERGE_N);
    (void)mergewho_list(&twice, MERGE22, MERGE_N);
    (void)mergewho_list(&twice, MERGE22, MERGE_N);
    EQM("twenty two candidates twice leave the table byte identical",
        &twice, sizeof twice, &once, sizeof once);

    /* The same, with the marked spellings of one player in the stream. */
    memset(&once, 0, sizeof once);
    memset(&twice, 0, sizeof twice);
    (void)mergewho_list(&once, marked, 3);
    (void)mergewho_list(&twice, marked, 3);
    (void)mergewho_list(&twice, marked, 3);
    EQM("one player under three spellings, twice over",
        &twice, sizeof twice, &once, sizeof once);

    /* And over capacity, where the second pass re-offers rows that were
     * dropped: they have to be dropped again rather than displace anyone. */
    lines = gen_who100();
    for (i = 0; i < RANKWHO_ROWS; i++) {
        strcpy(keep[i], lines[i]);
        ptr[i] = keep[i];
    }
    memset(&once, 0, sizeof once);
    memset(&twice, 0, sizeof twice);
    (void)mergewho_list(&once, ptr, RANKWHO_ROWS);
    (void)mergewho_list(&once, MERGE22, MERGE_N);
    (void)mergewho_list(&twice, ptr, RANKWHO_ROWS);
    (void)mergewho_list(&twice, MERGE22, MERGE_N);
    (void)mergewho_list(&twice, ptr, RANKWHO_ROWS);
    (void)mergewho_list(&twice, MERGE22, MERGE_N);
    EQL("the table stays at capacity", once.count, (long)RANKWHO_ROWS);
    EQM("and a second pass over a full table changes nothing",
        &twice, sizeof twice, &once, sizeof once);
}

/* The invariants, checked after every single merge over a long random stream.
 *
 * The slack row (RANK30_ROWS + 1, RANKWHO_ROWS + 1) exists so that a row can be
 * inserted and then dropped (spec.md section 3.9), so row[count] is legitimately
 * written to and cannot be part of an "untouched" claim.  What can be claimed is
 * that nothing is written past the table at all, which a guard behind it shows. */
static void case_ug7(void)
{
    static struct { Rank30Table t; char guard[64]; } g30;
    static struct { RankWhoTable t; char guard[64]; } gwho;
    static char want_guard[64];
    char line[80];
    RankRow r;
    RankRow dropped;
    int round;
    long bad_count = 0;
    long bad_order = 0;
    long bad_keys = 0;
    long bad_default = 0;
    long bad_guard = 0;

    memset(want_guard, 0xAA, sizeof want_guard);
    srand(20261021u);

    for (round = 0; round < 100; round++) {
        int step;

        memset(&g30, 0, sizeof g30);
        memset(&gwho, 0, sizeof gwho);
        memset(g30.guard, 0xAA, sizeof g30.guard);
        memset(gwho.guard, 0xAA, sizeof gwho.guard);

        for (step = 0; step < 500; step++) {
            char name[RANK_NAME_LEN + 1];
            int i;

            /* Names over a small alphabet, so collisions and near misses are
             * common rather than vanishingly rare. */
            for (i = 0; i < RANK_NAME_LEN; i++) {
                static const char alpha[10] = {
                    'a', 'B', '1', '@', '-', ' ', 'z', '9', '+', 'Q'
                };
                name[i] = alpha[rand() % 10];
            }
            name[RANK_NAME_LEN] = '\0';
            /* Every fifth candidate is a shipped row, so the "no filler in the
             * tables" invariant is actually exercised. */
            if ((step % 5) == 4) {
                strcpy(line, rank_default_line(rand() % RANK_ROWS));
            } else {
                char score[12];
                snprintf(score, sizeof score, "%ld", (long)(rand() % 999999) + 1L);
                make_line(line, sizeof line, score, name);
            }
            if (row_of(line, &r) != 0) {
                continue;
            }
            (void)rank_merge30(&g30.t, &r);
            (void)rank_mergewho(&gwho.t, &r, &dropped);

            if (g30.t.count < 0 || g30.t.count > RANK30_ROWS
             || gwho.t.count < 0 || gwho.t.count > RANKWHO_ROWS) {
                bad_count++;
                break;
            }
            for (i = 1; i < g30.t.count; i++) {
                if (g30.t.row[i - 1].score < g30.t.row[i].score) {
                    bad_order++;
                    break;
                }
            }
            for (i = 1; i < gwho.t.count; i++) {
                if (gwho.t.row[i - 1].score < gwho.t.row[i].score) {
                    bad_order++;
                    break;
                }
            }
            if (!who_keys_distinct(&gwho.t)) {
                bad_keys++;
            }
            for (i = 0; i < g30.t.count; i++) {
                if (rank_is_default_line(g30.t.row[i].raw) >= 0) {
                    bad_default++;
                    break;
                }
            }
            for (i = 0; i < gwho.t.count; i++) {
                if (rank_is_default_line(gwho.t.row[i].raw) >= 0) {
                    bad_default++;
                    break;
                }
            }
            if (memcmp(g30.guard, want_guard, sizeof want_guard) != 0
             || memcmp(gwho.guard, want_guard, sizeof want_guard) != 0) {
                bad_guard++;
                break;
            }
        }
    }
    EQL("merge steps that left a count out of range", bad_count, 0L);
    EQL("merge steps that left a score out of order", bad_order, 0L);
    EQL("merge steps that left two rows under one key", bad_keys, 0L);
    EQL("merge steps that let a shipped row into a table", bad_default, 0L);
    EQL("merge steps that wrote past the end of a table", bad_guard, 0L);
}

static void case_ug8(void)
{
    RankTable cur;
    RankTable out;
    char *bytes;
    size_t len;
    int i;
    int kept = 0;
    int all_omzn = 1;
    int omzn_rows = 0;
    int twohundred = 0;

    if (read_table_bytes(fix_scr13, FIX_SCR13_LEN, &cur) != RANK_ROWS) {
        die("fix_scr13");
    }

    /* The intermediate state the two decisions produce, stated where it can be
     * seen: six of the ten rows are records, and under decision 8 all six are
     * the same player, so deduplication leaves exactly one. */
    for (i = 0; i < cur.count; i++) {
        if (rank_is_default_line(cur.row[i].raw) < 0) {
            kept++;
            if (strcmp(cur.row[i].key, "OMZN") != 0) {
                all_omzn = 0;
            }
        }
    }
    EQL("six of the ten live rows are records", kept, 6);
    OK("and all six fold to the key OMZN", all_omzn);

    memset(&out, 0, sizeof out);
    rank_rebuild(&cur, &out);
    expect_table_lines(&out, G8_WANT, RANK_ROWS, "the live table rebuilt");

    for (i = 0; i < out.count; i++) {
        if (strcmp(out.row[i].key, "OMZN") == 0) {
            omzn_rows++;
        }
        if (out.row[i].score == 200000L) {
            twohundred++;
        }
    }
    EQL("one row survives deduplication under the key OMZN", omzn_rows, 1);
    EQL("and 200000 appears once, not twice", twohundred, 1);
    OK("OMZN@@@@ is not a second player any more",
       strstr(out.row[0].raw, "OMZN@@@@") == NULL);
    for (i = 0; i < out.count; i++) {
        OK(lbl("020000 was pushed out", i), out.row[i].score != 20000L);
    }

    bytes = written_table_bytes(&out, &len);
    if (OK("the rebuilt table writes", bytes != NULL)) {
        EQL("340 bytes", (long)len, 340L);
        free(bytes);
    }
}

static void case_ug9(void)
{
    static const char *const src[3] = {
        "the 2026-09-28 table", "the game's own backup", "the shipped table"
    };
    const char *data[3];
    size_t len[3];
    RankTable a;
    RankTable b;
    RankTable c;
    int k;

    data[0] = fix_scr;  len[0] = FIX_SCR_LEN;
    data[1] = fix_bak;  len[1] = FIX_BAK_LEN;
    data[2] = fix_def;  len[2] = FIX_DEF_LEN;

    /* The live table, three passes deep: the output holds eight shipped rows, so
     * without the filler exclusion the second pass duplicates them. */
    if (read_table_bytes(fix_scr13, FIX_SCR13_LEN, &a) == RANK_ROWS) {
        memset(&b, 0, sizeof b);
        rank_rebuild(&a, &b);
        memset(&c, 0, sizeof c);
        rank_rebuild(&b, &c);
        expect_table_lines(&c, G8_WANT, RANK_ROWS, "rebuilt twice");
        memset(&a, 0, sizeof a);
        rank_rebuild(&c, &a);
        expect_table_lines(&a, G8_WANT, RANK_ROWS, "rebuilt three times");
    }

    for (k = 0; k < 3; k++) {
        char *first;
        char *second;
        size_t fl;
        size_t sl;

        if (read_table_bytes(data[k], len[k], &a) < 0) {
            die(src[k]);
        }
        memset(&b, 0, sizeof b);
        rank_rebuild(&a, &b);
        memset(&c, 0, sizeof c);
        rank_rebuild(&b, &c);
        first = written_table_bytes(&b, &fl);
        second = written_table_bytes(&c, &sl);
        if (first != NULL && second != NULL) {
            eq_mem(__FILE__, __LINE__, lbl_s("rebuild is idempotent for", src[k]),
                   second, sl, first, fl);
        }
        free(first);
        free(second);
    }

    /* The empty table, which has no reader to come through. */
    memset(&a, 0, sizeof a);
    a.count = 0;
    memset(&b, 0, sizeof b);
    rank_rebuild(&a, &b);
    memset(&c, 0, sizeof c);
    rank_rebuild(&b, &c);
    expect_table_lines(&b, default_lines(), RANK_ROWS, "empty rebuilds to shipped");
    expect_table_lines(&c, default_lines(), RANK_ROWS, "and stays there");
}

static void case_ug10(void)
{
    RankTable cur;
    RankTable out;
    RankTable again;
    char *bytes;
    size_t len;
    int i;
    int j;

    table_of(G10_IN, RANK_ROWS, &cur);
    memset(&out, 0, sizeof out);
    rank_rebuild(&cur, &out);
    expect_table_lines(&out, G10_WANT, RANK_ROWS, "scores compared as numbers");

    bytes = written_table_bytes(&out, &len);
    if (OK("the table writes", bytes != NULL)) {
        /* Eight rows of 32 bytes and two of 31, so a reader that assumed a
         * fixed 34 byte row would already have gone wrong. */
        EQL("338 bytes", (long)len, 338L);
        free(bytes);
    }

    for (i = 0; i < 7; i++) {
        int found = 0;
        for (j = 0; j < out.count; j++) {
            if (strcmp(out.row[j].raw, G10_OUT[i]) == 0) {
                found = 1;
            }
        }
        eq_long(__FILE__, __LINE__, lbl("a low score took no seat", i), found, 0);
    }

    memset(&again, 0, sizeof again);
    rank_rebuild(&out, &again);
    expect_table_lines(&again, G10_WANT, RANK_ROWS, "and it is idempotent");
}

/* --------------------------------------------------------- 旧台帳の組み立て
 *
 * The grammar of spec.md section 3.4, in pieces, so that each variant below
 * differs from a readable ledger in exactly one place.  Every variant carries
 * the nine byte "backups" key, because without it migration never fires and the
 * run ends in E10 - a different defect from the one being tested.
 */
#define LPRE "{\r\n  \"lastUpdate\": \"2026-10-08T11:57:39\",\r\n  \"backups\""
#define LENT "{\"backupAt\": \"2026-10-08T11:57:39\", \"ranking\": ["
#define LROW "\"327400 30 01 OMZN     26/09/15 A\""
#define LEND "]}]}\r\n"
#define LGOOD LPRE ": [" LENT LROW LEND

/* 70 bytes, which decodes past the 63 byte limit of one row buffer. */
#define L70 "\"AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA\""

typedef struct { const char *name; const char *text; } LedgerBad;

static const LedgerBad LEDGER_BAD[25] = {
    { "no colon after the backups key",
      LPRE " [" LENT LROW LEND },
    { "backups is null",
      LPRE ": null}\r\n" },
    { "backups is an object",
      LPRE ": {}}\r\n" },
    { "the backups array never closes",
      LPRE ": [" LENT LROW "]}" },
    { "the outer object never closes",
      LPRE ": [" LENT LROW "]}]" },
    { "bytes after the closing brace",
      LGOOD "garbage" },
    { "a trailing comma in the entry list",
      LPRE ": [" LENT LROW "]},]}\r\n" },
    { "no comma between two entries",
      LPRE ": [" LENT LROW "]}" LENT LROW "]}]}\r\n" },
    { "an entry without its opening brace",
      LPRE ": [\"backupAt\": \"2026-10-08T11:57:39\", \"ranking\": [" LROW "]]}\r\n" },
    { "backupAt renamed",
      LPRE ": [{\"when\": \"2026-10-08T11:57:39\", \"ranking\": [" LROW LEND },
    { "no ranking key at all",
      LPRE ": [{\"backupAt\": \"2026-10-08T11:57:39\"}]}\r\n" },
    { "ranking before backupAt",
      LPRE ": [{\"ranking\": [" LROW "], \"backupAt\": \"2026-10-08T11:57:39\"}]}\r\n" },
    { "an entry without its closing brace",
      LPRE ": [" LENT LROW "]]}\r\n" },
    { "no comma between two rows",
      LPRE ": [" LENT LROW " " LROW LEND },
    { "a row string that never closes",
      LPRE ": [" LENT "\"327400 30 01 OMZN     26/09/15 A" },
    { "a raw CR inside a row string",
      LPRE ": [" LENT "\"3274\r00 30 01 OMZN     26/09/15 A\"" LEND },
    { "a raw LF inside a row string",
      LPRE ": [" LENT "\"3274\n00 30 01 OMZN     26/09/15 A\"" LEND },
    { "an \\x escape",
      LPRE ": [" LENT "\"327400 30 01 \\x41MZN    26/09/15 A\"" LEND },
    { "a \\u escape whose digits are not hex",
      LPRE ": [" LENT "\"327400 30 01 \\u00G0MZN 26/09/15 A\"" LEND },
    { "a \\u escape above 0x00ff",
      LPRE ": [" LENT "\"327400 30 01 \\u0100MZN 26/09/15 A\"" LEND },
    { "a row of twenty six bytes",
      LPRE ": [" LENT "\" 30 01 OMZN     26/09/15 A\"" LEND },
    { "a row of thirty six bytes",
      LPRE ": [" LENT "\"1234567890 30 01 OMZN     26/09/15 A\"" LEND },
    { "a non-digit in the score",
      LPRE ": [" LENT "\"32740X 30 01 OMZN     26/09/15 A\"" LEND },
    { "a separator that is not a space",
      LPRE ": [" LENT "\"327400 30 01 OMZN    X26/09/15 A\"" LEND },
    { "a string longer than the row buffer",
      LPRE ": [" LENT L70 LEND }
};

#define LEDGER_BAD_N (sizeof LEDGER_BAD / sizeof LEDGER_BAD[0])

/* Six thousand entries of ten rows each, over two mebibytes, written straight to
 * disk: the point of the case is that nothing holds the file in memory.  Scores
 * 100000..159999 are all distinct, so the all-time table fills to its floor, and
 * the hundred keys P00..P99 bring the per-user table exactly to capacity. */
#define HUGE_ENTRIES 6000

static HUNUSED long write_huge_ledger(const char *path)
{
    FILE *fp = fopen(path, "wb");
    int e;
    int r;

    if (fp == NULL) {
        die(path);
    }
    fputs("{\r\n  \"lastUpdate\": \"2026-10-08T11:57:39\",\r\n  \"backups\": [", fp);
    for (e = 0; e < HUGE_ENTRIES; e++) {
        if (e > 0) {
            fputc(',', fp);
        }
        fputs("\r\n    {\"backupAt\": \"2026-10-08T11:57:39\", \"ranking\": [", fp);
        for (r = 0; r < 10; r++) {
            long n = (long)e * 10L + (long)r;
            char row[80];
            if (r > 0) {
                fputc(',', fp);
            }
            snprintf(row, sizeof row, "\"%06ld 30 01 P%02ld      26/09/15 A\"",
                     100000L + n, n % 100L);
            fputs(row, fp);
        }
        fputs("]}", fp);
    }
    fputs("\r\n  ]\r\n}\r\n", fp);
    if (fclose(fp) != 0) {
        die(path);
    }
    return file_size(path);
}

/* ------------------------------------------------------------------- U-V */

static void case_uv1(void)
{
    static const struct { const char *what; const char *body; const char *want;
                          int wantlen; } tab[9] = {
        { "two character quote escape",   "\\\"\"",     "\"",        1 },
        { "two character backslash escape", "\\\\\"",   "\\",        1 },
        { "numeric form of a quote",      "\\u0022\"",  "\"",        1 },
        { "numeric form of a backslash, lowercase hex", "\\u005c\"", "\\", 1 },
        { "numeric form of a backslash, uppercase hex", "\\u005C\"", "\\", 1 },
        { "the bottom of the control range", "\\u0000\"", "\000",    1 },
        { "the top of the control range",  "\\u001f\"", "\037",      1 },
        { "the top of the byte range",     "\\u00ff\"", "\377",      1 },
        { "raw high bytes, passed through", "\202\240\"", "\202\240", 2 }
    };
    char buf[64];
    char body[80];
    size_t i;

    for (i = 0; i < sizeof tab / sizeof tab[0]; i++) {
        int rc;
        memset(buf, 0x5A, sizeof buf);
        rc = ledger_string_bytes(tab[i].body, strlen(tab[i].body),
                                 buf, (int)sizeof buf);
        if (!eq_long(__FILE__, __LINE__, lbl_s("decoded length", tab[i].what),
                     rc, tab[i].wantlen)) {
            continue;
        }
        eq_mem(__FILE__, __LINE__, lbl_s("decoded bytes", tab[i].what),
               buf, (size_t)rc, tab[i].want, (size_t)tab[i].wantlen);
    }

    /* The empty string is a string. */
    memset(buf, 0x5A, sizeof buf);
    EQL("an empty string decodes to nothing",
        ledger_string_bytes("\"", 1u, buf, (int)sizeof buf), 0);

    /* The length boundary.  A row is at most 35 bytes, but the buffer is the
     * line buffer, so 63 is what has to fit. */
    memset(body, 'A', 63u);
    body[63] = '"';
    EQL("sixty three bytes fit",
        ledger_string_bytes(body, 64u, buf, (int)sizeof buf), 63);
    memset(body, 'A', 64u);
    body[64] = '"';
    EQL("sixty four do not",
        ledger_string_bytes(body, 65u, buf, (int)sizeof buf), -1);
}

static void case_uv2(void)
{
    static const struct { const char *what; const char *body; } tab[13] = {
        { "an \\x escape",                      "\\x41\"" },
        { "a newline escape",                   "\\n\"" },
        { "a tab escape",                       "\\t\"" },
        { "a solidus escape",                   "\\/\"" },
        { "a backspace escape",                 "\\b\"" },
        { "a code point above 0x00ff",          "\\u0100\"" },
        { "an unpaired surrogate",              "\\uD800\"" },
        { "non-hex digits",                     "\\u00G0\"" },
        { "a \\u escape cut short by EOF",      "\\u00" },
        { "a backslash cut short by EOF",       "\\" },
        { "no closing quote before EOF",        "abc" },
        { "a raw CR",                           "ab\rcd\"" },
        { "a raw LF",                           "ab\ncd\"" }
    };
    static const char tab_ht[] = "ab\tcd\"";
    static const char raw_nul[] = { 'a', 'b', '\0', 'c', 'd', '"' };
    static const char raw_sub[] = { 'a', 'b', 0x1A, 'c', 'd', '"' };
    char buf[64];
    size_t i;

    for (i = 0; i < sizeof tab / sizeof tab[0]; i++) {
        eq_long(__FILE__, __LINE__, lbl_s("refused:", tab[i].what),
                ledger_string_bytes(tab[i].body, strlen(tab[i].body),
                                    buf, (int)sizeof buf), -1);
    }
    EQL("a raw TAB is refused",
        ledger_string_bytes(tab_ht, sizeof tab_ht - 1u, buf, (int)sizeof buf), -1);
    EQL("a raw NUL is refused",
        ledger_string_bytes(raw_nul, sizeof raw_nul, buf, (int)sizeof buf), -1);
    /* 0x1A is also the DOS end of file marker, so a reader that treats it as
     * EOF accepts this one by accident. */
    EQL("a raw 0x1A is refused",
        ledger_string_bytes(raw_sub, sizeof raw_sub, buf, (int)sizeof buf), -1);
}

static void case_uv3(void)
{
    static Rank30Table t30_lf;
    static RankWhoTable who_lf;
    static Rank30Table t30_crlf;
    static RankWhoTable who_crlf;
    int w1 = 0;

    memset(&t30_lf, 0, sizeof t30_lf);
    memset(&who_lf, 0, sizeof who_lf);
    cap_begin();
    EQL("the live ledger imports", import_bytes(fix_ledger_lf, FIX_LEDGER_LF_LEN,
                                                &t30_lf, &who_lf), 0);
    cap_end();
    /* Ten rows and a hundred seats to spare, so nobody is turned away. */
    w1 = cap_count("327400 30 01 OMZN     26/09/15 A");
    EQL("and nothing was reported as dropped", w1, 0);

    expect_r30_lines(&t30_lf, LEDGER_ROWS, 10, "imported all-time table");
    expect_who_lines(&who_lf, I26_WHO, 1, "imported per-user table");

    /* The grammar's whitespace rule absorbs the difference between LF and CRLF,
     * so the reconstructed CRLF form has to import to the same bytes.  The CRLF
     * fixture is a reconstruction rather than an observation, which is why the
     * oracle is this relation and not its byte count. */
    memset(&t30_crlf, 0, sizeof t30_crlf);
    memset(&who_crlf, 0, sizeof who_crlf);
    cap_begin();
    EQL("the CRLF ledger imports",
        import_bytes(fix_ledger_crlf, FIX_LEDGER_CRLF_LEN, &t30_crlf, &who_crlf), 0);
    cap_end();
    EQM("both line endings give the same all-time table",
        &t30_crlf, sizeof t30_crlf, &t30_lf, sizeof t30_lf);
    EQM("and the same per-user table",
        &who_crlf, sizeof who_crlf, &who_lf, sizeof who_lf);
}

static void case_uv4(void)
{
    static Rank30Table t30;
    static RankWhoTable who;
    static Rank30Table empty30;
    static RankWhoTable emptywho;
    size_t i;

    memset(&empty30, 0, sizeof empty30);
    memset(&emptywho, 0, sizeof emptywho);

    /* The readable form first, so a variant that fails for the wrong reason is
     * not mistaken for the grammar being enforced. */
    memset(&t30, 0, sizeof t30);
    memset(&who, 0, sizeof who);
    cap_begin();
    EQL("the assembled ledger is readable",
        import_bytes(LGOOD, strlen(LGOOD), &t30, &who), 0);
    cap_end();
    EQL("and it holds the one row", t30.count, 1);

    for (i = 0; i < LEDGER_BAD_N; i++) {
        memset(&t30, 0, sizeof t30);
        memset(&who, 0, sizeof who);
        cap_begin();
        eq_long(__FILE__, __LINE__, lbl_s("refused:", LEDGER_BAD[i].name),
                import_bytes(LEDGER_BAD[i].text, strlen(LEDGER_BAD[i].text),
                             &t30, &who), -1);
        cap_end();
        /* Refusing has to mean refusing wholesale: the tables the caller handed
         * in are what it still has. */
        eq_mem(__FILE__, __LINE__,
               lbl_s("all-time table untouched by:", LEDGER_BAD[i].name),
               &t30, sizeof t30, &empty30, sizeof empty30);
        eq_mem(__FILE__, __LINE__,
               lbl_s("per-user table untouched by:", LEDGER_BAD[i].name),
               &who, sizeof who, &emptywho, sizeof emptywho);
        /* Every variant must still look like a ledger, or it is testing E10. */
        OK(lbl("the variant still carries the backups key", (long)i),
           has_backups_bytes(LEDGER_BAD[i].text,
                             strlen(LEDGER_BAD[i].text)) == 1);
    }

    /* A hand edited file can put the nine byte key somewhere the grammar does
     * not expect it, because rank_has_backups is a raw scan.  The contract is
     * all or nothing rather than a particular answer: either it is refused and
     * the tables are untouched, or it is accepted and the tables are consistent. */
    {
        static const char *const odd =
            "{\r\n  \"lastUpdate\": \"2026-10-08T11:57:39\",\r\n"
            "  \"note\": \"\"backups\": [\",\r\n  \"backups\": [" LENT LROW LEND;
        int rc;
        memset(&t30, 0, sizeof t30);
        memset(&who, 0, sizeof who);
        cap_begin();
        rc = import_bytes(odd, strlen(odd), &t30, &who);
        cap_end();
        if (rc == -1) {
            EQM("a refused odd ledger left the all-time table alone",
                &t30, sizeof t30, &empty30, sizeof empty30);
            EQM("and the per-user table alone",
                &who, sizeof who, &emptywho, sizeof emptywho);
        } else if (EQL("an odd ledger is either refused or imported", rc, 0)) {
            OK("and an imported one is internally consistent",
               t30.count >= 0 && t30.count <= RANK30_ROWS
               && who.count >= 0 && who.count <= RANKWHO_ROWS
               && who_keys_distinct(&who));
        }
    }
}

static void case_uv5(void)
{
    static Rank30Table t30;
    static RankWhoTable who;
    char path[TPATH];
    long bytes;
    int i;

    snprintf(path, sizeof path, "%s/huge.txt", g_scratch);
    bytes = write_huge_ledger(path);
    OK("the synthetic ledger really is over two mebibytes",
       bytes > 2L * 1024L * 1024L);

    memset(&t30, 0, sizeof t30);
    memset(&who, 0, sizeof who);
    cap_begin();
    EQL("a two mebibyte ledger imports", import_file(path, &t30, &who), 0);
    cap_end();
    EQL("the all-time table is at its floor", t30.count, (long)RANK30_ROWS);
    EQL("and the per-user table at its capacity", who.count, (long)RANKWHO_ROWS);
    OK("with one row per key", who_keys_distinct(&who));
    for (i = 0; i < t30.count; i++) {
        eq_long(__FILE__, __LINE__, lbl("the highest thirty scores", i),
                t30.row[i].score, 159999L - (long)i);
    }
    rm_tree(path);
}

static void case_uv6(void)
{
    static Rank30Table t30;
    static RankWhoTable who;
    static const char *const empty_backups = LPRE ": []}\r\n";
    static const char *const empty_ranking =
        LPRE ": [{\"backupAt\": \"2026-10-08T11:57:39\", \"ranking\": []}]}\r\n";
    static const char *const mixed =
        LPRE ": [{\"backupAt\": \"2026-10-08T11:57:39\", \"ranking\": []},"
        LENT LROW LEND;

    memset(&t30, 0, sizeof t30);
    memset(&who, 0, sizeof who);
    cap_begin();
    EQL("an empty backups array imports",
        import_bytes(empty_backups, strlen(empty_backups), &t30, &who), 0);
    cap_end();
    EQL("with no all-time rows", t30.count, 0);
    EQL("and no per-user rows", who.count, 0);

    memset(&t30, 0, sizeof t30);
    memset(&who, 0, sizeof who);
    cap_begin();
    EQL("an entry with an empty ranking imports",
        import_bytes(empty_ranking, strlen(empty_ranking), &t30, &who), 0);
    cap_end();
    EQL("with no all-time rows", t30.count, 0);
    EQL("and no per-user rows", who.count, 0);

    memset(&t30, 0, sizeof t30);
    memset(&who, 0, sizeof who);
    cap_begin();
    EQL("an empty entry beside a full one imports",
        import_bytes(mixed, strlen(mixed), &t30, &who), 0);
    cap_end();
    expect_r30_lines(&t30, I26_WHO, 1, "only the full entry's row");
}

/* Bit flips, truncations, insertions and quote changes over the real ledger.
 * The bar is that the import answers rather than misbehaves, and that a refusal
 * never leaves half an import behind. */
static void case_uv7(void)
{
    static Rank30Table t30;
    static RankWhoTable who;
    static Rank30Table before30;
    static RankWhoTable beforewho;
    static char buf[1024];
    int iter;
    long bad_rc = 0;
    long bad_partial = 0;

    srand(20261022u);
    for (iter = 0; iter < 20000; iter++) {
        size_t n = FIX_LEDGER_CRLF_LEN;
        int kind;
        int rc;

        memcpy(buf, fix_ledger_crlf, FIX_LEDGER_CRLF_LEN);
        kind = rand() % 4;
        if (kind == 0) {
            size_t at = (size_t)(rand() % (int)n);
            buf[at] = (char)(buf[at] ^ (1 << (rand() % 8)));
        } else if (kind == 1) {
            n = (size_t)(rand() % (int)(FIX_LEDGER_CRLF_LEN + 1));
        } else if (kind == 2) {
            size_t at = (size_t)(rand() % (int)(n + 1));
            memmove(buf + at + 1, buf + at, n - at);
            buf[at] = (char)(rand() % 256);
            n++;
        } else {
            size_t at = (size_t)(rand() % (int)(n + 1));
            memmove(buf + at + 1, buf + at, n - at);
            buf[at] = '"';
            n++;
        }

        memset(&t30, 0, sizeof t30);
        memset(&who, 0, sizeof who);
        before30 = t30;
        beforewho = who;
        cap_begin();
        rc = import_bytes(buf, n, &t30, &who);
        cap_end();

        if (rc != 0 && rc != -1) {
            if (bad_rc == 0L) {
                fprintf(stderr, "FAIL  %s  [iteration %d kind %d] rc=%d\n",
                        g_case, iter, kind, rc);
                g_case_failed = 1;
            }
            bad_rc++;
        }
        if (rc == -1
         && (memcmp(&t30, &before30, sizeof t30) != 0
          || memcmp(&who, &beforewho, sizeof who) != 0)) {
            if (bad_partial == 0L) {
                fprintf(stderr, "FAIL  %s  [iteration %d kind %d] refused but "
                                "wrote to a table\n", g_case, iter, kind);
                g_case_failed = 1;
            }
            bad_partial++;
        }
    }
    EQL("mutated ledgers that answered neither 0 nor -1", bad_rc, 0L);
    EQL("refusals that had already written into a table", bad_partial, 0L);
}

/* --------------------------------------------------------------- 統合の支度
 *
 * Every integration case ends in expect_no_temps, which is I-48's crosscut: on
 * no path may RANKTMP.* or RANKOLD.* outlive a run, the one exception being the
 * single retained copy a stranded commit leaves behind.
 */

#define T_LAST    "2026-10-19T00:00:00"   /* a Monday */
#define T_SAMEWK  "2026-10-25T23:59:59"   /* the Sunday after: daily crosses, weekly does not */
#define T_NEXTWK  "2026-10-26T02:00:00"   /* the next Monday: both cross */
#define T_LEDGER  "2026-10-08T11:57:39"   /* the live ledger's lastUpdate */
#define T_LEDGER_SAMEDAY "2026-10-08T23:00:00"

/* A directory holding the live table and a canonical state file. */
static void env_live(Env *e, const char *tag, const char *last)
{
    env_make(e, tag);
    write_bytes(e->scr, fix_scr13, FIX_SCR13_LEN);
    env_state(e, last);
}

/* --------------------------------------------------------------- I-21..I-25 */

static void case_i21(void)
{
    Env e;

    /* Six days and twenty three hours, which is one calendar day by the daily
     * rule and no Monday at all by the weekly one. */
    env_live(&e, "i21-weekly", T_LAST);
    env_snapshot(&e);
    set_clock(T_SAMEWK);
    EQL("the weekly rule does nothing here", rr(&e, RANK_MODE_WEEKLY), 0);
    expect_unchanged(&e);
    expect_no_temps(&e, NULL);

    env_live(&e, "i21-daily", T_LAST);
    set_clock(T_SAMEWK);
    EQL("the daily rule resets", rr(&e, RANK_MODE_DAILY), 0);
    expect_rows_file(e.r30, I26_R30, 6, "RANK30.TXT after the daily reset");
    expect_rows_file(e.who, I26_WHO, 1, "RANKWHO.TXT after the daily reset");
    expect_rows_file(e.scr, G8_WANT, RANK_ROWS, "NX93.SCR after the daily reset");
    expect_state(e.txt, T_SAMEWK, "NX93.TXT after the daily reset");
    expect_no_temps(&e, NULL);
}

/* The discriminators of U-C5 plus two rows that cross in neither mode, run
 * through rank_run so that the rule is observed where it is actually wired. */
static void case_i22(void)
{
    static const int rows[7] = { 0, 1, 4, 7, 10, 2, 11 };   /* 1,2,5,8,11 and 3,12 */
    int k;
    int m;

    for (k = 0; k < 7; k++) {
        const CrossRow *c = &CROSS[rows[k]];
        for (m = 0; m < 2; m++) {
            int mode = (m == 0) ? RANK_MODE_WEEKLY : RANK_MODE_DAILY;
            int want = (m == 0) ? c->weekly : c->daily;
            Env e;
            char tag[64];

            snprintf(tag, sizeof tag, "i22-%d-%s", rows[k] + 1,
                     (m == 0) ? "w" : "d");
            env_live(&e, tag, c->last);
            env_snapshot(&e);
            set_clock(c->now);

            eq_long(__FILE__, __LINE__, lbl("the run reports success",
                                            (long)(rows[k] + 1) * 10L + m),
                    rr(&e, mode), 0);
            if (want) {
                OK(lbl("and the table was reset",
                       (long)(rows[k] + 1) * 10L + m),
                   !snap_same(e.scr, "scr") && !snap_same(e.txt, "txt")
                   && file_exists(e.r30) && file_exists(e.who));
            } else {
                expect_unchanged(&e);
            }
            expect_no_temps(&e, NULL);
        }
    }
}

static void case_i23(void)
{
    Env e;
    char *six;
    char *zero;
    size_t sl;
    size_t zl;
    int i;

    env_live(&e, "i23", T_LAST);
    /* The two tables exist too, so "nothing is written" covers all five files
     * and not just the two an idle run might plausibly leave alone. */
    write_rows_file(e.r30, I26_R30, 6);
    write_rows_file(e.who, I26_WHO, 1);
    env_snapshot(&e);
    set_clock(T_SAMEWK);

    for (i = 0; i < 5; i++) {
        eq_long(__FILE__, __LINE__, lbl("an idle run reports success", i),
                rr(&e, RANK_MODE_WEEKLY), 0);
        OK(lbl("the mode it ran under is on screen", i), cap_has("weekly"));
        OK(lbl("and the other mode is not", i), !cap_has("daily"));
        OK(lbl("the output is ASCII", i), cap_ascii_only());
        OK(lbl("nothing was announced as discarded", i),
           !cap_has_any_keep_name());
        expect_unchanged(&e);
        OK(lbl("no temporary was created", i),
           !file_exists(e.tmp30) && !file_exists(e.tmpwho)
           && !file_exists(e.tmpscr) && !file_exists(e.tmptxt));
        OK(lbl("and no retained copy was created", i),
           !file_exists(e.old30) && !file_exists(e.oldwho)
           && !file_exists(e.oldscr) && !file_exists(e.oldtxt));
    }
    OK("the elapsed day count is on screen", cap_has("6"));
    six = read_file(g_cap_path, &sl);

    /* A-5 carries the precise day-count oracle; here the point is only that the
     * count is really in the message, which a differential shows: two runs that
     * differ only in how long ago the reset was must not print the same line. */
    env_live(&e, "i23-zero", "2026-10-25T00:00:00");
    set_clock(T_SAMEWK);
    EQL("the same-day idle run reports success", rr(&e, RANK_MODE_WEEKLY), 0);
    OK("and prints a zero", cap_has("0"));
    zero = read_file(g_cap_path, &zl);

    if (six != NULL && zero != NULL) {
        OK("six days ago and zero days ago do not print the same line",
           !(sl == zl && memcmp(six, zero, sl) == 0));
    }
    free(six);
    free(zero);
    expect_no_temps(&e, NULL);
}

static void case_i24(void)
{
    Env e;

    /* Three weeks and three Mondays between the two instants. */
    env_live(&e, "i24", "2026-09-28T00:00:00");
    set_clock("2026-10-19T12:00:00");
    EQL("three weeks of absence resets", rr(&e, RANK_MODE_WEEKLY), 0);
    expect_rows_file(e.r30, I26_R30, 6, "RANK30.TXT");
    expect_rows_file(e.who, I26_WHO, 1, "RANKWHO.TXT");
    expect_rows_file(e.scr, G8_WANT, RANK_ROWS, "NX93.SCR");
    /* Once, not three times, and dated now rather than at the boundary it
     * crossed first. */
    expect_state(e.txt, "2026-10-19T12:00:00", "NX93.TXT holds the run's instant");
    expect_no_temps(&e, NULL);

    env_snapshot(&e);
    EQL("and the next run has nothing left to do", rr(&e, RANK_MODE_WEEKLY), 0);
    expect_unchanged(&e);
    expect_no_temps(&e, NULL);
}

/* The mode is not recorded anywhere, so switching it has to work in either
 * direction with no migration of its own. */
static void case_i25(void)
{
    Env e;

    env_live(&e, "i25-dw", T_LAST);
    set_clock("2026-10-20T09:00:00");
    EQL("a daily run on the Tuesday resets", rr(&e, RANK_MODE_DAILY), 0);
    expect_state(e.txt, "2026-10-20T09:00:00", "NX93.TXT after the daily reset");

    env_snapshot(&e);
    set_clock("2026-10-21T09:00:00");
    EQL("a weekly run on the Wednesday does nothing", rr(&e, RANK_MODE_WEEKLY), 0);
    expect_unchanged(&e);

    set_clock(T_NEXTWK);
    EQL("and a weekly run on the next Monday resets", rr(&e, RANK_MODE_WEEKLY), 0);
    expect_state(e.txt, T_NEXTWK, "NX93.TXT after the weekly reset");
    expect_no_temps(&e, NULL);

    env_live(&e, "i25-wd", T_LAST);
    set_clock(T_NEXTWK);
    EQL("a weekly run resets", rr(&e, RANK_MODE_WEEKLY), 0);
    env_snapshot(&e);
    set_clock("2026-10-26T23:00:00");
    EQL("a daily run the same evening does nothing", rr(&e, RANK_MODE_DAILY), 0);
    expect_unchanged(&e);
    set_clock("2026-10-27T01:00:00");
    EQL("and a daily run after midnight resets", rr(&e, RANK_MODE_DAILY), 0);
    expect_state(e.txt, "2026-10-27T01:00:00", "NX93.TXT after the daily reset");
    expect_no_temps(&e, NULL);
}

/* ------------------------------------------------------------- I-33, I-34 */

/* The thirteen states NX93.TXT can be in, and what each one means for a run
 * that would otherwise reset.  The order the conditions are tested in is part
 * of the contract, not an implementation detail: the whole reason the canonical
 * check is exact is that reading a damaged ledger as a state file discards every
 * record in it (E10). */
static void case_i33(void)
{
    static const struct {
        const char *name;
        const char *clock;
        int rc;
        int resets;
    } tab[13] = {
        { "a: absent",                          T_NEXTWK,         0, 1 },
        { "b: canonical, no crossing",          T_NEXTWK,         0, 0 },
        { "c: canonical, crossing",             T_NEXTWK,         0, 1 },
        { "d: old ledger, no crossing",         T_LEDGER_SAMEDAY, 0, 0 },
        { "e: old ledger, crossing",            T_NEXTWK,         0, 1 },
        { "f: old ledger with NX93OLD.TXT",     T_NEXTWK,         1, 0 },
        { "g: an unreadable old ledger",        T_NEXTWK,         1, 0 },
        { "h: non-canonical, no crossing",      T_NEXTWK,         0, 0 },
        { "i: non-canonical, crossing",         T_NEXTWK,         1, 0 },
        { "j: lastUpdate cannot be read",       T_NEXTWK,         1, 0 },
        { "k: lastUpdate is in the future",     T_NEXTWK,         1, 0 },
        { "l: absent, with NX93OLD.TXT",        T_NEXTWK,         0, 1 },
        { "m: canonical, with NX93OLD.TXT",     T_NEXTWK,         0, 1 }
    };
    static const char *const planted = "an earlier retirement, left in place\r\n";
    int i;

    for (i = 0; i < 13; i++) {
        Env e;
        char tag[64];

        snprintf(tag, sizeof tag, "i33-%c", 'a' + i);
        env_make(&e, tag);
        write_bytes(e.scr, fix_scr13, FIX_SCR13_LEN);

        switch (i) {
        case 0:  break;                                     /* no NX93.TXT */
        case 1:  env_state(&e, "2026-10-26T01:00:00"); break;
        case 2:  env_state(&e, T_LAST); break;
        case 3:  write_bytes(e.txt, fix_ledger_lf, FIX_LEDGER_LF_LEN); break;
        case 4:  write_bytes(e.txt, fix_ledger_lf, FIX_LEDGER_LF_LEN); break;
        case 5:  write_bytes(e.txt, fix_ledger_lf, FIX_LEDGER_LF_LEN);
                 write_text(e.oldledger, planted);
                 break;
        case 6:  write_text(e.txt, LEDGER_BAD[0].text); break;
        case 7:  write_text(e.txt,
                     "{\"lastUpdate\": \"2026-10-26T01:00:00\"}\n");
                 break;
        case 8:  write_text(e.txt, "{\"lastUpdate\": \"" T_LAST "\"}\n"); break;
        case 9:  write_text(e.txt, "{\"nothing\": \"here\"}\n"); break;
        case 10: env_state(&e, "2027-01-01T00:00:00"); break;
        case 11: write_text(e.oldledger, planted); break;
        default: env_state(&e, T_LAST);
                 write_text(e.oldledger, planted);
                 break;
        }

        env_snapshot(&e);
        set_clock(tab[i].clock);
        eq_long(__FILE__, __LINE__, lbl_s("exit code for", tab[i].name),
                rr(&e, RANK_MODE_DAILY), tab[i].rc);

        if (tab[i].resets) {
            OK(lbl_s("the table was rebuilt for", tab[i].name),
               !snap_same(e.scr, "scr"));
            OK(lbl_s("both new tables exist for", tab[i].name),
               file_exists(e.r30) && file_exists(e.who));
            expect_state(e.txt, tab[i].clock, lbl_s("NX93.TXT for", tab[i].name));
        } else {
            expect_unchanged(&e);
        }

        /* Migration fires only on a crossing run whose NX93.TXT is a ledger. */
        if (i == 0) {
            OK("no ledger means no retirement", !file_exists(e.oldledger));
        } else if (i == 4) {
            expect_file_bytes(e.oldledger, fix_ledger_lf, FIX_LEDGER_LF_LEN,
                              "NX93OLD.TXT is the ledger that was retired");
        } else if (i == 5 || i == 11 || i == 12) {
            expect_file_bytes(e.oldledger, planted, strlen(planted),
                              lbl_s("NX93OLD.TXT untouched for", tab[i].name));
        }
        expect_no_temps(&e, NULL);
    }
}

/* The same bytes on either side of a calendar boundary, because the contract is
 * that the conclusion differs: a file that is not canonical is only a problem
 * once the run actually wants to replace it. */
static void case_i34(void)
{
    static const int pick[7] = { 0, 1, 2, 3, 4, 6, 10 };
    char buf[800];
    int k;

    for (k = 0; k < 7; k++) {
        int v = pick[k];
        Env e;
        char tag[64];
        size_t n;

        /* Crossing: the run wants to replace this file and must refuse to,
         * rather than read it as a state file and throw the records away. */
        snprintf(tag, sizeof tag, "i34-%d-cross", v);
        env_make(&e, tag);
        write_bytes(e.scr, fix_scr13, FIX_SCR13_LEN);
        n = state_variant(v, T_LAST, buf, sizeof buf);
        write_bytes(e.txt, buf, n);
        env_snapshot(&e);
        set_clock(T_NEXTWK);
        eq_long(__FILE__, __LINE__,
                lbl_s("a crossing run refuses", STATE_VARIANT_NAME[v]),
                rr(&e, RANK_MODE_DAILY), 1);
        expect_unchanged(&e);
        expect_no_temps(&e, NULL);

        /* Variant 4 has lost the quote that closes its timestamp, so lastUpdate
         * cannot be read at all and the run stops before any crossing is
         * decided.  Its "no crossing gives 0" half is not assertable; see the
         * note on STATE_VARIANT_UNREADABLE. */
        if (v == STATE_VARIANT_UNREADABLE) {
            continue;
        }

        snprintf(tag, sizeof tag, "i34-%d-idle", v);
        env_make(&e, tag);
        write_bytes(e.scr, fix_scr13, FIX_SCR13_LEN);
        n = state_variant(v, "2026-10-26T01:00:00", buf, sizeof buf);
        write_bytes(e.txt, buf, n);
        env_snapshot(&e);
        set_clock(T_NEXTWK);
        eq_long(__FILE__, __LINE__,
                lbl_s("an idle run accepts", STATE_VARIANT_NAME[v]),
                rr(&e, RANK_MODE_DAILY), 0);
        expect_unchanged(&e);
        expect_no_temps(&e, NULL);
    }
}

/* ------------------------------------------------------------------- I-47 */

/* The longest of the new names is eleven bytes (RANKWHO.TXT and NX93OLD.TXT),
 * and rank_join needs room for the directory, a separator, the name and a
 * terminator.  A directory name of a single component is used so the boundary
 * can be hit exactly without depending on where the scratch directory sits. */
static void case_i47(void)
{
    char cwd[TPATH];
    char name[RANK_PATH_MAX + 32];
    Env e;
    int i;
    int len;

    EQL("a directory name far past the buffer is refused",
        rr_dir("xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx"
               "xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx"
               "xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx",
               RANK_MODE_DAILY), 1);

    if (getcwd(cwd, sizeof cwd) == NULL) {
        die("getcwd");
    }

    /* 115 + 1 + 11 + 1 == 128, so the longest name just fits; 116 does not. */
    for (len = 115; len <= 116; len++) {
        env_make(&e, lbl("i47", len));
        for (i = 0; i < len; i++) {
            name[i] = 'd';
        }
        name[len] = '\0';
        {
            char nested[TPATH];
            snprintf(nested, sizeof nested, "%s/%s", e.dir, name);
            if (mkdir(nested, 0755) != 0) {
                die(nested);
            }
            {
                char scr[TPATH];
                char txt[TPATH];
                snprintf(scr, sizeof scr, "%s/NX93.SCR", nested);
                snprintf(txt, sizeof txt, "%s/NX93.TXT", nested);
                write_bytes(scr, fix_scr13, FIX_SCR13_LEN);
                snap(scr, "deep-scr");
                {
                    char st[80];
                    size_t n = canon_state(st, sizeof st, T_LAST);
                    write_bytes(txt, st, n);
                }
                snap(txt, "deep-txt");

                if (chdir(e.dir) != 0) {
                    die(e.dir);
                }
                if (len == 115) {
                    eq_long(__FILE__, __LINE__,
                            "the longest name still fits a 115 byte directory",
                            rr_dir(name, RANK_MODE_DAILY), 0);
                } else {
                    eq_long(__FILE__, __LINE__,
                            "one byte more and the run refuses",
                            rr_dir(name, RANK_MODE_DAILY), 1);
                }
                if (chdir(cwd) != 0) {
                    die(cwd);
                }
                if (len == 116) {
                    OK("nothing was written in the directory it refused",
                       snap_same(scr, "deep-scr") && snap_same(txt, "deep-txt"));
                }
            }
        }
    }

    /* The DOS main calls rank_run with no directory at all, and both spellings
     * of "no directory" have to behave the same way. */
    for (i = 0; i < 2; i++) {
        env_live(&e, lbl("i47-bare", i), T_LAST);
        set_clock(T_NEXTWK);
        if (chdir(e.dir) != 0) {
            die(e.dir);
        }
        eq_long(__FILE__, __LINE__,
                lbl(i == 0 ? "rank_run(NULL)" : "rank_run(\"\")", i),
                rr_dir(i == 0 ? NULL : "", RANK_MODE_DAILY), 0);
        if (chdir(cwd) != 0) {
            die(cwd);
        }
        expect_rows_file(e.scr, G8_WANT, RANK_ROWS, lbl("bare NX93.SCR", i));
        expect_no_temps(&e, NULL);
    }
}

/* ------------------------------------------------------------------ X-2, X-3 */

/* A PC-9801 with a flat RTC battery comes up in 1980; a clock nudged back a few
 * hours within one day is the subtler version of the same thing, and a date-only
 * comparison sails straight past it. */
static void case_x2(void)
{
    static const char *const when[4] = {
        "2026-10-20T11:59:59",      /* one second */
        "2026-10-20T00:00:00",      /* the same day, midnight */
        "2026-10-19T12:00:00",      /* the day before */
        "2025-10-20T12:00:00"       /* a year before */
    };
    int i;
    int m;

    for (i = 0; i < 4; i++) {
        for (m = 0; m < 2; m++) {
            int mode = (m == 0) ? RANK_MODE_WEEKLY : RANK_MODE_DAILY;
            Env e;

            env_live(&e, lbl("x2", (long)i * 10L + m), "2026-10-20T12:00:00");
            write_rows_file(e.r30, I26_R30, 6);
            write_rows_file(e.who, I26_WHO, 1);
            env_snapshot(&e);
            set_clock(when[i]);

            eq_long(__FILE__, __LINE__,
                    lbl("a clock that moved backwards stops the run",
                        (long)i * 10L + m),
                    rr(&e, mode), 1);
            expect_unchanged(&e);
            expect_no_temps(&e, NULL);
        }
    }
}

/* Which condition is reported matters, because the advice differs: E5 says to
 * set the clock, E10 says the file is not one this program wrote, and E4 says it
 * is damaged.  Deciding the crossing first would report the wrong one. */
static void case_x3(void)
{
    Env e;
    char *future;
    char *e10;
    size_t fl;
    size_t el;

    /* 1. A future lastUpdate in a file that is neither canonical nor a ledger. */
    env_make(&e, "x3-future-odd");
    write_bytes(e.scr, fix_scr13, FIX_SCR13_LEN);
    write_text(e.txt, "{\"lastUpdate\": \"2027-01-01T00:00:00\"}\n");
    env_snapshot(&e);
    set_clock(T_NEXTWK);
    EQL("the future clock is reported, not the odd file", rr(&e, RANK_MODE_DAILY), 1);
    expect_unchanged(&e);
    expect_no_temps(&e, NULL);
    future = read_file(g_cap_path, &fl);

    env_make(&e, "x3-e10");
    write_bytes(e.scr, fix_scr13, FIX_SCR13_LEN);
    write_text(e.txt, "{\"lastUpdate\": \"" T_LAST "\"}\n");
    env_snapshot(&e);
    set_clock(T_NEXTWK);
    EQL("the same file with a past lastUpdate is refused too",
        rr(&e, RANK_MODE_DAILY), 1);
    expect_unchanged(&e);
    e10 = read_file(g_cap_path, &el);
    if (future != NULL && e10 != NULL) {
        OK("and the two are not reported with the same words",
           !(fl == el && memcmp(future, e10, fl) == 0));
    }
    free(future);
    free(e10);

    /* 2. A future lastUpdate in a real ledger: migration must not start, so no
     * retirement happens either. */
    env_make(&e, "x3-future-ledger");
    write_bytes(e.scr, fix_scr13, FIX_SCR13_LEN);
    {
        static char buf[1024];
        size_t n = FIX_LEDGER_LF_LEN;
        memcpy(buf, fix_ledger_lf, n);
        /* The LF ledger opens with '{' LF two spaces, so its timestamp starts at
         * offset 19 - one earlier than the canonical state file's, which opens
         * with CRLF.  Rewrite the year in place and leave everything else. */
        memcpy(buf + 19, "2027", 4u);
        write_bytes(e.txt, buf, n);
    }
    env_snapshot(&e);
    set_clock(T_NEXTWK);
    EQL("a ledger dated in the future stops the run", rr(&e, RANK_MODE_DAILY), 1);
    expect_unchanged(&e);
    OK("and nothing was retired", !file_exists(e.oldledger));
    expect_no_temps(&e, NULL);

    /* 3. A ledger whose lastUpdate cannot be read: damaged, and again no
     * migration. */
    env_make(&e, "x3-unreadable-ledger");
    write_bytes(e.scr, fix_scr13, FIX_SCR13_LEN);
    {
        static char buf[1024];
        size_t n = FIX_LEDGER_LF_LEN;
        memcpy(buf, fix_ledger_lf, n);
        memcpy(buf + 19, "20X6", 4u);
        write_bytes(e.txt, buf, n);
    }
    env_snapshot(&e);
    set_clock(T_NEXTWK);
    EQL("a ledger with an unreadable lastUpdate stops the run",
        rr(&e, RANK_MODE_DAILY), 1);
    expect_unchanged(&e);
    OK("and nothing was retired", !file_exists(e.oldledger));
    expect_no_temps(&e, NULL);
}

/* --------------------------------------------------------- I-26 .. I-36, I-46 */

/* I-36: the six records, with a hand-placed shipped row behind them.  The run
 * must neither remove it nor add a second copy. */
static const char *const I36_R30[7] = {
    "327400 30 01 OMZN     26/09/15 A",
    "315970 30 02 omzn     26/10/13 A",
    "300820 30 01 omzn     26/10/08 A",
    "300280 30 04 omzn     26/10/13 A",
    "299370 30 00 OMZN@@@@ 61/11/17 A",
    "286530 30 01 omzn     26/10/09 A",
    "200000 29 00 Bio_100% 93/10/01 A"
};

static void case_i26(void)
{
    Env e;
    int i;
    int j;

    env_live(&e, "i26", T_LAST);
    set_clock(T_NEXTWK);

    EQL("the weekly reset runs", rr(&e, RANK_MODE_WEEKLY), 0);
    expect_rows_file(e.r30, I26_R30, 6, "RANK30.TXT is the six records");
    EQL("204 bytes of all-time table", file_size(e.r30), 204L);
    expect_rows_file(e.who, I26_WHO, 1, "RANKWHO.TXT is one player");
    EQL("34 bytes of per-user table", file_size(e.who), 34L);
    expect_rows_file(e.scr, G8_WANT, RANK_ROWS, "NX93.SCR is the rebuilt table");
    EQL("340 bytes of table", file_size(e.scr), 340L);
    expect_state(e.txt, T_NEXTWK, "NX93.TXT is canonical");
    EQL("45 bytes of state", file_size(e.txt), 45L);
    OK("no ledger was retired, because there was none", !file_exists(e.oldledger));
    OK("nothing was announced as discarded", !cap_has_any_keep_name());
    expect_no_temps(&e, NULL);

    /* The filler rows are not records, so neither new file may hold one. */
    {
        static RankRow rows[RANKWHO_ROWS + 2];
        int n = rows_in_file(e.r30, rows, RANK30_ROWS);
        for (i = 0; i < n; i++) {
            eq_long(__FILE__, __LINE__, lbl("no shipped row in RANK30.TXT", i),
                    rank_is_default_line(rows[i].raw), -1);
        }
        n = rows_in_file(e.who, rows, RANKWHO_ROWS);
        for (j = 0; j < n; j++) {
            eq_long(__FILE__, __LINE__, lbl("no shipped row in RANKWHO.TXT", j),
                    rank_is_default_line(rows[j].raw), -1);
        }
    }
}

static void case_i27(void)
{
    static const char *const week[3] = {
        T_NEXTWK, "2026-11-02T02:00:00", "2026-11-09T02:00:00"
    };
    Env e;
    char *r30 = NULL;
    char *who = NULL;
    char *scr = NULL;
    char *txt = NULL;
    size_t r30l = 0;
    size_t whol = 0;
    size_t scrl = 0;
    size_t txtl = 0;
    int k;

    env_live(&e, "i27", T_LAST);

    for (k = 0; k < 3; k++) {
        set_clock(week[k]);
        eq_long(__FILE__, __LINE__, lbl("weekly reset", k),
                rr(&e, RANK_MODE_WEEKLY), 0);
        expect_state(e.txt, week[k], lbl("NX93.TXT after reset", k));
        expect_no_temps(&e, NULL);

        if (k == 0) {
            r30 = read_file(e.r30, &r30l);
            who = read_file(e.who, &whol);
            scr = read_file(e.scr, &scrl);
            txt = read_file(e.txt, &txtl);
            OK("the first reset produced all four files",
               r30 != NULL && who != NULL && scr != NULL && txt != NULL);
        } else {
            if (r30 != NULL) {
                expect_file_bytes(e.r30, r30, r30l,
                                  lbl("RANK30.TXT unchanged by reset", k));
            }
            if (who != NULL) {
                expect_file_bytes(e.who, who, whol,
                                  lbl("RANKWHO.TXT unchanged by reset", k));
            }
            if (scr != NULL) {
                expect_file_bytes(e.scr, scr, scrl,
                                  lbl("NX93.SCR unchanged by reset", k));
            }
            if (txt != NULL) {
                OK(lbl("NX93.TXT differs only in its timestamp", k),
                   state_same_but_time(e.txt, txt, txtl));
            }
        }
    }
    free(r30);
    free(who);
    free(scr);
    free(txt);
}

static void case_i28(void)
{
    Env e;

    env_make(&e, "i28");
    write_bytes(e.scr, fix_scr13, FIX_SCR13_LEN);
    set_clock(T_NEXTWK);

    EQL("a first run with no state file resets", rr(&e, RANK_MODE_DAILY), 0);
    OK("NX93.TXT was created", file_exists(e.txt));
    expect_state(e.txt, T_NEXTWK, "and it is canonical");
    OK("RANK30.TXT was created", file_exists(e.r30));
    OK("RANKWHO.TXT was created", file_exists(e.who));
    expect_rows_file(e.r30, I26_R30, 6, "RANK30.TXT");
    expect_rows_file(e.who, I26_WHO, 1, "RANKWHO.TXT");
    expect_rows_file(e.scr, G8_WANT, RANK_ROWS, "NX93.SCR");
    expect_no_temps(&e, NULL);
}

static void case_i29(void)
{
    Env e;

    env_make(&e, "i29");
    env_state(&e, T_LAST);
    set_clock(T_NEXTWK);

    EQL("a missing table is not an error", rr(&e, RANK_MODE_DAILY), 0);
    expect_file_bytes(e.scr, fix_def, FIX_DEF_LEN,
                      "NX93.SCR is the shipped table");
    OK("RANK30.TXT exists", file_exists(e.r30));
    EQL("and it is empty rather than absent", file_size(e.r30), 0L);
    OK("RANKWHO.TXT exists", file_exists(e.who));
    EQL("and it is empty rather than absent", file_size(e.who), 0L);
    expect_state(e.txt, T_NEXTWK, "NX93.TXT");
    expect_no_temps(&e, NULL);
}

/* The hundred and first player.  The run completes, because the exit code
 * vocabulary stays {0,1} and a full table is not a failure, but the row that
 * loses its seat is put on screen rather than quietly dropped. */
static void case_i30(void)
{
    static const struct { const char *what; const char *row; int drops_self; }
    tab[3] = {
        { "a newcomer in the middle", "495000 30 01 ZZZ      26/09/15 A", 0 },
        { "a newcomer below the floor", "1 30 01 ZZZ      26/09/15 A", 1 },
        { "a newcomer tying the floor", "490100 30 09 ZZZ      26/10/01 A", 1 }
    };
    static RankRow rows[RANKWHO_ROWS + 2];
    int k;

    for (k = 0; k < 3; k++) {
        Env e;
        const char *const *hundred;
        const char *scr[1];
        char last_row[RANK_MAX_LINE + 1];
        const char *dropped;
        int n;
        int i;
        int found = 0;
        int in_r30 = 0;

        env_make(&e, lbl("i30", k));
        hundred = gen_who100();
        strcpy(last_row, hundred[RANKWHO_ROWS - 1]);
        write_rows_file(e.who, hundred, RANKWHO_ROWS);
        /* The floor row is put in the all-time table first, so that "the row
         * that lost its seat can still be read afterwards" is observable at
         * all: RANKWHO's own rows are not inputs to RANK30. */
        {
            const char *one[1];
            one[0] = last_row;
            write_rows_file(e.r30, one, 1);
        }
        scr[0] = tab[k].row;
        write_rows_file(e.scr, scr, 1);
        env_state(&e, T_LAST);
        set_clock(T_NEXTWK);

        eq_long(__FILE__, __LINE__, lbl_s("the run completes for", tab[k].what),
                rr(&e, RANK_MODE_DAILY), 0);

        dropped = tab[k].drops_self ? tab[k].row : last_row;
        eq_long(__FILE__, __LINE__,
                lbl_s("the row that lost its seat is on screen for", tab[k].what),
                cap_count(dropped), 1);
        OK(lbl_s("and the output is ASCII for", tab[k].what), cap_ascii_only());

        n = rows_in_file(e.who, rows, RANKWHO_ROWS);
        eq_long(__FILE__, __LINE__,
                lbl_s("the table is exactly full for", tab[k].what),
                n, (long)RANKWHO_ROWS);
        for (i = 0; i < n; i++) {
            if (strcmp(rows[i].raw, dropped) == 0) {
                found = 1;
            }
        }
        eq_long(__FILE__, __LINE__,
                lbl_s("and the dropped row is not in it for", tab[k].what),
                found, 0);
        {
            RankWhoTable t;
            char *bytes;
            size_t len;
            bytes = read_file(e.who, &len);
            if (bytes != NULL) {
                if (who_load_bytes(bytes, len, &t) >= 0) {
                    OK(lbl_s("one row per key for", tab[k].what),
                       who_keys_distinct(&t));
                }
                free(bytes);
            }
        }

        n = rows_in_file(e.r30, rows, RANK30_ROWS);
        for (i = 0; i < n; i++) {
            if (strcmp(rows[i].raw, dropped) == 0) {
                in_r30 = 1;
            }
        }
        eq_long(__FILE__, __LINE__,
                lbl_s("the dropped row is still in the all-time table for",
                      tab[k].what), in_r30, 1);
        expect_no_temps(&e, NULL);
    }
}

/* The reported row goes out as bytes, never as a format string.  Bio_100% is
 * already in the shipped table; a player who types this name would otherwise
 * take the program with them. */
static void case_i31(void)
{
    Env e;
    const char *const *hundred;
    const char *scr[1];
    static const char *const nasty = "1 30 01 %n%s%%%% 26/09/15 A";

    env_make(&e, "i31");
    hundred = gen_who100();
    write_rows_file(e.who, hundred, RANKWHO_ROWS);
    scr[0] = nasty;
    write_rows_file(e.scr, scr, 1);
    env_state(&e, T_LAST);
    set_clock(T_NEXTWK);

    EQL("the run completes", rr(&e, RANK_MODE_DAILY), 0);
    EQL("and the name appears exactly as typed", cap_count("%n%s%%%%"), 1);
    OK("the output is ASCII", cap_ascii_only());
    expect_no_temps(&e, NULL);
}

/* Four files can be read only now, not two.  A file that refuses "r+b" cannot be
 * replaced on DOS either, so the run has to stop before it has written anything
 * rather than halfway through. */
static void case_i32(void)
{
    static const char *const what[5] = {
        "RANK30.TXT", "RANKWHO.TXT", "NX93.SCR", "NX93.TXT", "the directory"
    };
    int k;

    for (k = 0; k < 5; k++) {
        Env e;
        const char *target;
        int rc;

        env_live(&e, lbl("i32", k), T_LAST);
        set_clock(T_NEXTWK);
        if (rr(&e, RANK_MODE_DAILY) != 0) {
            OK(lbl_s("the baseline reset succeeded before", what[k]), 0);
            continue;
        }
        /* The next crossing has all four files in front of it. */
        set_clock("2026-11-02T02:00:00");
        env_snapshot(&e);

        switch (k) {
        case 0: target = e.r30; break;
        case 1: target = e.who; break;
        case 2: target = e.scr; break;
        case 3: target = e.txt; break;
        default: target = e.dir; break;
        }
        if (chmod(target, (k == 4) ? 0555 : 0444) != 0) {
            die(target);
        }
        rc = rr(&e, RANK_MODE_DAILY);
        if (chmod(target, (k == 4) ? 0755 : 0644) != 0) {
            die(target);
        }

        eq_long(__FILE__, __LINE__, lbl_s("a read-only", what[k]), rc, 1);
        expect_unchanged(&e);
        expect_no_temps(&e, NULL);
    }
}

static void case_i35(void)
{
    Env e;

    env_make(&e, "i35");
    write_bytes(e.scr, fix_def, FIX_DEF_LEN);
    env_state(&e, T_LAST);
    set_clock(T_NEXTWK);

    EQL("a table of nothing but filler resets cleanly",
        rr(&e, RANK_MODE_DAILY), 0);
    EQL("and the all-time table stays empty", file_size(e.r30), 0L);
    EQL("as does the per-user table", file_size(e.who), 0L);
    expect_file_bytes(e.scr, fix_def, FIX_DEF_LEN, "NX93.SCR is unchanged in content");
    expect_no_temps(&e, NULL);
}

/* A filler row somebody put in RANK30.TXT by hand is theirs to keep: the rule is
 * that the program never adds one, not that it polices the file. */
static void case_i36(void)
{
    Env e;
    static RankRow rows[RANK30_ROWS + 2];
    const char *one[1];
    int n;
    int i;
    int count = 0;

    env_make(&e, "i36");
    write_bytes(e.scr, fix_scr13, FIX_SCR13_LEN);
    one[0] = "200000 29 00 Bio_100% 93/10/01 A";
    write_rows_file(e.r30, one, 1);
    env_state(&e, T_LAST);
    set_clock(T_NEXTWK);

    EQL("the reset runs", rr(&e, RANK_MODE_DAILY), 0);
    expect_rows_file(e.r30, I36_R30, 7, "RANK30.TXT keeps the hand-placed row");

    n = rows_in_file(e.r30, rows, RANK30_ROWS);
    for (i = 0; i < n; i++) {
        if (strcmp(rows[i].raw, one[0]) == 0) {
            count++;
        }
    }
    EQL("it is there once: neither removed nor duplicated", count, 1);

    n = rows_in_file(e.who, rows, RANKWHO_ROWS);
    for (i = 0; i < n; i++) {
        eq_long(__FILE__, __LINE__, lbl("and not in the per-user table", i),
                strcmp(rows[i].raw, one[0]) != 0 ? 1 : 0, 1);
    }
    expect_no_temps(&e, NULL);
}

/* Debris from a run that was killed between two renames.  The commit frees the
 * retained name before it needs it, so the next run has to sail past whatever is
 * sitting there. */
static void case_i46(void)
{
    static const char *const stale = "left behind by an interrupted run\r\n";
    int variant;

    for (variant = 0; variant < 2; variant++) {
        Env e;
        const char *keeps[4];
        const char *temps[4];
        int i;
        int reports = 0;

        env_live(&e, lbl("i46", variant), T_LAST);
        set_clock(T_NEXTWK);
        if (rr(&e, RANK_MODE_DAILY) != 0) {
            OK(lbl("the baseline reset succeeded", variant), 0);
            continue;
        }
        set_clock("2026-11-02T02:00:00");

        temps[0] = e.tmp30;
        temps[1] = e.tmpwho;
        temps[2] = e.tmpscr;
        temps[3] = e.tmptxt;
        keeps[0] = e.old30;
        keeps[1] = e.oldwho;
        keeps[2] = e.oldscr;
        keeps[3] = e.oldtxt;

        /* The temporaries are always ordinary files: they are re-created with
         * "wb", which cannot open a directory, so occupying those four names
         * with directories would make the run fail for a reason that has
         * nothing to do with the debris being cleared.  The retained names are
         * the ones where the missing remove() is observable, and variant 1 puts
         * directories there - rename() will not overwrite one on either
         * system, which is what makes a dropped remove() visible on a host
         * whose rename() otherwise replaces silently. */
        for (i = 0; i < 4; i++) {
            write_text(temps[i], stale);
        }
        for (i = 0; i < 4; i++) {
            if (variant == 0) {
                write_text(keeps[i], stale);
            } else if (mkdir(keeps[i], 0755) != 0) {
                die(keeps[i]);
            }
        }
        for (i = 0; i < 4; i++) {
            OK(lbl("the debris is really in place", variant * 10 + i),
               file_exists(temps[i]) && file_exists(keeps[i]));
        }

        eq_long(__FILE__, __LINE__, lbl("the debris does not block the run",
                                        variant),
                rr(&e, RANK_MODE_DAILY), 0);

        if (variant == 0) {
            /* Four retained copies really are discarded here, so there are four
             * reports: one per name, each naming its own.  The temporaries are
             * only truncated, so they are not reported - four, not eight. */
            for (i = 0; i < 4; i++) {
                int n = cap_count(KEEP_NAMES[i]);
                reports += n;
                eq_long(__FILE__, __LINE__,
                        lbl_s("reported once:", KEEP_NAMES[i]), n, 1);
            }
            EQL("four reports, one per retained copy, not eight", reports, 4);
            OK("the warning says the copy goes away", cap_has_any(W2_GONE, 7));
            OK("and that it could be kept first", cap_has_any(W2_SAVE, 5));
            OK("in ASCII", cap_ascii_only());
        }
        expect_rows_file(e.scr, G8_WANT, RANK_ROWS, lbl("NX93.SCR", variant));
        expect_no_temps(&e, NULL);
    }
}

/* The crosscut, as a case of its own.  Every integration case above and below
 * ends in expect_no_temps; this one states what that helper covers, and pins the
 * one name deliberately left out of it: NX93OLD.TXT is migration's own product
 * and belongs to the snapshot set, not to the scaffolding. */
static void case_i48(void)
{
    Env e;

    env_live(&e, "i48-plain", T_LAST);
    set_clock(T_NEXTWK);
    EQL("a plain reset runs", rr(&e, RANK_MODE_DAILY), 0);
    OK("RANKTMP.30 is gone",  !file_exists(e.tmp30));
    OK("RANKTMP.WHO is gone", !file_exists(e.tmpwho));
    OK("RANKTMP.SCR is gone", !file_exists(e.tmpscr));
    OK("RANKTMP.TXT is gone", !file_exists(e.tmptxt));
    OK("RANKOLD.30 is gone",  !file_exists(e.old30));
    OK("RANKOLD.WHO is gone", !file_exists(e.oldwho));
    OK("RANKOLD.SCR is gone", !file_exists(e.oldscr));
    OK("RANKOLD.TXT is gone", !file_exists(e.oldtxt));
    OK("and the four files that outlive a run are all there",
       file_exists(e.r30) && file_exists(e.who)
       && file_exists(e.scr) && file_exists(e.txt));
    expect_no_temps(&e, NULL);

    env_make(&e, "i48-migrate");
    write_bytes(e.scr, fix_scr13, FIX_SCR13_LEN);
    write_bytes(e.txt, fix_ledger_lf, FIX_LEDGER_LF_LEN);
    set_clock(T_NEXTWK);
    EQL("a migrating reset runs", rr(&e, RANK_MODE_DAILY), 0);
    expect_no_temps(&e, NULL);
    OK("but NX93OLD.TXT survives, because it is not scaffolding",
       file_exists(e.oldledger));
}

/* --------------------------------------------------------------- 移行 (I-37..) */

static void case_i37(void)
{
    static const char *const variant[2] = {
        "the LF ledger", "the CRLF ledger"
    };
    const char *data[2];
    size_t len[2];
    char *r30[2];
    char *who[2];
    char *scr[2];
    size_t r30l[2];
    size_t whol[2];
    size_t scrl[2];
    int k;

    data[0] = fix_ledger_lf;   len[0] = FIX_LEDGER_LF_LEN;
    data[1] = fix_ledger_crlf; len[1] = FIX_LEDGER_CRLF_LEN;

    for (k = 0; k < 2; k++) {
        Env e;

        env_make(&e, lbl("i37", k));
        write_bytes(e.scr, fix_scr13, FIX_SCR13_LEN);
        write_bytes(e.txt, data[k], len[k]);
        set_clock(T_NEXTWK);

        eq_long(__FILE__, __LINE__, lbl_s("migration runs for", variant[k]),
                rr(&e, RANK_MODE_DAILY), 0);

        expect_rows_file(e.r30, I37_R30, 14,
                         lbl_s("RANK30.TXT for", variant[k]));
        eq_long(__FILE__, __LINE__,
                lbl_s("476 bytes of all-time table for", variant[k]),
                file_size(e.r30), 476L);
        expect_rows_file(e.who, I26_WHO, 1,
                         lbl_s("RANKWHO.TXT for", variant[k]));
        eq_long(__FILE__, __LINE__,
                lbl_s("34 bytes of per-user table for", variant[k]),
                file_size(e.who), 34L);
        expect_rows_file(e.scr, G8_WANT, RANK_ROWS,
                         lbl_s("NX93.SCR for", variant[k]));
        expect_state(e.txt, T_NEXTWK, lbl_s("NX93.TXT for", variant[k]));
        expect_file_bytes(e.oldledger, data[k], len[k],
                          lbl_s("NX93OLD.TXT is the ledger, byte for byte, for",
                                variant[k]));
        OK(lbl_s("the retirement is on screen for", variant[k]),
           cap_has("NX93OLD.TXT"));
        OK(lbl_s("in ASCII for", variant[k]), cap_ascii_only());
        expect_no_temps(&e, NULL);

        r30[k] = read_file(e.r30, &r30l[k]);
        who[k] = read_file(e.who, &whol[k]);
        scr[k] = read_file(e.scr, &scrl[k]);
    }

    /* The grammar's whitespace rule absorbs the line ending difference, so the
     * two ledgers have to produce the same three files.  This relation is the
     * oracle rather than the CRLF fixture's byte count, which is reconstructed
     * rather than observed. */
    if (r30[0] != NULL && r30[1] != NULL) {
        EQM("both line endings give the same RANK30.TXT",
            r30[1], r30l[1], r30[0], r30l[0]);
    }
    if (who[0] != NULL && who[1] != NULL) {
        EQM("and the same RANKWHO.TXT", who[1], whol[1], who[0], whol[0]);
    }
    if (scr[0] != NULL && scr[1] != NULL) {
        EQM("and the same NX93.SCR", scr[1], scrl[1], scr[0], scrl[0]);
    }
    for (k = 0; k < 2; k++) {
        free(r30[k]);
        free(who[k]);
        free(scr[k]);
    }
}

/* "A run that does not reset writes nothing" takes precedence over migration.
 * The old ledger stays where it is until the first crossing, which is also the
 * only thing it is still read for. */
static void case_i38(void)
{
    Env e;

    env_make(&e, "i38");
    write_bytes(e.scr, fix_scr13, FIX_SCR13_LEN);
    write_bytes(e.txt, fix_ledger_lf, FIX_LEDGER_LF_LEN);
    env_snapshot(&e);
    set_clock(T_LEDGER_SAMEDAY);

    EQL("an idle run with an old ledger does nothing", rr(&e, RANK_MODE_DAILY), 0);
    expect_unchanged(&e);
    OK("nothing was retired", !file_exists(e.oldledger));
    expect_no_temps(&e, NULL);
}

/* Once the state file is canonical the firing condition cannot be met again, so
 * no later run may touch the retirement. */
static void case_i39(void)
{
    static const char *const later[3] = {
        "2026-10-27T02:00:00", "2026-10-28T02:00:00", "2026-10-29T02:00:00"
    };
    Env e;
    int k;

    env_make(&e, "i39");
    write_bytes(e.scr, fix_scr13, FIX_SCR13_LEN);
    write_bytes(e.txt, fix_ledger_lf, FIX_LEDGER_LF_LEN);
    set_clock(T_NEXTWK);
    if (!EQL("migration runs", rr(&e, RANK_MODE_DAILY), 0)) {
        return;
    }

    for (k = 0; k < 3; k++) {
        set_clock(later[k]);
        eq_long(__FILE__, __LINE__, lbl("a later reset runs", k),
                rr(&e, RANK_MODE_DAILY), 0);
        expect_file_bytes(e.oldledger, fix_ledger_lf, FIX_LEDGER_LF_LEN,
                          lbl("NX93OLD.TXT is still the ledger", k));
        expect_state(e.txt, later[k], lbl("and NX93.TXT is still canonical", k));
        expect_no_temps(&e, NULL);
    }
}

/* An existing retirement is somebody's only copy of something.  Overwriting it
 * silently is the one move this whole design refuses to make. */
static void case_i40(void)
{
    static const char *const planted = "an earlier retirement\r\n";
    Env e;

    env_make(&e, "i40-present");
    write_bytes(e.scr, fix_scr13, FIX_SCR13_LEN);
    write_bytes(e.txt, fix_ledger_lf, FIX_LEDGER_LF_LEN);
    write_text(e.oldledger, planted);
    env_snapshot(&e);
    set_clock(T_NEXTWK);
    EQL("migration refuses to start", rr(&e, RANK_MODE_DAILY), 1);
    expect_unchanged(&e);
    expect_file_bytes(e.oldledger, planted, strlen(planted),
                      "the earlier retirement is untouched");
    expect_no_temps(&e, NULL);

    /* Existence is the condition, not content: an empty file still counts. */
    env_make(&e, "i40-empty");
    write_bytes(e.scr, fix_scr13, FIX_SCR13_LEN);
    write_bytes(e.txt, fix_ledger_lf, FIX_LEDGER_LF_LEN);
    write_bytes(e.oldledger, "", 0u);
    env_snapshot(&e);
    set_clock(T_NEXTWK);
    EQL("an empty retirement still stops it", rr(&e, RANK_MODE_DAILY), 1);
    expect_unchanged(&e);
    EQL("and it is still empty", file_size(e.oldledger), 0L);
    expect_no_temps(&e, NULL);

    /* The negative variant: with no ledger there is no migration, so the name
     * being occupied is none of the run's business. */
    env_make(&e, "i40-noledger");
    write_bytes(e.scr, fix_scr13, FIX_SCR13_LEN);
    write_text(e.oldledger, planted);
    set_clock(T_NEXTWK);
    EQL("without a ledger the run resets as usual", rr(&e, RANK_MODE_DAILY), 0);
    expect_rows_file(e.scr, G8_WANT, RANK_ROWS, "NX93.SCR");
    expect_file_bytes(e.oldledger, planted, strlen(planted),
                      "and the occupied name is left alone");
    expect_no_temps(&e, NULL);
}

/* The import builds its tables in memory, so a ledger it cannot read costs
 * nothing: twenty five ways of being unreadable, and not one byte written. */
static void case_i41(void)
{
    size_t i;

    for (i = 0; i < LEDGER_BAD_N; i++) {
        Env e;

        env_make(&e, lbl("i41", (long)i));
        write_bytes(e.scr, fix_scr13, FIX_SCR13_LEN);
        write_text(e.txt, LEDGER_BAD[i].text);
        env_snapshot(&e);
        set_clock(T_NEXTWK);

        eq_long(__FILE__, __LINE__, lbl_s("the run stops for:", LEDGER_BAD[i].name),
                rr(&e, RANK_MODE_DAILY), 1);
        expect_unchanged(&e);
        OK(lbl_s("no all-time table was created for:", LEDGER_BAD[i].name),
           !file_exists(e.r30));
        OK(lbl_s("no per-user table either for:", LEDGER_BAD[i].name),
           !file_exists(e.who));
        OK(lbl_s("and nothing was retired for:", LEDGER_BAD[i].name),
           !file_exists(e.oldledger));
        expect_no_temps(&e, NULL);
    }
}

static void case_i42(void)
{
    Env e;
    char copy[TPATH];
    long bytes;
    static RankRow rows[RANKWHO_ROWS + 2];
    int n;

    env_make(&e, "i42");
    write_bytes(e.scr, fix_scr13, FIX_SCR13_LEN);
    bytes = write_huge_ledger(e.txt);
    OK("the ledger really is over two mebibytes", bytes > 2L * 1024L * 1024L);
    snprintf(copy, sizeof copy, "%s/huge.copy", g_scratch);
    copy_file(e.txt, copy);
    set_clock(T_NEXTWK);

    EQL("a two mebibyte ledger migrates", rr(&e, RANK_MODE_DAILY), 0);
    n = rows_in_file(e.r30, rows, RANK30_ROWS);
    EQL("the all-time table is at its floor", n, (long)RANK30_ROWS);
    n = rows_in_file(e.who, rows, RANKWHO_ROWS);
    OK("and the per-user table is within its capacity",
       n >= 0 && n <= RANKWHO_ROWS);
    OK("NX93OLD.TXT is the ledger, byte for byte",
       files_equal(e.oldledger, copy));
    expect_state(e.txt, T_NEXTWK, "NX93.TXT");
    expect_no_temps(&e, NULL);
    rm_tree(copy);
}

/* The decode has to put back exactly the bytes the old writer escaped.  0x5C is
 * the second byte of several Shift-JIS characters, which is why the ledger
 * escaped it in the first place; losing the decode loses those names. */
static void case_i49(void)
{
    static const char *const want[3] = {
        "327400 30 01 AB\"CD\\EF 26/09/15 A",
        "300000 30 01 omzn\202\240   26/09/15 A",
        "290000 30 01 X\"Y\\Z    26/09/15 A"
    };
    static const char *const ledger =
        LPRE ": [" LENT
        "\"327400 30 01 AB\\\"CD\\\\EF 26/09/15 A\","
        "\"300000 30 01 omzn\202\240   26/09/15 A\","
        "\"290000 30 01 X\\u0022Y\\u005cZ    26/09/15 A\""
        LEND;
    Env e;
    RankWhoTable t;
    char *bytes;
    size_t len;

    /* No NX93.SCR, so the two tables hold nothing but the ledger's own rows. */
    env_make(&e, "i49");
    write_text(e.txt, ledger);
    set_clock(T_NEXTWK);

    EQL("a ledger with escapes migrates", rr(&e, RANK_MODE_DAILY), 0);
    expect_rows_file(e.r30, want, 3, "RANK30.TXT holds the decoded bytes");
    expect_rows_file(e.who, want, 3, "RANKWHO.TXT holds them too");

    /* And the high bytes did not take the key with them (U-K3, seen here from
     * the integration side). */
    bytes = read_file(e.who, &len);
    if (OK("RANKWHO.TXT reads back", bytes != NULL)) {
        if (who_load_bytes(bytes, len, &t) == 3) {
            EQS("the Shift-JIS name keeps its key", t.row[1].key, "OMZN\202\240");
            OK("which is not the key of the ASCII name it starts with",
               strcmp(t.row[1].key, "OMZN") != 0);
        }
        free(bytes);
    }
    expect_no_temps(&e, NULL);
}

/* The apply order is existing table, then NX93.SCR, then the ledger, and ties
 * keep that order.  Without it the same two instants give two different files
 * depending on which file was read first. */
static void case_i50(void)
{
    static const char *const scr[2] = {
        "327400 30 01 OMZN     26/09/15 A",
        "300820 30 01 TIEA     26/10/08 A"
    };
    static const char *const ledger =
        LPRE ": [" LENT
        "\"300820 30 01 TIEA     26/10/07 A\","
        "\"290000 30 01 OTHR     26/10/01 A\""
        LEND;
    static const char *const want_r30[4] = {
        "327400 30 01 OMZN     26/09/15 A",
        "300820 30 01 TIEA     26/10/08 A",
        "300820 30 01 TIEA     26/10/07 A",
        "290000 30 01 OTHR     26/10/01 A"
    };
    static const char *const want_who[3] = {
        "327400 30 01 OMZN     26/09/15 A",
        "300820 30 01 TIEA     26/10/08 A",
        "290000 30 01 OTHR     26/10/01 A"
    };
    Env e;

    env_make(&e, "i50");
    write_rows_file(e.scr, scr, 2);
    write_text(e.txt, ledger);
    set_clock(T_NEXTWK);

    EQL("the migration runs", rr(&e, RANK_MODE_DAILY), 0);
    expect_rows_file(e.r30, want_r30, 4,
                     "the table's tied row stands above the ledger's");
    /* The same tie in the per-user table means the incumbent keeps the seat, so
     * the row that survives is the one from NX93.SCR. */
    expect_rows_file(e.who, want_who, 3,
                     "and the per-user table kept the table's row");
    expect_no_temps(&e, NULL);
}

/* ------------------------------------------------------------ 故障注入の基線
 *
 * A three stage commit only moves the outgoing file aside when the target
 * already exists, so on a first write *_STRAND cannot reach E15 and degrades to
 * E14 - there is nothing to put back (test-design.md section 10.5).  The baseline
 * is therefore a directory where all four files exist, and where the next
 * crossing really changes all four: without the second half, "the stages before
 * the injection point hold the new bytes" is satisfied by files that never moved.
 * I-52 pins the degradation itself, from the other baseline.
 */

#define FSTAGES 4
#define FKINDS  4

#define F_T1 T_NEXTWK                   /* the clean reset that builds the baseline */
#define F_T2 "2026-10-27T02:00:00"      /* the crossing a fault is injected into */
#define F_T3 "2026-10-28T02:00:00"      /* one crossing more, where a retained copy goes */

static const char *const FAULT_BASENAME[FSTAGES] = {
    "RANK30.TXT", "RANKWHO.TXT", "NX93.SCR", "NX93.TXT"
};

static const char *const FAULT_KIND[FKINDS] = {
    "a failed write", "a failed commit", "a failed rename", "a stranded rename"
};

static const RankFault FAULTS[FSTAGES][FKINDS] = {
    { RANK_FAULT_R30_WRITE, RANK_FAULT_R30_COMMIT,
      RANK_FAULT_R30_RENAME, RANK_FAULT_R30_STRAND },
    { RANK_FAULT_WHO_WRITE, RANK_FAULT_WHO_COMMIT,
      RANK_FAULT_WHO_RENAME, RANK_FAULT_WHO_STRAND },
    { RANK_FAULT_SCR_WRITE, RANK_FAULT_SCR_COMMIT,
      RANK_FAULT_SCR_RENAME, RANK_FAULT_SCR_STRAND },
    { RANK_FAULT_TXT_WRITE, RANK_FAULT_TXT_COMMIT,
      RANK_FAULT_TXT_RENAME, RANK_FAULT_TXT_STRAND }
};

#define FKIND_STRAND 3

/* The table the baseline leaves in place before the second crossing: the game
 * has been played twice more, so NEWP's 999999 and omzn++++'s 420000 pushed the
 * two lowest shipped rows out of the ten.
 *
 * It matters that this table is not already what a rebuild of it produces.  The
 * second crossing has to change all four files, and NX93.SCR is the hard one:
 * every table whose records all belong to distinct players is a fixed point of
 * rank_rebuild (spec.md section 3.6, I12), so the change has to come from
 * somewhere else.  Here it comes from the key rule (section 3.8): omzn++++ and
 * OMZN are one player, so the rebuild keeps only their 420000 and the vacated
 * row is filled by the shipped 060000 coming back. */
static const char *const FAULT_SCR[RANK_ROWS] = {
    "999999 30 01 NEWP     26/10/26 A",
    "420000 30 03 omzn++++ 26/10/26 A",
    "327400 30 01 OMZN     26/09/15 A",
    "200000 29 00 Bio_100% 93/10/01 A",
    "180000 28 01 Bio_100% 93/10/01 A",
    "160000 27 02 Bio_100% 93/10/01 A",
    "140000 25 03 Bio_100% 93/10/01 A",
    "120000 22 04 Bio_100% 93/10/01 A",
    "100000 21 05 Bio_100% 93/10/01 A",
    "080000 19 06 Bio_100% 93/10/01 A"
};

/* What a clean second crossing produces, derived by hand so that the reference
 * run below is anchored rather than compared with itself.
 *
 * The all-time table starts from the six rows the baseline reset left (I26_R30)
 * and takes the three candidates in file order.  999999 and 420000 are new
 * bytes and seat themselves by score; 327400 is already there byte for byte, so
 * it dedupes away.  Eight rows. */
static const char *const FAULT_REF_R30[8] = {
    "999999 30 01 NEWP     26/10/26 A",
    "420000 30 03 omzn++++ 26/10/26 A",
    "327400 30 01 OMZN     26/09/15 A",
    "315970 30 02 omzn     26/10/13 A",
    "300820 30 01 omzn     26/10/08 A",
    "300280 30 04 omzn     26/10/13 A",
    "299370 30 00 OMZN@@@@ 61/11/17 A",
    "286530 30 01 omzn     26/10/09 A"
};

/* The per-user table starts from the one row the baseline left (I26_WHO, key
 * OMZN at 327400).  NEWP is a new key and seats at the top; omzn++++ folds to
 * OMZN and 420000 beats 327400, so it replaces that row and is re-seated below
 * NEWP; the third candidate is OMZN again at 327400, which no longer beats the
 * seat it used to hold.  Two rows, and the one that leaves is the player's own
 * older record, so no W1 is due. */
static const char *const FAULT_REF_WHO[2] = {
    "999999 30 01 NEWP     26/10/26 A",
    "420000 30 03 omzn++++ 26/10/26 A"
};

/* The rebuild drops the shipped rows from its input, keeps each player's best -
 * NEWP's 999999 and, for OMZN, 420000 over 327400 - and appends the shipped ten
 * before cutting at ten by score.  Two real rows leave room for 200000 down to
 * 060000, which is where 060000 comes back and 327400 goes. */
static const char *const FAULT_REF_SCR[RANK_ROWS] = {
    "999999 30 01 NEWP     26/10/26 A",
    "420000 30 03 omzn++++ 26/10/26 A",
    "200000 29 00 Bio_100% 93/10/01 A",
    "180000 28 01 Bio_100% 93/10/01 A",
    "160000 27 02 Bio_100% 93/10/01 A",
    "140000 25 03 Bio_100% 93/10/01 A",
    "120000 22 04 Bio_100% 93/10/01 A",
    "100000 21 05 Bio_100% 93/10/01 A",
    "080000 19 06 Bio_100% 93/10/01 A",
    "060000 16 07 Bio_100% 93/10/01 A"
};

static const char *fstage_path(const Env *e, int s)
{
    switch (s) {
    case 0:  return e->r30;
    case 1:  return e->who;
    case 2:  return e->scr;
    default: return e->txt;
    }
}

static const char *fstage_keep(const Env *e, int s)
{
    switch (s) {
    case 0:  return e->old30;
    case 1:  return e->oldwho;
    case 2:  return e->oldscr;
    default: return e->oldtxt;
    }
}

static char *g_fold[FSTAGES];
static size_t g_foldlen[FSTAGES];
static char *g_fnew[FSTAGES];
static size_t g_fnewlen[FSTAGES];

static int fault_baseline(Env *e, const char *tag)
{
    env_make(e, tag);
    write_bytes(e->scr, fix_scr13, FIX_SCR13_LEN);
    env_state(e, T_LAST);
    set_clock(F_T1);
    if (rr(e, RANK_MODE_DAILY) != 0) {
        is_true(__FILE__, __LINE__, lbl_s("the baseline reset succeeded for", tag), 0);
        return -1;
    }
    write_rows_file(e->scr, FAULT_SCR, RANK_ROWS);
    set_clock(F_T2);
    return 0;
}

static void fault_reference_free(void)
{
    int s;
    for (s = 0; s < FSTAGES; s++) {
        free(g_fold[s]);
        free(g_fnew[s]);
        g_fold[s] = NULL;
        g_fnew[s] = NULL;
    }
}

/* Captures what each of the four files holds before and after a clean second
 * crossing, and checks both halves against the hand-derived tables. */
static int fault_reference(void)
{
    Env e;
    int s;

    for (s = 0; s < FSTAGES; s++) {
        g_fold[s] = NULL;
        g_fnew[s] = NULL;
    }
    if (fault_baseline(&e, "fault-ref") != 0) {
        return -1;
    }
    for (s = 0; s < FSTAGES; s++) {
        g_fold[s] = read_file(fstage_path(&e, s), &g_foldlen[s]);
    }
    if (rr(&e, RANK_MODE_DAILY) != 0) {
        OK("the clean reference crossing succeeded", 0);
        return -1;
    }
    for (s = 0; s < FSTAGES; s++) {
        g_fnew[s] = read_file(fstage_path(&e, s), &g_fnewlen[s]);
    }
    expect_rows_file(e.r30, FAULT_REF_R30, 8, "the reference RANK30.TXT");
    expect_rows_file(e.who, FAULT_REF_WHO, 2, "the reference RANKWHO.TXT");
    expect_rows_file(e.scr, FAULT_REF_SCR, RANK_ROWS, "the reference NX93.SCR");
    expect_state(e.txt, F_T2, "the reference NX93.TXT");

    for (s = 0; s < FSTAGES; s++) {
        if (!is_true(__FILE__, __LINE__,
                     lbl_s("both states captured for", FAULT_BASENAME[s]),
                     g_fold[s] != NULL && g_fnew[s] != NULL)) {
            return -1;
        }
        is_true(__FILE__, __LINE__,
                lbl_s("and the clean crossing really changed", FAULT_BASENAME[s]),
                !(g_foldlen[s] == g_fnewlen[s]
                  && memcmp(g_fold[s], g_fnew[s], g_foldlen[s]) == 0));
    }
    return 0;
}

static void fault_expect_converged(const Env *e, const char *what)
{
    expect_file_bytes(e->r30, g_fnew[0], g_fnewlen[0],
                      lbl_s("RANK30.TXT converged after", what));
    expect_file_bytes(e->who, g_fnew[1], g_fnewlen[1],
                      lbl_s("RANKWHO.TXT converged after", what));
    expect_file_bytes(e->scr, g_fnew[2], g_fnewlen[2],
                      lbl_s("NX93.SCR converged after", what));
    is_true(__FILE__, __LINE__,
            lbl_s("and NX93.TXT differs only in its timestamp after", what),
            state_same_but_time(e->txt, g_fnew[3], g_fnewlen[3]));
}

/* ------------------------------------------------------------------- I-43 */

static void case_i43(void)
{
    char *out[FSTAGES][FKINDS];
    size_t outl[FSTAGES][FKINDS];
    int s;
    int k;

    for (s = 0; s < FSTAGES; s++) {
        for (k = 0; k < FKINDS; k++) {
            out[s][k] = NULL;
            outl[s][k] = 0u;
        }
    }
    if (fault_reference() != 0) {
        fault_reference_free();
        return;
    }

    for (s = 0; s < FSTAGES; s++) {
        for (k = 0; k < FKINDS; k++) {
            Env e;
            char tag[64];
            char what[96];
            int i;

            snprintf(tag, sizeof tag, "i43-%d-%d", s, k);
            snprintf(what, sizeof what, "%s on %s",
                     FAULT_KIND[k], FAULT_BASENAME[s]);
            if (fault_baseline(&e, tag) != 0) {
                continue;
            }

            rank_test_fault = FAULTS[s][k];
            eq_long(__FILE__, __LINE__, lbl_s("the run reports failure:", what),
                    rr(&e, RANK_MODE_DAILY), 1);
            rank_test_fault = RANK_FAULT_NONE;

            /* The commit order is strict, so everything before the injection
             * point is new and everything after it is untouched.  I1's
             * refutation would be NX93.SCR new while RANK30.TXT is still old. */
            for (i = 0; i < s; i++) {
                expect_file_bytes(fstage_path(&e, i), g_fnew[i], g_fnewlen[i],
                                  lbl_s("an earlier stage is new:",
                                        FAULT_BASENAME[i]));
            }
            for (i = s + 1; i < FSTAGES; i++) {
                expect_file_bytes(fstage_path(&e, i), g_fold[i], g_foldlen[i],
                                  lbl_s("a later stage is untouched:",
                                        FAULT_BASENAME[i]));
            }

            if (k != FKIND_STRAND) {
                /* E13 and E14 both leave the target holding what it held - for
                 * E14 that is the proof the recovery rename ran. */
                expect_file_bytes(fstage_path(&e, s), g_fold[s], g_foldlen[s],
                                  lbl_s("the target still holds its old bytes:",
                                        what));
                OK(lbl_s("and no retained copy was left:", what),
                   !file_exists(fstage_keep(&e, s)));
                expect_no_temps(&e, NULL);
            } else {
                /* E15: the target is gone and its bytes are under the retained
                 * name, which the message has to say out loud. */
                OK(lbl_s("the target is gone:", what),
                   !file_exists(fstage_path(&e, s)));
                expect_file_bytes(fstage_keep(&e, s), g_fold[s], g_foldlen[s],
                                  lbl_s("its bytes are all in the retained copy:",
                                        what));
                OK(lbl_s("whose name is on screen:", what),
                   cap_has(KEEP_NAMES[s]));
                expect_no_temps(&e, fstage_keep(&e, s));
            }

            /* The only machine oracle on the wording: the basename of every
             * file that really changed is in it (test-design.md section 12.3). */
            for (i = 0; i < s; i++) {
                OK(lbl_s("the message names what changed:", FAULT_BASENAME[i]),
                   cap_has(FAULT_BASENAME[i]));
            }
            OK(lbl_s("the message is ASCII:", what), cap_ascii_only());

            out[s][k] = read_file(g_cap_path, &outl[s][k]);
        }
    }

    /* A failed commit and a failed rename leave the target in exactly the same
     * state, so on this machine the screen is the only place the difference
     * between E13 and E14 can be seen at all. */
    for (s = 0; s < FSTAGES; s++) {
        if (out[s][1] != NULL && out[s][2] != NULL) {
            is_true(__FILE__, __LINE__,
                    lbl_s("a failed rename is not reported as a failed commit:",
                          FAULT_BASENAME[s]),
                    !(outl[s][1] == outl[s][2]
                      && memcmp(out[s][1], out[s][2], outl[s][1]) == 0));
        }
        for (k = 0; k < FKINDS; k++) {
            free(out[s][k]);
        }
    }
    fault_reference_free();
}

/* ------------------------------------------------------------------- I-44 */

static void case_i44(void)
{
    int s;
    int k;

    if (fault_reference() != 0) {
        fault_reference_free();
        return;
    }

    for (s = 0; s < FSTAGES; s++) {
        for (k = 0; k < FKINDS; k++) {
            Env e;
            char tag[64];
            char what[96];

            snprintf(tag, sizeof tag, "i44-%d-%d", s, k);
            snprintf(what, sizeof what, "%s on %s",
                     FAULT_KIND[k], FAULT_BASENAME[s]);
            if (fault_baseline(&e, tag) != 0) {
                continue;
            }

            rank_test_fault = FAULTS[s][k];
            if (rr(&e, RANK_MODE_DAILY) != 1) {
                is_true(__FILE__, __LINE__,
                        lbl_s("the fault really fired:", what), 0);
                rank_test_fault = RANK_FAULT_NONE;
                continue;
            }
            rank_test_fault = RANK_FAULT_NONE;

            eq_long(__FILE__, __LINE__, lbl_s("retrying at once succeeds:", what),
                    rr(&e, RANK_MODE_DAILY), 0);

            if (k != FKIND_STRAND) {
                /* Nothing was committed by halves, so the retry finishes the
                 * job and lands on the clean result.  In particular a failure
                 * in the last stage must not leave the first three different. */
                fault_expect_converged(&e, what);
                expect_no_temps(&e, NULL);
            } else {
                /* E15 is the one case where a copy outlives the run, and the
                 * retry must not quietly throw it away: it is the only copy of
                 * the bytes that went missing. */
                expect_file_bytes(fstage_keep(&e, s), g_fold[s], g_foldlen[s],
                                  lbl_s("the retained copy is still intact:",
                                        what));
                OK(lbl_s("and the retry announced no discard:", what),
                   !cap_has_any_keep_name());
                expect_no_temps(&e, fstage_keep(&e, s));

                /* Following the rename the message names, and then crossing
                 * once more, has to converge - which is what makes the advice
                 * worth printing. */
                if (rename(fstage_keep(&e, s), fstage_path(&e, s)) != 0) {
                    die(fstage_keep(&e, s));
                }
                set_clock(F_T3);
                eq_long(__FILE__, __LINE__,
                        lbl_s("the run after the recovery rename succeeds:", what),
                        rr(&e, RANK_MODE_DAILY), 0);
                fault_expect_converged(&e, what);
                expect_no_temps(&e, NULL);
            }
        }
    }
    fault_reference_free();
}

/* ------------------------------------------------------------------- I-45
 *
 * Migration shares stages one to three with an ordinary reset, so only the
 * fourth differs: its retained name is NX93OLD.TXT and it survives success.
 * Six injection points cover that difference; the other eleven combinations are
 * deliberately not taken, because after the import has finished in memory they
 * are indistinguishable from an ordinary reset (test-design.md section 10.5).
 */
static void case_i45(void)
{
    static const struct { const char *what; int stage; int kind; } tab[6] = {
        { "a failed write on RANK30.TXT",   0, 0 },
        { "a failed write on NX93.TXT",     3, 0 },
        { "a failed commit on NX93.TXT",    3, 1 },
        { "a failed rename on NX93.TXT",    3, 2 },
        { "a stranded rename on NX93.TXT",  3, 3 },
        { "a failed retirement rename",    -1, -1 }
    };
    int k;

    for (k = 0; k < 6; k++) {
        Env e;
        RankFault f;

        env_make(&e, lbl("i45", k));
        write_bytes(e.scr, fix_scr13, FIX_SCR13_LEN);
        write_bytes(e.txt, fix_ledger_lf, FIX_LEDGER_LF_LEN);
        env_snapshot(&e);
        set_clock(T_NEXTWK);

        f = (tab[k].stage < 0) ? RANK_FAULT_RETIRE_RENAME
                               : FAULTS[tab[k].stage][tab[k].kind];
        rank_test_fault = f;
        eq_long(__FILE__, __LINE__, lbl_s("the run reports failure:", tab[k].what),
                rr(&e, RANK_MODE_DAILY), 1);
        rank_test_fault = RANK_FAULT_NONE;

        if (k == 0) {
            /* The import is finished in memory before anything is written, so
             * a failure in the first stage leaves the disk untouched. */
            expect_unchanged(&e);
            OK("nothing was retired", !file_exists(e.oldledger));
            expect_no_temps(&e, NULL);
        } else {
            expect_rows_file(e.r30, I37_R30, 14,
                             lbl_s("RANK30.TXT is new:", tab[k].what));
            expect_rows_file(e.who, I26_WHO, 1,
                             lbl_s("RANKWHO.TXT is new:", tab[k].what));
            expect_rows_file(e.scr, G8_WANT, RANK_ROWS,
                             lbl_s("NX93.SCR is new:", tab[k].what));
            if (tab[k].kind == 3) {
                /* The one point where the retirement is observable: the old
                 * ledger's bytes end up under NX93OLD.TXT, not RANKOLD.TXT. */
                OK("NX93.TXT is gone", !file_exists(e.txt));
                expect_file_bytes(e.oldledger, fix_ledger_lf, FIX_LEDGER_LF_LEN,
                                  "and the ledger is under NX93OLD.TXT");
                OK("not under RANKOLD.TXT", !file_exists(e.oldtxt));
                OK("whose name is on screen", cap_has("NX93OLD.TXT"));
                expect_no_temps(&e, NULL);
            } else {
                expect_file_bytes(e.txt, fix_ledger_lf, FIX_LEDGER_LF_LEN,
                                  lbl_s("NX93.TXT is still the ledger:",
                                        tab[k].what));
                OK(lbl_s("and nothing was retired:", tab[k].what),
                   !file_exists(e.oldledger));
                expect_no_temps(&e, NULL);
            }
        }

        /* Retrying with no fault has to finish the migration, except after a
         * stranded fourth stage: NX93.TXT is gone, so nothing holding the
         * backups key is left and migration simply does not fire again.  That
         * is not E11 either, because E11 only guards a migration that fires. */
        eq_long(__FILE__, __LINE__, lbl_s("the retry succeeds:", tab[k].what),
                rr(&e, RANK_MODE_DAILY), 0);
        expect_rows_file(e.r30, I37_R30, 14,
                         lbl_s("RANK30.TXT after the retry:", tab[k].what));
        expect_rows_file(e.who, I26_WHO, 1,
                         lbl_s("RANKWHO.TXT after the retry:", tab[k].what));
        expect_rows_file(e.scr, G8_WANT, RANK_ROWS,
                         lbl_s("NX93.SCR after the retry:", tab[k].what));
        expect_state(e.txt, T_NEXTWK, lbl_s("NX93.TXT after the retry:",
                                            tab[k].what));
        expect_file_bytes(e.oldledger, fix_ledger_lf, FIX_LEDGER_LF_LEN,
                          lbl_s("NX93OLD.TXT holds the ledger after the retry:",
                                tab[k].what));
        expect_no_temps(&e, NULL);
    }
}

/* ------------------------------------------------------------------- I-51 */

/* A warning and a failure in the same run.  The warning was printed while the
 * tables were still being built, so the failure message afterwards has to be the
 * one that tells the truth about what is on disk. */
static void case_i51(void)
{
    Env e;
    const char *const *hundred;
    const char *scr[1];
    char last_row[RANK_MAX_LINE + 1];

    env_make(&e, "i51");
    hundred = gen_who100();
    strcpy(last_row, hundred[RANKWHO_ROWS - 1]);
    write_rows_file(e.who, hundred, RANKWHO_ROWS);
    {
        const char *one[1];
        one[0] = last_row;
        write_rows_file(e.r30, one, 1);
    }
    scr[0] = "495000 30 01 ZZZ      26/09/15 A";
    write_rows_file(e.scr, scr, 1);
    env_state(&e, T_LAST);
    snap(e.scr, "i51-scr");
    snap(e.txt, "i51-txt");
    set_clock(T_NEXTWK);

    rank_test_fault = RANK_FAULT_SCR_COMMIT;
    EQL("the run reports failure", rr(&e, RANK_MODE_DAILY), 1);
    rank_test_fault = RANK_FAULT_NONE;

    OK("NX93.SCR is untouched", snap_same(e.scr, "i51-scr"));
    OK("NX93.TXT is untouched", snap_same(e.txt, "i51-txt"));
    EQL("the row that lost its seat is still reported", cap_count(last_row), 1);
    OK("the message names the all-time table, which did change",
       cap_has("RANK30.TXT"));
    OK("and the per-user table, which did too", cap_has("RANKWHO.TXT"));
    OK("and NX93.SCR, which did not", cap_has("NX93.SCR"));
    OK("in ASCII", cap_ascii_only());
    expect_no_temps(&e, NULL);
}

/* ------------------------------------------------------------------- I-52 */

/* The degradation the fault baseline exists to avoid, pinned as a contract of
 * its own: with nothing to put back there is nothing to strand, so a first write
 * reports E14 and leaves no retained copy.  An implementation that returned
 * STRANDED from the "nothing was saved" branch would claim a file had been lost
 * when none had. */
static void case_i52(void)
{
    static const struct { const char *what; int stage; } tab[2] = {
        { "RANK30.TXT", 0 },
        { "RANKWHO.TXT", 1 }
    };
    int k;

    for (k = 0; k < 2; k++) {
        Env e;
        int s = tab[k].stage;
        int i;

        /* The I-26 baseline: neither new table exists yet. */
        env_live(&e, lbl("i52", k), T_LAST);
        set_clock(T_NEXTWK);

        rank_test_fault = FAULTS[s][FKIND_STRAND];
        eq_long(__FILE__, __LINE__,
                lbl_s("a stranded first write reports failure:", tab[k].what),
                rr(&e, RANK_MODE_DAILY), 1);
        rank_test_fault = RANK_FAULT_NONE;

        OK(lbl_s("the target was never created:", tab[k].what),
           !file_exists(fstage_path(&e, s)));
        OK(lbl_s("and no retained copy appeared:", tab[k].what),
           !file_exists(fstage_keep(&e, s)));
        for (i = 0; i < s; i++) {
            OK(lbl_s("an earlier stage was still written:", FAULT_BASENAME[i]),
               file_exists(fstage_path(&e, i)));
        }
        expect_no_temps(&e, NULL);

        /* And it converges on a retry like any other E13 or E14. */
        eq_long(__FILE__, __LINE__, lbl_s("the retry succeeds:", tab[k].what),
                rr(&e, RANK_MODE_DAILY), 0);
        expect_rows_file(e.r30, I26_R30, 6, lbl_s("RANK30.TXT:", tab[k].what));
        expect_rows_file(e.who, I26_WHO, 1, lbl_s("RANKWHO.TXT:", tab[k].what));
        expect_rows_file(e.scr, G8_WANT, RANK_ROWS,
                         lbl_s("NX93.SCR:", tab[k].what));
        expect_state(e.txt, T_NEXTWK, lbl_s("NX93.TXT:", tab[k].what));
        expect_no_temps(&e, NULL);
    }
}

/* ------------------------------------------------------------------- I-53
 *
 * After a stranded commit the retained copy survives the next run as well,
 * because the target is missing and the discard path is never reached.  The run
 * after that finds the target in place and discards the copy - which, with
 * GAME.BAT calling RANK every day, is about a day's notice.  So the discard gets
 * a line of its own first.
 *
 * All four files are taken rather than one representative: the discard happens in
 * the shared commit code with the name arriving as an argument, so an
 * implementation that hard-coded one name would pass exactly one variant.
 */
static void case_i53(void)
{
    int s;

    if (fault_reference() != 0) {
        fault_reference_free();
        return;
    }

    for (s = 0; s < FSTAGES; s++) {
        Env e;
        int i;

        if (fault_baseline(&e, lbl("i53", s)) != 0) {
            continue;
        }

        /* Stage one: strand it.  The copy appears; nothing has been discarded
         * yet, and the run failed. */
        rank_test_fault = FAULTS[s][FKIND_STRAND];
        eq_long(__FILE__, __LINE__,
                lbl_s("stranding reports failure:", FAULT_BASENAME[s]),
                rr(&e, RANK_MODE_DAILY), 1);
        rank_test_fault = RANK_FAULT_NONE;
        expect_file_bytes(fstage_keep(&e, s), g_fold[s], g_foldlen[s],
                          lbl_s("the retained copy holds the old bytes:",
                                FAULT_BASENAME[s]));

        /* Stage two: a clean retry.  The target was missing, so the commit never
         * reaches its discard and the copy is still there - unannounced,
         * because nothing was discarded. */
        eq_long(__FILE__, __LINE__,
                lbl_s("the retry succeeds:", FAULT_BASENAME[s]),
                rr(&e, RANK_MODE_DAILY), 0);
        expect_file_bytes(fstage_keep(&e, s), g_fold[s], g_foldlen[s],
                          lbl_s("the copy survives the retry:",
                                FAULT_BASENAME[s]));
        OK(lbl_s("and the retry says nothing about it:", KEEP_NAMES[s]),
           !cap_has(KEEP_NAMES[s]));

        /* Stage three: one crossing more.  Now the target is in place, the copy
         * is in the way, and it goes - after being announced. */
        set_clock(F_T3);
        eq_long(__FILE__, __LINE__,
                lbl_s("the next crossing succeeds:", FAULT_BASENAME[s]),
                rr(&e, RANK_MODE_DAILY), 0);
        OK(lbl_s("the copy is gone:", KEEP_NAMES[s]),
           !file_exists(fstage_keep(&e, s)));
        eq_long(__FILE__, __LINE__,
                lbl_s("announced once, by name:", KEEP_NAMES[s]),
                cap_count(KEEP_NAMES[s]), 1);
        OK(lbl_s("the line says the copy goes away:", KEEP_NAMES[s]),
           cap_has_any(W2_GONE, 7));
        OK(lbl_s("and that it could be kept first:", KEEP_NAMES[s]),
           cap_has_any(W2_SAVE, 5));
        OK(lbl_s("in ASCII:", KEEP_NAMES[s]), cap_ascii_only());
        /* One report per copy discarded, so the other three names stay out of
         * it; a hard-coded name would show up here as well. */
        for (i = 0; i < FSTAGES; i++) {
            if (i != s) {
                eq_long(__FILE__, __LINE__,
                        lbl_s("no other retained name is mentioned:",
                              KEEP_NAMES[i]),
                        cap_count(KEEP_NAMES[i]), 0);
            }
        }
        expect_no_temps(&e, NULL);
    }
    fault_reference_free();

    /* The negative variants.  Without them an implementation that printed the
     * line on every run would pass everything above.  The retained names are
     * what makes a discard report a discard report, so their absence is the
     * oracle; the vocabulary sets are not used in this direction, because words
     * like "save" and "before" can honestly appear in a success sentence. */
    {
        Env e;

        env_live(&e, "i53-clean", T_LAST);
        set_clock(T_NEXTWK);
        EQL("a clean reset succeeds", rr(&e, RANK_MODE_DAILY), 0);
        OK("and announces no discard", !cap_has_any_keep_name());
        expect_no_temps(&e, NULL);

        env_live(&e, "i53-idle", "2026-10-26T01:00:00");
        write_rows_file(e.r30, I26_R30, 6);
        write_rows_file(e.who, I26_WHO, 1);
        env_snapshot(&e);
        set_clock(T_NEXTWK);
        EQL("an idle run succeeds", rr(&e, RANK_MODE_DAILY), 0);
        OK("and announces no discard either", !cap_has_any_keep_name());
        expect_unchanged(&e);
        expect_no_temps(&e, NULL);
    }
}

/* -------------------------------------------------------------------- main */

static const Case CASES[] = {
    { "U-A1 mode-parse-accepts",            case_ua1 },
    { "U-A2 mode-parse-rejects",            case_ua2 },

    { "U-C1 week-index-anchors",            case_uc1 },
    { "U-C2 week-index-floor",              case_uc2 },
    { "U-C3 week-index-staircase",          case_uc3 },
    { "U-C4 time-before-lex",               case_uc4 },
    { "U-C5 crossed-table",                 case_uc5 },
    { "U-C6 crossed-sweep",                 case_uc6 },
    { "U-C7 crossed-bad-args",              case_uc7 },

    { "U-K1 key-spec-table",                case_uk1 },
    { "U-K2 key-same-and-different",        case_uk2 },
    { "U-K3 key-stops-at-high-byte",        case_uk3 },
    { "U-K4 key-rescue-rule",               case_uk4 },
    { "U-K5 key-adjacent-boundary",         case_uk5 },
    { "U-K6 key-buffer-bounds",             case_uk6 },
    { "U-K7 key-properties",                case_uk7 },
    { "U-K8 key-via-read-table",            case_uk8 },

    { "U-F1 rows-read-conventions",         case_uf1 },
    { "U-F2 rows-write-shape",              case_uf2 },
    { "U-F3 r30-load",                      case_uf3 },
    { "U-F4 who-load",                      case_uf4 },
    { "U-F5 state-write-canonical",         case_uf5 },
    { "U-F6 state-check-exact",             case_uf6 },
    { "U-F7 has-backups-scan",              case_uf7 },
    { "U-F8 state-roundtrip",               case_uf8 },
    { "U-F9 rows-fuzz",                     case_uf9 },

    { "U-G1 is-default-line",               case_ug1 },
    { "U-G2 merge30-rules",                 case_ug2 },
    { "U-G3 merge30-idempotent",            case_ug3 },
    { "U-G4 mergewho-rules",                case_ug4 },
    { "U-G5 mergewho-overflow",             case_ug5 },
    { "U-G6 mergewho-idempotent",           case_ug6 },
    { "U-G7 merge-invariants",              case_ug7 },
    { "U-G8 rebuild-live-scr",              case_ug8 },
    { "U-G9 rebuild-idempotent",            case_ug9 },
    { "U-G10 rebuild-numeric-order",        case_ug10 },

    { "U-V1 ledger-string-decode",          case_uv1 },
    { "U-V2 ledger-string-rejects",         case_uv2 },
    { "U-V3 import-live-ledger",            case_uv3 },
    { "U-V4 import-grammar-rejects",        case_uv4 },
    { "U-V5 import-streamed",               case_uv5 },
    { "U-V6 import-empty-arrays",           case_uv6 },
    { "U-V7 import-fuzz",                   case_uv7 },

    { "I-21 mode-selects-rule",             case_i21 },
    { "I-22 crossing-wiring",               case_i22 },
    { "I-23 idle-writes-nothing",           case_i23 },
    { "I-24 reset-once",                    case_i24 },
    { "I-25 mode-switch",                   case_i25 },
    { "I-26 reset-full",                    case_i26 },
    { "I-27 reset-idempotent",              case_i27 },
    { "I-28 first-run",                     case_i28 },
    { "I-29 missing-scr",                   case_i29 },
    { "I-30 who-overflow-w1",               case_i30 },
    { "I-31 w1-raw-bytes",                  case_i31 },
    { "I-32 readonly-four",                 case_i32 },
    { "I-33 state-decision-table",          case_i33 },
    { "I-34 state-check-strictness",        case_i34 },
    { "I-35 no-default-intrusion",          case_i35 },
    { "I-36 handplaced-defaults-kept",      case_i36 },
    { "I-37 migrate-live",                  case_i37 },
    { "I-38 migrate-deferred",              case_i38 },
    { "I-39 migrate-once",                  case_i39 },
    { "I-40 migrate-e11",                   case_i40 },
    { "I-41 migrate-e12-atomic",            case_i41 },
    { "I-42 migrate-huge",                  case_i42 },
    { "I-43 fault-16-points",               case_i43 },
    { "I-44 fault-converges",               case_i44 },
    { "I-45 fault-migrate-6",               case_i45 },
    { "I-46 stale-debris",                  case_i46 },
    { "I-47 path-and-bare-names",           case_i47 },
    { "I-48 temp-hygiene-crosscut",         case_i48 },
    { "I-49 migrate-escapes",               case_i49 },
    { "I-50 migrate-ties",                  case_i50 },
    { "I-51 fault-with-w1",                 case_i51 },
    { "I-52 strand-needs-existing-target",  case_i52 },
    { "I-53 w2-announces-discard",          case_i53 },

    { "X-2 future-detect",                  case_x2 },
    { "X-3 future-beats-everything",        case_x3 }
};

/* The longest case tag below, the longest file name a run opens, and the two
 * separators around them.  rank_run refuses a directory whose joined paths do
 * not fit RANK_PATH_MAX and reports E2, so a scratch directory that is merely
 * too deep would turn most of the integration cases red for a reason that has
 * nothing to do with RANK.C.  Saying so is better than letting it look like a
 * finding; run-daily.sh's own scratch path leaves ample room. */
#define TAG_MAX 24

int main(int argc, char **argv)
{
    if (argc >= 2
     && strlen(argv[1]) + 1u + (size_t)TAG_MAX + 1u + 11u + 1u
        > (size_t)RANK_PATH_MAX) {
        fprintf(stderr, "test_daily: the scratch directory is too deep: a case "
                        "directory plus RANKWHO.TXT must fit %d bytes, which "
                        "leaves %d for the path given.\n",
                RANK_PATH_MAX, RANK_PATH_MAX - TAG_MAX - 14);
        return 2;
    }
    return harness_main("test_daily", argc, argv,
                        CASES, sizeof CASES / sizeof CASES[0]);
}










