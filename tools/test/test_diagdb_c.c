/* PC test of components/diagdb (same cases as test_diagdb.py where they overlap) */
#include <stdio.h>
#include <string.h>
#include "diagdb.h"

static int fails;
#define CHECK(c) do { if (!(c)) { printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); fails++; } } while (0)

static const char *TEXT =
    "[ecu]\n"
    "Engine | 7E0 | 7E8 | uds | 10 3E 22 19 14 | 18 02 FF 00\n"
    "Cluster | 714 | 77E | uds | 3E 22      # only these two\n"
    "Airbag | 715 | 77F | kwp\n"
    "[did]\n"
    "* | F190 | VIN | ascii | | | | WVWZZZ1KZ6W000001\n"
    "Engine | F40C | Engine speed | u16 | 0.25 | 0 | rpm | 850\n"
    "Engine | 1000 | Coolant | s8 | 1 | 0 | C | -12\n"
    "Engine | 1002 | Raw | hex | | | | 01 02 0A\n"
    "Engine | F190 | VIN (engine) | ascii\n"
    "[kwp_id]\n"
    "Airbag | 80 | ECU name | ascii\n"
    "* | 81 | ECU serial | hex\n"
    "[dtc]\n"
    "* | P0301 | Cylinder 1 misfire detected\n"
    "Engine | P0301 | Cylinder 1 misfire (engine) | 2F\n"
    "Engine | U0100-87 | Lost communication with ECM | 08\n"
    "[bogus]\n"
    "x | y\n";

int main(int argc, char **argv)
{
    uint32_t c;
    CHECK(diagdb_parse_dtc("P0301", &c) && c == 0x030100);
    CHECK(diagdb_parse_dtc("u0100-87", &c) && c == 0xC10087);
    CHECK(diagdb_parse_dtc("B1234", &c) && c == 0x923400);
    CHECK(diagdb_parse_dtc("030100", &c) && c == 0x030100);
    CHECK(!diagdb_parse_dtc("X0301", &c) && !diagdb_parse_dtc("P03011", &c));
    char b[16];
    diagdb_format_dtc(0xC10087, b);
    CHECK(strcmp(b, "U0100-87") == 0);
    diagdb_format_dtc(0x030100, b);
    CHECK(strcmp(b, "P0301") == 0);

    static diagdb_ecu_t ecus[8];
    static diagdb_did_t dids[16];
    static diagdb_kwp_id_t kwp_ids[16];
    static diagdb_dtc_t dtcs[16];
    diagdb_t db = { .ecus = ecus, .dids = dids, .kwp_ids = kwp_ids, .dtcs = dtcs,
                    .max_ecu = 8, .max_did = 16, .max_kwp_id = 16, .max_dtc = 16 };
    FILE *f = fmemopen((void *)TEXT, strlen(TEXT), "r");
    CHECK(diagdb_parse(&db, f));
    fclose(f);
    CHECK(db.necu == 3 && db.ndid == 5 && db.nkwp_id == 2 && db.ndtc == 3);
    CHECK(db.skipped == 2);                     /* [bogus] + its line */
    CHECK(strncmp(db.error, "line 18", 7) == 0);
    int eng = diagdb_find_ecu(&db, "engine"), clu = diagdb_find_ecu(&db, "Cluster");
    CHECK(eng == 0 && clu == 1 && ecus[1].tx == 0x714 && ecus[1].rx == 0x77E);
    int airbag = diagdb_find_ecu(&db, "Airbag");
    CHECK(airbag == 2 && ecus[airbag].protocol == DIAGDB_PROTOCOL_KWP);
    CHECK(ecus[airbag].kwp_dtc_len == 4 &&
          memcmp(ecus[airbag].kwp_dtc_req, "\x18\x00\xFF\x00", 4) == 0);
    CHECK(ecus[eng].kwp_dtc_len == 4 && ecus[eng].kwp_dtc_req[1] == 0x02);

    const diagdb_did_t *d = diagdb_find_did(&db, eng, 0xF190);
    CHECK(d && strcmp(d->name, "VIN (engine)") == 0);       /* ECU entry wins */
    d = diagdb_find_did(&db, clu, 0xF190);
    CHECK(d && strcmp(d->name, "VIN") == 0);
    CHECK(diagdb_find_did(&db, clu, 0xF40C) == NULL);
    CHECK(diagdb_find_kwp_id(&db, airbag, 0x80) &&
          strcmp(diagdb_find_kwp_id(&db, airbag, 0x80)->name, "ECU name") == 0);
    CHECK(diagdb_find_kwp_id(&db, eng, 0x81) &&
          diagdb_find_kwp_id(&db, eng, 0x81)->ecu == DIAGDB_ALL);

    char out[64];
    diagdb_decode(diagdb_find_did(&db, eng, 0xF40C), (const uint8_t *)"\x0D\x48", 2, out, sizeof(out));
    CHECK(strcmp(out, "850 rpm") == 0);
    diagdb_decode(diagdb_find_did(&db, eng, 0x1000), (const uint8_t *)"\xF4", 1, out, sizeof(out));
    CHECK(strcmp(out, "-12 C") == 0);
    diagdb_decode(diagdb_find_did(&db, eng, 0x1002), (const uint8_t *)"\x01\x02\x0A", 3, out, sizeof(out));
    CHECK(strcmp(out, "01 02 0A") == 0);
    diagdb_decode(diagdb_find_did(&db, clu, 0xF190), (const uint8_t *)"WVW  \0\xFF", 7, out, sizeof(out));
    CHECK(strcmp(out, "WVW") == 0);
    diagdb_decode(diagdb_find_did(&db, eng, 0xF40C), (const uint8_t *)"\x0D", 1, out, sizeof(out));
    CHECK(strstr(out, "too short") != NULL);

    CHECK(strcmp(diagdb_dtc_text(&db, eng, 0x030100), "Cylinder 1 misfire (engine)") == 0);
    CHECK(strcmp(diagdb_dtc_text(&db, clu, 0x030100), "Cylinder 1 misfire detected") == 0);
    CHECK(strcmp(diagdb_dtc_text(&db, clu, 0x03011C), "Cylinder 1 misfire detected") == 0);
    CHECK(diagdb_dtc_text(&db, clu, 0x030200) == NULL);

    if (argc > 1) {                              /* the starter file */
        FILE *g = fopen(argv[1], "r");
        CHECK(g && diagdb_parse(&db, g));
        if (g) fclose(g);
        CHECK(db.necu == 2 && db.skipped == 0 && db.ndtc == 16);
    }
    printf(fails ? "%d FAILED\n" : "all diagdb (C) tests passed\n", fails);
    return fails != 0;
}
