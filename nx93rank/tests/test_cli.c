/*
 * test_cli.c - entry point suite for nx93rank/dist/RANK.C (nx93-daily-reset).
 *
 *   test_cli <scratch-dir> [name-substring]
 *
 * Exits 0 only when every selected case passed and at least one case ran.
 * Covers A-1..A-7 of docs/TEST-DESIGN.md section 11:
 * usage and argc counting, the exit code vocabulary, the directory argv[0]
 * names, the mode lexeme and the elapsed day count, the differential that keeps
 * the thirteen messages apart, and whether E8's advice actually works.
 *
 * Unlike test_daily.c this suite keeps RANK.C's own main, renamed to rank_entry
 * so that the harness can supply its own.  That rename is the whole technique:
 * the number of arguments, the usage text and the value main returns are not
 * observable through rank_run, and RANK_NO_MAIN drops the only code that
 * decides them.  argv[0] is handed over as <dir>/RANK.EXE, so
 * rank_dir_from_argv0 runs for real rather than being stubbed by the test.
 *
 * Oracles come from test-design.md and spec.md by hand; none of them was read
 * back out of an implementation.  This suite is written before the
 * implementation exists, so it is expected to be red: the compiler's
 * undeclared-symbol list is the implementation's work list.
 *
 * RANK_TEST_CLOCK and RANK_TEST_FAULT enable seams the DOS build never compiles
 * (run.sh asserts their absence from the DOS object).  RANK_C_PATH lets
 * run-daily.sh point the suite at a throwaway copy of the source, which is how
 * fault attribution and mutation testing run without touching the repository.
 */

#ifdef RANK_NO_MAIN
#error "test_cli.c observes RANK.C's own main; build it without RANK_NO_MAIN"
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

/* RANK.C's entry point becomes rank_entry for the duration of the include, and
 * the macro is retired immediately afterwards so that this file's own main is
 * the one the linker finds. */
#define main rank_entry
#include RANK_C_PATH
#undef main

#include "fixtures_daily.h"
#include "harness_daily.h"

/* ------------------------------------------------------------------ 契約表明
 *
 * The two mode constants are all this suite needs a compiler to pin; the rest of
 * spec.md section 3.9 is pinned in test_daily.c, which is the suite that uses it.
 */
typedef char chk_cli_mode_weekly[(RANK_MODE_WEEKLY == 0) ? 1 : -1];
typedef char chk_cli_mode_daily[(RANK_MODE_DAILY == 1) ? 1 : -1];

/* ---------------------------------------------------------------- 行ファイル
 *
 * Restated from test_daily.c rather than lifted into harness_daily.h, for the
 * reason the header already gives about test_rank.c: harness_daily.h is
 * finished work shared with a suite this task must not disturb.  Only the three
 * helpers this file actually needs are here.
 *
 * 101 rows of 34 bytes is 3434, so one buffer covers every table written below.
 */
#define ROWSBUF 4096

static size_t rows_image(const char *const *lines, int n, char *out)
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

static void write_rows_file(const char *path, const char *const *lines, int n)
{
    static char buf[ROWSBUF];
    size_t at = rows_image(lines, n, buf);
    write_bytes(path, buf, at);
}

static void expect_rows_file(const char *path, const char *const *lines,
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

/* ------------------------------------------------------------- 状態ファイル */

/* The canonical 45 byte state file of spec.md section 3.4, written by hand so
 * that a test never depends on the writer it is there to check. */
static void expect_state(const char *path, const char *ts, const char *what)
{
    char want[80];
    char *got;
    size_t len;

    snprintf(want, sizeof want, "{\r\n  \"lastUpdate\": \"%s\"\r\n}\r\n", ts);
    got = read_file(path, &len);
    if (!is_true(__FILE__, __LINE__, lbl_s(what, "exists"), got != NULL)) {
        return;
    }
    eq_mem(__FILE__, __LINE__, what, got, len, want, strlen(want));
    free(got);
}

/* --------------------------------------------------------------- 入口を呼ぶ
 *
 * argv as DOS hands it over.  The strings are copied into writable storage
 * because main's second parameter is char **, and every call goes through the
 * stdout capture so that the screen oracles are always available and the
 * suite's own output stays readable.
 */

#define CLI_ARGS_MAX 3

static char g_argv_bytes[CLI_ARGS_MAX + 1][TPATH];
static char *g_argv[CLI_ARGS_MAX + 2];

static int cli(const char *argv0, const char *const *args, int nargs)
{
    int i;
    int rc;

    if (argv0 == NULL || nargs < 0 || nargs > CLI_ARGS_MAX) {
        fprintf(stderr, "harness failure: %s: cli(%d)\n", g_case, nargs);
        exit(2);
    }
    for (i = 0; i <= nargs; i++) {
        const char *s = (i == 0) ? argv0 : args[i - 1];
        if (s == NULL || strlen(s) + 1u > sizeof g_argv_bytes[0]) {
            fprintf(stderr, "harness failure: %s: cli argument %d\n", g_case, i);
            exit(2);
        }
        strcpy(g_argv_bytes[i], s);
        g_argv[i] = g_argv_bytes[i];
    }
    g_argv[nargs + 1] = NULL;   /* argv[argc] is a null pointer (C89 5.1.2.2.1) */

    cap_begin();
    rc = rank_entry(nargs + 1, g_argv);
    cap_end();
    return rc;
}

/* RANK.EXE launched out of the directory that holds the files, with whatever
 * argument list the case wants to try. */
static int cli_sw_args(const Env *e, const char *const *args, int nargs)
{
    char argv0[TPATH];

    snprintf(argv0, sizeof argv0, "%s%cRANK.EXE", e->dir, RANK_SEP);
    return cli(argv0, args, nargs);
}

/* The ordinary form: one switch, or none at all. */
static int cli_sw(const Env *e, const char *sw)
{
    const char *args[1];

    args[0] = sw;
    return cli_sw_args(e, args, (sw != NULL) ? 1 : 0);
}

/* ------------------------------------------------------------------ 画面の数 */

/* The elapsed day count has to appear as a decimal.  A plain substring search
 * would match any digit that happened to be inside a date or a score, so the
 * spelling is required to stand alone: no digit on either side of it. */
static int cap_has_decimal(long n)
{
    char want[24];
    const char *hay = cap_text();
    size_t hlen = cap_len();
    size_t wlen;
    size_t i;

    snprintf(want, sizeof want, "%ld", n);
    wlen = strlen(want);
    if (wlen == 0u || hlen < wlen) {
        return 0;
    }
    for (i = 0; i + wlen <= hlen; i++) {
        if (memcmp(hay + i, want, wlen) != 0) {
            continue;
        }
        if (i > 0u && hay[i - 1u] >= '0' && hay[i - 1u] <= '9') {
            continue;
        }
        if (i + wlen < hlen && hay[i + wlen] >= '0' && hay[i + wlen] <= '9') {
            continue;
        }
        return 1;
    }
    return 0;
}

/* ------------------------------------------------------------ ディレクトリ */

/* How many entries a directory holds, "." and ".." aside.  A-4's point is that
 * the current directory stays empty, and naming the thirteen files it must not
 * hold would still miss a fourteenth. */
static int dir_entries(const char *path)
{
    DIR *d = opendir(path);
    struct dirent *ent;
    int n = 0;

    if (d == NULL) {
        die(path);
    }
    while ((ent = readdir(d)) != NULL) {
        if (strcmp(ent->d_name, ".") == 0 || strcmp(ent->d_name, "..") == 0) {
            continue;
        }
        n++;
    }
    (void)closedir(d);
    return n;
}

/* ------------------------------------------------------------------- 時刻
 *
 * 2026-10-19 is a Monday (week 2964 of spec.md section 3.5), 2026-10-25 the
 * Sunday that closes the same week, 2026-10-26 the next Monday.  The pair
 * T_LAST / T_SAMEWK is the one place the two rules disagree, which is what makes
 * "no switch means weekly" observable at all.
 */
#define T_LAST     "2026-10-19T00:00:00"
#define T_MIDWK    "2026-10-22T12:00:00"   /* the Thursday: three days on */
#define T_SAMEWK   "2026-10-25T23:59:59"   /* the Sunday: six days on */
#define T_NEXTWK   "2026-10-26T02:00:00"   /* the next Monday: both rules cross */
#define T_NEXTWK00 "2026-10-26T00:00:00"   /* the boundary itself */
#define T_SAMEDAY  "2026-10-26T01:00:00"   /* an hour before T_NEXTWK */
#define T_FUTURE   "2027-01-01T00:00:00"
#define T_LEDGER_SAMEDAY "2026-10-08T23:00:00"   /* the live ledger's own day */

/* A directory holding the live table and a canonical state file. */
static void env_live(Env *e, const char *tag, const char *last)
{
    env_make(e, tag);
    write_bytes(e->scr, fix_scr13, FIX_SCR13_LEN);
    env_state(e, last);
}

static const char *const PLANTED_OLD = "an earlier retirement, left in place\r\n";

static const char *const LIVE_R30[2] = {
    "327400 30 01 OMZN     26/09/15 A",
    "286530 30 01 omzn     26/10/09 A"
};

/* Two rows, and neither of them is what a reset of the live table would write.
 * With the single row OMZN holds at 327400 the reset rebuilds the same byte for
 * byte, and "RANKWHO.TXT did not change" is true of a file a reset would not
 * have changed either.  Here TARO is a key the live table never mentions, so the
 * reset leaves it alone, and OMZN sits at 299370, which the live table's 327400
 * beats: a reset replaces that row.  Distinct keys, because a RANKWHO.TXT whose
 * keys collide is E8 and would refuse the run before it could reset anything. */
static const char *const LIVE_WHO[2] = {
    "500000 30 01 TARO     26/09/20 A",
    "299370 30 00 OMZN@@@@ 61/11/17 A"
};

/* The same, with all five surviving files present, so that "nothing changed" is
 * a statement about five files and not about the two an argument error might
 * plausibly leave alone.  NX93OLD.TXT is safe to plant beside a canonical state
 * file: migration only fires when NX93.TXT is a ledger (spec.md section 3.7,
 * which test_daily.c's I-33 row m covers directly). */
static void env_live_all(Env *e, const char *tag, const char *last)
{
    env_live(e, tag, last);
    write_rows_file(e->r30, LIVE_R30, 2);
    write_rows_file(e->who, LIVE_WHO, 2);
    write_text(e->oldledger, PLANTED_OLD);
}

/* ------------------------------------------------------------- 行の生成器 */

/* The hundred row RANKWHO.TXT the overflow case starts from: keys U00..U99,
 * scores 500000 - 100*i, so the floor is U99 at 490100 (test-design.md
 * section 8, U-G5).  Same generator as test_daily.c's, restated for the same
 * reason as the row helpers above. */
static const char *const *gen_who100(void)
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

/* The newcomer of I-30 variant one: a hundred and first player who lands in the
 * middle, so the row that loses its seat is the floor row rather than itself. */
#define WHO100_FLOOR "490100 30 01 U99      26/09/15 A"
#define WHO100_NEW   "495000 30 01 ZZZ      26/09/15 A"

/* A hundred player table that is one newcomer away from overflowing. */
static void env_who_full(Env *e, const char *tag)
{
    const char *scr[1];

    env_make(e, tag);
    write_rows_file(e->who, gen_who100(), RANKWHO_ROWS);
    scr[0] = WHO100_NEW;
    write_rows_file(e->scr, scr, 1);
    env_state(e, T_LAST);
}

/* ------------------------------------------------------------------- A-1
 *
 * A typo must not turn into a reset.  Every variant runs at an instant where a
 * reset really would happen, so "nothing changed" is the argument check talking
 * and not the calendar.
 */

typedef struct {
    const char *what;
    const char *const args[CLI_ARGS_MAX];
    int nargs;
} BadArgs;

#define A1_N 12

static const BadArgs A1[A1_N] = {
    { "an unknown switch",             { "/X",  NULL, NULL }, 1 },
    { "an unknown switch, lower case", { "/x",  NULL, NULL }, 1 },
    { "a word",                        { "FOO", NULL, NULL }, 1 },
    { "a dash where a slash belongs",  { "-D",  NULL, NULL }, 1 },
    { "the same, lower case",          { "-d",  NULL, NULL }, 1 },
    { "one letter too many",           { "/DD", NULL, NULL }, 1 },
    { "an empty argument",             { "",    NULL, NULL }, 1 },
    { "a bare slash",                  { "/",   NULL, NULL }, 1 },
    { "the letter with no slash",      { "D",   NULL, NULL }, 1 },
    { "two switches that disagree",    { "/D",  "/W", NULL }, 2 },
    { "the same switch twice",         { "/D",  "/D", NULL }, 2 },
    { "three switches",                { "/D",  "/W", "/D" }, 3 }
};

static void case_a1(void)
{
    int i;

    for (i = 0; i < A1_N; i++) {
        Env e;
        char tag[32];

        snprintf(tag, sizeof tag, "a1-%02d", i);
        env_live_all(&e, tag, T_LAST);
        env_snapshot(&e);
        set_clock(T_NEXTWK);

        eq_long(__FILE__, __LINE__, lbl_s("the run refuses", A1[i].what),
                cli_sw_args(&e, A1[i].args, A1[i].nargs), 1);
        is_true(__FILE__, __LINE__,
                lbl_s("and the usage names the daily switch for", A1[i].what),
                cap_has("/D"));
        is_true(__FILE__, __LINE__,
                lbl_s("and the weekly one for", A1[i].what), cap_has("/W"));
        is_true(__FILE__, __LINE__,
                lbl_s("in ASCII for", A1[i].what), cap_ascii_only());
        expect_unchanged(&e);
        expect_no_temps(&e, NULL);
    }
}

/* ------------------------------------------------------------------- A-2 */

/* The shipped GAME.BAT calls RANK with no switch at all, so an empty argument
 * list has to keep meaning weekly (spec.md section 3.1). */
static void case_a2(void)
{
    Env e;

    env_live_all(&e, "a2-idle", T_LAST);
    env_snapshot(&e);
    set_clock(T_SAMEWK);
    EQL("with no switch the Sunday run does nothing", cli_sw(&e, NULL), 0);
    expect_unchanged(&e);
    expect_no_temps(&e, NULL);

    env_live(&e, "a2-reset", T_LAST);
    env_snapshot(&e);
    set_clock(T_NEXTWK00);
    EQL("and the Monday run resets", cli_sw(&e, NULL), 0);
    OK("the table was rebuilt", !snap_same(e.scr, "scr"));
    OK("both new tables were created", file_exists(e.r30) && file_exists(e.who));
    expect_state(e.txt, T_NEXTWK00, "NX93.TXT after the reset");
    expect_no_temps(&e, NULL);
}

/* ------------------------------------------------------------------- A-3
 *
 * The exit code vocabulary is {0, 1} and stays there: GAME.BAT branches on
 * `if errorlevel 1`, which is a "greater or equal" test, so a third value would
 * read as an error wherever the branches were written in the wrong order
 * (spec.md section 2, decision 6).  The hundred and first player is the case
 * with a motive to break it - the table is full, which is worth saying but is
 * not a failure - so that run is asserted to be 0 exactly.
 *
 * The thirteen states of NX93.TXT are replayed here for their exit codes only.
 * What each one does to the five files is I-33's, in test_daily.c.
 */

#define A3_STATES 13

static const char *const A3_STATE_NAME[A3_STATES] = {
    "a: absent",
    "b: canonical, no crossing",
    "c: canonical, crossing",
    "d: old ledger, no crossing",
    "e: old ledger, crossing",
    "f: old ledger with NX93OLD.TXT",
    "g: an unreadable old ledger",
    "h: non-canonical, no crossing",
    "i: non-canonical, crossing",
    "j: lastUpdate cannot be read",
    "k: lastUpdate is in the future",
    "l: absent, with NX93OLD.TXT",
    "m: canonical, with NX93OLD.TXT"
};

static const char *const A3_STATE_CLOCK[A3_STATES] = {
    T_NEXTWK, T_NEXTWK, T_NEXTWK, T_LEDGER_SAMEDAY, T_NEXTWK, T_NEXTWK,
    T_NEXTWK, T_NEXTWK, T_NEXTWK, T_NEXTWK, T_NEXTWK, T_NEXTWK, T_NEXTWK
};

/* A ledger cut off inside its entries: lastUpdate still reads, "backups" is
 * still there, so migration fires and then finds the grammar unfinished (E12).
 * The twenty bytes removed are the four closing lines. */
#define A3_LEDGER_CUT 20u

static void plant_state(Env *e, int i)
{
    write_bytes(e->scr, fix_scr13, FIX_SCR13_LEN);
    switch (i) {
    case 0:  break;                                      /* no NX93.TXT */
    case 1:  env_state(e, T_SAMEDAY); break;
    case 2:  env_state(e, T_LAST); break;
    case 3:  write_bytes(e->txt, fix_ledger_lf, FIX_LEDGER_LF_LEN); break;
    case 4:  write_bytes(e->txt, fix_ledger_lf, FIX_LEDGER_LF_LEN); break;
    case 5:  write_bytes(e->txt, fix_ledger_lf, FIX_LEDGER_LF_LEN);
             write_text(e->oldledger, PLANTED_OLD);
             break;
    case 6:  write_bytes(e->txt, fix_ledger_lf,
                         FIX_LEDGER_LF_LEN - A3_LEDGER_CUT);
             break;
    case 7:  write_text(e->txt, "{\"lastUpdate\": \"" T_SAMEDAY "\"}\n"); break;
    case 8:  write_text(e->txt, "{\"lastUpdate\": \"" T_LAST "\"}\n"); break;
    case 9:  write_text(e->txt, "{\"nothing\": \"here\"}\n"); break;
    case 10: env_state(e, T_FUTURE); break;
    case 11: write_text(e->oldledger, PLANTED_OLD); break;
    default: env_state(e, T_LAST);
             write_text(e->oldledger, PLANTED_OLD);
             break;
    }
}

static void case_a3(void)
{
    int i;

    for (i = 0; i < A1_N; i++) {
        Env e;
        char tag[32];
        int rc;

        snprintf(tag, sizeof tag, "a3-1-%02d", i);
        env_live(&e, tag, T_LAST);
        set_clock(T_NEXTWK);
        rc = cli_sw_args(&e, A1[i].args, A1[i].nargs);
        /* Which value a rejected list gives is A-1's; here the question is only
         * whether the program has learnt a third one. */
        is_true(__FILE__, __LINE__,
                lbl_s("the exit code is 0 or 1 for", A1[i].what),
                rc == 0 || rc == 1);
    }

    {
        Env e;
        int rc;

        env_live(&e, "a3-2-idle", T_LAST);
        set_clock(T_SAMEWK);
        rc = cli_sw(&e, NULL);
        OK("the exit code is 0 or 1 for an idle run", rc == 0 || rc == 1);

        env_live(&e, "a3-2-reset", T_LAST);
        set_clock(T_NEXTWK);
        rc = cli_sw(&e, NULL);
        OK("the exit code is 0 or 1 for a reset", rc == 0 || rc == 1);
    }

    {
        Env e;

        env_who_full(&e, "a3-w1");
        set_clock(T_NEXTWK);
        EQL("a full per-user table is a warning, not a failure",
            cli_sw(&e, "/D"), 0);
        EQL("and the row that lost its seat is on screen once",
            cap_count(WHO100_FLOOR), 1);
    }

    for (i = 0; i < A3_STATES; i++) {
        Env e;
        char tag[32];
        int rc;

        snprintf(tag, sizeof tag, "a3-33-%c", (char)('a' + i));
        env_make(&e, tag);
        plant_state(&e, i);
        set_clock(A3_STATE_CLOCK[i]);
        rc = cli_sw(&e, "/D");
        is_true(__FILE__, __LINE__,
                lbl_s("the exit code is 0 or 1 for", A3_STATE_NAME[i]),
                rc == 0 || rc == 1);
    }
}

/* ------------------------------------------------------------------- A-4
 *
 * DOSSHELL can start RANK.EXE with a current directory other than A:\GAME\NX93,
 * and without argv[0] the program would build a fresh NX93.SCR wherever it
 * happened to be launched from - silently, since an absent table is not an
 * error (spec.md section 7 D.3).
 */
static void case_a4(void)
{
    char cwd[TPATH];
    char elsewhere[TPATH];
    Env e;

    if (getcwd(cwd, sizeof cwd) == NULL) {
        die("getcwd");
    }

    env_live(&e, "a4-prog", T_LAST);
    case_dir("a4-cwd", elsewhere);
    set_clock(T_NEXTWK);

    if (chdir(elsewhere) != 0) {
        die(elsewhere);
    }
    EQL("the run works where argv[0] says, not where it was started",
        cli_sw(&e, "/D"), 0);
    if (chdir(cwd) != 0) {
        die(cwd);
    }

    OK("the table beside RANK.EXE was rebuilt", file_exists(e.r30));
    OK("and so was the per-user table", file_exists(e.who));
    expect_state(e.txt, T_NEXTWK, "NX93.TXT beside RANK.EXE");
    EQL("while the current directory is still empty", dir_entries(elsewhere), 0);
    expect_no_temps(&e, NULL);

    /* No separator in argv[0] means no directory part, which is what DOS 2.x
     * leaves behind as well; then the current directory is the right answer. */
    env_live(&e, "a4-bare", T_LAST);
    set_clock(T_NEXTWK);
    if (chdir(e.dir) != 0) {
        die(e.dir);
    }
    {
        const char *args[1];
        args[0] = "/D";
        EQL("argv[0] without a directory part falls back to the current one",
            cli("RANK.EXE", args, 1), 0);
    }
    if (chdir(cwd) != 0) {
        die(cwd);
    }
    OK("the current directory's table was rebuilt",
       file_exists(e.r30) && file_exists(e.who));
    expect_state(e.txt, T_NEXTWK, "NX93.TXT in the current directory");
    expect_no_temps(&e, NULL);
}

/* ------------------------------------------------------------------- A-5
 *
 * Which rule the run went by is not recorded anywhere (decision: no mode field
 * in NX93.TXT), so the screen is the only place it is ever said.  The elapsed
 * day count is the other half: spec.md section 3.2 keeps "last reset was N
 * day(s) ago" from the current program, which is what keeps rank_elapsed_days
 * and rank_put_days alive.
 *
 * The design also lists a twenty day variant.  It is not reachable and is named
 * here rather than quietly dropped: an idle run is by definition one that
 * crossed no boundary, so the daily rule allows a count of 0 only and the weekly
 * rule at most 6 (a Monday to the Sunday that closes its week).  Twenty days
 * apart, both rules reset, and a reset is not the message that carries the
 * count.  The last block below asserts that reset rather than the count.
 */
#define A5_UNREACHABLE_DAYS 20

static void case_a5(void)
{
    static const struct { const char *what; const char *last; const char *now;
                          long days; }
    idle[3] = {
        { "the same day",        T_SAMEDAY, T_NEXTWK, 0L },
        { "three days earlier",  T_LAST,    T_MIDWK,  3L },
        { "six days earlier",    T_LAST,    T_SAMEWK, 6L }
    };
    Env e;
    int i;

    /* The mode lexeme, on an idle run and on a reset, in both modes.  Asserting
     * that the other lexeme is absent is what stops a message that prints both. */
    env_live(&e, "a5-w-idle", T_LAST);
    set_clock(T_SAMEWK);
    EQL("an idle weekly run succeeds", cli_sw(&e, "/W"), 0);
    OK("it says weekly", cap_has("weekly"));
    OK("and does not say daily", !cap_has("daily"));
    OK("in ASCII", cap_ascii_only());

    env_live(&e, "a5-d-idle", T_SAMEDAY);
    set_clock(T_NEXTWK);
    EQL("an idle daily run succeeds", cli_sw(&e, "/D"), 0);
    OK("it says daily", cap_has("daily"));
    OK("and does not say weekly", !cap_has("weekly"));

    env_live(&e, "a5-w-reset", T_LAST);
    set_clock(T_NEXTWK);
    EQL("a weekly reset succeeds", cli_sw(&e, "/W"), 0);
    OK("it says weekly too", cap_has("weekly"));
    OK("and still not daily", !cap_has("daily"));

    env_live(&e, "a5-d-reset", T_LAST);
    set_clock(T_SAMEWK);
    EQL("a daily reset succeeds", cli_sw(&e, "/D"), 0);
    OK("it says daily too", cap_has("daily"));
    OK("and still not weekly", !cap_has("weekly"));

    /* The elapsed day count, for the three counts an idle run can have. */
    for (i = 0; i < 3; i++) {
        char tag[32];

        snprintf(tag, sizeof tag, "a5-d%ld", idle[i].days);
        env_live(&e, tag, idle[i].last);
        set_clock(idle[i].now);
        eq_long(__FILE__, __LINE__,
                lbl_s("an idle run succeeds with the reset at", idle[i].what),
                cli_sw(&e, "/W"), 0);
        is_true(__FILE__, __LINE__,
                lbl_s("and puts the day count on screen for", idle[i].what),
                cap_has_decimal(idle[i].days));
    }

    /* A migration says so, because the retired ledger is now under a name the
     * user has to know about (spec.md section 3.2). */
    env_make(&e, "a5-migr");
    write_bytes(e.scr, fix_scr13, FIX_SCR13_LEN);
    write_bytes(e.txt, fix_ledger_lf, FIX_LEDGER_LF_LEN);
    set_clock(T_NEXTWK);
    EQL("a migrating reset succeeds", cli_sw(&e, "/D"), 0);
    OK("it names the file the old ledger was retired to",
       cap_has("NX93OLD.TXT"));
    OK("and says daily", cap_has("daily"));
    OK("in ASCII", cap_ascii_only());
    expect_no_temps(&e, NULL);

    /* Why twenty is unreachable, as an observation rather than a comment: the
     * gap really is twenty days by the contract-unchanged counter, and a run
     * across it resets instead of going idle. */
    {
        RankTime from;
        RankTime to;

        if (rank_time_parse("2026-10-06T02:00:00", &from) != 0
         || rank_time_parse(T_NEXTWK, &to) != 0) {
            fprintf(stderr, "harness failure: %s: bad anchor\n", g_case);
            exit(2);
        }
        EQL("the gap the design asked a count for is twenty days",
            rank_elapsed_days(&from, &to), (long)A5_UNREACHABLE_DAYS);
    }
    env_live(&e, "a5-20d", "2026-10-06T02:00:00");
    set_clock(T_NEXTWK);
    EQL("and a weekly run across it resets rather than going idle",
        cli_sw(&e, "/W"), 0);
    OK("the tables were rebuilt, so that really was a reset",
       file_exists(e.r30) && file_exists(e.who));
    expect_no_temps(&e, NULL);
}

/* ------------------------------------------------------------------- A-6
 *
 * Eleven error conditions and two warnings, thirteen screens, no two of them
 * the same bytes.  The pair with the strongest motive to collapse into one
 * sentence is E4 and E10 - both are "NX93.TXT is not what I expected" - and the
 * pair that actually loses a user's records if it does is W1 and W2, since both
 * are warnings that end in exit code 0.
 *
 * spec.md section 3.2 leaves the wording to the implementation, so the oracle is
 * differential plus the two properties the spec does fix: the RANK: prefix and
 * ASCII only (Japanese belongs in GAME.BAT's ECHO).  The exit code of each
 * input is asserted as well - not for its own sake, which is A-3's, but because
 * a setup that failed to reach the condition it was built for would otherwise
 * let this case pass while comparing thirteen screens of the wrong thing.
 */

#define A6_N 13

static const char *const A6_NAME[A6_N] = {
    "E1 an unknown switch",
    "E2 a directory name too long",
    "E4 lastUpdate cannot be read",
    "E5 lastUpdate is in the future",
    "E6 NX93.SCR has a row that cannot be read",
    "E7 RANK30.TXT has a row that cannot be read",
    "E8 RANKWHO.TXT holds two rows for one player",
    "E9 a read-only file",
    "E10 NX93.TXT is neither canonical nor a ledger",
    "E11 NX93OLD.TXT is already there",
    "E12 the old ledger is damaged",
    "W1 the per-user table is full",
    "W2 a retained copy is discarded"
};

static const int A6_EXIT[A6_N] = { 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 0, 0 };

/* A row five bytes long: outside the accepted 27..35, so the reader rejects the
 * file rather than skipping the line (spec.md section 3.4). */
#define A6_BAD_ROW "bogus\r\n"

/* E2 needs a directory name that fits rank_dir_from_argv0's buffer and then
 * does not fit a joined path: 120 + 1 + 11 + 1 is 133, over RANK_PATH_MAX.  The
 * directory is never opened, so it does not need to exist. */
#define A6_LONG_DIR 120

static int a6_run(int i)
{
    Env e;
    char tag[32];
    int rc;

    snprintf(tag, sizeof tag, "a6-%02d", i);

    switch (i) {
    case 0:
        env_live(&e, tag, T_LAST);
        set_clock(T_NEXTWK);
        return cli_sw(&e, "/X");

    case 1: {
        char argv0[A6_LONG_DIR + 16];
        const char *args[1];
        int k;

        for (k = 0; k < A6_LONG_DIR; k++) {
            argv0[k] = 'd';
        }
        argv0[A6_LONG_DIR] = RANK_SEP;
        strcpy(argv0 + A6_LONG_DIR + 1, "RANK.EXE");
        args[0] = "/D";
        set_clock(T_NEXTWK);
        return cli(argv0, args, 1);
    }

    case 2:
        env_make(&e, tag);
        write_bytes(e.scr, fix_scr13, FIX_SCR13_LEN);
        write_text(e.txt, "{\"nothing\": \"here\"}\n");
        set_clock(T_NEXTWK);
        return cli_sw(&e, "/D");

    case 3:
        env_live(&e, tag, T_FUTURE);
        set_clock(T_NEXTWK);
        return cli_sw(&e, "/D");

    case 4:
        env_make(&e, tag);
        write_text(e.scr, A6_BAD_ROW);
        env_state(&e, T_LAST);
        set_clock(T_NEXTWK);
        return cli_sw(&e, "/D");

    case 5:
        env_live(&e, tag, T_LAST);
        write_text(e.r30, A6_BAD_ROW);
        set_clock(T_NEXTWK);
        return cli_sw(&e, "/D");

    case 6:
        env_live(&e, tag, T_LAST);
        {
            const char *two[2];
            two[0] = "327400 30 01 OMZN     26/09/15 A";
            two[1] = "299370 30 00 OMZN@@@@ 61/11/17 A";
            write_rows_file(e.who, two, 2);
        }
        set_clock(T_NEXTWK);
        return cli_sw(&e, "/D");

    case 7:
        env_live(&e, tag, T_LAST);
        set_clock(T_NEXTWK);
        if (chmod(e.scr, 0444) != 0) {
            die(e.scr);
        }
        rc = cli_sw(&e, "/D");
        if (chmod(e.scr, 0644) != 0) {
            die(e.scr);
        }
        return rc;

    case 8:
        env_make(&e, tag);
        write_bytes(e.scr, fix_scr13, FIX_SCR13_LEN);
        write_text(e.txt, "{\"lastUpdate\": \"" T_LAST "\"}\n");
        set_clock(T_NEXTWK);
        return cli_sw(&e, "/D");

    case 9:
        env_make(&e, tag);
        write_bytes(e.scr, fix_scr13, FIX_SCR13_LEN);
        write_bytes(e.txt, fix_ledger_lf, FIX_LEDGER_LF_LEN);
        write_text(e.oldledger, PLANTED_OLD);
        set_clock(T_NEXTWK);
        return cli_sw(&e, "/D");

    case 10:
        env_make(&e, tag);
        write_bytes(e.scr, fix_scr13, FIX_SCR13_LEN);
        write_bytes(e.txt, fix_ledger_lf, FIX_LEDGER_LF_LEN - A3_LEDGER_CUT);
        set_clock(T_NEXTWK);
        return cli_sw(&e, "/D");

    case 11:
        env_who_full(&e, tag);
        set_clock(T_NEXTWK);
        return cli_sw(&e, "/D");

    default: {
        /* W2 takes three runs.  A clean reset first, so that all four files
         * exist and a stranded commit can actually strand one (a three stage
         * commit only moves the outgoing file aside when the target is already
         * there).  Then the strand, then a retry that leaves the copy alone
         * because the target is missing, and only the crossing after that finds
         * the copy in the way and discards it.  The capture kept is the third
         * run's, which is the one that carries the warning. */
        const char *newcomer[1];

        env_live(&e, tag, T_LAST);
        set_clock(T_NEXTWK);
        if (cli_sw(&e, "/D") != 0) {
            is_true(__FILE__, __LINE__, "the W2 baseline reset succeeded", 0);
            return -1;
        }
        newcomer[0] = "999999 30 01 NEWP     26/10/26 A";
        write_rows_file(e.scr, newcomer, 1);

        set_clock("2026-10-27T02:00:00");
        rank_test_fault = RANK_FAULT_R30_STRAND;
        rc = cli_sw(&e, "/D");
        rank_test_fault = RANK_FAULT_NONE;
        if (rc != 1 || !file_exists(e.old30)) {
            is_true(__FILE__, __LINE__,
                    "the W2 baseline stranded RANK30.TXT", 0);
            return -1;
        }
        if (cli_sw(&e, "/D") != 0) {
            is_true(__FILE__, __LINE__, "the W2 baseline retry succeeded", 0);
            return -1;
        }
        set_clock("2026-10-28T02:00:00");
        rc = cli_sw(&e, "/D");
        OK("the retained copy was discarded", !file_exists(e.old30));
        EQL("and named exactly once", cap_count("RANKOLD.30"), 1);
        return rc;
    }
    }
}

static void case_a6(void)
{
    char *msg[A6_N];
    size_t len[A6_N];
    int i;
    int j;

    for (i = 0; i < A6_N; i++) {
        msg[i] = NULL;
        len[i] = 0;
    }

    for (i = 0; i < A6_N; i++) {
        int rc = a6_run(i);

        eq_long(__FILE__, __LINE__, lbl_s("the exit code for", A6_NAME[i]),
                rc, A6_EXIT[i]);
        is_true(__FILE__, __LINE__, lbl_s("the screen is ASCII for", A6_NAME[i]),
                cap_ascii_only());
        is_true(__FILE__, __LINE__,
                lbl_s("and carries the RANK: prefix for", A6_NAME[i]),
                cap_has("RANK:"));
        msg[i] = read_file(g_cap_path, &len[i]);
        if (msg[i] == NULL) {
            die(g_cap_path);
        }
    }

    for (i = 0; i < A6_N; i++) {
        for (j = i + 1; j < A6_N; j++) {
            char what[256];

            snprintf(what, sizeof what, "%s reads differently from %s",
                     A6_NAME[i], A6_NAME[j]);
            is_true(__FILE__, __LINE__, what,
                    !(len[i] == len[j] && memcmp(msg[i], msg[j], len[i]) == 0));
        }
    }

    for (i = 0; i < A6_N; i++) {
        free(msg[i]);
    }
}

/* ------------------------------------------------------------------- A-7
 *
 * E8 is the one error a user can walk into by hand editing: decision 8 folds
 * OMZN and OMZN@@@@ onto one key, so a RANKWHO.TXT that holds a row for each is
 * two rows for one player and the file is refused.  Every run after that stops
 * at the same place, which means the message is the user's only way out, and
 * GAME.BAT pauses on it daily until they find it.
 *
 * The machine oracle is the pair of names (spec.md section 5, E8): the file to
 * get rid of, and the file the records are still in.  Then the advice is taken
 * and observed to work, which is the only part of a message's correctness that
 * does not have to be left to a human reviewer.
 *
 * The two rows planted here are rows of fix_scr13, which is deliberate.  The
 * second half of E8's advice - "the records are still in RANK30.TXT" - holds for
 * a row that is also an NX93.SCR candidate, because that is where the all-time
 * table's input comes from (spec.md section 3.6).  It does not hold for a row
 * that only ever lived in RANKWHO.TXT: nothing carries such a row into the
 * all-time table, which spec.md section 3.2 states outright.  So the planted
 * rows are chosen from the ones where the advice is literally true, and the two
 * assertions at the end observe that rather than assuming it.
 */
static void case_a7(void)
{
    static const char *const two[2] = {
        "327400 30 01 OMZN     26/09/15 A",
        "299370 30 00 OMZN@@@@ 61/11/17 A"
    };
    static const char *const one[1] = {
        "327400 30 01 OMZN     26/09/15 A"
    };
    Env e;
    char *r30;
    size_t r30l;

    env_live(&e, "a7", T_LAST);
    write_rows_file(e.who, two, 2);
    env_snapshot(&e);
    set_clock(T_NEXTWK);

    EQL("two rows for one player are refused", cli_sw(&e, "/D"), 1);
    expect_unchanged(&e);
    expect_no_temps(&e, NULL);

    OK("the message names the file that is in the way", cap_has("RANKWHO.TXT"));
    OK("and the file the records are still in", cap_has("RANK30.TXT"));
    OK("in ASCII", cap_ascii_only());

    /* Take the advice. */
    if (unlink(e.who) != 0) {
        die(e.who);
    }
    EQL("deleting it is enough: the next run completes", cli_sw(&e, "/D"), 0);
    expect_rows_file(e.who, one, 1, "RANKWHO.TXT was built again");
    expect_state(e.txt, T_NEXTWK, "NX93.TXT after the recovered run");
    expect_no_temps(&e, NULL);

    /* And in this configuration the advice's second half is true as well: both
     * rows that were in the refused file are in the all-time table, because both
     * are rows of NX93.SCR. */
    r30 = read_file(e.r30, &r30l);
    if (is_true(__FILE__, __LINE__, "RANK30.TXT exists afterwards", r30 != NULL)) {
        EQL("the surviving row is in the all-time table",
            mem_count(r30, r30l, two[0]), 1);
        EQL("and so is the row that was folded into it",
            mem_count(r30, r30l, two[1]), 1);
    }
    free(r30);
}

/* -------------------------------------------------------------------- main */

static const Case CASES[] = {
    { "A-1 usage-rejects",          case_a1 },
    { "A-2 backward-compatible",    case_a2 },
    { "A-3 exit-vocabulary",        case_a3 },
    { "A-4 dir-from-argv0",         case_a4 },
    { "A-5 mode-lexeme-and-days",   case_a5 },
    { "A-6 message-differential",   case_a6 },
    { "A-7 e8-recovery-named",      case_a7 }
};

/* The longest case tag below is ten bytes, the longest file name a run opens is
 * eleven, and there is a separator on either side of the tag.  rank_run refuses
 * a directory whose joined paths do not fit RANK_PATH_MAX and reports E2, so a
 * scratch directory that is merely too deep would turn most of this suite red
 * for a reason that has nothing to do with RANK.C.  Saying so is better than
 * letting it look like a finding; run-daily.sh's own scratch path leaves ample
 * room.  A-4 and A-6 build argv[0] as <dir>/RANK.EXE, which only needs the
 * directory part to fit, so this bound covers them too. */
#define TAG_MAX 16

int main(int argc, char **argv)
{
    if (argc >= 2
     && strlen(argv[1]) + 1u + (size_t)TAG_MAX + 1u + 11u + 1u
        > (size_t)RANK_PATH_MAX) {
        fprintf(stderr, "test_cli: the scratch directory is too deep: a case "
                        "directory plus RANKWHO.TXT must fit %d bytes, which "
                        "leaves %d for the path given.\n",
                RANK_PATH_MAX, RANK_PATH_MAX - TAG_MAX - 14);
        return 2;
    }
    /* A-4 changes the current directory, which is the only way to tell "beside
     * RANK.EXE" from "where it was started".  Everything the harness opens -
     * the capture file, the snapshot set, each case directory - is reached
     * through this path, so a relative one would stop resolving mid-case.  Fail
     * on it here with the reason, in the spirit of the check above and of H4,
     * rather than leaving a stray open() to report a missing file. */
    if (argc >= 2 && argv[1][0] != '/') {
        fprintf(stderr, "test_cli: the scratch directory must be an absolute "
                        "path: A-4 runs with a different current directory.\n");
        return 2;
    }
    return harness_main("test_cli", argc, argv,
                        CASES, sizeof CASES / sizeof CASES[0]);
}
