/* RANK.C - daily or weekly reset of the NyaHaX'93 ranking table (A:\GAME\NX93).
 *
 * RANK /D resets when the calendar day changes, RANK /W (and no switch at all)
 * when the week does.  What a reset takes out of NX93.SCR is kept in two plain
 * text files beside it, RANK30.TXT for the thirty best rows ever seen and
 * RANKWHO.TXT for each player's own best, and NX93.TXT is left holding nothing
 * but the instant of the last reset.
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
 * The contract this program is tested against lives in docs/SPEC.md, and
 * docs/DECISIONS.md says why each rule has the shape it does.  Read the
 * latter before changing behaviour: several of the plainer-looking
 * alternatives were tried and rejected for reasons the code cannot show.
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

#define RANK30_ROWS     30   /* rows kept in the all-time table, RANK30.TXT */
#define RANKWHO_ROWS   100   /* rows kept in the per-user table, RANKWHO.TXT */

/* Which calendar boundary a reset is measured against.  The switch on the
 * command line is the only source of it: nothing on disk records which rule a
 * run used, because the crossing test needs no history of it. */
#define RANK_MODE_WEEKLY 0
#define RANK_MODE_DAILY  1

/* The canonical state file, byte for byte:
 *
 *   {<CR><LF>  "lastUpdate": "YYYY-MM-DDTHH:MM:SS"<CR><LF>}<CR><LF>
 *
 * which is 26 bytes of literal and a 19 byte timestamp starting at offset 20.
 * Both numbers are checked rather than assumed; see rank_state_check. */
#define RANK_STATE_LEN   45
#define RANK_STATE_TS_AT 20

/* Shortest and longest acceptable ranking line.  Everything but the score
 * is fixed width, so the length pins down the score width:
 *   <score> SP <lv:2> SP <miss:2> SP <name:8> SP <date:8> SP <mode:1>
 * which is 26 bytes plus 1..9 score digits. */
#define RANK_FIXED_LEN  26
#define RANK_MIN_LINE   (RANK_FIXED_LEN + 1)
#define RANK_MAX_SCORE_DIGITS 9

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

/* The two plain text tables.  Each holds one row of slack past the capacity it
 * publishes, because that is the work space a merge needs: the candidate goes
 * into its place in the order first, and only then is whatever fell off the
 * end taken out of the count.  A resting table is never longer than 30 or 100.
 *
 * Neither of these may be an automatic variable: the default DOS stack is
 * about 2 KB and RankWhoTable alone is over 8 KB. */
typedef struct {
    RankRow row[RANK30_ROWS + 1];
    int count;
} Rank30Table;

typedef struct {
    RankRow row[RANKWHO_ROWS + 1];
    int count;
} RankWhoTable;

#ifdef RANK_TEST_CLOCK
RankTime rank_test_now;
#endif

#ifdef RANK_TEST_FAULT
/* One injection point per file per failure mode, plus the retiring rename that
 * only the migration performs.  WRITE and COMMIT turn back before the commit
 * is entered; RENAME fails the replacing rename and lets the real rename back
 * run, which on a hosted system succeeds, so the recovered case is observable;
 * STRAND fails both, which is the only way to reach a stranded commit at all.
 * Collapsing RENAME and STRAND into one point would leave the recovery path -
 * the entire reason the three step commit exists - untested. */
typedef enum {
    RANK_FAULT_NONE = 0,
    RANK_FAULT_R30_WRITE,
    RANK_FAULT_R30_COMMIT,
    RANK_FAULT_R30_RENAME,
    RANK_FAULT_R30_STRAND,
    RANK_FAULT_WHO_WRITE,
    RANK_FAULT_WHO_COMMIT,
    RANK_FAULT_WHO_RENAME,
    RANK_FAULT_WHO_STRAND,
    RANK_FAULT_SCR_WRITE,
    RANK_FAULT_SCR_COMMIT,
    RANK_FAULT_SCR_RENAME,
    RANK_FAULT_SCR_STRAND,
    RANK_FAULT_TXT_WRITE,
    RANK_FAULT_TXT_COMMIT,
    RANK_FAULT_TXT_RENAME,
    RANK_FAULT_TXT_STRAND,
    RANK_FAULT_RETIRE_RENAME
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

/* Which Monday-based week a day number falls in.  1970-01-01 is a Thursday and
 * day zero, so adding three moves the week boundary onto Monday 00:00.  The
 * division has to floor rather than truncate: C89 leaves a negative quotient's
 * rounding to the implementation, and truncation would fold the Monday of
 * 1969-12-29 into the week before it, so two adjacent Mondays would share an
 * index and a crossing would be missed. */
long rank_week_index(long days)
{
    return rank_floor_div(days + 3L, 7L);
}

/* Is `a` the earlier instant?  All six fields, in order, because a clock that
 * went back a few hours inside one day is exactly the RTC failure this is for,
 * and a comparison that stopped at the date could not see it.  A NULL side has
 * no position in time, so it is before nothing. */
int rank_time_before(const RankTime *a, const RankTime *b)
{
    if (a == NULL || b == NULL) {
        return 0;
    }
    if (a->year != b->year) {
        return (a->year < b->year) ? 1 : 0;
    }
    if (a->month != b->month) {
        return (a->month < b->month) ? 1 : 0;
    }
    if (a->day != b->day) {
        return (a->day < b->day) ? 1 : 0;
    }
    if (a->hour != b->hour) {
        return (a->hour < b->hour) ? 1 : 0;
    }
    if (a->minute != b->minute) {
        return (a->minute < b->minute) ? 1 : 0;
    }
    if (a->second != b->second) {
        return (a->second < b->second) ? 1 : 0;
    }
    return 0;
}

/* Has a reset boundary been crossed between `last` and `now`?  1 yes, 0 no,
 * -1 for a NULL time or a mode that is neither of the two.
 *
 * The question is about the calendar and not about elapsed time: playing at
 * 23:59 on Sunday and again at 00:01 on Monday crosses a weekly boundary even
 * though two minutes have passed, and six days and twenty three hours inside
 * one week crosses nothing.  An unknown mode is refused rather than treated as
 * the default, so a caller that lost track of it cannot silently reset daily.
 *
 * Detecting a clock that has gone backwards is not done here; the caller
 * applies rank_time_before first, and the answer this gives for a `now` in the
 * past is not part of the contract. */
int rank_crossed(const RankTime *last, const RankTime *now, int mode)
{
    long dl;
    long dn;

    if (last == NULL || now == NULL) {
        return -1;
    }
    if (mode != RANK_MODE_WEEKLY && mode != RANK_MODE_DAILY) {
        return -1;
    }
    dl = rank_days_from_civil(last->year, last->month, last->day);
    dn = rank_days_from_civil(now->year, now->month, now->day);
    if (mode == RANK_MODE_DAILY) {
        return (dl != dn) ? 1 : 0;
    }
    return (rank_week_index(dl) != rank_week_index(dn)) ? 1 : 0;
}

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

/* Reads at most `maxrows` rows and returns how many, or -1.  One row too many,
 * a line that does not parse and an over-long line are all failures: a row
 * that cannot be rewritten must not be dropped silently.  A blank line carries
 * no row, so it is skipped rather than refused; there is nothing in it to
 * lose.  CR LF, bare LF and bare CR all end a line; 0x1A ends the file (the
 * game's own NX93.BAK has one, the live NX93.SCR does not).
 *
 * This is the only line reader in the program.  NX93.SCR and the two plain
 * text tables are specified to share one set of conventions, and the way to
 * keep that true is for there to be one automaton rather than an assertion
 * that two of them agree. */
int rank_rows_read(FILE *fp, RankRow *rows, int maxrows)
{
    char buf[RANK_MAX_LINE + 1];
    int c;
    int d;
    int len;
    int n;

    if (fp == NULL || rows == NULL || maxrows < 0) {
        return -1;
    }
    n = 0;
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
            if (n >= maxrows) {
                return -1;   /* one row past what the caller can hold */
            }
            if (rank_parse_line(buf, &rows[n]) != 0) {
                return -1;
            }
            n++;
        }
        if (c == 0x1A || c == EOF) {
            break;
        }
    }
    if (ferror(fp)) {
        return -1;
    }
    return n;
}

/* The game's ten row table, which is rank_rows_read with the game's limit. */
int rank_read_table(FILE *fp, RankTable *out)
{
    int n;

    if (fp == NULL || out == NULL) {
        return -1;
    }
    out->count = 0;
    n = rank_rows_read(fp, out->row, RANK_ROWS);
    if (n < 0) {
        return -1;
    }
    out->count = n;
    return n;
}

/* Exactly `count` lines, each the original bytes plus CR LF, and no trailing
 * 0x1A: neither the live table nor the shipped one has one.  An empty table is
 * an empty file rather than a blank line. */
int rank_rows_write(FILE *fp, const RankRow *rows, int count)
{
    int i;

    if (fp == NULL || count < 0 || (rows == NULL && count > 0)) {
        return -1;
    }
    for (i = 0; i < count; i++) {
        if (fputs(rows[i].raw, fp) == EOF) {
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

int rank_write_table(FILE *fp, const RankTable *t)
{
    if (fp == NULL || t == NULL) {
        return -1;
    }
    return rank_rows_write(fp, t->row, t->count);
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

/* ---------------------------------------------------------------- tables */

/* Sort into descending score, stably.  Insertion sort with a strict <, so rows
 * that tie keep the order they were in; that is what makes the two plain text
 * tables reproducible, since their files record no other tie breaker than the
 * order the rows stand in.  The scratch row is static because the DOS stack is
 * about 2 KB and this is called from the deepest part of the read phase. */
static void rank_sort_rows(RankRow *rows, int count)
{
    static RankRow tmp;
    int i;
    int j;

    for (i = 1; i < count; i++) {
        tmp = rows[i];
        j = i;
        while (j > 0 && rows[j - 1].score < tmp.score) {
            rows[j] = rows[j - 1];
            j--;
        }
        rows[j] = tmp;
    }
}

/* Where a candidate belongs in a table that is already descending: in front of
 * the first row it beats, which leaves it behind every row it ties. */
static int rank_slot_for(const RankRow *rows, int count, long score)
{
    int i;

    for (i = 0; i < count; i++) {
        if (rows[i].score < score) {
            return i;
        }
    }
    return count;
}

/* Open that slot and put the row in it.  The array must have room for one row
 * past *count; both tables carry exactly that much slack.  `r` must not point
 * into the array, since the rows above the slot move before it is read. */
static void rank_insert_row(RankRow *rows, int *count, const RankRow *r)
{
    int at;
    int i;

    at = rank_slot_for(rows, *count, r->score);
    for (i = *count; i > at; i--) {
        rows[i] = rows[i - 1];
    }
    rows[at] = *r;
    (*count)++;
}

/* Read RANK30.TXT.  The file's own order is not trusted - a hand edit may have
 * disturbed it - so it is normalised on the way in; the sort is stable, so a
 * file that was already in order comes back unchanged.  One row too many is a
 * refusal rather than a truncation, because silently dropping the thirty first
 * row would be deciding that it is worth less than the thirty above it. */
int rank_r30_load(FILE *fp, Rank30Table *t)
{
    int n;

    if (fp == NULL || t == NULL) {
        return -1;
    }
    t->count = 0;
    n = rank_rows_read(fp, t->row, RANK30_ROWS);
    if (n < 0) {
        return -1;
    }
    rank_sort_rows(t->row, n);
    t->count = n;
    return n;
}

/* Read RANKWHO.TXT.  One row per player is this file's invariant, so a file
 * holding two rows under one key is damaged and all of it is refused: which of
 * the two to believe is not ours to decide.  Two spellings of one name are a
 * duplicate here even though they are different bytes, which a hand edit can
 * produce by accident now that a mark on the end of a name is not part of the
 * identity. */
int rank_who_load(FILE *fp, RankWhoTable *t)
{
    int n;
    int i;
    int j;

    if (fp == NULL || t == NULL) {
        return -1;
    }
    t->count = 0;
    n = rank_rows_read(fp, t->row, RANKWHO_ROWS);
    if (n < 0) {
        return -1;
    }
    for (i = 0; i < n; i++) {
        for (j = i + 1; j < n; j++) {
            if (strcmp(t->row[i].key, t->row[j].key) == 0) {
                return -1;
            }
        }
    }
    rank_sort_rows(t->row, n);
    t->count = n;
    return n;
}

/* Offer one row to the all-time table.  0 unless an argument was NULL.
 *
 * A shipped row is a seat holder rather than a record, so it never enters.
 * Rows that are equal byte for byte are one row: after a reset every player's
 * best is still in NX93.SCR and would be offered again on the next reset, so
 * without this the thirty seats would fill with one day's repetitions.  Two
 * rows of the same player on different days are different bytes and both
 * belong here - that is the division of labour with the per-user table, which
 * keeps one row per player and dedupes on identity instead.
 *
 * The thirty first row falls off the bottom without a word.  Breaking the
 * floor of a top thirty is what a top thirty is. */
int rank_merge30(Rank30Table *t, const RankRow *r)
{
    int i;

    if (t == NULL || r == NULL) {
        return -1;
    }
    if (rank_is_default_line(r->raw) >= 0) {
        return 0;
    }
    for (i = 0; i < t->count; i++) {
        if (strcmp(t->row[i].raw, r->raw) == 0) {
            return 0;
        }
    }
    rank_insert_row(t->row, &t->count, r);
    if (t->count > RANK30_ROWS) {
        t->count = RANK30_ROWS;
    }
    return 0;
}

/* Offer one row to the per-user table.  1 when a row lost its seat and was
 * copied to *dropped, 0 when none did, -1 if an argument was NULL.
 *
 * A player already in the table is replaced only by a strictly better score,
 * and is then re-seated at the score's place in the order.  A tie keeps the
 * row that is already there, which is what makes merging the same candidates
 * twice leave the table alone.  Nothing is reported when a player's own older
 * row goes: that is an update, not a loss.
 *
 * A new player is seated, and if that makes a hundred and first the lowest row
 * leaves.  It may be the newcomer itself, when it is the lowest.  Either way
 * the row is handed back, because a record that leaves the table has nowhere
 * else to be read unless the same row also stands in the all-time table, and
 * discarding it silently would be the one thing this whole file exists to
 * prevent. */
int rank_mergewho(RankWhoTable *t, const RankRow *r, RankRow *dropped)
{
    int i;
    int found;

    if (t == NULL || r == NULL) {
        return -1;
    }
    if (rank_is_default_line(r->raw) >= 0) {
        return 0;
    }
    found = -1;
    for (i = 0; i < t->count; i++) {
        if (strcmp(t->row[i].key, r->key) == 0) {
            found = i;
            break;
        }
    }
    if (found >= 0) {
        if (r->score <= t->row[found].score) {
            return 0;
        }
        for (i = found; i + 1 < t->count; i++) {
            t->row[i] = t->row[i + 1];
        }
        t->count--;
    }
    rank_insert_row(t->row, &t->count, r);
    if (t->count > RANKWHO_ROWS) {
        if (dropped != NULL) {
            *dropped = t->row[RANKWHO_ROWS];
        }
        t->count = RANKWHO_ROWS;
        return 1;
    }
    return 0;
}

/* W1: say which record did not get a seat.  The row goes out with fputs and
 * never as a format argument - a player may well be called Bio_100%, and one
 * of the names in the live table is there to prove the point.  The screen may
 * scroll away when the game starts, so the message is a last resort rather
 * than the record; the row is in the all-time table if it fits there. */
static void rank_report_drop(const RankRow *r)
{
    fputs("RANK: RANKWHO.TXT is full; this record lost its place:\n", stdout);
    fputs("RANK:   ", stdout);
    fputs(r->raw, stdout);
    fputc('\n', stdout);
}

/* ------------------------------------------------------------ json reading
 *
 * Nothing in this program writes JSON any more.  What is left reads it: the
 * one timestamp NX93.TXT carries, and - once in the life of a machine - the
 * ledger an older RANK.EXE kept, whose rows are taken into the two plain text
 * tables and never written back in this form. */

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

/* ------------------------------------------------- state file / old import */

static int rank_hex(int c)
{
    if (c >= '0' && c <= '9') {
        return c - '0';
    }
    if (c >= 'a' && c <= 'f') {
        return c - 'a' + 10;
    }
    if (c >= 'A' && c <= 'F') {
        return c - 'A' + 10;
    }
    return -1;
}

/* One string body, from just past its opening quote to just past its closing
 * one.  Returns the decoded length, or -1 for an escape the old writer could
 * not have produced, a raw control byte, a string too long for `buf` or an
 * end of file before the closing quote.
 *
 * With `buf` NULL the string is checked and thrown away, which is what the
 * backupAt value needs: its content is not ours to constrain, so it must not
 * be held to a buffer's size either.  Nothing is counted in that mode, so a
 * long value cannot overflow a 16 bit int on the way past.
 *
 * Two character escapes are only the two the grammar allows.  A raw byte of
 * 0x80 or above passes through untouched, which is the point: a name typed on
 * the 98 is Shift-JIS, and the second byte of several common characters is
 * 0x5C, which is why the writer escaped it in the first place.  Raw 0x00 to
 * 0x1F are refused - the writer always escaped those - and that includes 0x1A,
 * so a reader that mistook the DOS end of file marker for the real one cannot
 * pass this by accident. */
static int rank_decode_string(FILE *fp, char *buf, int bufsize)
{
    int n;
    int c;
    int hi;
    int lo;

    n = 0;
    for (;;) {
        c = getc(fp);
        if (c == EOF) {
            return -1;
        }
        if (c == '"') {
            break;
        }
        if (c == '\\') {
            c = getc(fp);
            if (c == 'u') {
                if (getc(fp) != '0' || getc(fp) != '0') {
                    return -1;   /* only 00XX is a byte */
                }
                hi = rank_hex(getc(fp));
                lo = rank_hex(getc(fp));
                if (hi < 0 || lo < 0) {
                    return -1;
                }
                c = hi * 16 + lo;
            } else if (c != '"' && c != '\\') {
                return -1;
            }
        } else if (c < 0x20) {
            return -1;
        }
        if (buf != NULL) {
            if (n + 1 >= bufsize) {
                return -1;
            }
            buf[n] = (char)c;
            n++;
        }
    }
    if (buf != NULL) {
        buf[n] = '\0';
    }
    return n;
}

int rank_ledger_string(FILE *fp, char *buf, int bufsize)
{
    if (fp == NULL || buf == NULL || bufsize <= 0) {
        return -1;
    }
    return rank_decode_string(fp, buf, bufsize);
}

/* The canonical state file and nothing else.  It is the whole file, so there
 * is no stream position to leave behind. */
int rank_state_write(FILE *fp, const RankTime *now)
{
    char ts[20];

    if (fp == NULL || now == NULL) {
        return -1;
    }
    rank_time_format(now, ts);
    if (fputs("{\r\n  \"lastUpdate\": \"", fp) == EOF) {
        return -1;
    }
    if (fputs(ts, fp) == EOF) {
        return -1;
    }
    if (fputs("\"\r\n}\r\n", fp) == EOF) {
        return -1;
    }
    if (fflush(fp) != 0) {
        return -1;
    }
    return 0;
}

/* Is this the canonical state file?  1 or 0, read from the start.
 *
 * The match is exact: the length, every literal byte and a timestamp that
 * parses.  Nothing weaker will do, because the path this guards is the one
 * that throws the old ledger away.  A check that settled for "lastUpdate is
 * readable" would read a ledger whose backups key had been damaged as a state
 * file and discard every record in it.  A future field in here therefore
 * arrives together with a new version of this function; fail closed is worth
 * more than forward compatibility for a file this program is the only writer
 * of. */
int rank_state_check(FILE *fp)
{
    static const char head[] = "{\r\n  \"lastUpdate\": \"";
    static const char tail[] = "\"\r\n}\r\n";
    char buf[RANK_STATE_LEN + 1];
    char ts[20];
    RankTime when;
    size_t n;

    if (fp == NULL) {
        return 0;
    }
    rewind(fp);
    n = fread(buf, 1u, (size_t)RANK_STATE_LEN + 1u, fp);
    if (ferror(fp) || n != (size_t)RANK_STATE_LEN) {
        return 0;   /* shorter or longer, either way not this file */
    }
    if (memcmp(buf, head, sizeof head - 1u) != 0) {
        return 0;
    }
    if (memcmp(buf + RANK_STATE_TS_AT + 19, tail, sizeof tail - 1u) != 0) {
        return 0;
    }
    memcpy(ts, buf + RANK_STATE_TS_AT, 19u);
    ts[19] = '\0';
    return (rank_time_parse(ts, &when) == 0) ? 1 : 0;
}

/* Does the file hold the nine byte key, quotes included, anywhere in it?  That
 * is what tells an old ledger from a hand written file that is neither (and
 * whose records must not be discarded as if it were one).  Nine bytes and not
 * eight: a name field is eight bytes wide, so the quoted key cannot fit in one
 * and a player cannot forge this by choosing a name. */
int rank_has_backups(FILE *fp)
{
    if (fp == NULL) {
        return 0;
    }
    rewind(fp);
    return (rank_stream_find(fp, "\"backups\"") == 0) ? 1 : 0;
}

/* Expect this exact byte sequence next. */
static int rank_match(FILE *fp, const char *s)
{
    while (*s != '\0') {
        if (getc(fp) != (int)(unsigned char)*s) {
            return -1;
        }
        s++;
    }
    return 0;
}

/* One element of the backups array, whose opening brace the caller has already
 * read as `c`.  The two keys are required in order and nothing else is
 * allowed: this grammar describes the output of one writer, and anything else
 * in the file means the migration has misread something, which is worth
 * stopping for rather than guessing at.
 *
 * With `t30` NULL the entry is only checked.  The row buffer is the line
 * buffer, so a string too long to be a row is refused here rather than
 * silently cut down to one. */
static int rank_ledger_entry(FILE *fp, int c, Rank30Table *t30, RankWhoTable *who)
{
    static RankRow row;
    static RankRow dropped;
    static char buf[RANK_MAX_LINE + 1];
    int rc;

    if (c != '{') {
        return -1;
    }
    if (rank_skip_ws(fp) != '"' || rank_match(fp, "backupAt\"") != 0) {
        return -1;
    }
    if (rank_skip_ws(fp) != ':' || rank_skip_ws(fp) != '"') {
        return -1;
    }
    if (rank_decode_string(fp, (char *)0, 0) < 0) {
        return -1;
    }
    if (rank_skip_ws(fp) != ',') {
        return -1;
    }
    if (rank_skip_ws(fp) != '"' || rank_match(fp, "ranking\"") != 0) {
        return -1;
    }
    if (rank_skip_ws(fp) != ':' || rank_skip_ws(fp) != '[') {
        return -1;
    }
    c = rank_skip_ws(fp);
    if (c != ']') {
        for (;;) {
            if (c != '"') {
                return -1;
            }
            if (rank_ledger_string(fp, buf, (int)sizeof buf) < 0) {
                return -1;
            }
            if (rank_parse_line(buf, &row) != 0) {
                return -1;   /* a row we could not rewrite: do not drop it */
            }
            if (t30 != NULL) {
                if (rank_merge30(t30, &row) != 0) {
                    return -1;
                }
                rc = rank_mergewho(who, &row, &dropped);
                if (rc < 0) {
                    return -1;
                }
                if (rc == 1) {
                    rank_report_drop(&dropped);
                }
            }
            c = rank_skip_ws(fp);
            if (c == ']') {
                break;
            }
            if (c != ',') {
                return -1;
            }
            c = rank_skip_ws(fp);
        }
    }
    return (rank_skip_ws(fp) == '}') ? 0 : -1;
}

/* One pass over the whole ledger: the backups array, then the closing brace,
 * then nothing but an optional DOS end of file marker.  Trailing bytes are a
 * refusal, because a file with something after the object is not the file this
 * grammar describes.
 *
 * No line counter exists anywhere in here, and none may be added.  A ledger of
 * two mebibytes is forty thousand rows, which does not fit in the 16 bit int
 * of the machine this runs on, and a host test cannot show that: the only
 * defence is that there is nothing to overflow. */
static int rank_ledger_pass(FILE *fp, Rank30Table *t30, RankWhoTable *who)
{
    int c;

    if (rank_ledger_seek_backups(fp) != 0) {
        return -1;
    }
    c = rank_skip_ws(fp);
    if (c != ']') {
        for (;;) {
            if (rank_ledger_entry(fp, c, t30, who) != 0) {
                return -1;
            }
            c = rank_skip_ws(fp);
            if (c == ']') {
                break;
            }
            if (c != ',') {
                return -1;
            }
            c = rank_skip_ws(fp);
        }
    }
    if (rank_skip_ws(fp) != '}') {
        return -1;
    }
    c = rank_skip_ws(fp);
    if (c == 0x1A) {
        c = getc(fp);
    }
    return (c == EOF) ? 0 : -1;
}

/* Take every row of an old ledger into the two tables.  0 or -1, and on -1 the
 * tables are exactly as they were handed over.
 *
 * That is why the file is read twice.  Merging as the rows arrive would leave
 * half an import behind when the grammar broke two thirds of the way in, and
 * the caller would then have to choose between writing a half-migrated table
 * and recovering the tables itself.  The alternative - merging into a private
 * copy - costs eleven kilobytes of static storage, which is worth more here
 * than the second read of a file that is read once in the life of a machine.
 * The ledger streams either way; it is never held in memory. */
int rank_import_ledger(FILE *fp, Rank30Table *t30, RankWhoTable *who)
{
    if (fp == NULL || t30 == NULL || who == NULL) {
        return -1;
    }
    rewind(fp);
    if (rank_ledger_pass(fp, (Rank30Table *)0, (RankWhoTable *)0) != 0) {
        return -1;
    }
    rewind(fp);
    /* The second pass reads the same bytes with the same grammar, so it cannot
     * fail where the first succeeded.  It is still checked, so that a reader
     * does not have to take that on trust. */
    return rank_ledger_pass(fp, t30, who);
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
#define RANK_COMMIT_NOTMOVED (-3)  /* `dst` could not even be moved aside */

/* The file name at the end of a path.  What the screen has to name is what the
 * user will see in a DIR listing, not the path this program happened to build
 * out of argv[0]. */
static const char *rank_basename(const char *path)
{
    const char *p;
    const char *last;

    last = path;
    for (p = path; *p != '\0'; p++) {
        if (rank_is_sep((unsigned char)*p)) {
            last = p + 1;
        }
    }
    return last;
}

/* W2: the copy left behind by an interrupted run is about to go.
 *
 * After a stranded commit (the one failure that leaves a file only under its
 * retained name) the copy survives the next run as well, because the target is
 * missing and the discard below is never reached.  The run after that finds the
 * target in place and the copy in the way, and with GAME.BAT calling RANK every
 * day that is about a day's notice.  So the discard says so first.  The exit
 * code stays 0: this is the tidying up a successful reset does.
 *
 * This is a notice, not advice.  The discard follows on the next statement, so
 * there is no moment at which the reader could act on the copy, and telling
 * them to save it first would name something they cannot do.  What it buys them
 * is knowing that a run was once interrupted here. */
static void rank_report_keep(const char *keep)
{
    fputs("RANK: removing ", stdout);
    fputs(rank_basename(keep), stdout);
    fputs(", a copy an interrupted run left behind.\n", stdout);
}

/* Put `tmp` in place of `dst` without ever leaving `dst` missing.
 *
 * DOS has no rename that replaces, so the old file must move aside first.
 * Doing that with remove() would destroy it, and a rename() that then failed
 * would leave nothing behind while the caller reported that nothing had
 * changed.  Instead the old file is renamed to `keep`, and if the second
 * rename fails it is renamed back.  Only once `tmp` is in place is `keep`
 * discarded.  Records are the whole reason this program exists, so the path
 * that could lose them must be recoverable.
 *
 * `keep_retained` leaves the retained copy in place on success, which the
 * migration's own fourth stage needs: there `keep` is NX93OLD.TXT, and keeping
 * the old ledger rather than deleting it is the point of retiring it.  That
 * stage can also be sure the discard above does not fire, because a migration
 * refuses to start at all when NX93OLD.TXT already exists.
 *
 * The three failure flags are distinct on purpose.  `fail_rename` fails the
 * replacing rename and lets the real recovery rename run, which is the only way
 * to observe a commit that failed and put the old file back - the recovery is
 * the entire reason the three steps exist.  `fail_strand` fails both, which is
 * the only way to reach a stranded commit on a system whose rename back always
 * succeeds.  Folding them into one flag would leave the recovery untested. */
static int rank_commit(const char *tmp, const char *dst, const char *keep,
                       int keep_retained, int fail_retire,
                       int fail_rename, int fail_strand)
{
    int saved;

    saved = 0;
    if (rank_exists(dst)) {
        if (rank_exists(keep)) {
            rank_report_keep(keep);   /* W2, before the bytes go */
        }
        (void)remove(keep);   /* a leftover from an earlier interrupted run */
        if (fail_retire || rename(dst, keep) != 0) {
            return RANK_COMMIT_NOTMOVED;   /* dst was never moved */
        }
        saved = 1;
    }
    if (fail_rename || fail_strand || rename(tmp, dst) != 0) {
        if (!saved) {
            return RANK_COMMIT_FAILED;   /* there was nothing to lose */
        }
        if (fail_strand || rename(keep, dst) != 0) {
            return RANK_COMMIT_STRANDED;
        }
        return RANK_COMMIT_FAILED;
    }
    if (saved && !keep_retained) {
        /* Failing here leaves a stray file and nothing else: the next run
         * removes the name before it needs it again. */
        (void)remove(keep);
    }
    return RANK_COMMIT_OK;
}

/* ------------------------------------------------------------------- run */

/* The switch that picks the rule: no argument is weekly, which is what the
 * shipped GAME.BAT used to pass.  Both cases of both letters are accepted,
 * because DOS users type switches either way, and the comparison is four
 * strcmps rather than a case fold so that <ctype.h> stays out of this file.
 *
 * Anything else is refused and leaves *mode alone.  Falling back to the
 * default would turn a mistyped switch into the other rule without a word,
 * which on a machine whose GAME.BAT says /D means a day's records keep their
 * seats for a week, and on one that says /W means they lose them daily. */
int rank_mode_parse(const char *arg, int *mode)
{
    if (mode == NULL) {
        return -1;
    }
    if (arg == NULL) {
        *mode = RANK_MODE_WEEKLY;
        return 0;
    }
    if (strcmp(arg, "/D") == 0 || strcmp(arg, "/d") == 0) {
        *mode = RANK_MODE_DAILY;
        return 0;
    }
    if (strcmp(arg, "/W") == 0 || strcmp(arg, "/w") == 0) {
        *mode = RANK_MODE_WEEKLY;
        return 0;
    }
    return -1;
}

/* The thirteen names a run may open, built once before anything is opened so
 * that a directory too deep to hold the longest of them is refused before a
 * single file has been created (E2).  The five that outlive a run, the four
 * temporaries the stages write into, and the four the commit step parks the
 * outgoing file in. */
static char rank_p_scr[RANK_PATH_MAX];
static char rank_p_txt[RANK_PATH_MAX];
static char rank_p_r30[RANK_PATH_MAX];
static char rank_p_who[RANK_PATH_MAX];
static char rank_p_retired[RANK_PATH_MAX];
static char rank_p_tmp_scr[RANK_PATH_MAX];
static char rank_p_tmp_txt[RANK_PATH_MAX];
static char rank_p_tmp_r30[RANK_PATH_MAX];
static char rank_p_tmp_who[RANK_PATH_MAX];
static char rank_p_old_scr[RANK_PATH_MAX];
static char rank_p_old_txt[RANK_PATH_MAX];
static char rank_p_old_r30[RANK_PATH_MAX];
static char rank_p_old_who[RANK_PATH_MAX];

/* What the reading phase fills in and the writing phase hands to the writers.
 * At file scope rather than automatic for the reason the types' own comment
 * gives: RankWhoTable on its own is four times the default DOS stack. */
static Rank30Table rank_t30;
static RankWhoTable rank_who;
static RankTable rank_cur;
static RankTable rank_next;

/* The instant this run writes into NX93.TXT, read once at the start so that
 * every file it produces carries the same one. */
static RankTime rank_at;

/* The four stages, in the only order they ever run in. */
#define RANK_STAGE_R30 0
#define RANK_STAGE_WHO 1
#define RANK_STAGE_SCR 2
#define RANK_STAGE_TXT 3

/* What NX93.TXT turned out to be. */
#define RANK_TXT_ABSENT    0
#define RANK_TXT_CANONICAL 1
#define RANK_TXT_LEGACY    2   /* an older RANK.EXE's ledger: migrate it */
#define RANK_TXT_OTHER     3   /* neither, so not ours to replace */

static int rank_paths_init(const char *dir)
{
    if (rank_join(rank_p_scr, dir, "NX93.SCR") != 0
     || rank_join(rank_p_txt, dir, "NX93.TXT") != 0
     || rank_join(rank_p_r30, dir, "RANK30.TXT") != 0
     || rank_join(rank_p_who, dir, "RANKWHO.TXT") != 0
     || rank_join(rank_p_retired, dir, "NX93OLD.TXT") != 0
     || rank_join(rank_p_tmp_scr, dir, "RANKTMP.SCR") != 0
     || rank_join(rank_p_tmp_txt, dir, "RANKTMP.TXT") != 0
     || rank_join(rank_p_tmp_r30, dir, "RANKTMP.30") != 0
     || rank_join(rank_p_tmp_who, dir, "RANKTMP.WHO") != 0
     || rank_join(rank_p_old_scr, dir, "RANKOLD.SCR") != 0
     || rank_join(rank_p_old_txt, dir, "RANKOLD.TXT") != 0
     || rank_join(rank_p_old_r30, dir, "RANKOLD.30") != 0
     || rank_join(rank_p_old_who, dir, "RANKOLD.WHO") != 0) {
        return -1;
    }
    return 0;
}

static const char *rank_stage_name(int which)
{
    switch (which) {
    case RANK_STAGE_R30: return "RANK30.TXT";
    case RANK_STAGE_WHO: return "RANKWHO.TXT";
    case RANK_STAGE_SCR: return "NX93.SCR";
    default:             return "NX93.TXT";
    }
}

static const char *rank_stage_tmp(int which)
{
    switch (which) {
    case RANK_STAGE_R30: return rank_p_tmp_r30;
    case RANK_STAGE_WHO: return rank_p_tmp_who;
    case RANK_STAGE_SCR: return rank_p_tmp_scr;
    default:             return rank_p_tmp_txt;
    }
}

static const char *rank_stage_dst(int which)
{
    switch (which) {
    case RANK_STAGE_R30: return rank_p_r30;
    case RANK_STAGE_WHO: return rank_p_who;
    case RANK_STAGE_SCR: return rank_p_scr;
    default:             return rank_p_txt;
    }
}

/* Where the outgoing file goes while its replacement is put in place.  Only
 * the state file's answer depends on anything: a migration retires the old
 * ledger to NX93OLD.TXT and keeps it, where an ordinary run parks NX93.TXT in
 * RANKOLD.TXT and discards it once the swap is done. */
static const char *rank_stage_keep(int which, int migrate)
{
    switch (which) {
    case RANK_STAGE_R30: return rank_p_old_r30;
    case RANK_STAGE_WHO: return rank_p_old_who;
    case RANK_STAGE_SCR: return rank_p_old_scr;
    default:             return migrate ? rank_p_retired : rank_p_old_txt;
    }
}

/* The word for the rule this run went by.  Nothing on disk records it - the
 * crossing test needs no history of it - so the screen is the only place it is
 * ever said, and both messages that end a run say it. */
static const char *rank_mode_word(int mode)
{
    return (mode == RANK_MODE_DAILY) ? "daily" : "weekly";
}

static void rank_say_bad_mode(void)
{
    rank_say("RANK: ERROR the update rule is neither daily nor weekly.");
    rank_say("RANK: use RANK /D for daily or RANK /W for weekly.");
    rank_say("RANK: nothing was changed.");
}

static void rank_say_stage(const char *name, const char *tail)
{
    fputs("RANK: ERROR ", stdout);
    fputs(name, stdout);
    fputc(' ', stdout);
    fputs(tail, stdout);
    fputc('\n', stdout);
}

/* Which files this run really did replace before it stopped.  A message that
 * said only "the reset failed" would hide that the two tables are already new,
 * and the reader would have no reason to go and look at them; the stage index
 * is all it takes to tell the truth, because the order is fixed. */
static void rank_say_changed(int which)
{
    int i;

    if (which == RANK_STAGE_R30) {
        rank_say("RANK: no file had been replaced yet.");
        return;
    }
    fputs("RANK: already replaced:", stdout);
    for (i = 0; i < which; i++) {
        fputc(' ', stdout);
        fputs(rank_stage_name(i), stdout);
    }
    fputc('\n', stdout);
}

static void rank_say_retry(void)
{
    rank_say("RANK: run RANK again to carry on from where it stopped.");
}

/* ---------------------------------------------------- the reading phase */

/* What NX93.TXT is, and when it says the last reset happened.  0 with *kind
 * and *last set, or -1 with the diagnosis already on the screen.
 *
 * The timestamp is read first and by the reader that has always read it, so an
 * old ledger and a new state file are treated alike up to this point.  Which
 * of the two it is only matters once a reset is certain, and it is decided by
 * an exact match rather than by "the timestamp parsed": the path that answer
 * opens is the one that throws an old ledger away. */
static int rank_classify_txt(int *kind, RankTime *last)
{
    FILE *fp;
    int canon;

    memset(last, 0, sizeof *last);
    *kind = RANK_TXT_ABSENT;
    fp = fopen(rank_p_txt, "rb");
    if (fp == NULL) {
        return 0;
    }
    if (rank_ledger_read_last_update(fp, last) != 0) {
        (void)fclose(fp);
        rank_say("RANK: ERROR NX93.TXT is damaged: lastUpdate cannot be read.");
        rank_say("RANK: repair it, or delete it to reset on the next run.");
        rank_say("RANK: nothing was changed.");
        return -1;
    }
    canon = rank_state_check(fp);
    if (canon) {
        *kind = RANK_TXT_CANONICAL;
    } else if (rank_has_backups(fp)) {
        *kind = RANK_TXT_LEGACY;
    } else {
        *kind = RANK_TXT_OTHER;
    }
    (void)fclose(fp);
    return 0;
}

/* Can all four files be replaced?  Opening for update neither creates nor
 * truncates, so a file that exists and refuses it is read only - and on DOS a
 * read only file cannot be deleted either, which would make the rename below
 * fail halfway.  Asking first keeps that failure free of side effects, and
 * naming the file turns the answer into something the player can act on. */
static int rank_check_writable(void)
{
    const char *bad;

    bad = (const char *)0;
    if (!rank_writable(rank_p_r30)) {
        bad = rank_stage_name(RANK_STAGE_R30);
    } else if (!rank_writable(rank_p_who)) {
        bad = rank_stage_name(RANK_STAGE_WHO);
    } else if (!rank_writable(rank_p_scr)) {
        bad = rank_stage_name(RANK_STAGE_SCR);
    } else if (!rank_writable(rank_p_txt)) {
        bad = rank_stage_name(RANK_STAGE_TXT);
    }
    if (bad == NULL) {
        return 0;
    }
    fputs("RANK: ERROR ", stdout);
    fputs(bad, stdout);
    fputs(" is read only, so it cannot be replaced.\n", stdout);
    rank_say("RANK: clear the flag with ATTRIB -R; nothing was changed.");
    return -1;
}

/* Read everything and merge everything, writing nothing.  0, or -1 with the
 * diagnosis on the screen and the disk untouched.
 *
 * Every handle this opens is closed before it returns, which is what makes the
 * phase boundary real: the state file is about to be renamed, and renaming a
 * file that is still open is not something DOS forgives.  The merge order is
 * the existing tables, then the rows leaving NX93.SCR, then an old ledger if
 * there is one, and that order is what makes two rows of equal score land in a
 * reproducible place. */
static int rank_collect(int migrate)
{
    static RankRow dropped;
    FILE *fp;
    int n;
    int i;
    int rc;

    rank_cur.count = 0;
    fp = fopen(rank_p_scr, "rb");
    if (fp != NULL) {
        n = rank_read_table(fp, &rank_cur);
        (void)fclose(fp);
        if (n < 0) {
            rank_say("RANK: ERROR NX93.SCR holds a line that cannot be read,");
            rank_say("RANK: or more than ten of them.  Repair it by hand;");
            rank_say("RANK: nothing was changed.");
            return -1;
        }
    }

    rank_t30.count = 0;
    fp = fopen(rank_p_r30, "rb");
    if (fp != NULL) {
        n = rank_r30_load(fp, &rank_t30);
        (void)fclose(fp);
        if (n < 0) {
            rank_say("RANK: ERROR RANK30.TXT holds a line that cannot be read,");
            rank_say("RANK: or more than thirty of them.  Repair it by hand;");
            rank_say("RANK: nothing was changed.");
            return -1;
        }
    }

    rank_who.count = 0;
    fp = fopen(rank_p_who, "rb");
    if (fp != NULL) {
        n = rank_who_load(fp, &rank_who);
        (void)fclose(fp);
        if (n < 0) {
            rank_say("RANK: ERROR RANKWHO.TXT holds two rows for one player,");
            rank_say("RANK: a line that cannot be read, or more than a hundred");
            rank_say("RANK: rows.  Delete RANKWHO.TXT and the next run builds");
            rank_say("RANK: it again from NX93.SCR.  Rows that only");
            rank_say("RANK: RANKWHO.TXT held are lost that way, so copy out");
            rank_say("RANK: any you added by hand - RANK30.TXT has the rest.");
            rank_say("RANK: nothing was changed.");
            return -1;
        }
    }

    if (rank_check_writable() != 0) {
        return -1;
    }

    for (i = 0; i < rank_cur.count; i++) {
        rc = rank_merge30(&rank_t30, &rank_cur.row[i]);
        if (rc == 0) {
            rc = rank_mergewho(&rank_who, &rank_cur.row[i], &dropped);
        }
        if (rc < 0) {
            rank_say("RANK: ERROR the records could not be merged;");
            rank_say("RANK: nothing was changed.");
            return -1;
        }
        if (rc == 1) {
            rank_report_drop(&dropped);   /* W1 */
        }
    }

    if (migrate) {
        fp = fopen(rank_p_txt, "rb");
        if (fp == NULL || rank_import_ledger(fp, &rank_t30, &rank_who) != 0) {
            if (fp != NULL) {
                (void)fclose(fp);
            }
            rank_say("RANK: ERROR the old NX93.TXT could not be read to the");
            rank_say("RANK: end, so none of it was taken over.  Repair it and");
            rank_say("RANK: the next run tries again; nothing was changed.");
            return -1;
        }
        (void)fclose(fp);
    }

    rank_rebuild(&rank_cur, &rank_next);
    return 0;
}

/* ---------------------------------------------------- the writing phase */

/* One stage: write the temporary, then put it in place of the real file.
 *
 * The temporary is removed on every failing path, so no run leaves one behind.
 * The failure flags arrive as plain ints rather than as a fault enumerator,
 * which is what keeps the test seam out of the DOS build: there rank_fault is
 * a macro that discards its argument, so the enumerator's name never reaches
 * the compiler at all. */
static int rank_stage(int which, int migrate, int f_write, int f_commit,
                      int f_retire, int f_rename, int f_strand)
{
    const char *tmp;
    const char *dst;
    const char *keep;
    const char *name;
    FILE *fp;
    int rc;

    tmp = rank_stage_tmp(which);
    dst = rank_stage_dst(which);
    keep = rank_stage_keep(which, migrate);
    name = rank_stage_name(which);

    fp = fopen(tmp, "wb");
    if (fp == NULL) {
        fputs("RANK: ERROR cannot create ", stdout);
        fputs(rank_basename(tmp), stdout);
        fputs(", so ", stdout);
        fputs(name, stdout);
        fputs(" is unchanged.\n", stdout);
        rank_say_changed(which);
        rank_say("RANK: make room on the disk, or clear ATTRIB -R, and run");
        rank_say("RANK: RANK again.");
        return -1;
    }
    switch (which) {
    case RANK_STAGE_R30:
        rc = rank_rows_write(fp, rank_t30.row, rank_t30.count);
        break;
    case RANK_STAGE_WHO:
        rc = rank_rows_write(fp, rank_who.row, rank_who.count);
        break;
    case RANK_STAGE_SCR:
        rc = rank_write_table(fp, &rank_next);
        break;
    default:
        rc = rank_state_write(fp, &rank_at);
        break;
    }
    if (rc != 0 || f_write) {
        (void)fclose(fp);
        (void)remove(tmp);
        rank_say_stage(name, "could not be written; it is unchanged.");
        rank_say_changed(which);
        rank_say_retry();
        return -1;
    }
    if (fclose(fp) != 0) {
        (void)remove(tmp);
        rank_say_stage(name, "could not be closed; it is unchanged.");
        rank_say_changed(which);
        rank_say_retry();
        return -1;
    }
    if (f_commit) {
        (void)remove(tmp);
        rank_say_stage(name, "was not replaced: the swap never started.");
        rank_say_changed(which);
        rank_say_retry();
        return -1;
    }

    rc = rank_commit(tmp, dst, keep,
                     (which == RANK_STAGE_TXT) ? migrate : 0,
                     f_retire, f_rename, f_strand);
    if (rc == RANK_COMMIT_OK) {
        return 0;
    }
    (void)remove(tmp);
    if (rc == RANK_COMMIT_STRANDED) {
        rank_say_stage(name, "could not be put back after a failed swap.");
        fputs("RANK: every byte it held is in ", stdout);
        fputs(rank_basename(keep), stdout);
        fputs(" - rename that back\n", stdout);
        fputs("RANK: to ", stdout);
        fputs(name, stdout);
        fputs(" by hand before running RANK again.\n", stdout);
        rank_say_changed(which);
    } else if (rc == RANK_COMMIT_NOTMOVED) {
        rank_say_stage(name, "could not be moved aside; it is unchanged.");
        rank_say_changed(which);
        rank_say_retry();
    } else {
        rank_say_stage(name, "was not replaced; it is as it was before.");
        rank_say_changed(which);
        rank_say_retry();
    }
    return -1;
}

/* The four stages, spelled out one after another rather than driven from a
 * table, because the order is a contract and not a configuration.
 *
 * NX93.SCR is the only file a reset takes anything away from, so the two
 * tables that receive those rows have to be on disk before it is touched; and
 * the state file comes last, because a run that recorded the reset and then
 * failed to rewrite NX93.SCR would never reset again.  Stopping at the first
 * failure is what makes the half-done states enumerable at all. */
static int rank_write_all(int migrate)
{
    if (rank_stage(RANK_STAGE_R30, migrate,
                   rank_fault(RANK_FAULT_R30_WRITE),
                   rank_fault(RANK_FAULT_R30_COMMIT),
                   0,
                   rank_fault(RANK_FAULT_R30_RENAME),
                   rank_fault(RANK_FAULT_R30_STRAND)) != 0) {
        return -1;
    }
    if (rank_stage(RANK_STAGE_WHO, migrate,
                   rank_fault(RANK_FAULT_WHO_WRITE),
                   rank_fault(RANK_FAULT_WHO_COMMIT),
                   0,
                   rank_fault(RANK_FAULT_WHO_RENAME),
                   rank_fault(RANK_FAULT_WHO_STRAND)) != 0) {
        return -1;
    }
    if (rank_stage(RANK_STAGE_SCR, migrate,
                   rank_fault(RANK_FAULT_SCR_WRITE),
                   rank_fault(RANK_FAULT_SCR_COMMIT),
                   0,
                   rank_fault(RANK_FAULT_SCR_RENAME),
                   rank_fault(RANK_FAULT_SCR_STRAND)) != 0) {
        return -1;
    }
    if (rank_stage(RANK_STAGE_TXT, migrate,
                   rank_fault(RANK_FAULT_TXT_WRITE),
                   rank_fault(RANK_FAULT_TXT_COMMIT),
                   rank_fault(RANK_FAULT_RETIRE_RENAME),
                   rank_fault(RANK_FAULT_TXT_RENAME),
                   rank_fault(RANK_FAULT_TXT_STRAND)) != 0) {
        return -1;
    }
    return 0;
}

/* ------------------------------------------------------------------ a run
 *
 * Read everything, decide, then write.  The decision is made entirely from
 * what was read, and the first byte is written only after the last handle has
 * been closed.  A run that decides not to reset writes nothing at all - not
 * even a note that it looked - which is what lets an old ledger sit there
 * untouched until the first crossing. */
int rank_run(const char *dir, int mode)
{
    RankTime last;
    char daysbuf[16];
    int kind;
    int crossed;
    int migrate;

    if (mode != RANK_MODE_WEEKLY && mode != RANK_MODE_DAILY) {
        rank_say_bad_mode();
        return 1;
    }
    if (rank_paths_init(dir) != 0) {
        rank_say("RANK: ERROR the directory name is too long to build a file");
        rank_say("RANK: name in; put the game nearer the root of the drive.");
        rank_say("RANK: nothing was changed.");
        return 1;
    }
    if (rank_now(&rank_at) != 0) {
        rank_say("RANK: ERROR the clock cannot be read; set it with DATE and");
        rank_say("RANK: TIME, then try again.  Nothing was changed.");
        return 1;
    }
    if (rank_classify_txt(&kind, &last) != 0) {
        return 1;
    }

    if (kind == RANK_TXT_ABSENT) {
        crossed = 1;   /* nothing records a reset, so this run is the first */
    } else {
        /* A clock that went backwards comes first and in full: parking
         * lastUpdate in the future would hold the reset off indefinitely, and a
         * comparison that stopped at the date could not see a clock nudged back
         * a few hours inside one day. */
        if (rank_time_before(&rank_at, &last)) {
            rank_say("RANK: ERROR NX93.TXT is dated in the future.");
            rank_say("RANK: the clock has gone backwards - set it with DATE");
            rank_say("RANK: and TIME.  Nothing was changed.");
            return 1;
        }
        crossed = rank_crossed(&last, &rank_at, mode);
        if (crossed < 0) {
            rank_say_bad_mode();
            return 1;
        }
        if (crossed == 0) {
            rank_put_days(rank_elapsed_days(&last, &rank_at), daysbuf);
            fputs("RANK: ", stdout);
            fputs(rank_mode_word(mode), stdout);
            fputs(" rule: last reset was ", stdout);
            fputs(daysbuf, stdout);
            fputs(" day(s) ago; nothing to do.\n", stdout);
            return 0;
        }
    }

    /* A reset is certain from here, so what NX93.TXT is becomes the question.
     * Anything that is neither this program's state file nor an older one's
     * ledger is refused rather than overwritten: it may be somebody's only copy
     * of something, and this is the only moment at which saying so costs
     * nothing. */
    if (kind == RANK_TXT_OTHER) {
        rank_say("RANK: ERROR NX93.TXT is neither the state file this program");
        rank_say("RANK: writes nor a ledger an older RANK.EXE left.  Move it");
        rank_say("RANK: aside or delete it; nothing was changed.");
        return 1;
    }
    migrate = (kind == RANK_TXT_LEGACY) ? 1 : 0;
    if (migrate && rank_exists(rank_p_retired)) {
        rank_say("RANK: ERROR NX93OLD.TXT is already there, so the old");
        rank_say("RANK: NX93.TXT has nowhere to be retired to.  Rename");
        rank_say("RANK: NX93OLD.TXT and run RANK again; nothing was changed.");
        return 1;
    }

    if (rank_collect(migrate) != 0) {
        return 1;
    }
    if (rank_write_all(migrate) != 0) {
        return 1;
    }

    fputs("RANK: ", stdout);
    fputs(rank_mode_word(mode), stdout);
    fputs(" rule: the ranking has been reset.\n", stdout);
    rank_say("RANK: what left NX93.SCR is in RANK30.TXT and RANKWHO.TXT.");
    if (migrate) {
        rank_say("RANK: the old NX93.TXT was retired to NX93OLD.TXT.");
    }
    return 0;
}

#ifndef RANK_NO_MAIN
/* The usage goes to stdout, where GAME.BAT's PAUSE leaves it on the screen,
 * and it spells out both switches so that the text says what to type. */
static void rank_usage(void)
{
    rank_say("RANK: usage: RANK [/D | /W]");
    rank_say("RANK:   /D  reset the ranking when the calendar day changes");
    rank_say("RANK:   /W  reset it when the week changes (the default)");
    rank_say("RANK: nothing was changed.");
}

int main(int argc, char **argv)
{
    char dir[RANK_PATH_MAX];
    int mode;

    /* No switch is weekly, which is what the shipped GAME.BAT used to pass.
     * Anything the parser does not know is refused rather than quietly read as
     * the default: on a machine whose GAME.BAT says /D, a mistyped switch would
     * otherwise keep a day's records for a week without a word. */
    mode = RANK_MODE_WEEKLY;
    if (argc > 2
     || (argc == 2 && argv != NULL && rank_mode_parse(argv[1], &mode) != 0)) {
        rank_usage();
        return 1;
    }
    /* Work in the directory RANK.EXE was loaded from, not the current one:
     * DOSSHELL can start it from anywhere, and NX93.SCR must not be created
     * next to whatever that happened to be.  DOS 2.x leaves argv[0] empty,
     * hence the fallback. */
    if (argv != NULL && argv[0] != NULL
     && rank_dir_from_argv0(argv[0], dir, sizeof dir) == 0) {
        return rank_run(dir, mode);
    }
    return rank_run((const char *)0, mode);
}
#endif
