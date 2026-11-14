/* PC test of components/dbc: tools/test/test_dbc.sh */
#include <stdio.h>
#include <string.h>
#include <math.h>
#include "dbc.h"

static int fails;
#define CHECK(c) do { if (!(c)) { printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); fails++; } } while (0)
#define NEAR(a, b) (fabs((a) - (b)) < 1e-6)

static const char *DBC =
    "VERSION \"\"\n"
    "BO_ 513 BASE1: 8 ECU\n"
    " SG_ RPM : 0|16@1+ (1,0) [0|8000] \"rpm\" Vector__XXX\n"
    " SG_ Timing : 16|16@1- (0.02,0) [-30|60] \"deg\" X\n"
    " SG_ Clt : 32|8@1+ (1,-40) [-40|215] \"C\" X\n"
    "\n"
    "BO_ 512 BASE0: 8 ECU\n"
    " SG_ Warn : 0|16@1+ (1,0) [0|65535] \"\" X\n"
    "BO_ 2566844926 EXTMSG: 8 ECU\n"
    " SG_ BigE : 7|16@0+ (0.1,0) [0|6553.5] \"kPa\" X\n"
    " SG_ Neg8 : 23|8@0- (1,0) [-128|127] \"\" X\n"
    "BO_ 600 MUXED: 8 ECU\n"
    " SG_ Sel M : 0|8@1+ (1,0) [0|255] \"\" X\n"
    " SG_ A m1 : 8|16@1+ (1,0) [0|65535] \"\" X\n"
    " SG_ B m2 : 8|16@1+ (1,0) [0|65535] \"\" X\n"
    "BA_ \"GenMsgCycleTime\" BO_ 513 10;\n"
    "VAL_ 512 Warn 0 \"none\" ;\n";

int main(void)
{
    /* raw extraction */
    uint8_t d1[8] = { 0x34, 0x12, 0, 0, 0, 0, 0, 0 };
    CHECK(dbc_extract(d1, 0, 16, true) == 0x1234);
    CHECK(dbc_extract(d1, 4, 8, true) == 0x23);
    uint8_t d2[8] = { 0x12, 0x34, 0x56, 0, 0, 0, 0, 0 };
    CHECK(dbc_extract(d2, 7, 16, false) == 0x1234);     /* Motorola, bytes 0-1 */
    CHECK(dbc_extract(d2, 15, 16, false) == 0x3456);    /* bytes 1-2 */
    CHECK(dbc_extract(d2, 3, 4, false) == 0x2);         /* low nibble of byte 0 */
    CHECK(dbc_extract(d2, 7, 4, false) == 0x1);         /* high nibble of byte 0 */

    static dbc_message_t msgs[16];
    static dbc_signal_t sigs[32];
    dbc_t db = { .msgs = msgs, .sigs = sigs, .max_msg = 16, .max_sig = 32 };
    FILE *f = fmemopen((void *)DBC, strlen(DBC), "r");
    int n = dbc_parse(&db, f);
    fclose(f);
    CHECK(n == 9);
    CHECK(db.nmsg == 4);
    CHECK(db.skipped == 0);
    CHECK(msgs[0].id == 512 && msgs[1].id == 513 && msgs[2].id == 600);    /* sorted, ext last */
    CHECK(msgs[3].ext && msgs[3].id == (2566844926u & 0x1FFFFFFF));

    int rpm = dbc_find(&db, "rpm"), tim = dbc_find(&db, "Timing"), clt = dbc_find(&db, "CLT");
    CHECK(rpm >= 0 && tim >= 0 && clt >= 0);
    CHECK(strcmp(sigs[rpm].unit, "rpm") == 0);
    uint8_t f1[8] = { 0xB8, 0x0B, 0x9C, 0xFF, 130, 0, 0, 0 };  /* 3000 rpm, -100 * 0.02 = -2 deg, 90 C */
    CHECK(dbc_decode(&db, 513, false, f1, 8, 100) >= 0);
    CHECK(NEAR(sigs[rpm].value, 3000));
    CHECK(NEAR(sigs[tim].value, -2.0));
    CHECK(NEAR(sigs[clt].value, 90));
    CHECK(sigs[rpm].ts_ms == 100);
    CHECK(dbc_decode(&db, 514, false, f1, 8, 100) == -1);
    CHECK(dbc_decode(&db, 513, true, f1, 8, 100) == -1);       /* extended 513 is another ID */

    int big = dbc_find(&db, "BigE"), neg = dbc_find(&db, "Neg8");
    uint8_t f2[8] = { 0x03, 0xE8, 0xFE, 0, 0, 0, 0, 0 };       /* 1000 * 0.1 = 100 kPa, -2 */
    CHECK(dbc_decode(&db, 2566844926u & 0x1FFFFFFF, true, f2, 8, 5) >= 0);
    CHECK(NEAR(sigs[big].value, 100.0));
    CHECK(NEAR(sigs[neg].value, -2));

    int a = dbc_find(&db, "A"), b = dbc_find(&db, "B");
    uint8_t m1[8] = { 1, 0x10, 0x00 }, m2[8] = { 2, 0x20, 0x00 };
    dbc_decode(&db, 600, false, m1, 8, 7);
    CHECK(NEAR(sigs[a].value, 0x10) && sigs[b].ts_ms == 0);
    dbc_decode(&db, 600, false, m2, 8, 8);
    CHECK(NEAR(sigs[a].value, 0x10) && NEAR(sigs[b].value, 0x20));

    /* short frame: Clt (byte 4) not in a 4-byte frame, keeps its value */
    uint8_t f3[8] = { 0xD0, 0x07, 0, 0, 0 };
    dbc_decode(&db, 513, false, f3, 4, 200);
    CHECK(NEAR(sigs[rpm].value, 2000) && NEAR(sigs[clt].value, 90));

    printf(fails ? "%d FAILED\n" : "all dbc tests passed\n", fails);
    return fails != 0;
}
