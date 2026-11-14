/*
 * Dash page (B1 of the roadmap): what dash_svc decodes from the CAN
 * broadcast with the DBC on the SD card, as a live signal table.
 *   dash_status, dash_signals (monospace: objects.dash_signal_table)
 * Decoding runs only while the page is shown. The DBC is loaded the first
 * time the page opens, and again with Reload DBC.
 */
#include <stdio.h>
#include "esp_attr.h"
#include "bridge.h"
#include "dash_svc.h"
#include "sd_card.h"

#define ROWS    160

static uivar_t v_status = UIVAR("dash_status");
static uivar_t v_signals = UIVAR("dash_signals");

static bool s_tried;
EXT_RAM_BSS_ATTR static char s_table[ROWS * 64 + 128];

void view_dash_reload(void)
{
    dash_svc_load();
    s_tried = true;
}

void view_dash_update(bool visible)
{
    dash_svc_set_active(visible);
    if (!visible) {
        return;
    }
    if (!s_tried && waveshare_sd_card_mounted()) {
        view_dash_reload();
    }

    dash_svc_status_t st;
    dash_svc_get_status(&st);
    if (!waveshare_sd_card_mounted()) {
        uv_str(&v_status, "No SD card: put the DBC (e.g. rusEFI's) in /db/dbc/ on the card.");
    } else if (!st.loaded) {
        uv_strf(&v_status, "%s\nPut a .dbc file in /db/dbc/ on the SD card, then Reload DBC.",
                st.error[0] ? st.error : "no DBC loaded");
    } else {
        uv_strf(&v_status, "%s: %u messages, %u signals%s\n"
                "%lu frames decoded, %u of %u messages seen, %lu frames with other IDs%s",
                st.file, st.nmsg, st.nsig, st.skipped ? " (some lines skipped)" : "",
                (unsigned long)st.frames, st.msgs_seen, st.nmsg, (unsigned long)st.unknown,
                st.frames == 0 ? "\nNothing yet: is the ECU broadcasting, at the CAN page's bitrate?" : "");
    }
    if (st.loaded) {
        dash_svc_format_signals(s_table, sizeof(s_table), ROWS);
        uv_str(&v_signals, s_table);
    } else {
        uv_str(&v_signals, "");
    }
}
