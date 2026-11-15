/* RANK.C - weekly reset of the NyaHaX'93 ranking table (A:\GAME\NX93).
 *
 * Cross-build RANK.EXE for DOS on a host with:       nx93rank/build.sh
 * Build on the PC-9801 with Microsoft QuickC 2.00:   QCL RANK.C
 * Build on a host for the regression suite with:     clang -x c -std=c89
 *
 * The cross build is the usual one and QuickC is the fallback, so this file
 * has to keep passing both.  Note that gcc and clang read the uppercase .C
 * extension as C++, which silently discards -std=c89; every build path here
 * passes -x c or copies the file to a lowercase name.
 *
 * The rules this program implements live in
 * .work/plans/nx93-ranking-reset/plan.md; the contract it is tested against
 * lives in .work/plans/nx93-ranking-reset/test-design.md.
 *
 * Constraints that shape the whole file, none of which a compiler will
 * remind you about:
 *
 *   - ASCII only, in code and in comments.  The source is stored on the 98
 *     as Shift-JIS, and the second byte of characters such as SO, HYOU and
 *     NOU is 0x5C, which QuickC reads as the start of an escape sequence.
 *     Japanese text belongs in GAME.BAT, where ECHO does not escape.
 *   - C89 only: no // comments, no declarations after statements, no
 *     variable length arrays.  QuickC 2.00 predates C99 by a decade.
 *   - int is 16 bit on the 98.  Scores are long, and every intermediate
 *     value in the calendar arithmetic is long as well.
 *   - No heap.  The ledger is streamed, never loaded.  Tables live in
 *     static storage because the default DOS stack is about 2 KB and one
 *     RankTable is already most of that; DOS is single tasked and the test
 *     harness is single threaded, so there is no sharing to guard.
 *   - No <ctype.h>.  Name fields may hold bytes >= 0x80, and passing those
 *     to the standard case folding functions is undefined behaviour in
 *     ANSI C, so the folding here is done by hand over ASCII a-z only.
 *   - No calendar library arithmetic for day differences.  In a zone
 *     with daylight saving, seven calendar days can be 6.96 days of elapsed
 *     time, which would make the reset skip a week.  Civil-day arithmetic
 *     is done here instead.
 *   - Ranking lines are never passed to a printf-family format argument.
 *     The stock ranking is held by a player called Bio_100%.
 */

#include <stdio.h>
#include <stddef.h>
#include <string.h>

#ifndef RANK_TEST_CLOCK
#include <time.h>
#endif

#define RANK_ROWS       10   /* rows the game keeps */
#define RANK_NAME_LEN    8
#define RANK_MAX_LINE   63   /* line buffer; only lengths 27..35 are accepted */
#define RANK_PATH_MAX  128

/* A reset happens once this many whole days have passed. */
#define RANK_PERIOD_DAYS 7L

/* Shortest and longest acceptable ranking line.  Everything but the score
 * is fixed width, so the length pins down the score width:
 *   <score> SP <lv:2> SP <miss:2> SP <name:8> SP <date:8> SP <mode:1>
 * which is 26 bytes plus 1..9 score digits. */
#define RANK_FIXED_LEN  26
#define RANK_MIN_LINE   (RANK_FIXED_LEN + 1)
#define RANK_MAX_SCORE_DIGITS 9

/* Longest run of whitespace tolerated between "backups": [ and the first
 * element.  Anything longer is not a ledger this program wrote, and the
 * comma placement cannot be decided without buffering it. */
#define RANK_WS_MAX 64

/* Are we building for MS-DOS?  No single macro is common to every compiler
 * that targets it: QuickC 2.00 defines MSDOS, while ia16-elf-gcc defines
 * __MSDOS__ and _DOS but not the bare name.  Hosted compilers define none of
 * them.  Getting this wrong is silent - the program would keep building, with
 * '/' for a separator and "A:" no longer read as a drive prefix - so every
 * site that differs between DOS and the host keys off this one macro and the
 * list of spellings is never repeated. */
#if defined(MSDOS) || defined(__MSDOS__) || defined(_DOS)
#define RANK_DOS 1
#endif

/* Path separator. */
#ifdef RANK_DOS
#define RANK_SEP '\\'
#else
#define RANK_SEP '/'
#endif

typedef struct {
    int year;    /* four digit */
    int month;   /* 1-12 */
    int day;     /* 1-31 */
    int hour;    /* 0-23 */
    int minute;  /* 0-59 */
    int second;  /* 0-59 */
} RankTime;

typedef struct {
    char raw[RANK_MAX_LINE + 1];   /* input bytes verbatim, no CR/LF/0x1A */
    long score;                    /* long: int is 16 bit on the 98 */
    char name[RANK_NAME_LEN + 1];  /* the 8 byte name field, unaltered */
    char key[RANK_NAME_LEN + 1];   /* name, 0x20 and trailing marks trimmed,
                                    * ASCII a-z folded up; see rank_fold_key */
} RankRow;

typedef struct {
    RankRow row[RANK_ROWS * 2];
    int count;
} RankTable;

#ifdef RANK_TEST_CLOCK
RankTime rank_test_now;
#endif

#ifdef RANK_TEST_FAULT
typedef enum {
    RANK_FAULT_NONE = 0,
    RANK_FAULT_LEDGER_WRITE,
    RANK_FAULT_LEDGER_COMMIT,
    RANK_FAULT_LEDGER_RENAME,
    RANK_FAULT_SCR_WRITE,
    RANK_FAULT_SCR_COMMIT,
    RANK_FAULT_SCR_RENAME
} RankFault;

RankFault rank_test_fault = RANK_FAULT_NONE;

static int rank_fault(RankFault which)
{
    return rank_test_fault == which;
}
#else
/* The argument is discarded, so its identifier never reaches the compiler
 * and the fault enum need not exist in the DOS build. */
#define rank_fault(which) 0
#endif

/* ------------------------------------------------------------------ text */

/* Console output only; a failed write to the screen has nowhere to go. */
static void rank_say(const char *msg)
{
    fputs(msg, stdout);
    fputc('\n', stdout);
}

/* Decimal form of days, into a buffer of at least 16 bytes.  Written by
 * hand so that no ranking data can ever reach a format string. */
static void rank_put_days(long days, char *buf)
{
    char tmp[16];
    long v;
    int n;
    int k;

    v = days;
    n = 0;
    k = 0;
    if (v < 0L) {
        buf[k] = '-';
        k++;
        v = -v;
    }
    if (v == 0L) {
        tmp[n] = '0';
        n++;
    }
    while (v > 0L) {
        tmp[n] = (char)('0' + (int)(v % 10L));
        n++;
        v /= 10L;
    }
    while (n > 0) {
        n--;
        buf[k] = tmp[n];
        k++;
    }
    buf[k] = '\0';
}

/* ------------------------------------------------------------------ time */

/* Floor division by a positive divisor, independent of how the C
 * implementation truncates negative quotients (C89 leaves that
 * implementation defined). */
static long rank_floor_div(long a, long b)
{
    if (a >= 0L) {
        return a / b;
    }
    return -(((-a) + b - 1L) / b);
}

long rank_days_from_civil(int y, int m, int d)
{
    long ly;
    long era;
    long yoe;
    long doy;
    long doe;

    /* Howard Hinnant's days_from_civil, proleptic Gregorian, 1970-01-01 = 0.
     * March is treated as the first month so that the leap day falls at the
     * end of the year.  Every term is long: yoe * 365 alone reaches 145635,
     * which does not fit in a 16 bit int. */
    ly = (long)y - ((m <= 2) ? 1L : 0L);
    era = rank_floor_div(ly, 400L);
    yoe = ly - era * 400L;                                   /* [0, 399] */
    doy = (153L * (long)(m + ((m > 2) ? -3 : 9)) + 2L) / 5L + (long)d - 1L;
    doe = yoe * 365L + yoe / 4L - yoe / 100L + doy;           /* [0, 146096] */
    return era * 146097L + doe - 719468L;
}

static int rank_is_leap(int y)
{
    if ((y % 4) != 0) {
        return 0;
    }
    if ((y % 100) != 0) {
        return 1;
    }
    return (y % 400) == 0;
}

static int rank_days_in_month(int y, int m)
{
    static const int len[12] = { 31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31 };

    if (m < 1 || m > 12) {
        return 0;
    }
    if (m == 2 && rank_is_leap(y)) {
        return 29;
    }
    return len[m - 1];
}

static int rank_digit(int c)
{
    if (c < '0' || c > '9') {
        return -1;
    }
    return c - '0';
}

static int rank_two(const char *s)
{
    int hi;
    int lo;

    hi = rank_digit((unsigned char)s[0]);
    lo = rank_digit((unsigned char)s[1]);
    if (hi < 0 || lo < 0) {
        return -1;
    }
    return hi * 10 + lo;
}

/* "YYYY-MM-DDTHH:MM:SS", exactly 19 bytes, fully range checked. */
int rank_time_parse(const char *s, RankTime *out)
{
    int y;
    int i;
    int v;
    RankTime t;

    if (s == NULL || out == NULL) {
        return -1;
    }
    if (strlen(s) != 19u) {
        return -1;
    }
    if (s[4] != '-' || s[7] != '-' || s[10] != 'T' || s[13] != ':' || s[16] != ':') {
        return -1;
    }
    y = 0;
    for (i = 0; i < 4; i++) {
        v = rank_digit((unsigned char)s[i]);
        if (v < 0) {
            return -1;
        }
        y = y * 10 + v;
    }
    t.year = y;
    t.month = rank_two(s + 5);
    t.day = rank_two(s + 8);
    t.hour = rank_two(s + 11);
    t.minute = rank_two(s + 14);
    t.second = rank_two(s + 17);
    if (t.month < 1 || t.month > 12) {
        return -1;
    }
    if (t.day < 1 || t.day > rank_days_in_month(t.year, t.month)) {
        return -1;
    }
    if (t.hour < 0 || t.hour > 23) {
        return -1;
    }
    if (t.minute < 0 || t.minute > 59) {
        return -1;
    }
    if (t.second < 0 || t.second > 59) {
        return -1;
    }
    *out = t;
    return 0;
}

static void rank_put2(char *p, int v)
{
    p[0] = (char)('0' + (v / 10) % 10);
    p[1] = (char)('0' + v % 10);
}

void rank_time_format(const RankTime *t, char *buf20)
{
    if (t == NULL || buf20 == NULL) {
        return;
    }
    buf20[0] = (char)('0' + (t->year / 1000) % 10);
    buf20[1] = (char)('0' + (t->year / 100) % 10);
    buf20[2] = (char)('0' + (t->year / 10) % 10);
    buf20[3] = (char)('0' + t->year % 10);
    buf20[4] = '-';
    rank_put2(buf20 + 5, t->month);
    buf20[7] = '-';
    rank_put2(buf20 + 8, t->day);
    buf20[10] = 'T';
    rank_put2(buf20 + 11, t->hour);
    buf20[13] = ':';
    rank_put2(buf20 + 14, t->minute);
    buf20[16] = ':';
    rank_put2(buf20 + 17, t->second);
    buf20[19] = '\0';
}

/* Whole days from `from` to `to`; negative when `to` is earlier.  The time
 * of day only ever removes a day, never adds one, so 6d23h59m59s is 6. */
long rank_elapsed_days(const RankTime *from, const RankTime *to)
{
    long days;
    long sf;
    long st;

    if (from == NULL || to == NULL) {
        return 0L;
    }
    days = rank_days_from_civil(to->year, to->month, to->day)
         - rank_days_from_civil(from->year, from->month, from->day);
    sf = from->hour * 3600L + from->minute * 60L + (long)from->second;
    st = to->hour * 3600L + to->minute * 60L + (long)to->second;
    if (st < sf) {
        days -= 1L;
    }
    return days;
}

#ifdef RANK_TEST_CLOCK
int rank_now(RankTime *out)
{
    if (out == NULL) {
        return -1;
    }
    *out = rank_test_now;
    return 0;
}
#else
/* localtime() is used to read the wall clock, which is the only portable
 * way to get local civil time.  It is deliberately not used for any
 * arithmetic; see rank_elapsed_days. */
int rank_now(RankTime *out)
{
    time_t now;
    struct tm *lt;

    if (out == NULL) {
        return -1;
    }
    now = time((time_t *)0);
    if (now == (time_t)-1) {
        return -1;
    }
    lt = localtime(&now);
    if (lt == NULL) {
        return -1;
    }
    out->year = lt->tm_year + 1900;
    out->month = lt->tm_mon + 1;
    out->day = lt->tm_mday;
    out->hour = lt->tm_hour;
    out->minute = lt->tm_min;
    out->second = lt->tm_sec;
    if (out->second > 59) {
        out->second = 59;   /* a leap second must still round trip */
    }
    return 0;
}
#endif

/* ------------------------------------------------------------------ rows */

/* The identity of a player, derived from the 8 byte name field in four steps:
 *
 *   1. trim 0x20 from both ends; call what is left the trimmed field;
 *   2. from the end of it, drop every byte that is ASCII (< 0x80) and not
 *      A-Z a-z 0-9, and stop at the first byte >= 0x80;
 *   3. if that left nothing, use the whole trimmed field instead;
 *   4. fold ASCII a-z upwards.
 *
 * Step 2 is there because players mark their own name - omzn@@@@ and
 * omzn++++ are the same person as omzn - while omzn-2 is somebody else,
 * because a trailing digit is not a mark.  Matching on a prefix instead
 * would not work: omzn is a prefix of omzn@@@@ which is a prefix of
 * omzn@@@@omzn, so "same player" would stop being transitive and would
 * depend on the order the rows happen to be in.
 *
 * Stopping at 0x80 in step 2 is required, not an optimisation.  A name typed
 * on the 98 is Shift-JIS, and its last byte is almost always >= 0x80; were
 * those dropped as "not alphanumeric", every Japanese name would collapse to
 * the empty key and every one of those players would become the same person.
 * The comparison is done on unsigned char for the same reason that the fold
 * in step 4 is: on a compiler where char is signed, 0xE0 is negative and
 * would compare below 0x80.
 *
 * Step 3 keeps a name that is nothing but marks distinguishable: @@@@ and
 * ---- both lose everything in step 2, and without the rescue they would
 * share the empty key with each other and with a name of eight spaces.
 */
static void rank_fold_key(const char *name, char *key)
{
    int b;
    int e;
    int t;
    int i;
    unsigned char c;

    b = 0;
    e = RANK_NAME_LEN;
    while (b < e && name[b] == ' ') {
        b++;
    }
    while (e > b && name[e - 1] == ' ') {
        e--;
    }

    t = e;
    while (t > b) {
        c = (unsigned char)name[t - 1];
        if (c >= 0x80) {
            break;
        }
        if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z')
         || (c >= '0' && c <= '9')) {
            break;
        }
        t--;
    }
    if (t == b) {
        t = e;
    }

    for (i = 0; b + i < t; i++) {
        c = (unsigned char)name[b + i];
        if (c >= 'a' && c <= 'z') {
            c = (unsigned char)(c - 'a' + 'A');
        }
        key[i] = (char)c;
    }
    key[i] = '\0';
}

/* A line is accepted on its shape alone.  The separators are counted from
 * the end because the score is the only variable width field: the game
 * writes 98840 where the shipped table has 080000.  The lv, miss, date and
 * mode fields are not validated, because the game's own formatting varies
 * and rejecting a line we cannot rewrite loses it. */
int rank_parse_line(const char *line, RankRow *out)
{
    size_t len;
    int ndigit;
    int i;
    int v;
    long score;

    if (line == NULL || out == NULL) {
        return -1;
    }
    len = strlen(line);
    if (len < (size_t)RANK_MIN_LINE
     || len > (size_t)(RANK_FIXED_LEN + RANK_MAX_SCORE_DIGITS)) {
        return -1;
    }
    if (line[len - 2] != ' ' || line[len - 11] != ' ' || line[len - 20] != ' '
     || line[len - 23] != ' ' || line[len - 26] != ' ') {
        return -1;
    }

    ndigit = (int)(len - (size_t)RANK_FIXED_LEN);
    score = 0L;
    for (i = 0; i < ndigit; i++) {
        v = rank_digit((unsigned char)line[i]);
        if (v < 0) {
            return -1;
        }
        score = score * 10L + (long)v;   /* at most 9 digits: fits in long */
    }

    memcpy(out->raw, line, len);
    out->raw[len] = '\0';
    out->score = score;
    memcpy(out->name, line + (len - 19), (size_t)RANK_NAME_LEN);
    out->name[RANK_NAME_LEN] = '\0';
    rank_fold_key(out->name, out->key);
    return 0;
}

const char *rank_default_line(int i)
{
    /* The ranking shipped in nx93_110.lzh, byte for byte. */
    static const char *const lines[RANK_ROWS] = {
        "200000 29 00 Bio_100% 93/10/01 A",
        "180000 28 01 Bio_100% 93/10/01 A",
        "160000 27 02 Bio_100% 93/10/01 A",
        "140000 25 03 Bio_100% 93/10/01 A",
        "120000 22 04 Bio_100% 93/10/01 A",
        "100000 21 05 Bio_100% 93/10/01 A",
        "080000 19 06 Bio_100% 93/10/01 A",
        "060000 16 07 Bio_100% 93/10/01 A",
        "040000 13 08 Bio_100% 93/10/01 A",
        "020000 08 09 Bio_100% 93/10/01 A"
    };

    if (i < 0 || i >= RANK_ROWS) {
        return NULL;
    }
    return lines[i];
}

/* Which of the shipped rows this line is, or -1 for none of them.
 *
 * The test is whole-line equality and nothing weaker.  A shipped row is a
 * seat holder rather than a record, so it is kept out of the history, and
 * the only safe way to tell a seat holder from a record is that every byte
 * agrees: a real player who scores under the name Bio_100% has a different
 * date, and a player whose key folds to BIO_100 is still a player.  The
 * chance of someone matching all of score, level, misses, name, date and
 * mode is small enough to ignore, and if they manage it they lose one row
 * out of the history, not their place in the table. */
int rank_is_default_line(const char *raw)
{
    int i;

    if (raw == NULL) {
        return -1;
    }
    for (i = 0; i < RANK_ROWS; i++) {
        if (strcmp(raw, rank_default_line(i)) == 0) {
            return i;
        }
    }
    return -1;
}

/* Reads at most RANK_ROWS rows and returns how many, or -1.  An eleventh
 * row, a line that does not parse and an over-long line are all failures: a
 * row that cannot be rewritten must not be dropped silently.  A blank line
 * carries no row, so it is skipped rather than refused; there is nothing in
 * it to lose.  CR LF, bare LF and bare CR all end a line; 0x1A ends the
 * file (the game's own NX93.BAK has one, the live NX93.SCR does not). */
int rank_read_table(FILE *fp, RankTable *out)
{
    char buf[RANK_MAX_LINE + 1];
    int c;
    int d;
    int len;

    if (fp == NULL || out == NULL) {
        return -1;
    }
    out->count = 0;
    for (;;) {
        c = getc(fp);
        if (c == EOF || c == 0x1A) {
            break;
        }
        len = 0;
        while (c != EOF && c != '\r' && c != '\n' && c != 0x1A) {
            if (len >= RANK_MAX_LINE) {
                return -1;   /* over long: do not resynchronise, just fail */
            }
            buf[len] = (char)c;
            len++;
            c = getc(fp);
        }
        buf[len] = '\0';
        if (c == '\r') {
            d = getc(fp);
            if (d != '\n' && d != EOF) {
                ungetc(d, fp);
            }
        }
        if (len > 0) {
            if (out->count >= RANK_ROWS) {
                return -1;   /* eleventh row */
            }
            if (rank_parse_line(buf, &out->row[out->count]) != 0) {
                return -1;
            }
            out->count++;
        }
        if (c == 0x1A || c == EOF) {
            break;
        }
    }
    if (ferror(fp)) {
        return -1;
    }
    return out->count;
}

/* Exactly t->count lines, each the original bytes plus CR LF, and no
 * trailing 0x1A: neither the live table nor the shipped one has one. */
int rank_write_table(FILE *fp, const RankTable *t)
{
    int i;

    if (fp == NULL || t == NULL) {
        return -1;
    }
    for (i = 0; i < t->count; i++) {
        if (fputs(t->row[i].raw, fp) == EOF) {
            return -1;
        }
        if (fputs("\r\n", fp) == EOF) {
            return -1;
        }
    }
    if (fflush(fp) != 0) {
        return -1;
    }
    return 0;
}

/* Keep one row per player, merge with the shipped table, take the top ten.
 *
 * Rows of cur that are a shipped row byte for byte are dropped before
 * deduplication, so they take part in it on neither side.  Without that,
 * a table that still holds empty seats would treat Bio_100% as a player
 * whose best is 200000, and the ten shipped rows appended below would put a
 * second 200000 in the table and push the bottom rows out.  Dropping them
 * first also makes this function idempotent: feeding its own output back in
 * reproduces it byte for byte.
 *
 * A row survives deduplication when no row with the same key beats it on
 * score, and no equal scoring row with the same key came earlier.  That
 * leaves survivors in their original order, which is what makes the stable
 * sort below deterministic for different players on the same score.  The
 * shipped rows are deliberately not deduplicated against each other: ten
 * identical Bio_100% entries are what fills the empty seats. */
void rank_rebuild(const RankTable *cur, RankTable *out)
{
    static RankTable work;   /* ~1.7 KB; far too large for the DOS stack */
    RankRow tmp;
    int n;
    int m;
    int i;
    int j;
    int win;

    if (out == NULL) {
        return;
    }
    work.count = 0;
    n = (cur != NULL) ? cur->count : 0;
    if (n > RANK_ROWS) {
        n = RANK_ROWS;
    }
    for (i = 0; i < n; i++) {
        if (rank_is_default_line(cur->row[i].raw) >= 0) {
            continue;   /* a seat holder, not a record */
        }
        win = 1;
        for (j = 0; j < n; j++) {
            if (j == i) {
                continue;
            }
            if (rank_is_default_line(cur->row[j].raw) >= 0) {
                continue;   /* and so it cannot outscore a record either */
            }
            if (strcmp(cur->row[i].key, cur->row[j].key) != 0) {
                continue;
            }
            if (cur->row[j].score > cur->row[i].score
             || (cur->row[j].score == cur->row[i].score && j < i)) {
                win = 0;
                break;
            }
        }
        if (win) {
            work.row[work.count] = cur->row[i];
            work.count++;
        }
    }
    for (i = 0; i < RANK_ROWS; i++) {
        if (work.count >= RANK_ROWS * 2) {
            break;
        }
        if (rank_parse_line(rank_default_line(i), &work.row[work.count]) == 0) {
            work.count++;
        }
    }

    /* Insertion sort: stable, and the strict < keeps equal scores in the
     * order they were appended, so current rows outrank shipped ones. */
    m = work.count;
    for (i = 1; i < m; i++) {
        tmp = work.row[i];
        j = i;
        while (j > 0 && work.row[j - 1].score < tmp.score) {
            work.row[j] = work.row[j - 1];
            j--;
        }
        work.row[j] = tmp;
    }

    if (m > RANK_ROWS) {
        m = RANK_ROWS;
    }
    for (i = 0; i < m; i++) {
        out->row[i] = work.row[i];
    }
    out->count = m;
}

/* ---------------------------------------------------------------- ledger */

static int rank_is_space(int c)
{
    return c == ' ' || c == '\t' || c == '\r' || c == '\n';
}

static int rank_skip_ws(FILE *fp)
{
    int c;

    do {
        c = getc(fp);
    } while (rank_is_space(c));
    return c;
}

/* Find `pat` in the stream, leaving the position just past it.  The naive
 * restart is correct here because neither "lastUpdate" nor "backups"
 * contains a quote other than its own delimiters, so no proper prefix of
 * either pattern is also a suffix of it. */
static int rank_stream_find(FILE *fp, const char *pat)
{
    size_t k;
    int c;

    k = 0;
    while ((c = getc(fp)) != EOF) {
        if ((char)c == pat[k]) {
            k++;
            if (pat[k] == '\0') {
                return 0;
            }
        } else {
            k = ((char)c == pat[0]) ? 1u : 0u;
        }
    }
    return -1;
}

static int rank_copy_rest(FILE *in, FILE *out)
{
    static char buf[512];
    size_t n;

    for (;;) {
        n = fread(buf, 1, sizeof buf, in);
        if (n > 0u && fwrite(buf, 1, n, out) != n) {
            return -1;
        }
        if (n < sizeof buf) {
            break;
        }
    }
    if (ferror(in)) {
        return -1;
    }
    return 0;
}

/* JSON string body.  Only what RFC 8259 requires is escaped; bytes >= 0x80
 * are emitted raw so that a Shift-JIS name survives unchanged.  That is the
 * whole point of escaping 0x5C: the second byte of SO, HYOU or NOU is 0x5C
 * and would otherwise open an escape sequence in the ledger. */
int rank_json_escape(FILE *fp, const char *s, size_t len)
{
    static const char hex[] = "0123456789abcdef";
    size_t i;
    unsigned char c;

    if (fp == NULL || (s == NULL && len > 0u)) {
        return -1;
    }
    for (i = 0; i < len; i++) {
        c = (unsigned char)s[i];
        if (c == '"' || c == '\\') {
            if (fputc('\\', fp) == EOF || fputc((int)c, fp) == EOF) {
                return -1;
            }
        } else if (c < 0x20u) {
            if (fputs("\\u00", fp) == EOF) {
                return -1;
            }
            if (fputc(hex[(c >> 4) & 0x0Fu], fp) == EOF) {
                return -1;
            }
            if (fputc(hex[c & 0x0Fu], fp) == EOF) {
                return -1;
            }
        } else {
            if (fputc((int)c, fp) == EOF) {
                return -1;
            }
        }
    }
    return 0;
}

int rank_ledger_read_last_update(FILE *fp, RankTime *out)
{
    char val[20];
    int c;
    int i;

    if (fp == NULL || out == NULL) {
        return -1;
    }
    if (rank_stream_find(fp, "\"lastUpdate\"") != 0) {
        return -1;
    }
    if (rank_skip_ws(fp) != ':') {
        return -1;
    }
    if (rank_skip_ws(fp) != '"') {
        return -1;
    }
    for (i = 0; i < 19; i++) {
        c = getc(fp);
        if (c == EOF || c == '"') {
            return -1;   /* value shorter than a timestamp */
        }
        val[i] = (char)c;
    }
    val[19] = '\0';
    if (getc(fp) != '"') {
        return -1;       /* value longer than a timestamp, or unterminated */
    }
    return rank_time_parse(val, out);
}

/* Leave `fp` positioned just after the '[' of the "backups" array. */
static int rank_ledger_seek_backups(FILE *fp)
{
    if (rank_stream_find(fp, "\"backups\"") != 0) {
        return -1;
    }
    if (rank_skip_ws(fp) != ':') {
        return -1;
    }
    if (rank_skip_ws(fp) != '[') {
        return -1;
    }
    return 0;
}

static int rank_write_entry(FILE *out, const char *ts, const RankTable *backup)
{
    int i;

    if (fputs("    {\r\n      \"backupAt\": \"", out) == EOF) {
        return -1;
    }
    if (fputs(ts, out) == EOF) {
        return -1;
    }
    if (fputs("\",\r\n      \"ranking\": [", out) == EOF) {
        return -1;
    }
    if (backup->count <= 0) {
        if (fputs("]\r\n    }", out) == EOF) {
            return -1;
        }
        return 0;
    }
    for (i = 0; i < backup->count; i++) {
        if (fputs("\r\n        \"", out) == EOF) {
            return -1;
        }
        if (rank_json_escape(out, backup->row[i].raw,
                             strlen(backup->row[i].raw)) != 0) {
            return -1;
        }
        if (i + 1 < backup->count) {
            if (fputs("\",", out) == EOF) {
                return -1;
            }
        } else {
            if (fputc('"', out) == EOF) {
                return -1;
            }
        }
    }
    if (fputs("\r\n      ]\r\n    }", out) == EOF) {
        return -1;
    }
    return 0;
}

/* Write a fresh ledger into `out`, newest backup first.  `old` may be NULL
 * (first run); otherwise it is read from the start, and everything after
 * its "backups" [ is copied through byte for byte, so the file never has to
 * be held in memory.  A comma is emitted between the new entry and the old
 * text only when the old array actually had an element. */
int rank_ledger_write(FILE *out, FILE *old, const RankTime *now,
                      const RankTable *backup)
{
    char ts[20];
    char ws[RANK_WS_MAX];
    int nws;
    int c;

    if (out == NULL || now == NULL || backup == NULL) {
        return -1;
    }
    rank_time_format(now, ts);

    if (fputs("{\r\n  \"lastUpdate\": \"", out) == EOF) {
        return -1;
    }
    if (fputs(ts, out) == EOF) {
        return -1;
    }
    if (fputs("\",\r\n  \"backups\": [\r\n", out) == EOF) {
        return -1;
    }
    if (rank_fault(RANK_FAULT_LEDGER_WRITE)) {
        return -1;
    }
    if (rank_write_entry(out, ts, backup) != 0) {
        return -1;
    }

    if (old == NULL) {
        if (fputs("\r\n  ]\r\n}\r\n", out) == EOF) {
            return -1;
        }
        return 0;
    }

    if (rank_ledger_seek_backups(old) != 0) {
        return -1;
    }

    nws = 0;
    for (;;) {
        c = getc(old);
        if (!rank_is_space(c)) {
            break;
        }
        if (nws >= RANK_WS_MAX) {
            return -1;
        }
        ws[nws] = (char)c;
        nws++;
    }
    if (c == EOF) {
        return -1;   /* truncated ledger: the array never closes */
    }
    if (c != ']') {
        if (fputc(',', out) == EOF) {
            return -1;
        }
    }
    if (nws > 0 && fwrite(ws, 1, (size_t)nws, out) != (size_t)nws) {
        return -1;
    }
    if (fputc(c, out) == EOF) {
        return -1;
    }
    return rank_copy_rest(old, out);
}

/* ----------------------------------------------------------------- paths */

static int rank_is_sep(int c)
{
    return c == '\\' || c == '/' || c == ':';
}

/* True when `dir` already ends in something a path may be appended to
 * directly.  On DOS a trailing colon is a drive prefix: A:NX93.SCR means
 * the current directory of drive A, and A:\NX93.SCR does not. */
static int rank_ends_in_sep(int c)
{
#ifdef RANK_DOS
    return c == '\\' || c == '/' || c == ':';
#else
    return c == '/';
#endif
}

static int rank_join(char *buf, const char *dir, const char *name)
{
    size_t dl;
    size_t nl;
    size_t need;

    if (dir == NULL) {
        dir = "";
    }
    dl = strlen(dir);
    nl = strlen(name);
    if (dl == 0u) {
        if (nl + 1u > (size_t)RANK_PATH_MAX) {
            return -1;
        }
        memcpy(buf, name, nl + 1u);
        return 0;
    }
    need = dl + nl + 2u;   /* worst case: dir + separator + name + NUL */
    if (need > (size_t)RANK_PATH_MAX) {
        return -1;
    }
    memcpy(buf, dir, dl);
    if (!rank_ends_in_sep((unsigned char)dir[dl - 1u])) {
        buf[dl] = RANK_SEP;
        dl++;
    }
    memcpy(buf + dl, name, nl + 1u);
    return 0;
}

/* The directory part of argv[0], without a trailing separator, so that
 * rank_run can add its own.  DOSSHELL can launch RANK.EXE with a current
 * directory other than A:\GAME\NX93, and without this the program would
 * create a fresh NX93.SCR wherever it happened to be started.
 *
 * Two forms keep their separator because dropping it would change which
 * directory is named: "A:RANK.EXE" yields "A:" (current directory of drive
 * A, not the root), and "A:\RANK.EXE" yields "A:\" (the root, not the
 * current directory).  Returns -1 when there is no directory part, in
 * which case the caller falls back to the current directory. */
int rank_dir_from_argv0(const char *argv0, char *buf, size_t bufsize)
{
    size_t len;
    size_t i;
    size_t cut;
    int found;

    if (argv0 == NULL || buf == NULL || bufsize == 0u) {
        return -1;
    }
    len = strlen(argv0);
    if (len == 0u) {
        return -1;
    }
    cut = 0u;
    found = 0;
    for (i = 0; i < len; i++) {
        if (rank_is_sep((unsigned char)argv0[i])) {
            cut = i;
            found = 1;
        }
    }
    if (!found) {
        return -1;
    }
    if (argv0[cut] == ':') {
        cut++;                 /* drive prefix: keep the colon */
    } else if (cut == 0u) {
        cut = 1u;              /* root directory: keep the slash */
    } else if (argv0[cut - 1u] == ':') {
        cut++;                 /* drive root: keep the slash */
    }
    if (cut + 1u > bufsize) {
        return -1;
    }
    memcpy(buf, argv0, cut);
    buf[cut] = '\0';
    return 0;
}

/* Can an existing file be replaced?  Opening for update neither creates nor
 * truncates.  A file that exists but refuses "r+b" is read only, and on DOS
 * a read only file cannot be deleted either, so the rename below would fail
 * halfway.  Checking first keeps that failure free of side effects. */
static int rank_writable(const char *path)
{
    FILE *fp;

    fp = fopen(path, "r+b");
    if (fp != NULL) {
        fclose(fp);
        return 1;
    }
    fp = fopen(path, "rb");
    if (fp == NULL) {
        return 1;   /* absent: creating it is the caller's problem */
    }
    fclose(fp);
    return 0;
}

static int rank_exists(const char *path)
{
    FILE *fp;

    fp = fopen(path, "rb");
    if (fp == NULL) {
        return 0;
    }
    fclose(fp);
    return 1;
}

/* Results of rank_commit.  The distinction matters because the caller has to
 * tell the truth about what is on disk afterwards. */
#define RANK_COMMIT_OK        0
#define RANK_COMMIT_FAILED  (-1)   /* `dst` still holds what it held */
#define RANK_COMMIT_STRANDED (-2)  /* `dst` is gone; its bytes are in `keep` */

/* Put `tmp` in place of `dst` without ever leaving `dst` missing.
 *
 * DOS has no rename that replaces, so the old file must move aside first.
 * Doing that with remove() would destroy it, and a rename() that then failed
 * would leave nothing behind while the caller reported that nothing had
 * changed.  Instead the old file is renamed to `keep`, and if the second
 * rename fails it is renamed back.  Only once `tmp` is in place is `keep`
 * discarded.  The ledger is the whole reason this program exists, so the
 * path that could lose it must be recoverable. */
static int rank_commit(const char *tmp, const char *dst, const char *keep,
                       int fail_rename)
{
    int saved;

    saved = 0;
    if (rank_exists(dst)) {
        (void)remove(keep);   /* a leftover from an earlier interrupted run */
        if (rename(dst, keep) != 0) {
            return RANK_COMMIT_FAILED;   /* dst was never moved */
        }
        saved = 1;
    }
    if (fail_rename || rename(tmp, dst) != 0) {
        if (!saved) {
            return RANK_COMMIT_FAILED;   /* there was nothing to lose */
        }
        if (rename(keep, dst) != 0) {
            return RANK_COMMIT_STRANDED;
        }
        return RANK_COMMIT_FAILED;
    }
    if (saved) {
        /* Failing here leaves a stray file and nothing else: the next run
         * removes the name before it needs it again. */
        (void)remove(keep);
    }
    return RANK_COMMIT_OK;
}

/* ------------------------------------------------------------------- run */

int rank_run(const char *dir)
{
    static RankTable cur;
    static RankTable next;
    static char p_scr[RANK_PATH_MAX];
    static char p_txt[RANK_PATH_MAX];
    static char p_tmp_scr[RANK_PATH_MAX];
    static char p_tmp_txt[RANK_PATH_MAX];
    static char p_old_scr[RANK_PATH_MAX];
    static char p_old_txt[RANK_PATH_MAX];
    char daysbuf[16];
    RankTime now;
    RankTime last;
    FILE *ftxt;
    FILE *fscr;
    FILE *fout;
    long days;
    int rc;

    if (rank_join(p_scr, dir, "NX93.SCR") != 0
     || rank_join(p_txt, dir, "NX93.TXT") != 0
     || rank_join(p_tmp_scr, dir, "RANKTMP.SCR") != 0
     || rank_join(p_tmp_txt, dir, "RANKTMP.TXT") != 0
     || rank_join(p_old_scr, dir, "RANKOLD.SCR") != 0
     || rank_join(p_old_txt, dir, "RANKOLD.TXT") != 0) {
        rank_say("RANK: ERROR the directory name is too long; nothing changed");
        return 1;
    }

    if (rank_now(&now) != 0) {
        rank_say("RANK: ERROR cannot read the clock; nothing changed");
        return 1;
    }

    /* The date decides first and unconditionally.  A missing NX93.TXT is
     * the first run and resets straight away; the backup happens before
     * anything is discarded, so nothing is lost. */
    ftxt = fopen(p_txt, "rb");
    if (ftxt != NULL) {
        if (rank_ledger_read_last_update(ftxt, &last) != 0) {
            fclose(ftxt);
            rank_say("RANK: ERROR NX93.TXT is damaged; nothing changed");
            return 1;
        }
        days = rank_elapsed_days(&last, &now);
        if (days < 0L) {
            fclose(ftxt);
            rank_say("RANK: ERROR NX93.TXT is dated in the future.");
            rank_say("RANK: the clock has gone backwards - set it with DATE.");
            rank_say("RANK: nothing changed.");
            return 1;
        }
        if (days < RANK_PERIOD_DAYS) {
            fclose(ftxt);
            rank_put_days(days, daysbuf);
            fputs("RANK: last reset was ", stdout);
            fputs(daysbuf, stdout);
            fputs(" day(s) ago; nothing to do\n", stdout);
            return 0;
        }
        /* A ledger whose backups array is unusable is damaged, and saying so
         * now keeps the diagnosis honest and creates no temporary file. */
        rewind(ftxt);
        if (rank_ledger_seek_backups(ftxt) != 0) {
            fclose(ftxt);
            rank_say("RANK: ERROR NX93.TXT has no usable backups list.");
            rank_say("RANK: repair or delete NX93.TXT; nothing changed.");
            return 1;
        }
        rewind(ftxt);
    }

    /* A missing NX93.SCR is not an error, just an empty table: a fresh
     * install repairs itself.  A line that cannot be read is an error. */
    cur.count = 0;
    fscr = fopen(p_scr, "rb");
    if (fscr != NULL) {
        int rows;

        rows = rank_read_table(fscr, &cur);
        fclose(fscr);
        if (rows < 0) {
            if (ftxt != NULL) {
                fclose(ftxt);
            }
            rank_say("RANK: ERROR NX93.SCR has a bad or extra line; nothing changed");
            return 1;
        }
    }

    if (!rank_writable(p_txt) || !rank_writable(p_scr)) {
        if (ftxt != NULL) {
            fclose(ftxt);
        }
        rank_say("RANK: ERROR NX93.SCR or NX93.TXT is read only; nothing changed");
        return 1;
    }

    /* Step one: the backup.  NX93.SCR is not touched until the new ledger
     * is on disk, so a failure here costs nothing. */
    fout = fopen(p_tmp_txt, "wb");
    if (fout == NULL) {
        if (ftxt != NULL) {
            fclose(ftxt);
        }
        rank_say("RANK: ERROR cannot create RANKTMP.TXT; nothing changed");
        return 1;
    }
    if (rank_ledger_write(fout, ftxt, &now, &cur) != 0) {
        fclose(fout);
        (void)remove(p_tmp_txt);
        if (ftxt != NULL) {
            fclose(ftxt);
        }
        rank_say("RANK: ERROR cannot write NX93.TXT; nothing changed");
        return 1;
    }
    if (fclose(fout) != 0) {
        (void)remove(p_tmp_txt);
        if (ftxt != NULL) {
            fclose(ftxt);
        }
        rank_say("RANK: ERROR cannot write NX93.TXT; nothing changed");
        return 1;
    }
    if (ftxt != NULL) {
        fclose(ftxt);
        ftxt = NULL;
    }
    if (rank_fault(RANK_FAULT_LEDGER_COMMIT)) {
        (void)remove(p_tmp_txt);
        rank_say("RANK: ERROR cannot replace NX93.TXT; nothing changed");
        return 1;
    }
    rc = rank_commit(p_tmp_txt, p_txt, p_old_txt,
                     rank_fault(RANK_FAULT_LEDGER_RENAME));
    if (rc != RANK_COMMIT_OK) {
        (void)remove(p_tmp_txt);
        if (rc == RANK_COMMIT_STRANDED) {
            rank_say("RANK: ERROR NX93.TXT could not be put back.");
            rank_say("RANK: the old ledger is in RANKOLD.TXT - rename it to");
            rank_say("RANK: NX93.TXT by hand before running this again.");
        } else {
            rank_say("RANK: ERROR cannot replace NX93.TXT; nothing changed");
        }
        return 1;
    }

    /* Step two: the table itself.  Everything discarded here is already in
     * NX93.TXT. */
    rank_rebuild(&cur, &next);
    fout = fopen(p_tmp_scr, "wb");
    if (fout == NULL) {
        rank_say("RANK: ERROR cannot create RANKTMP.SCR; NX93.SCR is unchanged");
        return 1;
    }
    if (rank_write_table(fout, &next) != 0 || rank_fault(RANK_FAULT_SCR_WRITE)) {
        fclose(fout);
        (void)remove(p_tmp_scr);
        rank_say("RANK: ERROR cannot write NX93.SCR; it is unchanged");
        return 1;
    }
    if (fclose(fout) != 0) {
        (void)remove(p_tmp_scr);
        rank_say("RANK: ERROR cannot write NX93.SCR; it is unchanged");
        return 1;
    }
    if (rank_fault(RANK_FAULT_SCR_COMMIT)) {
        (void)remove(p_tmp_scr);
        rank_say("RANK: ERROR cannot replace NX93.SCR; it is unchanged");
        return 1;
    }
    rc = rank_commit(p_tmp_scr, p_scr, p_old_scr,
                     rank_fault(RANK_FAULT_SCR_RENAME));
    if (rc != RANK_COMMIT_OK) {
        (void)remove(p_tmp_scr);
        if (rc == RANK_COMMIT_STRANDED) {
            rank_say("RANK: ERROR NX93.SCR could not be put back.");
            rank_say("RANK: the old table is in RANKOLD.SCR - rename it to");
            rank_say("RANK: NX93.SCR by hand, or let the game rebuild it.");
        } else {
            rank_say("RANK: ERROR cannot replace NX93.SCR; it is unchanged");
        }
        return 1;
    }

    rank_say("RANK: the ranking has been reset.");
    rank_say("RANK: the previous table is saved in NX93.TXT.");
    return 0;
}

#ifndef RANK_NO_MAIN
int main(int argc, char **argv)
{
    char dir[RANK_PATH_MAX];

    (void)argc;
    /* Work in the directory RANK.EXE was loaded from, not the current one:
     * DOSSHELL can start it from anywhere, and NX93.SCR must not be created
     * next to whatever that happened to be.  DOS 2.x leaves argv[0] empty,
     * hence the fallback. */
    if (argv != NULL && argv[0] != NULL
     && rank_dir_from_argv0(argv[0], dir, sizeof dir) == 0) {
        return rank_run(dir);
    }
    return rank_run((const char *)0);
}
#endif
