#include "settings.h"

#include <stddef.h>
#include <string.h>
#include "esp_log.h"
#include "esp_timer.h"
#include "nvs_flash.h"
#include "nvs.h"
#include "freertos/FreeRTOS.h"

static const char *TAG = "settings";

#define NVS_NAMESPACE   "obd_dash"
#define NVS_KEY         "cfg"
#define SETTINGS_VER    2     /* 2: PID selection became opt-in */
#define SAVE_DELAY_MS   2000

typedef struct {
    uint16_t       version;
    uint16_t       size;
    app_settings_t data;
} stored_t;

static app_settings_t     s_cfg;
static bool               s_dirty;
static portMUX_TYPE       s_lock = portMUX_INITIALIZER_UNLOCKED;
static esp_timer_handle_t s_save_timer;

static void set_defaults(app_settings_t *c)
{
    memset(c, 0, sizeof(*c));
    c->conn_method = SETTINGS_CONN_OBD;
    c->rusefi_base_id = 512;
    c->gauge_anim = true;
    c->pids_per_request = 6;        /* falls back to 1 if the ECU can't */
    /* Default selection: what the current dashboard shows. */
    static const uint8_t default_pids[] = { 0x05 /* coolant */, 0x0C /* rpm */,
                                            0x0D /* speed */, 0x24 /* lambda */ };
    for (size_t i = 0; i < sizeof(default_pids); i++) {
        c->pid_selected[default_pids[i] >> 3] |= (uint8_t)(1u << (default_pids[i] & 7));
    }
}

esp_err_t settings_flush(void)
{
    stored_t blob = { .version = SETTINGS_VER, .size = sizeof(app_settings_t) };

    portENTER_CRITICAL(&s_lock);
    bool dirty = s_dirty;
    blob.data = s_cfg;
    s_dirty = false;
    portEXIT_CRITICAL(&s_lock);

    if (!dirty) {
        return ESP_OK;
    }

    nvs_handle_t h;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &h);
    if (err == ESP_OK) {
        err = nvs_set_blob(h, NVS_KEY, &blob, sizeof(blob));
        if (err == ESP_OK) {
            err = nvs_commit(h);
        }
        nvs_close(h);
    }
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "save failed: %s", esp_err_to_name(err));
        portENTER_CRITICAL(&s_lock);
        s_dirty = true;     /* retry on next change */
        portEXIT_CRITICAL(&s_lock);
    } else {
        ESP_LOGI(TAG, "saved");
    }
    return err;
}

static void save_timer_cb(void *arg)
{
    settings_flush();
}

static void mark_dirty(void)
{
    /* called with s_lock released */
    if (s_save_timer) {
        esp_timer_stop(s_save_timer);
        esp_timer_start_once(s_save_timer, SAVE_DELAY_MS * 1000ULL);
    }
}

static esp_err_t load(void)
{
    nvs_handle_t h;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READONLY, &h);
    if (err != ESP_OK) {
        return err;
    }
    stored_t blob;
    size_t len = sizeof(blob);
    err = nvs_get_blob(h, NVS_KEY, &blob, &len);
    nvs_close(h);
    if (err != ESP_OK) {
        return err;
    }
    /* Accept blobs written before fields were appended (same version, shorter). */
    size_t hdr = offsetof(stored_t, data);
    if (len < hdr || blob.version != SETTINGS_VER || blob.size > sizeof(app_settings_t) ||
        len != hdr + blob.size) {
        ESP_LOGW(TAG, "stored settings incompatible (ver %u), using defaults", blob.version);
        return ESP_ERR_INVALID_VERSION;
    }
    memcpy(&s_cfg, &blob.data, blob.size);      /* s_cfg holds defaults for the rest */
    return ESP_OK;
}

esp_err_t settings_init(void)
{
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_LOGW(TAG, "NVS partition erased (%s)", esp_err_to_name(err));
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "nvs_flash_init: %s", esp_err_to_name(err));
        set_defaults(&s_cfg);
        return err;
    }

    set_defaults(&s_cfg);
    if (load() == ESP_OK) {
        ESP_LOGI(TAG, "loaded (conn %u, rusEFI 0x%03X)", s_cfg.conn_method, s_cfg.rusefi_base_id);
    } else {
        ESP_LOGI(TAG, "no stored settings, using defaults");
    }

    const esp_timer_create_args_t args = { .callback = save_timer_cb, .name = "settings_save" };
    return esp_timer_create(&args, &s_save_timer);
}

void settings_get(app_settings_t *out)
{
    portENTER_CRITICAL(&s_lock);
    *out = s_cfg;
    portEXIT_CRITICAL(&s_lock);
}

/* Assign a field and schedule a save only if the value actually changed. */
#define SETTINGS_SETTER(field, value)               \
    do {                                            \
        bool changed = false;                       \
        portENTER_CRITICAL(&s_lock);                \
        if (s_cfg.field != (value)) {               \
            s_cfg.field = (value);                  \
            s_dirty = changed = true;               \
        }                                           \
        portEXIT_CRITICAL(&s_lock);                 \
        if (changed) {                              \
            mark_dirty();                           \
        }                                           \
    } while (0)

void settings_set_conn_method(uint8_t method)   { SETTINGS_SETTER(conn_method, method); }
void settings_set_rusefi_base_id(uint16_t id)   { SETTINGS_SETTER(rusefi_base_id, id); }
void settings_set_log_raw(bool on)              { SETTINGS_SETTER(log_raw, on); }
void settings_set_log_params(bool on)           { SETTINGS_SETTER(log_params, on); }
void settings_set_gauge_anim(bool on)           { SETTINGS_SETTER(gauge_anim, on); }
void settings_set_pids_per_request(uint8_t n)   { SETTINGS_SETTER(pids_per_request, n); }

void settings_set_pid_selected(uint8_t pid, bool selected)
{
    uint8_t mask = (uint8_t)(1u << (pid & 7));
    bool changed = false;
    portENTER_CRITICAL(&s_lock);
    uint8_t cur = s_cfg.pid_selected[pid >> 3];
    uint8_t next = selected ? (uint8_t)(cur | mask) : (uint8_t)(cur & ~mask);
    if (next != cur) {
        s_cfg.pid_selected[pid >> 3] = next;
        s_dirty = changed = true;
    }
    portEXIT_CRITICAL(&s_lock);
    if (changed) {
        mark_dirty();
    }
}

bool settings_pid_selected(uint8_t pid)
{
    portENTER_CRITICAL(&s_lock);
    bool sel = (s_cfg.pid_selected[pid >> 3] >> (pid & 7)) & 1;
    portEXIT_CRITICAL(&s_lock);
    return sel;
}

void settings_set_ecu_supported(const uint8_t bitmap[256 / 8])
{
    bool changed = false;
    portENTER_CRITICAL(&s_lock);
    if (!s_cfg.ecu_support_valid || memcmp(s_cfg.ecu_supported, bitmap, sizeof(s_cfg.ecu_supported)) != 0) {
        memcpy(s_cfg.ecu_supported, bitmap, sizeof(s_cfg.ecu_supported));
        s_cfg.ecu_support_valid = true;
        s_dirty = changed = true;
    }
    portEXIT_CRITICAL(&s_lock);
    if (changed) {
        mark_dirty();
    }
}

bool settings_get_ecu_supported(uint8_t bitmap[256 / 8])
{
    portENTER_CRITICAL(&s_lock);
    bool valid = s_cfg.ecu_support_valid;
    memcpy(bitmap, s_cfg.ecu_supported, sizeof(s_cfg.ecu_supported));
    portEXIT_CRITICAL(&s_lock);
    return valid;
}
