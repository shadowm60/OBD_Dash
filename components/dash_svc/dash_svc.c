#include "dash_svc.h"

#include <stdio.h>
#include <string.h>
#include <strings.h>
#include <math.h>
#include <dirent.h>
#include "esp_log.h"
#include "esp_heap_caps.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "can_if.h"
#include "dbc.h"

static const char *TAG = "dash_svc";

#define MAX_MSG     256
#define MAX_SIG     1024
#define QUEUE_LEN   512

static SemaphoreHandle_t s_lock;
static QueueHandle_t     s_q;
static dbc_t             s_db;
static dash_svc_status_t s_st;
static uint8_t          *s_seen;            /* per message: received at least once */

#define LOCK()   xSemaphoreTake(s_lock, portMAX_DELAY)
#define UNLOCK() xSemaphoreGive(s_lock)

static bool has_ext(const char *name, const char *ext)
{
    size_t n = strlen(name), e = strlen(ext);
    return n > e && strcasecmp(name + n - e, ext) == 0;
}

/* First *.dbc in dir (alphabetically smallest, so the choice is stable). */
static bool find_dbc(const char *dir, char *out, size_t len)
{
    DIR *d = opendir(dir);
    if (d == NULL) {
        return false;
    }
    char best[64] = "";
    struct dirent *e;
    while ((e = readdir(d)) != NULL) {
        if (e->d_type != DT_DIR && has_ext(e->d_name, ".dbc") && strlen(e->d_name) < sizeof(best) &&
            (best[0] == '\0' || strcasecmp(e->d_name, best) < 0)) {
            strcpy(best, e->d_name);
        }
    }
    closedir(d);
    if (best[0] == '\0') {
        return false;
    }
    snprintf(out, len, "%s/%s", dir, best);
    return true;
}

esp_err_t dash_svc_load(void)
{
    char path[128];
    if (!find_dbc(DASH_DBC_DIR, path, sizeof(path)) && !find_dbc("/sdcard", path, sizeof(path))) {
        LOCK();
        s_st.loaded = false;
        s_st.file[0] = '\0';
        s_db.nmsg = s_db.nsig = 0;
        snprintf(s_st.error, sizeof(s_st.error), "no .dbc file in %s or /sdcard", DASH_DBC_DIR);
        UNLOCK();
        return ESP_ERR_NOT_FOUND;
    }
    FILE *f = fopen(path, "r");
    if (f == NULL) {
        LOCK();
        snprintf(s_st.error, sizeof(s_st.error), "cannot open the DBC");
        UNLOCK();
        return ESP_FAIL;
    }
    LOCK();
    int n = dbc_parse(&s_db, f);
    memset(s_seen, 0, MAX_MSG);
    s_st.loaded = n > 0;
    s_st.nmsg = s_db.nmsg;
    s_st.nsig = s_db.nsig;
    s_st.skipped = s_db.skipped;
    s_st.frames = s_st.unknown = s_st.dropped = 0;
    s_st.msgs_seen = 0;
    const char *base = strrchr(path, '/');
    strlcpy(s_st.file, base ? base + 1 : path, sizeof(s_st.file));
    if (n < 0) {
        snprintf(s_st.error, sizeof(s_st.error), "read error");
    } else if (n == 0) {
        snprintf(s_st.error, sizeof(s_st.error), "no signals found in the file");
    } else {
        s_st.error[0] = '\0';
    }
    UNLOCK();
    fclose(f);
    ESP_LOGI(TAG, "%s: %u messages, %u signals, %lu lines skipped", path, s_db.nmsg, s_db.nsig,
             (unsigned long)s_db.skipped);
    return n > 0 ? ESP_OK : ESP_FAIL;
}

void dash_svc_set_active(bool on)
{
    if (s_lock == NULL) {
        return;
    }
    LOCK();
    bool was = s_st.active;
    s_st.active = on;
    UNLOCK();
    if (on == was) {
        return;
    }
    if (on) {
        xQueueReset(s_q);
        can_if_set_monitor(CAN_IF_MON_DASH, s_q);
        can_if_set_accept_all(CAN_IF_ALL_DASH, true);
    } else {
        can_if_set_monitor(CAN_IF_MON_DASH, NULL);
        can_if_set_accept_all(CAN_IF_ALL_DASH, false);
    }
    ESP_LOGI(TAG, "decoding %s", on ? "on" : "off");
}

static void dash_task(void *arg)
{
    can_frame_t f;
    while (1) {
        if (xQueueReceive(s_q, &f, portMAX_DELAY) != pdTRUE) {
            continue;
        }
        int n = 0;
        LOCK();
        do {
            if (f.tx) {
                continue;               /* our own OBD requests */
            }
            int m = dbc_decode(&s_db, f.id, f.ext, f.data, f.dlc, f.ts_ms);
            if (m >= 0) {
                s_st.frames++;
                if (!s_seen[m]) {
                    s_seen[m] = 1;
                    s_st.msgs_seen++;
                }
            } else {
                s_st.unknown++;
            }
        } while (++n < 64 && xQueueReceive(s_q, &f, 0) == pdTRUE);
        UNLOCK();
    }
}

void dash_svc_get_status(dash_svc_status_t *out)
{
    if (s_lock == NULL) {
        memset(out, 0, sizeof(*out));
        return;
    }
    LOCK();
    *out = s_st;
    UNLOCK();
}

/* decimals from the signal's resolution: scale 1 -> 0, 0.1 -> 1, 0.02 -> 2 */
static int decimals(double scale)
{
    if (scale <= 0 || scale >= 1) {
        return 0;
    }
    int d = (int)ceil(-log10(scale) - 1e-9);
    return d > 3 ? 3 : d;
}

int dash_svc_format_signals(char *buf, size_t len, int max_rows)
{
    size_t o = 0;
    buf[0] = '\0';
    if (s_lock == NULL) {
        return 0;
    }
    uint32_t now = can_if_now_ms();
    o += snprintf(buf + o, len - o, "%-22s %12s %-8s %s\n", "SIGNAL", "VALUE", "UNIT", "AGE");
    LOCK();
    int rows = 0;
    for (uint16_t i = 0; i < s_db.nsig && rows < max_rows && o + 80 < len; i++) {
        const dbc_signal_t *s = &s_db.sigs[i];
        if (s->ts_ms == 0) {
            o += snprintf(buf + o, len - o, "%-22.22s %12s %-8.8s\n", s->name, "--", s->unit);
        } else {
            uint32_t age = now - s->ts_ms;
            char a[12];
            if (age < 1000) {
                snprintf(a, sizeof(a), "%lums", (unsigned long)age);
            } else {
                snprintf(a, sizeof(a), "%lus", (unsigned long)(age / 1000));
            }
            o += snprintf(buf + o, len - o, "%-22.22s %12.*f %-8.8s %s\n", s->name, decimals(s->scale),
                          s->value, s->unit, a);
        }
        rows++;
    }
    int n = s_db.nsig;
    UNLOCK();
    if (n > max_rows && o + 32 < len) {
        snprintf(buf + o, len - o, "... %d more signals\n", n - max_rows);
    }
    return n;
}

bool dash_svc_get(const char *name, double *value, uint32_t *age_ms, char *unit, size_t unit_len)
{
    if (s_lock == NULL) {
        return false;
    }
    LOCK();
    int i = dbc_find(&s_db, name);
    bool ok = i >= 0 && s_db.sigs[i].ts_ms != 0;
    if (ok) {
        *value = s_db.sigs[i].value;
        if (age_ms) {
            *age_ms = can_if_now_ms() - s_db.sigs[i].ts_ms;
        }
        if (unit) {
            snprintf(unit, unit_len, "%s", s_db.sigs[i].unit);
        }
    }
    UNLOCK();
    return ok;
}

esp_err_t dash_svc_init(void)
{
    s_lock = xSemaphoreCreateMutex();
    s_q = xQueueCreateWithCaps(QUEUE_LEN, sizeof(can_frame_t), MALLOC_CAP_SPIRAM);
    s_db.msgs = heap_caps_calloc(MAX_MSG, sizeof(dbc_message_t), MALLOC_CAP_SPIRAM);
    s_db.sigs = heap_caps_calloc(MAX_SIG, sizeof(dbc_signal_t), MALLOC_CAP_SPIRAM);
    s_seen = heap_caps_calloc(MAX_MSG, 1, MALLOC_CAP_SPIRAM);
    if (!s_lock || !s_q || !s_db.msgs || !s_db.sigs || !s_seen) {
        return ESP_ERR_NO_MEM;
    }
    s_db.max_msg = MAX_MSG;
    s_db.max_sig = MAX_SIG;
    /* stack in PSRAM: decoding only, never writes flash (the DBC is read in the caller's task) */
    if (xTaskCreatePinnedToCoreWithCaps(dash_task, "dash", 4096, NULL, 4, NULL, 0, MALLOC_CAP_SPIRAM) != pdPASS) {
        return ESP_ERR_NO_MEM;
    }
    return ESP_OK;
}
