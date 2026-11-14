/*
 * diagdb - diagnostic database (docs/diag_db.md): ECUs, data identifiers,
 * DTC texts of one make, parsed from /db/diag/<make>.txt. The columns
 * marked "sim" in the spec are skipped (they are for a bench simulator).
 * tools/diagdb.py is the reference implementation of the same format.
 *
 * Pure C, no ESP-IDF: also builds on the PC (tools/test/test_diagdb_c.sh).
 * Caller provides the tables (PSRAM on the dash). Not thread safe.
 */
#ifndef DIAGDB_H
#define DIAGDB_H

#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>

#ifdef __cplusplus
extern "C" {
#endif

#define DIAGDB_NAME_LEN  24
#define DIAGDB_TEXT_LEN  64
#define DIAGDB_UNIT_LEN  8
#define DIAGDB_ALL       (-1)        /* entry for every ECU ('*') */

typedef enum { DIAGDB_ASCII, DIAGDB_HEX, DIAGDB_U8, DIAGDB_U16, DIAGDB_U32, DIAGDB_S8, DIAGDB_S16, DIAGDB_S32 } diagdb_type_t;

typedef struct {
    char     name[DIAGDB_NAME_LEN];
    uint16_t tx, rx;
} diagdb_ecu_t;

typedef struct {
    int16_t       ecu;              /* index or DIAGDB_ALL */
    uint16_t      did;
    char          name[DIAGDB_TEXT_LEN];
    diagdb_type_t type;
    double        scale, offset;
    char          unit[DIAGDB_UNIT_LEN];
} diagdb_did_t;

typedef struct {
    int16_t  ecu;
    uint32_t code;                  /* 3 bytes: SAE code + failure type byte */
    char     text[DIAGDB_TEXT_LEN];
} diagdb_dtc_t;

typedef struct {
    diagdb_ecu_t *ecus;
    diagdb_did_t *dids;
    diagdb_dtc_t *dtcs;
    uint16_t      max_ecu, max_did, max_dtc;
    uint16_t      necu, ndid, ndtc;
    char          error[80];        /* first problem: "line 12: ..." ("" = none) */
    uint32_t      skipped;          /* lines not understood (the first one in error) */
} diagdb_t;

/* Parse a whole file. Returns false on a read error or if nothing usable was
 * found; bad lines are skipped and counted, the first one described in error. */
bool diagdb_parse(diagdb_t *db, FILE *f);

int  diagdb_find_ecu(const diagdb_t *db, const char *name);
/* ECU specific entry first, then '*'; NULL if none. */
const diagdb_did_t *diagdb_find_did(const diagdb_t *db, int ecu, uint16_t did);
/* ECU text, '*' text, then the same SAE code with failure type 00; NULL if none. */
const char *diagdb_dtc_text(const diagdb_t *db, int ecu, uint32_t code);

/* "P0301" / "P0301-1C" / "030100" -> code; false if malformed. */
bool diagdb_parse_dtc(const char *s, uint32_t *code);
/* code -> "P0301" or "P0301-1C" (buf >= 10). */
void diagdb_format_dtc(uint32_t code, char *buf);
/* Data bytes of a 0x62 answer -> text ("850 rpm", "WVW...", "01 02 0A"). */
void diagdb_decode(const diagdb_did_t *d, const uint8_t *data, int len, char *out, int out_len);

#ifdef __cplusplus
}
#endif

#endif /* DIAGDB_H */
