#include "obd_dtc.h"

#include <stdio.h>

void obd_dtc_format(uint16_t code, char out[6])
{
    static const char system[4] = { 'P', 'C', 'B', 'U' };
    snprintf(out, 6, "%c%01X%03X",
             system[(code >> 14) & 0x3],
             (code >> 12) & 0x3,
             code & 0x0FFF);
}

int obd_dtc_parse(const uint8_t *resp, uint16_t len, uint16_t *codes, int max_codes)
{
    if (resp == NULL || len < 1) {
        return 0;
    }

    /* ISO 15765-4 (CAN): [SID+0x40] [count] [hi lo]... -> even length.
     * Older/other transports omit the count:       [SID+0x40] [hi lo]... -> odd length. */
    uint16_t pos = (len % 2 == 0) ? 2 : 1;
    int n = 0;

    for (; pos + 1 < len && n < max_codes; pos += 2) {
        uint16_t code = (uint16_t)((resp[pos] << 8) | resp[pos + 1]);
        if (code != 0) {
            codes[n++] = code;
        }
    }
    return n;
}
