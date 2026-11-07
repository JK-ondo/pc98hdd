/* fixtures_daily.h - real NX93 files for the daily-reset suite, byte-exact.
 *
 * GENERATED, do not hand-edit.  Regenerate with the recipe in README.md.
 *
 * They are embedded rather than read at run time because hdd/ is a mirror of
 * the real PC-9801 disk: the suite must neither read from it nor write to it.
 *
 * Every literal is exact including CR, LF and any trailing 0x1A.  The recipe
 * re-checks that the sources hold zero non-ASCII bytes, because the LF/CRLF
 * pair below is only sound while the pull is a byte-for-byte identity.
 */
#ifndef NX93RANK_FIXTURES_DAILY_H
#define NX93RANK_FIXTURES_DAILY_H

/* A:\GAME\NX93\NX93.SCR as the game left it (2026-10-13 pull).
   6 player rows followed by 4 shipped Bio_100% filler rows.
   340 bytes, 10 line(s), CRLF, no 0x1A. */
static const char fix_scr13[] =
    "327400 30 01 OMZN     26/09/15 A\r\n"
    "315970 30 02 omzn     26/10/13 A\r\n"
    "300820 30 01 omzn     26/10/08 A\r\n"
    "300280 30 04 omzn     26/10/13 A\r\n"
    "299370 30 00 OMZN@@@@ 61/11/17 A\r\n"
    "286530 30 01 omzn     26/10/09 A\r\n"
    "200000 29 00 Bio_100% 93/10/01 A\r\n"
    "180000 28 01 Bio_100% 93/10/01 A\r\n"
    "160000 27 02 Bio_100% 93/10/01 A\r\n"
    "140000 25 03 Bio_100% 93/10/01 A\r\n";
#define FIX_SCR13_LEN (sizeof fix_scr13 - 1u)

/* A:\GAME\NX93\NX93.TXT, the old JSON ledger, exactly as 98hdd pull left it:
   581 bytes, LF line ends, zero CR, zero non-ASCII.
   This is the migration input of spec.md section 3.4. */
static const char fix_ledger_lf[] =
    "{\n"
    "  \"lastUpdate\": \"2026-10-08T11:57:39\",\n"
    "  \"backups\": [\n"
    "    {\n"
    "      \"backupAt\": \"2026-10-08T11:57:39\",\n"
    "      \"ranking\": [\n"
    "        \"327400 30 01 OMZN     26/09/15 A\",\n"
    "        \"316770 30 02 OMZN     26/09/17 A\",\n"
    "        \"315870 30 01 OMZN     26/09/16 A\",\n"
    "        \"304890 30 00 omzn     26/09/25 A\",\n"
    "        \"299370 30 00 OMZN@@@@ 61/11/17 A\",\n"
    "        \"291290 30 02 omzn     26/09/18 A\",\n"
    "        \"286340 30 01 omzn     26/09/15 A\",\n"
    "        \"285680 30 05 omzn     26/09/15 A\",\n"
    "        \"265680 29 06 omzn     26/09/25 A\",\n"
    "        \"261740 29 05 omzn     26/09/14 A\"\n"
    "      ]\n"
    "    }\n"
    "  ]\n"
    "}\n";
#define FIX_LEDGER_LF_LEN (sizeof fix_ledger_lf - 1u)

/* The same ledger with every LF restored to CRLF: 601 bytes
   (= 581 + 20 LF).  RECONSTRUCTED, not observed: whether the
   file on the 98 really holds these bytes belongs to the on-machine checks
   (spec.md section 7 H).  Deleting every CR yields fix_ledger_lf, which the
   recipe verifies; the grammar's ws rule must absorb the difference, so both
   fixtures have to import to byte-identical tables. */
static const char fix_ledger_crlf[] =
    "{\r\n"
    "  \"lastUpdate\": \"2026-10-08T11:57:39\",\r\n"
    "  \"backups\": [\r\n"
    "    {\r\n"
    "      \"backupAt\": \"2026-10-08T11:57:39\",\r\n"
    "      \"ranking\": [\r\n"
    "        \"327400 30 01 OMZN     26/09/15 A\",\r\n"
    "        \"316770 30 02 OMZN     26/09/17 A\",\r\n"
    "        \"315870 30 01 OMZN     26/09/16 A\",\r\n"
    "        \"304890 30 00 omzn     26/09/25 A\",\r\n"
    "        \"299370 30 00 OMZN@@@@ 61/11/17 A\",\r\n"
    "        \"291290 30 02 omzn     26/09/18 A\",\r\n"
    "        \"286340 30 01 omzn     26/09/15 A\",\r\n"
    "        \"285680 30 05 omzn     26/09/15 A\",\r\n"
    "        \"265680 29 06 omzn     26/09/25 A\",\r\n"
    "        \"261740 29 05 omzn     26/09/14 A\"\r\n"
    "      ]\r\n"
    "    }\r\n"
    "  ]\r\n"
    "}\r\n";
#define FIX_LEDGER_CRLF_LEN (sizeof fix_ledger_crlf - 1u)

#endif /* NX93RANK_FIXTURES_DAILY_H */
