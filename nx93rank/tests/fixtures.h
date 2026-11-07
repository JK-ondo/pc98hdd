/* fixtures.h - real NX93 ranking files, embedded as byte-exact literals.
 *
 * GENERATED, do not hand-edit.  Regenerate with the recipe in README.md.
 *
 * They are embedded rather than read at run time for two reasons:
 *   - the default ranking comes from ~/test/nx93_110/NX93.SCR, which lives
 *     outside this repository and does not exist on another machine;
 *   - hdd/ is a mirror of the real PC-9801 disk, so the suite must neither
 *     read from it nor write to it.
 *
 * Every literal is exact including CR, LF and any trailing 0x1A.
 */
#ifndef NX93RANK_FIXTURES_H
#define NX93RANK_FIXTURES_H

/* A:\GAME\NX93\NX93.SCR as the game left it (2026-09-28 pull). 10 rows, CRLF, no 0x1A.
   340 bytes, 10 line(s). */
static const char fix_scr[] =
    "327400 30 01 OMZN     26/09/15 A\r\n"
    "316770 30 02 OMZN     26/09/17 A\r\n"
    "315870 30 01 OMZN     26/09/16 A\r\n"
    "304890 30 00 omzn     26/09/25 A\r\n"
    "299370 30 00 OMZN@@@@ 61/11/17 A\r\n"
    "291290 30 02 omzn     26/09/18 A\r\n"
    "286340 30 01 omzn     26/09/15 A\r\n"
    "285680 30 05 omzn     26/09/15 A\r\n"
    "265680 29 06 omzn     26/09/25 A\r\n"
    "261740 29 05 omzn     26/09/14 A\r\n";
#define FIX_SCR_LEN (sizeof fix_scr - 1u)

/* A:\GAME\NX93\NX93.BAK, the game's own previous table. 10 rows, CRLF, trailing 0x1A.
   332 bytes, 10 line(s). */
static const char fix_bak[] =
    "299370 30 00 OMZN@@@@ 61/11/17 A\r\n"
    "98840 30 01 OMZN@@@@ 63/10/16 A\r\n"
    "80000 30 00 OMZN@@@@ 38/05/15 A\r\n"
    "75170 30 02 OMZN++++ 35/09/28 A\r\n"
    "64830 30 02 omzn     80/02/01 A\r\n"
    "54600 30 01 OMZN$$$$ 26/09/08 A\r\n"
    "40520 30 00 OMZN~~~~ 04/04/11 A\r\n"
    "38400 30 01 omzn     80/02/01 A\r\n"
    "26010 30 02 omzn     98/12/13 A\r\n"
    "15030 30 06 omzo     26/09/14 A\r\n"
    "\032";
#define FIX_BAK_LEN (sizeof fix_bak - 1u)

/* The default ranking: NX93.SCR from the nx93_110.lzh distribution archive.
   340 bytes, 10 line(s). */
static const char fix_def[] =
    "200000 29 00 Bio_100% 93/10/01 A\r\n"
    "180000 28 01 Bio_100% 93/10/01 A\r\n"
    "160000 27 02 Bio_100% 93/10/01 A\r\n"
    "140000 25 03 Bio_100% 93/10/01 A\r\n"
    "120000 22 04 Bio_100% 93/10/01 A\r\n"
    "100000 21 05 Bio_100% 93/10/01 A\r\n"
    "080000 19 06 Bio_100% 93/10/01 A\r\n"
    "060000 16 07 Bio_100% 93/10/01 A\r\n"
    "040000 13 08 Bio_100% 93/10/01 A\r\n"
    "020000 08 09 Bio_100% 93/10/01 A\r\n";
#define FIX_DEF_LEN (sizeof fix_def - 1u)

#endif /* NX93RANK_FIXTURES_H */
