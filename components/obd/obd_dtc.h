/*
 * obd_dtc - Diagnostic Trouble Code helpers (SAE J2012 encoding).
 */
#ifndef OBD_DTC_H
#define OBD_DTC_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* "P0133" etc. `out` must hold at least 6 bytes. */
void obd_dtc_format(uint16_t code, char out[6]);

/* Parse a response to Service 03 / 07 / 0A (0x43 / 0x47 / 0x4A ...).
 * Handles both the CAN form with a count byte and the legacy form without.
 * Zero entries (padding) are skipped. Returns the number of codes written. */
int obd_dtc_parse(const uint8_t *resp, uint16_t len, uint16_t *codes, int max_codes);

#ifdef __cplusplus
}
#endif

#endif /* OBD_DTC_H */
