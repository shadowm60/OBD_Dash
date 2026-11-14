#include "canmon.h"

#include <string.h>
#include <stdio.h>
#include <inttypes.h>
#include "esp_log.h"
#include "esp_timer.h"
#include "esp_heap_caps.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "can_if.h"

static const char *TAG = "canmon";

#define MON_QUEUE_LEN   2048            /* ~0.5 s of a full 500 kbit/s bus (PSRAM) */
#define CMD_QUEUE_LEN   8
#define TABLE_SIZE      512             /* distinct IDs (PSRAM) */

typedef enum { CMD_START, CMD_STOP, CMD_RESET, CMD_BUS } cmd_type_t;

typedef struct {
    cmd_type_t type;
    uint32_t   bitrate;
    bool       listen_only;
} cmd_t;

typedef struct {
    uint32_t key;           /* id | ext << 31 */
    uint8_t  dlc;
    uint8_t  data[8];
    uint32_t count;
    uint32_t last_ms;
    uint32_t period_ms;     /* smoothed */
} id_entry_t;

static QueueHandle_t     s_mon_q, s_cmd_q;
static SemaphoreHandle_t s_lock;            /* s_st + the ID table */
static canmon_stats_t    s_st;
static id_entry_t       *s_tab;             /* sorted by key */
static int               s_ntab;

/* per-second counters, task only */
static uint32_t s_sec_frames, s_sec_bits;
static uint32_t s_start_ms;
static uint32_t s_drop_base;                /* can_if mon_dropped at reset */

/* where the task is, for canmon_debug_log() */
static TaskHandle_t      s_task;
static volatile uint32_t s_loop_ms;         /* last pass of the main loop */
static volatile int      s_stage;
static volatile uint32_t s_cmds_done;
enum { ST_CMD = 1, ST_WAIT, ST_DRAIN, ST_SECOND, ST_DO_CMD };

#define LOCK()   xSemaphoreTake(s_lock, portMAX_DELAY)
#define UNLOCK() xSemaphoreGive(s_lock)

static uint32_t now_ms(void)
{
    return (uint32_t)(esp_timer_get_time() / 1000);
}

/* ---------------------------------------------------------------- ID table */

static int tab_find(uint32_t key, bool *found)
{
    int lo = 0, hi = s_ntab;
    while (lo < hi) {
        int mid = (lo + hi) / 2;
        if (s_tab[mid].key < key) {
            lo = mid + 1;
        } else {
            hi = mid;
        }
    }
    *found = lo < s_ntab && s_tab[lo].key == key;
    return lo;
}

static void tab_update(const can_frame_t *f)
{
    uint32_t key = f->id | (f->ext ? 0x80000000u : 0);
    bool found;
    int i = tab_find(key, &found);
    id_entry_t *e;
    if (found) {
        e = &s_tab[i];
        uint32_t dt = f->ts_ms - e->last_ms;
        /* smoothed period, first value taken as is */
        e->period_ms = e->count == 1 ? dt : (e->period_ms * 7 + dt) / 8;
    } else {
        if (s_ntab >= TABLE_SIZE) {
            s_st.id_table_full = true;
            return;
        }
        memmove(&s_tab[i + 1], &s_tab[i], (s_ntab - i) * sizeof(*s_tab));
        s_ntab++;
        e = &s_tab[i];
        memset(e, 0, sizeof(*e));
        e->key = key;
    }
    e->dlc = f->dlc;
    memcpy(e->data, f->data, f->dlc);
    e->count++;
    e->last_ms = f->ts_ms;
}

/* Approximate bits on the wire incl. stuffing (~10 % of the stuffable part)
 * and the 3 bit interframe space. */
static uint32_t frame_bits(const can_frame_t *f)
{
    uint32_t stuffable = (f->ext ? 54 : 34) + 8u * f->dlc;
    return stuffable + stuffable / 10 + 13 + 3;
}

/* ---------------------------------------------------------------- commands */

static void reset_counters(void)
{
    can_if_stats_t cs;
    can_if_get_stats(&cs);
    s_drop_base = cs.mon_dropped;
    LOCK();
    s_ntab = 0;
    s_st.frames = s_st.fps = s_st.fps_peak = 0;
    s_st.std_frames = s_st.ext_frames = 0;
    s_st.load_pct = 0;
    s_st.ids = 0;
    s_st.id_table_full = false;
    s_st.dropped = 0;
    UNLOCK();
    s_sec_frames = s_sec_bits = 0;
    s_start_ms = now_ms();
}

static void do_cmd(const cmd_t *c)
{
    switch (c->type) {
    case CMD_START:
        reset_counters();
        xQueueReset(s_mon_q);
        can_if_set_monitor(CAN_IF_MON_VIEW, s_mon_q);
        can_if_set_accept_all(CAN_IF_ALL_MONITOR, true);
        LOCK();
        s_st.running = true;
        UNLOCK();
        ESP_LOGI(TAG, "monitor started");
        break;
    case CMD_STOP:
        can_if_set_monitor(CAN_IF_MON_VIEW, NULL);
        can_if_set_accept_all(CAN_IF_ALL_MONITOR, false);
        xQueueReset(s_mon_q);
        LOCK();
        s_st.running = false;
        s_st.fps = 0;
        s_st.load_pct = 0;
        UNLOCK();
        ESP_LOGI(TAG, "monitor stopped");
        break;
    case CMD_RESET:
        reset_counters();
        break;
    case CMD_BUS: {
        can_if_config_t cfg;
        can_if_get_config(&cfg);
        cfg.bitrate = c->bitrate;
        cfg.listen_only = c->listen_only;
        esp_err_t err = can_if_start(&cfg);     /* stops and recreates the node */
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "bus restart failed: %s", esp_err_to_name(err));
        }
        reset_counters();
        break;
    }
    }
}

/* ---------------------------------------------------------------- task */

static const char *state_name(can_if_bus_state_t s)
{
    switch (s) {
    case CAN_IF_BUS_ACTIVE:  return "active";
    case CAN_IF_BUS_WARNING: return "warning";
    case CAN_IF_BUS_PASSIVE: return "error passive";
    case CAN_IF_BUS_OFF:     return "bus-off";
    default:                 return "stopped";
    }
}

static void each_second(void)
{
    can_if_stats_t cs;
    can_if_get_stats(&cs);
    can_if_config_t cfg;
    can_if_get_config(&cfg);
    uint32_t bitrate = cfg.bitrate ? cfg.bitrate : 1;
    uint32_t load = (uint32_t)((uint64_t)s_sec_bits * 100 / bitrate);

    LOCK();
    s_st.bitrate = cfg.bitrate;
    s_st.listen_only = cfg.listen_only;
    s_st.fps = s_sec_frames;
    if (s_sec_frames > s_st.fps_peak) {
        s_st.fps_peak = s_sec_frames;
    }
    s_st.load_pct = load > 100 ? 100 : load;
    s_st.state = state_name(cs.state);
    s_st.bus_errors = cs.bus_errors;
    s_st.tec = cs.tec;
    s_st.rec = cs.rec;
    s_st.dropped = cs.mon_dropped - s_drop_base;
    s_st.uptime_s = s_st.running ? (now_ms() - s_start_ms) / 1000 : s_st.uptime_s;
    UNLOCK();
    s_sec_frames = s_sec_bits = 0;
}

static void canmon_task(void *arg)
{
    can_frame_t f;
    cmd_t c;
    uint32_t next_sec = now_ms() + 1000;

    while (1) {
        s_loop_ms = now_ms();
        s_stage = ST_CMD;
        while (xQueueReceive(s_cmd_q, &c, 0) == pdTRUE) {
            s_stage = ST_DO_CMD;
            do_cmd(&c);
            s_cmds_done++;
        }

        /* Drain the monitor queue in batches; the lock is taken per batch. */
        s_stage = ST_WAIT;
        if (xQueueReceive(s_mon_q, &f, pdMS_TO_TICKS(50)) == pdTRUE) {
            int n = 0;
            s_stage = ST_DRAIN;
            LOCK();
            do {
                tab_update(&f);         /* our own OBD requests (TX) too */
                s_st.frames++;
                if (f.ext) {
                    s_st.ext_frames++;
                } else {
                    s_st.std_frames++;
                }
                s_sec_frames++;
                s_sec_bits += frame_bits(&f);
            } while (++n < 256 && xQueueReceive(s_mon_q, &f, 0) == pdTRUE);
            s_st.ids = s_ntab;
            UNLOCK();
        }

        uint32_t now = now_ms();
        if ((int32_t)(now - next_sec) >= 0) {
            next_sec += 1000;
            if ((int32_t)(now - next_sec) > 1000) {
                next_sec = now + 1000;      /* we were stalled, don't catch up */
            }
            s_stage = ST_SECOND;
            each_second();
        }
    }
}

/* ---------------------------------------------------------------- API */

static esp_err_t post(cmd_t c)
{
    if (s_cmd_q == NULL) {
        return ESP_ERR_INVALID_STATE;
    }
    return xQueueSend(s_cmd_q, &c, pdMS_TO_TICKS(100)) == pdTRUE ? ESP_OK : ESP_ERR_TIMEOUT;
}

esp_err_t canmon_start(void) { return post((cmd_t){ .type = CMD_START }); }
/* The essential part of a stop is done right here (both calls are plain
 * stores), so the monitor stops even if its task is busy; the task then
 * empties the queue. */
esp_err_t canmon_stop(void)
{
    if (s_lock == NULL) {
        return ESP_ERR_INVALID_STATE;
    }
    can_if_set_monitor(CAN_IF_MON_VIEW, NULL);
    can_if_set_accept_all(CAN_IF_ALL_MONITOR, false);
    if (xSemaphoreTake(s_lock, pdMS_TO_TICKS(100)) == pdTRUE) {
        s_st.running = false;
        s_st.fps = 0;
        s_st.load_pct = 0;
        UNLOCK();
    } else {
        ESP_LOGW(TAG, "stop: stats lock busy");
    }
    return post((cmd_t){ .type = CMD_STOP });
}

void canmon_debug_log(void)
{
    static const char *const names[] = { "?", "cmd", "wait frames", "drain", "each second", "do_cmd" };
    int st = s_stage;
    ESP_LOGW(TAG, "task %s, stage %s, last loop %" PRIu32 " ms ago, cmds done %" PRIu32
             ", cmd queue %u, frame queue %u",
             s_task == NULL ? "none" : eTaskGetState(s_task) == eBlocked ? "blocked"
                 : eTaskGetState(s_task) == eReady ? "ready (starved?)"
                 : eTaskGetState(s_task) == eRunning ? "running" : "other",
             st >= 0 && st <= ST_DO_CMD ? names[st] : "?", now_ms() - s_loop_ms, s_cmds_done,
             (unsigned)uxQueueMessagesWaiting(s_cmd_q), (unsigned)uxQueueMessagesWaiting(s_mon_q));
}
esp_err_t canmon_reset(void) { return post((cmd_t){ .type = CMD_RESET }); }

esp_err_t canmon_set_bus(uint32_t bitrate, bool listen_only)
{
    return post((cmd_t){ .type = CMD_BUS, .bitrate = bitrate, .listen_only = listen_only });
}

void canmon_get_stats(canmon_stats_t *out)
{
    if (s_lock == NULL) {
        memset(out, 0, sizeof(*out));
        out->state = "stopped";
        return;
    }
    LOCK();
    *out = s_st;
    UNLOCK();
}

int canmon_format_ids(char *buf, size_t len, int max_rows)
{
    size_t o = 0;
    o += snprintf(buf + o, len - o, "ID        DLC DATA                     COUNT  PERIOD\n");
    if (s_lock == NULL) {
        return 0;
    }
    LOCK();
    int n = s_ntab;
    for (int i = 0; i < n && i < max_rows && o + 64 < len; i++) {
        const id_entry_t *e = &s_tab[i];
        bool ext = e->key & 0x80000000u;
        uint32_t id = e->key & 0x1FFFFFFFu;
        o += snprintf(buf + o, len - o, ext ? "%08" PRIX32 "  %u   " : "%03" PRIX32 "       %u   ", id, e->dlc);
        for (int b = 0; b < 8; b++) {
            o += snprintf(buf + o, len - o, b < e->dlc ? "%02X " : "   ", e->data[b]);
        }
        if (e->count > 1) {
            o += snprintf(buf + o, len - o, "%6" PRIu32 " %5" PRIu32 "ms\n", e->count, e->period_ms);
        } else {
            o += snprintf(buf + o, len - o, "%6" PRIu32 "      -\n", e->count);
        }
    }
    UNLOCK();
    if (n > max_rows && o + 32 < len) {
        snprintf(buf + o, len - o, "... %d more IDs\n", n - max_rows);
    }
    return n;
}

esp_err_t canmon_init(void)
{
    s_lock = xSemaphoreCreateMutex();
    s_cmd_q = xQueueCreate(CMD_QUEUE_LEN, sizeof(cmd_t));
    s_mon_q = xQueueCreateWithCaps(MON_QUEUE_LEN, sizeof(can_frame_t), MALLOC_CAP_SPIRAM);
    s_tab = heap_caps_calloc(TABLE_SIZE, sizeof(id_entry_t), MALLOC_CAP_SPIRAM);
    if (!s_lock || !s_cmd_q || !s_mon_q || !s_tab) {
        return ESP_ERR_NO_MEM;
    }
    s_st.state = "stopped";
    /* stack in PSRAM: internal RAM is needed by Wi-Fi / BLE; this task never writes flash */
    if (xTaskCreatePinnedToCoreWithCaps(canmon_task, "canmon", 4096, NULL, 4, &s_task, 0, MALLOC_CAP_SPIRAM) != pdPASS) {
        return ESP_ERR_NO_MEM;
    }
    return ESP_OK;
}
