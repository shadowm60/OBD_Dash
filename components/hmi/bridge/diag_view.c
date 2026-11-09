/*
 * diag_view - DTC and freeze frame screens.
 *
 * The EEZ project has the widgets but no events for Read / Clear / Save, so the
 * handlers are attached here. Once the screens are redesigned in EEZ Studio
 * these can become regular EEZ actions calling the same functions.
 */
#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include <sys/stat.h>
#include "lvgl.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "screens.h"
#include "vars.h"
#include "eez_vars.h"
#include "comsm.h"
#include "obd_dtc.h"
#include "vehicle_data.h"
#include "views.h"

static const char *TAG = "diag_view";

#define SD_MOUNT_POINT  "/sdcard"   /* board/sd_card.h MOUNT_POINT */
#define DTC_TEXT_LEN    1024
#define FF_TEXT_LEN     1536

extern objects_t objects;

static char      s_dtc_text[DTC_TEXT_LEN];
static uint32_t  s_dtc_seq = UINT32_MAX;
static uint32_t  s_ff_seq = UINT32_MAX;
static lv_obj_t *s_ff_label;

/* ---------------------------------------------------------------- formatting */

static const char *kind_title[COMSM_DTC_KIND_COUNT] = {
    [COMSM_DTC_STORED]    = "Stored",
    [COMSM_DTC_PENDING]   = "Pending",
    [COMSM_DTC_PERMANENT] = "Permanent",
};

static size_t appendf(char *buf, size_t cap, size_t pos, const char *fmt, ...)
{
    if (pos >= cap) {
        return pos;
    }
    va_list ap;
    va_start(ap, fmt);
    int n = vsnprintf(buf + pos, cap - pos, fmt, ap);
    va_end(ap);
    return (n < 0) ? pos : pos + (size_t)n;
}

static void format_dtc_report(const comsm_dtc_report_t *r, char *buf, size_t cap)
{
    size_t p = 0;
    buf[0] = '\0';

    switch (r->state) {
    case COMSM_DIAG_IDLE:
        appendf(buf, cap, 0, "Press Read to read trouble codes.");
        return;
    case COMSM_DIAG_BUSY:
        appendf(buf, cap, 0, "Reading...");
        return;
    case COMSM_DIAG_FAILED:
        appendf(buf, cap, 0, "Error: %s", r->error);
        return;
    case COMSM_DIAG_DONE:
        break;
    }

    if (r->cleared) {
        p = appendf(buf, cap, p, "Trouble codes cleared.\n\n");
    }
    for (int k = 0; k < COMSM_DTC_KIND_COUNT; k++) {
        if (!r->supported[k]) {
            p = appendf(buf, cap, p, "%s: not supported by ECU\n", kind_title[k]);
            continue;
        }
        if (r->count[k] == 0) {
            p = appendf(buf, cap, p, "%s: none\n", kind_title[k]);
            continue;
        }
        p = appendf(buf, cap, p, "%s (%u):\n", kind_title[k], r->count[k]);
        for (int i = 0; i < r->count[k]; i++) {
            char code[6];
            obd_dtc_format(r->codes[k][i], code);
            p = appendf(buf, cap, p, "   %s\n", code);
        }
    }
}

static void format_freeze_frame(const comsm_freeze_frame_t *f, char *buf, size_t cap)
{
    size_t p = 0;
    buf[0] = '\0';

    switch (f->state) {
    case COMSM_DIAG_IDLE:
        appendf(buf, cap, 0, "Freeze frame is read when this screen opens (connection required).");
        return;
    case COMSM_DIAG_BUSY:
        appendf(buf, cap, 0, "Reading freeze frame...");
        return;
    case COMSM_DIAG_FAILED:
        appendf(buf, cap, 0, "Error: %s", f->error);
        return;
    case COMSM_DIAG_DONE:
        break;
    }

    if (f->dtc == 0) {
        appendf(buf, cap, 0, "No freeze frame stored.");
        return;
    }
    char code[6];
    obd_dtc_format(f->dtc, code);
    p = appendf(buf, cap, p, "Freeze frame for %s\n\n", code);
    for (int i = 0; i < f->count; i++) {
        const vd_signal_info_t *info = vd_info(f->values[i].signal);
        p = appendf(buf, cap, p, "%-20s %8.*f %s\n",
                    info->name, info->decimals, f->values[i].value, info->unit);
    }
}

/* ---------------------------------------------------------------- message box */

static void msgbox_close_cb(lv_event_t *e)
{
    lv_obj_t *mbox = lv_event_get_current_target(e);
    lv_msgbox_close_async(mbox);   /* deleting inside its own event is unsafe */
}

static void show_message(const char *title, const char *text)
{
    static const char *btns[] = { "OK", "" };
    lv_obj_t *mbox = lv_msgbox_create(NULL, title, text, btns, false);
    lv_obj_set_style_text_font(mbox, &lv_font_montserrat_20, LV_PART_MAIN);
    lv_obj_add_event_cb(mbox, msgbox_close_cb, LV_EVENT_VALUE_CHANGED, NULL);
    lv_obj_center(mbox);
}

static void clear_confirm_cb(lv_event_t *e)
{
    lv_obj_t *mbox = lv_event_get_current_target(e);
    const char *btn = lv_msgbox_get_active_btn_text(mbox);
    if (btn && strcmp(btn, "Clear") == 0) {
        ESP_LOGI(TAG, "user confirmed DTC clear");
        comsm_clear_dtcs();
    }
    lv_msgbox_close_async(mbox);   /* deleting inside its own event is unsafe */
}

/* ---------------------------------------------------------------- button handlers */

static void read_btn_cb(lv_event_t *e)
{
    comsm_read_dtcs();
}

static void clear_btn_cb(lv_event_t *e)
{
    static const char *btns[] = { "Clear", "Cancel", "" };
    lv_obj_t *mbox = lv_msgbox_create(NULL, "Clear trouble codes?",
        "This erases stored and pending codes and the freeze frame, "
        "turns the MIL off and resets the readiness monitors.\n\n"
        "Use with ignition on and engine off.",
        btns, false);
    lv_obj_set_style_text_font(mbox, &lv_font_montserrat_20, LV_PART_MAIN);
    lv_obj_set_width(mbox, 520);
    lv_obj_add_event_cb(mbox, clear_confirm_cb, LV_EVENT_VALUE_CHANGED, NULL);
    lv_obj_center(mbox);
}

static void save_btn_cb(lv_event_t *e)
{
    static comsm_dtc_report_t r;
    comsm_get_dtc_report(&r);
    if (r.state != COMSM_DIAG_DONE) {
        show_message("Save", "Read the trouble codes first.");
        return;
    }

    char path[40];
    struct stat st;
    int n;
    for (n = 1; n < 1000; n++) {
        snprintf(path, sizeof(path), SD_MOUNT_POINT "/dtc_%03d.txt", n);
        if (stat(path, &st) != 0) {
            break;
        }
    }

    FILE *f = (n < 1000) ? fopen(path, "w") : NULL;
    if (f == NULL) {
        show_message("Save failed", "Could not write to the SD card.");
        return;
    }
    comsm_status_t cs;
    comsm_get_status(&cs);
    fprintf(f, "OBD_Dash DTC report\nECU: 0x%03lX\nUptime: %llu s\n\n%s\n",
            (unsigned long)cs.ecu_id, esp_timer_get_time() / 1000000ULL, s_dtc_text);
    fclose(f);

    char msg[64];
    snprintf(msg, sizeof(msg), "Saved to %s", path);
    show_message("Saved", msg);
    ESP_LOGI(TAG, "%s", msg);
}

static void dtc_screen_loaded_cb(lv_event_t *e)
{
    comsm_status_t cs;
    comsm_get_status(&cs);
    if (cs.state == COMSM_RUNNING) {
        comsm_read_dtcs();
    }
}

static void ff_screen_loaded_cb(lv_event_t *e)
{
    comsm_read_freeze_frame();
}

/* ---------------------------------------------------------------- view API */

void diag_view_init(void)
{
    lv_obj_add_event_cb(objects.read_dtc_bt, read_btn_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_add_event_cb(objects.clear_dtc_bt, clear_btn_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_add_event_cb(objects.save_dtc_bt, save_btn_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_add_event_cb(objects.read_dtc, dtc_screen_loaded_cb, LV_EVENT_SCREEN_LOADED, NULL);

    /* Freeze frame screen is empty in the EEZ project: add a text label. */
    s_ff_label = lv_label_create(objects.freez_frame);
    lv_obj_set_width(s_ff_label, lv_pct(95));
    lv_obj_align(s_ff_label, LV_ALIGN_TOP_LEFT, 20, 20);
    lv_label_set_long_mode(s_ff_label, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_font(s_ff_label, &lv_font_montserrat_20, LV_PART_MAIN);
    lv_obj_add_event_cb(objects.freez_frame, ff_screen_loaded_cb, LV_EVENT_SCREEN_LOADED, NULL);
}

void diag_view_refresh(void)
{
    /* static: these are a few hundred bytes and only used from the LVGL task */
    static comsm_dtc_report_t   r;
    static comsm_freeze_frame_t f;
    static char                 ff_text[FF_TEXT_LEN];

    comsm_get_dtc_report(&r);
    if (r.seq != s_dtc_seq) {
        s_dtc_seq = r.seq;
        format_dtc_report(&r, s_dtc_text, sizeof(s_dtc_text));
        eez_var_set_string(FLOW_GLOBAL_VARIABLE_READ_DTC_TEXT, s_dtc_text);
    }

    comsm_get_freeze_frame(&f);
    if (f.seq != s_ff_seq) {
        s_ff_seq = f.seq;
        format_freeze_frame(&f, ff_text, sizeof(ff_text));
        lv_label_set_text(s_ff_label, ff_text);
    }
}
