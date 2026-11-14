/*
 * dbc - CAN database (Vector .dbc) subset: parse messages + signals, decode
 * frames into physical values.
 *
 * Supported: BO_ (11-bit and 29-bit IDs, bit 31 = extended), SG_ with Intel
 * (@1) and Motorola (@0) byte order, signed / unsigned, scale + offset, unit,
 * min / max, simple multiplexing (M / mN). Ignored: everything else (value
 * tables, attributes, comments, float signals via SIG_VALTYPE_ are decoded
 * as integers).
 *
 * Pure C, no ESP-IDF: also builds on the PC (tools/test_dbc.sh). Not thread
 * safe: one owner decodes (dash_svc), readers copy values out under its lock.
 */
#ifndef DBC_H
#define DBC_H

#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>

#ifdef __cplusplus
extern "C" {
#endif

#define DBC_NAME_LEN   32
#define DBC_UNIT_LEN   12

typedef struct {
    char     name[DBC_NAME_LEN];
    char     unit[DBC_UNIT_LEN];
    uint16_t msg;               /* index into the message table */
    uint8_t  start;             /* DBC start bit */
    uint8_t  len;               /* 1..64 */
    bool     intel;             /* @1 little endian, else @0 Motorola */
    bool     is_signed;
    int16_t  mux;               /* -1 always present, -2 = this is the multiplexer, >= 0 = only with that mux value */
    double   scale, offset, min, max;
    /* last decoded value */
    double   value;
    uint32_t ts_ms;             /* 0 = never received */
} dbc_signal_t;

typedef struct {
    uint32_t id;                /* 11 or 29 bit */
    bool     ext;
    char     name[DBC_NAME_LEN];
    uint8_t  dlc;
    uint16_t first_sig, nsig;   /* signals of this message: [first_sig, first_sig + nsig) */
    int16_t  mux_sig;           /* index of the multiplexer signal, -1 = none */
    uint32_t count;             /* frames decoded */
} dbc_message_t;

typedef struct {
    dbc_message_t *msgs;        /* sorted by (ext, id) after dbc_parse */
    dbc_signal_t  *sigs;
    uint16_t       nmsg, nsig;
    uint16_t       max_msg, max_sig;
    uint32_t       skipped;     /* SG_ lines that could not be parsed / didn't fit */
} dbc_t;

/* Caller provides the tables (e.g. PSRAM). Returns the number of signals
 * parsed, < 0 on a read error. Lines longer than 512 bytes are skipped. */
int  dbc_parse(dbc_t *db, FILE *f);

/* Decode one frame; returns the message index or -1 if not in the database. */
int  dbc_decode(dbc_t *db, uint32_t id, bool ext, const uint8_t *data, uint8_t dlc, uint32_t ts_ms);

/* Raw bit extraction, exposed for the tests. */
uint64_t dbc_extract(const uint8_t *data, uint8_t start, uint8_t len, bool intel);

/* Signal index by name (case-insensitive), -1 if none. */
int  dbc_find(const dbc_t *db, const char *name);

#ifdef __cplusplus
}
#endif

#endif /* DBC_H */
