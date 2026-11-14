#include "datalog.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <inttypes.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <time.h>
#include "esp_log.h"
#include "esp_heap_caps.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "can_if.h"
#include "comsm.h"
#include "obd_pids.h"
#include "vehicle_data.h"

static const char *TAG = "datalog";

#define TASK_STACK          4096
#define TASK_PRIO           3           /* below comsm (6) and LVGL: logging never slows polling */
#define RAW_QUEUE_LEN       512         /* ~0.1 s of a fully loaded 500 kbit/s bus */
#define FILE_BUF_SIZE       8192
#define OPEN_RETRY_MS       5000        /* e.g. no SD card: retry quietly */
#define STALE_AFTER_MS      2500
#define MAX_COLUMNS         VD_COUNT
#define MAX_FILES           999
#define EPOCH_VALID         1700000000  /* clock set (SNTP / RTC) */

typedef struct {
    FILE     *f;
    char      buf[FILE_BUF_SIZE];       /* stdio buffer: one SD write per 8 KB */
    uint32_t  retry_at_ms;
    bool      failed;                   /* last open failed (log once) */
    bool      real_time;                /* clock was set when the file was opened */
    int64_t   epoch_off_ms;             /* real time = boot ms + this (real_time only) */
} log_file_t;

static char          s_dir[16];
static QueueHandle_t s_raw_queue;
static volatile bool s_want_raw;
static volatile bool s_want_params;
static volatile bool s_want_all_traffic;

static log_file_t    s_raw;
static log_file_t    s_par;
static log_file_t    s_evt;                     /* opened / closed with the raw log */
static vd_signal_t   s_cols[MAX_COLUMNS];
static int           s_ncols;

static datalog_status_t s_status;
static portMUX_TYPE     s_lock = portMUX_INITIALIZER_UNLOCKED;

/* ---------------------------------------------------------------- status */

static void set_error(const char *msg)
{
    ESP_LOGW(TAG, "%s", msg);
    portENTER_CRITICAL(&s_lock);
    snprintf(s_status.error, sizeof(s_status.error), "%s", msg);
    s_status.error_count++;
    portEXIT_CRITICAL(&s_lock);
}

void datalog_get_status(datalog_status_t *out)
{
    can_if_stats_t cs;
    can_if_get_stats(&cs);
    portENTER_CRITICAL(&s_lock);
    *out = s_status;
    portEXIT_CRITICAL(&s_lock);
    out->raw_dropped = cs.mon_dropped;
}

bool datalog_is_open(const char *name)
{
    datalog_status_t st;
    datalog_get_status(&st);
    return (st.raw_active && strcmp(name, st.raw_file) == 0) ||
           (st.params_active && strcmp(name, st.params_file) == 0) ||
           (st.evt_active && strcmp(name, st.evt_file) == 0);
}

/* ---------------------------------------------------------------- files */

/* Time base of a file, fixed when it is opened: real time if the clock is set
 * (SNTP / RTC), else ms since boot. Fixed per file so an SNTP sync during a
 * log can't make its timestamps jump. */
static void set_time_base(log_file_t *lf)
{
    struct timeval tv;
    gettimeofday(&tv, NULL);
    lf->real_time = tv.tv_sec > EPOCH_VALID;
    lf->epoch_off_ms = lf->real_time
        ? (int64_t)tv.tv_sec * 1000 + tv.tv_usec / 1000 - (int64_t)can_if_now_ms() : 0;
}

/* boot ms -> the file's time base, as seconds + milliseconds */
static void file_time(const log_file_t *lf, uint32_t ms, int64_t *sec, uint32_t *msec)
{
    int64_t t = (int64_t)ms + lf->epoch_off_ms;
    *sec = t / 1000;
    *msec = (uint32_t)(t % 1000);
}

/* Opens <dir>/<prefix>_YYYYMMDD_HHMMSS.<ext> (local time) when the clock is set,
 * else <dir>/<prefix>_NNN.<ext> with the first free number. */
static bool open_next(log_file_t *lf, const char *prefix, const char *ext, char *name_out, size_t name_len)
{
    uint32_t now = can_if_now_ms();
    if (lf->failed && (int32_t)(now - lf->retry_at_ms) < 0) {
        return false;
    }

    set_time_base(lf);
    char stamp[20] = "";
    if (lf->real_time) {
        time_t t = time(NULL);
        struct tm tm;
        localtime_r(&t, &tm);
        strftime(stamp, sizeof(stamp), "%Y%m%d_%H%M%S", &tm);
    }

    char path[64], name[40];
    struct stat st;
    for (int n = 1; n <= MAX_FILES; n++) {
        if (stamp[0] == '\0') {
            snprintf(name, sizeof(name), "%s_%03d.%s", prefix, n, ext);
        } else if (n == 1) {
            snprintf(name, sizeof(name), "%s_%s.%s", prefix, stamp, ext);
        } else {
            snprintf(name, sizeof(name), "%s_%s_%d.%s", prefix, stamp, n, ext);   /* same second */
        }
        snprintf(path, sizeof(path), "%s/%s", s_dir, name);
        if (stat(path, &st) == 0) {
            continue;
        }
        lf->f = fopen(path, "w");
        if (lf->f == NULL) {
            break;
        }
        setvbuf(lf->f, lf->buf, _IOFBF, sizeof(lf->buf));
        lf->failed = false;
        strlcpy(name_out, name, name_len);      /* names are <= 27 chars */
        ESP_LOGI(TAG, "logging to %s", path);
        return true;
    }

    if (!lf->failed) {
        char msg[48];
        snprintf(msg, sizeof(msg), "cannot create %s log (SD card?)", prefix);
        set_error(msg);
    }
    lf->failed = true;
    lf->retry_at_ms = now + OPEN_RETRY_MS;
    return false;
}

static void sync_file(log_file_t *lf)
{
    if (lf->f == NULL) {
        return;
    }
    if (fflush(lf->f) != 0 || fsync(fileno(lf->f)) != 0 || ferror(lf->f)) {
        set_error("SD write failed, log closed");
        fclose(lf->f);
        lf->f = NULL;
        lf->failed = true;
        lf->retry_at_ms = can_if_now_ms() + OPEN_RETRY_MS;
    }
}

static void close_file(log_file_t *lf)
{
    if (lf->f != NULL) {
        sync_file(lf);
        if (lf->f != NULL) {
            fclose(lf->f);
            lf->f = NULL;
        }
    }
    lf->failed = false;     /* re-enabling tries again right away */
}

/* ---------------------------------------------------------------- event log
 *
 * evt_NNN.txt, written while raw logging is on: connection state changes,
 * request timeouts and (once a second, only when something changed) the CAN
 * error counters. Same time base as can_NNN.log, so a gap in the raw log can
 * be matched to what the dash was doing. Frames that failed to send never show
 * up in the raw log; they show up here as tx_failed / tx_retries.
 */
#define EVT_STATS_PERIOD_MS 1000

static uint32_t       s_evt_change = UINT32_MAX;
static uint32_t       s_evt_timeouts;
static can_if_stats_t s_evt_cs;
static uint32_t       s_evt_next_stats;

static const char *bus_state_name(can_if_bus_state_t st)
{
    switch (st) {
        case CAN_IF_BUS_ACTIVE:  return "active";
        case CAN_IF_BUS_WARNING: return "warning";
        case CAN_IF_BUS_PASSIVE: return "passive";
        case CAN_IF_BUS_OFF:     return "bus-off";
        default:                 return "stopped";
    }
}

static void evt_printf(uint32_t now, const char *fmt, ...) __attribute__((format(printf, 2, 3)));
static void evt_printf(uint32_t now, const char *fmt, ...)
{
    if (s_evt.f == NULL) {
        return;
    }
    int64_t sec;
    uint32_t ms;
    file_time(&s_evt, now, &sec, &ms);
    fprintf(s_evt.f, "(%lld.%03" PRIu32 ") ", (long long)sec, ms);
    if (s_evt.real_time) {
        time_t t = (time_t)sec;
        struct tm tm;
        char hms[12];
        localtime_r(&t, &tm);
        strftime(hms, sizeof(hms), "%H:%M:%S", &tm);
        fprintf(s_evt.f, "%s ", hms);
    }
    va_list ap;
    va_start(ap, fmt);
    vfprintf(s_evt.f, fmt, ap);
    va_end(ap);
    fputc('\n', s_evt.f);
}

static void evt_service(uint32_t now)
{
    if (s_raw.f != NULL && s_evt.f == NULL && !s_evt.failed) {
        char name[32];
        if (open_next(&s_evt, "evt", "txt", name, sizeof(name))) {
            fprintf(s_evt.f, "# OBD_Dash event log, time = %s (as in the can_ log), then local time\n",
                    s_evt.real_time ? "Unix time" : "s since boot (clock not set)");
            s_evt_change = UINT32_MAX;              /* write the current state first */
            can_if_get_stats(&s_evt_cs);
            evt_printf(now, "can: tx %" PRIu32 " failed %" PRIu32 " retries %" PRIu32
                       " rx %" PRIu32 " rx_dropped %" PRIu32 " bus_errors %" PRIu32 " state %s",
                       s_evt_cs.tx_frames, s_evt_cs.tx_failed, s_evt_cs.tx_retries, s_evt_cs.rx_frames,
                       s_evt_cs.rx_dropped, s_evt_cs.bus_errors, bus_state_name(s_evt_cs.state));
            s_evt_next_stats = now + EVT_STATS_PERIOD_MS;
            portENTER_CRITICAL(&s_lock);
            strcpy(s_status.evt_file, name);
            s_status.evt_active = true;
            portEXIT_CRITICAL(&s_lock);
        }
    } else if (s_raw.f == NULL && s_evt.f != NULL) {
        close_file(&s_evt);
        portENTER_CRITICAL(&s_lock);
        s_status.evt_active = false;
        portEXIT_CRITICAL(&s_lock);
    }
    if (s_raw.f == NULL) {
        s_evt.failed = false;
    }
    if (s_evt.f == NULL) {
        return;
    }

    comsm_status_t st;
    comsm_get_status(&st);
    if (st.change_count != s_evt_change) {
        s_evt_change = st.change_count;
        s_evt_timeouts = st.timeouts;
        evt_printf(now, "comsm: %s  ecu %03" PRIX32 "  responses %" PRIu32 " timeouts %" PRIu32,
                   comsm_state_name(st.state), st.ecu_id, st.responses, st.timeouts);
    } else if (st.timeouts != s_evt_timeouts) {
        if (st.timeouts > s_evt_timeouts) {
            evt_printf(now, "timeout +%" PRIu32 " (total %" PRIu32 ", responses %" PRIu32 ")",
                       st.timeouts - s_evt_timeouts, st.timeouts, st.responses);
        }
        s_evt_timeouts = st.timeouts;
    }

    if ((int32_t)(now - s_evt_next_stats) < 0) {
        return;
    }
    s_evt_next_stats = now + EVT_STATS_PERIOD_MS;
    can_if_stats_t cs;
    can_if_get_stats(&cs);
    can_if_stats_t *o = &s_evt_cs;
    if (cs.tx_failed != o->tx_failed || cs.tx_retries != o->tx_retries || cs.tx_dropped != o->tx_dropped ||
        cs.rx_dropped != o->rx_dropped || cs.mon_dropped != o->mon_dropped ||
        cs.bus_errors != o->bus_errors || cs.state != o->state) {
        evt_printf(now, "can: +tx_failed %" PRIu32 " +retries %" PRIu32 " +tx_dropped %" PRIu32
                   " +rx_dropped %" PRIu32 " +log_dropped %" PRIu32 " +bus_errors %" PRIu32
                   " state %s  (tx %" PRIu32 " rx %" PRIu32 " in the last second)",
                   cs.tx_failed - o->tx_failed, cs.tx_retries - o->tx_retries, cs.tx_dropped - o->tx_dropped,
                   cs.rx_dropped - o->rx_dropped, cs.mon_dropped - o->mon_dropped,
                   cs.bus_errors - o->bus_errors, bus_state_name(cs.state),
                   cs.tx_frames - o->tx_frames, cs.rx_frames - o->rx_frames);
    }
    *o = cs;
}

/* ---------------------------------------------------------------- raw CAN */

static void raw_write(const can_frame_t *fr)
{
    /* candump -L: (sec.usec) iface id#data */
    char line[64];
    int64_t sec;
    uint32_t ms;
    file_time(&s_raw, fr->ts_ms, &sec, &ms);
    int n = snprintf(line, sizeof(line), "(%lld.%03" PRIu32 "000) can0 ", (long long)sec, ms);
    n += snprintf(line + n, sizeof(line) - n, fr->ext ? "%08" PRIX32 "#" : "%03" PRIX32 "#", fr->id);
    for (int i = 0; i < fr->dlc; i++) {
        n += snprintf(line + n, sizeof(line) - n, "%02X", fr->data[i]);
    }
    line[n++] = '\n';
    fwrite(line, 1, n, s_raw.f);
}

static void raw_service(void)
{
    if (s_want_raw && s_raw.f == NULL) {
        char name[32];
        if (open_next(&s_raw, "can", "log", name, sizeof(name))) {
            xQueueReset(s_raw_queue);
            can_if_set_monitor(CAN_IF_MON_LOG, s_raw_queue);
            portENTER_CRITICAL(&s_lock);
            strcpy(s_status.raw_file, name);
            s_status.raw_frames = 0;
            s_status.raw_active = true;
            portEXIT_CRITICAL(&s_lock);
        }
    } else if (!s_want_raw && s_raw.f != NULL) {
        can_if_set_monitor(CAN_IF_MON_LOG, NULL);
        close_file(&s_raw);
        ESP_LOGI(TAG, "raw log closed (%" PRIu32 " frames)", s_status.raw_frames);
    }
    if (s_raw.f == NULL) {
        can_if_set_monitor(CAN_IF_MON_LOG, NULL);
        portENTER_CRITICAL(&s_lock);
        s_status.raw_active = false;
        portEXIT_CRITICAL(&s_lock);
    }
    /* Full bus capture only while a raw log is written: no extra load otherwise. */
    can_if_set_accept_all(CAN_IF_ALL_LOG, s_raw.f != NULL && s_want_all_traffic);
}

/* ---------------------------------------------------------------- parameters */

/* Columns = signals of the PIDs currently polled (selected and supported). */
static int current_columns(vd_signal_t *cols)
{
    int n = 0;
    for (size_t i = 0; i < obd_pid_count(); i++) {
        const obd_pid_desc_t *d = obd_pid_desc(i);
        if (!obd_pid_is_enabled(i) || !obd_pid_is_supported(d->pid)) {
            continue;
        }
        bool dup = false;   /* e.g. PID 24 and 34 both feed LAMBDA */
        for (int k = 0; k < n; k++) {
            dup |= (cols[k] == d->signal);
        }
        if (!dup && n < MAX_COLUMNS) {
            cols[n++] = d->signal;
        }
    }
    return n;
}

static void params_open(const vd_signal_t *cols, int n)
{
    char name[32];
    if (!open_next(&s_par, "par", "csv", name, sizeof(name))) {
        return;
    }
    memcpy(s_cols, cols, n * sizeof(cols[0]));
    s_ncols = n;

    fputs(s_par.real_time ? "time_s,local_time" : "time_s", s_par.f);
    for (int i = 0; i < n; i++) {
        const vd_signal_info_t *info = vd_info(cols[i]);
        if (info->unit[0] != '\0') {
            fprintf(s_par.f, ",%s [%s]", info->name, info->unit);
        } else {
            fprintf(s_par.f, ",%s", info->name);
        }
    }
    fputc('\n', s_par.f);

    portENTER_CRITICAL(&s_lock);
    strcpy(s_status.params_file, name);
    s_status.params_rows = 0;
    s_status.params_active = true;
    portEXIT_CRITICAL(&s_lock);
}

static void params_row(uint32_t now)
{
    int64_t sec;
    uint32_t ms;
    file_time(&s_par, now, &sec, &ms);
    fprintf(s_par.f, "%lld.%03" PRIu32, (long long)sec, ms);
    if (s_par.real_time) {
        time_t t = (time_t)sec;
        struct tm tm;
        char ts[24];
        localtime_r(&t, &tm);
        strftime(ts, sizeof(ts), "%Y-%m-%d %H:%M:%S", &tm);
        fprintf(s_par.f, ",%s.%03" PRIu32, ts, ms);
    }
    for (int i = 0; i < s_ncols; i++) {
        vd_sample_t smp;
        if (vd_get(s_cols[i], STALE_AFTER_MS, &smp)) {
            fprintf(s_par.f, ",%.*f", vd_info(s_cols[i])->decimals, smp.value);
        } else {
            fputc(',', s_par.f);
        }
    }
    fputc('\n', s_par.f);
    portENTER_CRITICAL(&s_lock);
    s_status.params_rows++;
    portEXIT_CRITICAL(&s_lock);
}

static void params_service(uint32_t now)
{
    /* The supported-PID list survives a disconnect, so it alone would keep the
     * file open (empty rows) after Disconnect: close when OBD is idle. A short
     * ECU dropout (retry) keeps the file, rows only while polling. */
    comsm_status_t cs;
    comsm_get_status(&cs);
    vd_signal_t cols[MAX_COLUMNS];
    int n = (s_want_params && cs.state != COMSM_IDLE) ? current_columns(cols) : 0;

    /* Nothing polled (disconnected / nothing selected) or columns changed: close. */
    bool changed = (n != s_ncols) || memcmp(cols, s_cols, n * sizeof(cols[0])) != 0;
    if (s_par.f != NULL && (n == 0 || changed)) {
        close_file(&s_par);
        ESP_LOGI(TAG, "params log closed (%" PRIu32 " rows)", s_status.params_rows);
        s_ncols = 0;
    }
    if (n > 0 && s_par.f == NULL && cs.state == COMSM_RUNNING) {
        params_open(cols, n);
    }
    if (s_par.f != NULL) {
        if (cs.state == COMSM_RUNNING) {
            params_row(now);
        }
    } else {
        portENTER_CRITICAL(&s_lock);
        s_status.params_active = false;
        portEXIT_CRITICAL(&s_lock);
    }
}

/* ---------------------------------------------------------------- task */

static void datalog_task(void *arg)
{
    uint32_t next_row = can_if_now_ms();
    uint32_t next_sync = next_row + CONFIG_DATALOG_SYNC_PERIOD_MS;
    can_frame_t fr;

    while (1) {
        raw_service();
        evt_service(can_if_now_ms());

        /* Drain CAN frames until the next parameter row is due. */
        uint32_t now = can_if_now_ms();
        int32_t wait = (int32_t)(next_row - now);
        if (xQueueReceive(s_raw_queue, &fr, pdMS_TO_TICKS(wait > 0 ? wait : 0)) == pdTRUE) {
            uint32_t written = 0;
            do {
                if (s_raw.f != NULL) {
                    raw_write(&fr);
                    written++;
                }
            } while (written < RAW_QUEUE_LEN && xQueueReceive(s_raw_queue, &fr, 0) == pdTRUE);
            portENTER_CRITICAL(&s_lock);
            s_status.raw_frames += written;
            portEXIT_CRITICAL(&s_lock);
        }

        now = can_if_now_ms();
        if ((int32_t)(now - next_row) >= 0) {
            params_service(now);
            next_row += CONFIG_DATALOG_PARAM_PERIOD_MS;
            if ((int32_t)(now - next_row) > CONFIG_DATALOG_PARAM_PERIOD_MS) {
                next_row = now + CONFIG_DATALOG_PARAM_PERIOD_MS;   /* fell behind: skip */
            }
        }
        if ((int32_t)(now - next_sync) >= 0) {
            sync_file(&s_raw);
            sync_file(&s_par);
            sync_file(&s_evt);
            next_sync = now + CONFIG_DATALOG_SYNC_PERIOD_MS;
        }
    }
}

esp_err_t datalog_init(const char *dir)
{
    snprintf(s_dir, sizeof(s_dir), "%s", dir);
    /* ~12 KB: in PSRAM, internal RAM is needed by Wi-Fi / BLE */
    s_raw_queue = xQueueCreateWithCaps(RAW_QUEUE_LEN, sizeof(can_frame_t), MALLOC_CAP_SPIRAM);
    if (s_raw_queue == NULL) {
        return ESP_ERR_NO_MEM;
    }
    if (xTaskCreate(datalog_task, "datalog", TASK_STACK, NULL, TASK_PRIO, NULL) != pdPASS) {
        return ESP_ERR_NO_MEM;
    }
    ESP_LOGI(TAG, "ready (%s, row every %d ms)", s_dir, CONFIG_DATALOG_PARAM_PERIOD_MS);
    return ESP_OK;
}

void datalog_enable_raw(bool on)
{
    s_want_raw = on;
}

void datalog_enable_params(bool on)
{
    s_want_params = on;
}

void datalog_set_all_traffic(bool on)
{
    s_want_all_traffic = on;
}
