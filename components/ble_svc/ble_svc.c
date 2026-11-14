#include "ble_svc.h"

#include <string.h>
#include <stdio.h>
#include "esp_attr.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/stream_buffer.h"
#include "esp_heap_caps.h"
#include "esp_timer.h"
#include "nimble/nimble_port.h"
#include "nimble/nimble_port_freertos.h"
#include "host/ble_hs.h"
#include "host/util/util.h"
#include "services/gap/ble_svc_gap.h"

static const char *TAG = "ble_svc";

/* Provided by NimBLE's store/config (NVS backed with BT_NIMBLE_NVS_PERSIST). */
void ble_store_config_init(void);

static SemaphoreHandle_t s_lock;
static ble_svc_status_t  s_st;
EXT_RAM_BSS_ATTR static ble_svc_dev_t s_devs[BLE_SVC_MAX_DEVS];
static uint8_t           s_own_addr_type;
static uint16_t          s_conn = BLE_HS_CONN_HANDLE_NONE;

#define LOCK()   xSemaphoreTake(s_lock, portMAX_DELAY)
#define UNLOCK() xSemaphoreGive(s_lock)

static int gap_event(struct ble_gap_event *event, void *arg);

static void set_event(const char *fmt, int v)
{
    LOCK();
    snprintf(s_st.last_event, sizeof(s_st.last_event), fmt, v);
    UNLOCK();
}

static void addr_to_str(const uint8_t a[6], char *out)
{
    sprintf(out, "%02X:%02X:%02X:%02X:%02X:%02X", a[5], a[4], a[3], a[2], a[1], a[0]);
}

static int count_bonds(void)
{
    int n = 0;
    ble_store_util_count(BLE_STORE_OBJ_TYPE_PEER_SEC, &n);
    return n;
}

/* Short names for the common 16-bit services. */
static const char *svc16_name(uint16_t u)
{
    switch (u) {
    case 0x1800: return "Generic Access";
    case 0x1801: return "Generic Attribute";
    case 0x180A: return "Device Information";
    case 0x180D: return "Heart Rate";
    case 0x180F: return "Battery";
    case 0x1805: return "Current Time";
    case 0x1809: return "Health Thermometer";
    case 0x1810: return "Blood Pressure";
    case 0x1812: return "HID";
    case 0x1816: return "Cycling Speed/Cadence";
    case 0x1818: return "Cycling Power";
    case 0x1819: return "Location/Navigation";
    case 0x181A: return "Environmental Sensing";
    case 0x181C: return "User Data";
    case 0x1826: return "Fitness Machine";
    case 0xFE59: return "Nordic DFU";
    default:     return "";
    }
}

/* ---------------------------------------------------------------- scanning */

static void on_disc(const struct ble_gap_disc_desc *d)
{
    struct ble_hs_adv_fields f;
    char name[32] = "";
    if (ble_hs_adv_parse_fields(&f, d->data, d->length_data) == 0 && f.name != NULL && f.name_len > 0) {
        int n = f.name_len < (int)sizeof(name) - 1 ? f.name_len : (int)sizeof(name) - 1;
        memcpy(name, f.name, n);
        name[n] = '\0';
    }
    bool connectable = d->event_type == BLE_HCI_ADV_RPT_EVTYPE_ADV_IND ||
                       d->event_type == BLE_HCI_ADV_RPT_EVTYPE_DIR_IND;

    LOCK();
    ble_svc_dev_t *e = NULL;
    for (int i = 0; i < s_st.ndev; i++) {
        if (memcmp(s_devs[i].addr, d->addr.val, 6) == 0 && s_devs[i].addr_type == d->addr.type) {
            e = &s_devs[i];
            break;
        }
    }
    if (e == NULL && s_st.ndev < BLE_SVC_MAX_DEVS) {
        e = &s_devs[s_st.ndev++];
        memset(e, 0, sizeof(*e));
        memcpy(e->addr, d->addr.val, 6);
        e->addr_type = d->addr.type;
        addr_to_str(d->addr.val, e->addr_str);
    }
    if (e != NULL) {
        e->rssi = d->rssi;
        if (name[0] != '\0') {
            snprintf(e->name, sizeof(e->name), "%s", name);   /* scan response may bring it later */
        }
        e->connectable |= connectable;
    }
    UNLOCK();
}

esp_err_t ble_svc_scan(int seconds)
{
    if (s_lock == NULL) {
        return ESP_ERR_INVALID_STATE;     /* not initialised yet */
    }
    LOCK();
    ble_svc_state_t st = s_st.state;
    UNLOCK();
    if (st != BLE_SVC_READY) {
        return ESP_ERR_INVALID_STATE;
    }
    struct ble_gap_disc_params p = {
        .filter_duplicates = 0,     /* keep RSSI and late scan responses coming */
        .passive = 0,               /* active: ask for scan responses (names) */
    };
    LOCK();
    s_st.ndev = 0;
    UNLOCK();
    int rc = ble_gap_disc(s_own_addr_type, seconds * 1000, &p, gap_event, NULL);
    if (rc != 0) {
        set_event("scan failed (%d)", rc);
        return ESP_FAIL;
    }
    LOCK();
    s_st.state = BLE_SVC_SCANNING;
    s_st.last_event[0] = '\0';
    UNLOCK();
    return ESP_OK;
}

esp_err_t ble_svc_scan_stop(void)
{
    if (s_lock == NULL) {
        return ESP_ERR_INVALID_STATE;     /* not initialised yet */
    }
    int rc = ble_gap_disc_cancel();
    LOCK();
    if (s_st.state == BLE_SVC_SCANNING) {
        s_st.state = BLE_SVC_READY;
        s_st.scan_count++;
    }
    UNLOCK();
    return rc == 0 ? ESP_OK : ESP_ERR_INVALID_STATE;
}

/* ---------------------------------------------------------------- GATT discovery
 * All services, then the characteristics of each one (an ELM327 adapter's
 * serial port is a vendor service with a write + notify characteristic).
 * Runs at connect and again once the link is encrypted: some devices only
 * expose their services after pairing. */

#define MAX_DISC_SVCS 12
static struct { uint16_t start, end; } s_rng[MAX_DISC_SVCS];
static int  s_nrng, s_rng_idx;
static bool s_disc_busy, s_disc_again;

/* ---- serial port: chosen during characteristic discovery */
#define SER_VGATE_UUID  "bef8d6c9-9c21-4c9e-b632-bd58c1009f9f"
#define SER_RX_BUF      4096
static int      s_ser_rank;                 /* 0 none, 1 FFE1, 2 18F0, 3 Vgate */
static uint16_t s_ser_tx, s_ser_rx, s_ser_rx_end, s_ser_rx2;   /* rx2: 2AF0 notify (18F0 layout) */
static bool     s_ser_wnr;                  /* write without response */
static volatile bool s_ser_ready;
static StreamBufferHandle_t s_ser_buf;
static const char *s_ser_name = "";

static void serial_reset(void)
{
    s_ser_rank = 0;
    s_ser_tx = s_ser_rx = s_ser_rx_end = s_ser_rx2 = 0;
    s_ser_ready = false;
    LOCK();
    s_st.serial_ready = false;
    s_st.serial_info[0] = '\0';
    UNLOCK();
}

static void serial_setup(uint16_t conn);

static int  on_svc(uint16_t conn, const struct ble_gatt_error *err, const struct ble_gatt_svc *svc, void *arg);

static void add_line(const char *line)
{
    LOCK();
    if (s_st.nsvc < BLE_SVC_MAX_SVCS) {
        snprintf(s_st.svcs[s_st.nsvc++], sizeof(s_st.svcs[0]), "%s", line);
    }
    UNLOCK();
}

/* 16-bit: "2A19"; 128-bit: first 8 hex digits + "..." (full UUID in the log) */
static void uuid_short(const ble_uuid_t *u, char *out, size_t len)
{
    char buf[BLE_UUID_STR_LEN];
    ble_uuid_to_str(u, buf);
    if (u->type == BLE_UUID_TYPE_16) {
        snprintf(out, len, "%04X", BLE_UUID16(u)->value);
    } else {
        snprintf(out, len, "%.8s...", buf);
    }
}

static void disc_start(uint16_t conn)
{
    if (s_disc_busy) {
        s_disc_again = true;            /* finish this round, then run again */
        return;
    }
    s_disc_busy = true;
    s_disc_again = false;
    s_nrng = s_rng_idx = 0;
    serial_reset();
    LOCK();
    s_st.nsvc = 0;
    s_st.svc_done = false;
    UNLOCK();
    if (ble_gattc_disc_all_svcs(conn, on_svc, NULL) != 0) {
        s_disc_busy = false;
        LOCK();
        s_st.svc_done = true;
        UNLOCK();
    }
}

static void disc_finished(uint16_t conn)
{
    s_disc_busy = false;
    if (s_disc_again) {
        disc_start(conn);
        return;
    }
    LOCK();
    s_st.svc_done = true;
    UNLOCK();
    ESP_LOGI(TAG, "GATT discovery done");
    serial_setup(conn);
}

static int on_chr(uint16_t conn, const struct ble_gatt_error *err, const struct ble_gatt_chr *chr, void *arg);

static void disc_next_chrs(uint16_t conn)
{
    while (s_rng_idx < s_nrng) {
        int i = s_rng_idx++;
        if (ble_gattc_disc_all_chrs(conn, s_rng[i].start, s_rng[i].end, on_chr, NULL) == 0) {
            return;                     /* continues in on_chr */
        }
    }
    disc_finished(conn);
}

static int on_chr(uint16_t conn, const struct ble_gatt_error *err, const struct ble_gatt_chr *chr, void *arg)
{
    if (err->status != 0) {             /* BLE_HS_EDONE or an error: next service */
        if (err->status != BLE_HS_EDONE) {
            ESP_LOGW(TAG, "characteristic discovery failed (%d)", err->status);
        }
        disc_next_chrs(conn);
        return 0;
    }
    char full[BLE_UUID_STR_LEN], u[16], line[40];
    ble_uuid_to_str(&chr->uuid.u, full);
    uuid_short(&chr->uuid.u, u, sizeof(u));
    uint8_t p = chr->properties;
    char props[8];
    snprintf(props, sizeof(props), "%s%s%s%s%s",
             p & BLE_GATT_CHR_PROP_READ ? "R" : "", p & BLE_GATT_CHR_PROP_WRITE ? "W" : "",
             p & BLE_GATT_CHR_PROP_WRITE_NO_RSP ? "w" : "", p & BLE_GATT_CHR_PROP_NOTIFY ? "N" : "",
             p & BLE_GATT_CHR_PROP_INDICATE ? "I" : "");
    snprintf(line, sizeof(line), "  %s %s", u, props);
    add_line(line);

    /* serial port candidates, best one wins */
    uint16_t end = s_rng[s_rng_idx - 1].end;            /* service of this characteristic */
    bool can_w = p & (BLE_GATT_CHR_PROP_WRITE | BLE_GATT_CHR_PROP_WRITE_NO_RSP);
    bool can_n = p & BLE_GATT_CHR_PROP_NOTIFY;
    uint16_t u16 = chr->uuid.u.type == BLE_UUID_TYPE_16 ? chr->uuid.u16.value : 0;
    if (strcmp(full, SER_VGATE_UUID) == 0 && can_w && can_n && s_ser_rank < 3) {
        s_ser_rank = 3;
        s_ser_tx = s_ser_rx = chr->val_handle;
        s_ser_rx_end = end;
        s_ser_wnr = p & BLE_GATT_CHR_PROP_WRITE_NO_RSP;
        s_ser_name = "bef8d6c9 (Vgate)";
    } else if (u16 == 0x2AF1 && can_w && s_ser_rank < 2) {
        s_ser_tx = chr->val_handle;
        s_ser_wnr = p & BLE_GATT_CHR_PROP_WRITE_NO_RSP;
        if (s_ser_rx2) {
            s_ser_rank = 2;
            s_ser_rx = s_ser_rx2;
            s_ser_name = "2AF1 / 2AF0";
        }
    } else if (u16 == 0x2AF0 && can_n && s_ser_rank < 2) {
        s_ser_rx2 = chr->val_handle;
        s_ser_rx_end = end;
        if (s_ser_tx) {
            s_ser_rank = 2;
            s_ser_rx = s_ser_rx2;
            s_ser_name = "2AF1 / 2AF0";
        }
    } else if (u16 == 0xFFE1 && can_w && can_n && s_ser_rank < 1) {
        s_ser_rank = 1;
        s_ser_tx = s_ser_rx = chr->val_handle;
        s_ser_rx_end = end;
        s_ser_wnr = p & BLE_GATT_CHR_PROP_WRITE_NO_RSP;
        s_ser_name = "FFE1 (HM-10 style)";
    }
    ESP_LOGI(TAG, "  characteristic %s handle %u props 0x%02X (%s)", full, chr->val_handle, p, props);
    return 0;
}

static int on_svc(uint16_t conn, const struct ble_gatt_error *err, const struct ble_gatt_svc *svc, void *arg)
{
    if (err->status == BLE_HS_EDONE) {
        disc_next_chrs(conn);
        return 0;
    }
    if (err->status != 0) {
        set_event("service discovery failed (%d)", err->status);
        ESP_LOGW(TAG, "service discovery failed (%d)", err->status);
        disc_next_chrs(conn);           /* list what was found */
        return 0;
    }
    char full[BLE_UUID_STR_LEN];
    ble_uuid_to_str(&svc->uuid.u, full);
    ESP_LOGI(TAG, "service %s handles %u-%u", full, svc->start_handle, svc->end_handle);
    if (s_nrng < MAX_DISC_SVCS) {
        s_rng[s_nrng].start = svc->start_handle;
        s_rng[s_nrng].end = svc->end_handle;
        s_nrng++;
    }
    char line[40];
    if (svc->uuid.u.type == BLE_UUID_TYPE_16) {
        uint16_t u = svc->uuid.u16.value;
        snprintf(line, sizeof(line), "%04X %s", u, svc16_name(u));
    } else {
        char buf[BLE_UUID_STR_LEN];
        ble_uuid_to_str(&svc->uuid.u, buf);
        snprintf(line, sizeof(line), "%.36s", buf);
    }
    add_line(line);
    return 0;
}

/* ---------------------------------------------------------------- serial port */

static int on_cccd_written(uint16_t conn, const struct ble_gatt_error *err, struct ble_gatt_attr *attr, void *arg)
{
    if (err->status != 0) {
        ESP_LOGW(TAG, "serial: enabling notifications failed (%d)", err->status);
        set_event("serial: notify on failed (%d)", err->status);
        return 0;
    }
    xStreamBufferReset(s_ser_buf);
    s_ser_ready = true;
    LOCK();
    s_st.serial_ready = true;
    snprintf(s_st.serial_info, sizeof(s_st.serial_info), "%s, MTU %u", s_ser_name, ble_att_mtu(conn));
    UNLOCK();
    ESP_LOGI(TAG, "serial port ready: %s, tx %u rx %u, MTU %u", s_ser_name, s_ser_tx, s_ser_rx, ble_att_mtu(conn));
    return 0;
}

static void write_cccd(uint16_t conn, uint16_t handle)
{
    static const uint8_t on[2] = { 0x01, 0x00 };    /* notifications */
    if (ble_gattc_write_flat(conn, handle, on, sizeof(on), on_cccd_written, NULL) != 0) {
        ESP_LOGW(TAG, "serial: CCCD write not possible");
    }
}

/* Find the CCCD (0x2902) after the notify characteristic's value. */
static int on_dsc(uint16_t conn, const struct ble_gatt_error *err, uint16_t chr_val_handle,
                  const struct ble_gatt_dsc *dsc, void *arg)
{
    static uint16_t found;
    if (err->status == 0) {
        if (found == 0 && dsc->uuid.u.type == BLE_UUID_TYPE_16 && dsc->uuid.u16.value == 0x2902) {
            found = dsc->handle;
        }
        return 0;
    }
    /* done (or failed): use what was found, else the usual place right after the value */
    uint16_t h = found ? found : (uint16_t)(s_ser_rx + 1);
    found = 0;
    ESP_LOGI(TAG, "serial: CCCD at handle %u", h);
    write_cccd(conn, h);
    return 0;
}

static void serial_setup(uint16_t conn)
{
    if (s_ser_rank == 0) {
        ESP_LOGI(TAG, "no serial port on this device");
        return;
    }
    if (ble_gattc_disc_all_dscs(conn, s_ser_rx, s_ser_rx_end, on_dsc, NULL) != 0) {
        write_cccd(conn, s_ser_rx + 1);
    }
}

static void serial_rx(const struct os_mbuf *om)
{
    uint8_t buf[256];
    uint16_t len = OS_MBUF_PKTLEN(om);
    for (uint16_t off = 0; off < len;) {
        uint16_t n = len - off > sizeof(buf) ? sizeof(buf) : len - off;
        if (os_mbuf_copydata(om, off, n, buf) != 0) {
            return;
        }
        if (xStreamBufferSend(s_ser_buf, buf, n, 0) != n) {
            ESP_LOGW(TAG, "serial: receive buffer full, data lost");
        }
        off += n;
    }
}

bool ble_svc_serial_ready(void)
{
    return s_ser_ready;
}

esp_err_t ble_svc_serial_write(const void *data, size_t len)
{
    uint16_t conn = s_conn;
    if (!s_ser_ready || conn == BLE_HS_CONN_HANDLE_NONE) {
        return ESP_ERR_INVALID_STATE;
    }
    size_t chunk = ble_att_mtu(conn) > 3 ? ble_att_mtu(conn) - 3 : 20;
    const uint8_t *p = data;
    while (len > 0) {
        size_t n = len < chunk ? len : chunk;
        int rc;
        int tries = 0;
        do {
            rc = s_ser_wnr ? ble_gattc_write_no_rsp_flat(conn, s_ser_tx, p, n)
                           : ble_gattc_write_flat(conn, s_ser_tx, p, n, NULL, NULL);
            if (rc == BLE_HS_ENOMEM || rc == BLE_HS_EBUSY) {
                vTaskDelay(pdMS_TO_TICKS(5));       /* host buffers full: wait */
            }
        } while ((rc == BLE_HS_ENOMEM || rc == BLE_HS_EBUSY) && ++tries < 40);
        if (rc != 0) {
            ESP_LOGW(TAG, "serial write failed (%d)", rc);
            return ESP_FAIL;
        }
        p += n;
        len -= n;
    }
    return ESP_OK;
}

size_t ble_svc_serial_read(void *buf, size_t max, uint32_t wait_ms)
{
    if (s_ser_buf == NULL) {
        return 0;
    }
    return xStreamBufferReceive(s_ser_buf, buf, max, pdMS_TO_TICKS(wait_ms));
}

void ble_svc_serial_flush(void)
{
    if (s_ser_buf) {
        xStreamBufferReset(s_ser_buf);
    }
}

/* ---------------------------------------------------------------- connection */

static void update_sec(uint16_t conn)
{
    struct ble_gap_conn_desc desc;
    if (ble_gap_conn_find(conn, &desc) != 0) {
        return;
    }
    LOCK();
    s_st.encrypted = desc.sec_state.encrypted;
    s_st.bonded = desc.sec_state.bonded;
    s_st.authenticated = desc.sec_state.authenticated;
    UNLOCK();
}

/* ---- auto-connect (a remembered device, e.g. the OBD adapter) */
static esp_timer_handle_t s_auto_timer;
static volatile bool      s_auto_on, s_auto_paused;
static ble_svc_dev_t      s_auto_dev;
#define AUTO_PERIOD_MS    5000
#define AUTO_TIMEOUT_MS   4000

static esp_err_t connect_dev(const ble_svc_dev_t *dp, int32_t timeout_ms);

static void auto_cb(void *arg)
{
    if (!s_auto_on || s_auto_paused) {
        return;
    }
    LOCK();
    bool ready = s_st.state == BLE_SVC_READY;
    UNLOCK();
    if (ready) {                        /* not scanning, not connected / connecting */
        ESP_LOGI(TAG, "auto-connect: %s %s", s_auto_dev.addr_str, s_auto_dev.name);
        connect_dev(&s_auto_dev, AUTO_TIMEOUT_MS);
    }
}

void ble_svc_set_autoconnect(bool on, const uint8_t addr[6], uint8_t addr_type, const char *name)
{
    if (on && addr) {
        memset(&s_auto_dev, 0, sizeof(s_auto_dev));
        memcpy(s_auto_dev.addr, addr, 6);
        s_auto_dev.addr_type = addr_type;
        addr_to_str(addr, s_auto_dev.addr_str);
        snprintf(s_auto_dev.name, sizeof(s_auto_dev.name), "%s", name ? name : "");
    }
    s_auto_on = on && addr;
}

bool ble_svc_autoconnect_paused(void)
{
    return s_auto_paused;
}

esp_err_t ble_svc_connect(int index)
{
    if (s_lock == NULL) {
        return ESP_ERR_INVALID_STATE;     /* not initialised yet */
    }
    LOCK();
    if (index < 0 || index >= s_st.ndev ||
        (s_st.state != BLE_SVC_READY && s_st.state != BLE_SVC_SCANNING)) {
        UNLOCK();
        return ESP_ERR_INVALID_ARG;
    }
    ble_svc_dev_t d = s_devs[index];
    bool scanning = s_st.state == BLE_SVC_SCANNING;
    UNLOCK();

    if (scanning) {
        ble_svc_scan_stop();
    }
    s_auto_paused = false;              /* a manual connect re-enables auto-connect */
    return connect_dev(&d, 15000);
}

static esp_err_t connect_dev(const ble_svc_dev_t *dp, int32_t timeout_ms)
{
    ble_svc_dev_t d = *dp;
    ble_addr_t addr = { .type = d.addr_type };
    memcpy(addr.val, d.addr, 6);
    /* Short connection interval: every adapter request costs two connection
     * events (write, notify), so this sets the OBD rate over BLE. 7.5-20 ms;
     * the peripheral may ask for a longer one (logged). */
    const struct ble_gap_conn_params cp = {
        .scan_itvl = 0x0010, .scan_window = 0x0010,
        .itvl_min = 6, .itvl_max = 16,              /* x 1.25 ms */
        .latency = 0, .supervision_timeout = 400,   /* x 10 ms = 4 s */
        .min_ce_len = 0, .max_ce_len = 0,
    };
    int rc = ble_gap_connect(s_own_addr_type, &addr, timeout_ms, &cp, gap_event, NULL);
    if (rc != 0) {
        ESP_LOGW(TAG, "ble_gap_connect: %d", rc);
        set_event("connect failed (%d)", rc);
        return ESP_FAIL;
    }
    LOCK();
    s_st.state = BLE_SVC_CONNECTING;
    snprintf(s_st.peer_addr, sizeof(s_st.peer_addr), "%s", d.addr_str);
    memcpy(s_st.peer_addr_raw, d.addr, 6);
    s_st.peer_addr_type = d.addr_type;
    snprintf(s_st.peer_name, sizeof(s_st.peer_name), "%s", d.name);
    s_st.peer_rssi = d.rssi;
    s_st.nsvc = 0;
    s_st.svc_done = false;
    s_st.passkey = 0;
    s_st.encrypted = s_st.bonded = s_st.authenticated = false;
    snprintf(s_st.last_event, sizeof(s_st.last_event), "connecting...");
    UNLOCK();
    ESP_LOGI(TAG, "connecting to %s %s", d.addr_str, d.name);
    return ESP_OK;
}

esp_err_t ble_svc_disconnect(void)
{
    if (s_lock == NULL) {
        return ESP_ERR_INVALID_STATE;     /* not initialised yet */
    }
    s_auto_paused = true;               /* until the next manual connect */
    LOCK();
    uint16_t c = s_conn;
    bool connecting = s_st.state == BLE_SVC_CONNECTING;
    UNLOCK();
    if (connecting) {
        ble_gap_conn_cancel();
        LOCK();
        s_st.state = BLE_SVC_READY;
        UNLOCK();
        return ESP_OK;
    }
    if (c == BLE_HS_CONN_HANDLE_NONE) {
        return ESP_ERR_INVALID_STATE;
    }
    return ble_gap_terminate(c, BLE_ERR_REM_USER_CONN_TERM) == 0 ? ESP_OK : ESP_FAIL;
}

esp_err_t ble_svc_forget_all(void)
{
    if (s_lock == NULL) {
        return ESP_ERR_INVALID_STATE;     /* not initialised yet */
    }
    ble_svc_disconnect();
    int rc = ble_store_clear();
    LOCK();
    s_st.nbonds = count_bonds();
    snprintf(s_st.last_event, sizeof(s_st.last_event), rc == 0 ? "bonds deleted" : "delete failed");
    UNLOCK();
    return rc == 0 ? ESP_OK : ESP_FAIL;
}

/* ---------------------------------------------------------------- GAP events */

static int gap_event(struct ble_gap_event *event, void *arg)
{
    switch (event->type) {
    case BLE_GAP_EVENT_DISC:
        on_disc(&event->disc);
        return 0;

    case BLE_GAP_EVENT_DISC_COMPLETE:
        LOCK();
        if (s_st.state == BLE_SVC_SCANNING) {
            s_st.state = BLE_SVC_READY;
        }
        s_st.scan_count++;
        UNLOCK();
        ESP_LOGI(TAG, "scan done: %d devices", s_st.ndev);
        return 0;

    case BLE_GAP_EVENT_CONNECT:
        if (event->connect.status != 0) {
            LOCK();
            s_st.state = BLE_SVC_READY;
            snprintf(s_st.last_event, sizeof(s_st.last_event), "connect failed (%d)", event->connect.status);
            UNLOCK();
            ESP_LOGW(TAG, "connect failed, status %d", event->connect.status);
            return 0;
        }
        LOCK();
        s_conn = event->connect.conn_handle;
        s_st.state = BLE_SVC_CONNECTED;
        snprintf(s_st.last_event, sizeof(s_st.last_event), "connected, pairing...");
        UNLOCK();
        {
            struct ble_gap_conn_desc cd;
            if (ble_gap_conn_find(event->connect.conn_handle, &cd) == 0) {
                ESP_LOGI(TAG, "connected, handle %d, interval %.2f ms", event->connect.conn_handle,
                         cd.conn_itvl * 1.25);
            }
        }
        ble_gattc_exchange_mtu(event->connect.conn_handle, NULL, NULL);    /* bigger packets */
        /* Pair (or restore encryption with an existing bond), then list services. */
        if (ble_gap_security_initiate(event->connect.conn_handle) != 0) {
            set_event("connected (no pairing)", 0);
        }
        disc_start(event->connect.conn_handle);
        return 0;

    case BLE_GAP_EVENT_CONN_UPDATE: {
        struct ble_gap_conn_desc cd;
        if (event->conn_update.status == 0 && ble_gap_conn_find(event->conn_update.conn_handle, &cd) == 0) {
            ESP_LOGI(TAG, "connection interval now %.2f ms", cd.conn_itvl * 1.25);
        }
        return 0;
    }

    case BLE_GAP_EVENT_NOTIFY_RX:
        if (s_ser_ready && event->notify_rx.attr_handle == s_ser_rx) {
            serial_rx(event->notify_rx.om);
        }
        return 0;

    case BLE_GAP_EVENT_DISCONNECT:
        s_disc_busy = s_disc_again = false;
        serial_reset();
        LOCK();
        s_conn = BLE_HS_CONN_HANDLE_NONE;
        s_st.state = BLE_SVC_READY;
        s_st.encrypted = s_st.authenticated = false;
        s_st.passkey = 0;
        snprintf(s_st.last_event, sizeof(s_st.last_event), "disconnected (0x%X)", event->disconnect.reason);
        s_st.nbonds = count_bonds();
        UNLOCK();
        ESP_LOGI(TAG, "disconnected, reason 0x%X", event->disconnect.reason);
        return 0;

    case BLE_GAP_EVENT_ENC_CHANGE:
        update_sec(event->enc_change.conn_handle);
        LOCK();
        if (event->enc_change.status == 0) {
            snprintf(s_st.last_event, sizeof(s_st.last_event), "%s", s_st.bonded ? "paired + bonded" : "encrypted");
        } else {
            snprintf(s_st.last_event, sizeof(s_st.last_event), "pairing failed (%d)", event->enc_change.status);
        }
        s_st.nbonds = count_bonds();
        UNLOCK();
        ESP_LOGI(TAG, "encryption change, status %d", event->enc_change.status);
        if (event->enc_change.status == 0) {
            disc_start(event->enc_change.conn_handle);  /* may expose more now */
        }
        return 0;

    case BLE_GAP_EVENT_PASSKEY_ACTION: {
        struct ble_sm_io io = { .action = event->passkey.params.action };
        if (io.action == BLE_SM_IOACT_NUMCMP) {
            /* No way to confirm on both sides yet: accept, show the number. */
            LOCK();
            s_st.passkey = event->passkey.params.numcmp;
            UNLOCK();
            io.numcmp_accept = 1;
            ble_sm_inject_io(event->passkey.conn_handle, &io);
        } else if (io.action == BLE_SM_IOACT_DISP) {
            io.passkey = 123456;
            LOCK();
            s_st.passkey = io.passkey;
            UNLOCK();
            ble_sm_inject_io(event->passkey.conn_handle, &io);
        } else {
            set_event("device wants a passkey typed in (unsupported)", 0);
        }
        return 0;
    }

    case BLE_GAP_EVENT_REPEAT_PAIRING: {
        /* The peer lost its bond: forget ours and pair again. */
        struct ble_gap_conn_desc desc;
        if (ble_gap_conn_find(event->repeat_pairing.conn_handle, &desc) == 0) {
            ble_store_util_delete_peer(&desc.peer_id_addr);
        }
        return BLE_GAP_REPEAT_PAIRING_RETRY;
    }

    default:
        return 0;
    }
}

/* ---------------------------------------------------------------- host */

static void on_sync(void)
{
    if (ble_hs_util_ensure_addr(0) != 0 || ble_hs_id_infer_auto(0, &s_own_addr_type) != 0) {
        ESP_LOGE(TAG, "no usable address");
        return;
    }
    uint8_t a[6] = { 0 };
    ble_hs_id_copy_addr(s_own_addr_type, a, NULL);
    char buf[18];
    addr_to_str(a, buf);
    LOCK();
    s_st.state = BLE_SVC_READY;
    s_st.nbonds = count_bonds();
    UNLOCK();
    ESP_LOGI(TAG, "ready, address %s, %d bonds", buf, s_st.nbonds);
}

static void on_reset(int reason)
{
    LOCK();
    s_st.state = BLE_SVC_OFF;
    s_conn = BLE_HS_CONN_HANDLE_NONE;
    UNLOCK();
    ESP_LOGW(TAG, "host reset, reason %d", reason);
}

static void host_task(void *param)
{
    nimble_port_run();
    nimble_port_freertos_deinit();
}

esp_err_t ble_svc_init(void)
{
    s_lock = xSemaphoreCreateMutex();
    s_ser_buf = xStreamBufferCreateWithCaps(SER_RX_BUF, 1, MALLOC_CAP_SPIRAM);
    const esp_timer_create_args_t ta = { .callback = auto_cb, .name = "ble_auto" };
    if (s_lock == NULL || s_ser_buf == NULL || esp_timer_create(&ta, &s_auto_timer) != ESP_OK) {
        return ESP_ERR_NO_MEM;
    }
    esp_timer_start_periodic(s_auto_timer, AUTO_PERIOD_MS * 1000);
    esp_err_t err = nimble_port_init();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "nimble init: %s", esp_err_to_name(err));
        return err;
    }
    ble_hs_cfg.reset_cb = on_reset;
    ble_hs_cfg.sync_cb = on_sync;
    ble_hs_cfg.store_status_cb = ble_store_util_status_rr;

    /* We have a display but no keyboard on the peer side of the dialogue:
     * DisplayYesNo gives numeric comparison with phones, just works otherwise. */
    ble_hs_cfg.sm_io_cap = BLE_SM_IO_CAP_DISP_YES_NO;
    ble_hs_cfg.sm_bonding = 1;
    ble_hs_cfg.sm_mitm = 0;
    ble_hs_cfg.sm_sc = 1;
    ble_hs_cfg.sm_our_key_dist = BLE_SM_PAIR_KEY_DIST_ENC | BLE_SM_PAIR_KEY_DIST_ID;
    ble_hs_cfg.sm_their_key_dist = BLE_SM_PAIR_KEY_DIST_ENC | BLE_SM_PAIR_KEY_DIST_ID;

    ble_svc_gap_init();
    ble_svc_gap_device_name_set("OBD-Dash");
    ble_store_config_init();
    nimble_port_freertos_init(host_task);
    return ESP_OK;
}

void ble_svc_get_status(ble_svc_status_t *out)
{
    if (s_lock == NULL) {
        memset(out, 0, sizeof(*out));
        return;
    }
    uint16_t c;
    LOCK();
    c = s_conn;
    UNLOCK();
    if (c != BLE_HS_CONN_HANDLE_NONE) {
        int8_t rssi;
        if (ble_gap_conn_rssi(c, &rssi) == 0) {
            LOCK();
            s_st.peer_rssi = rssi;
            UNLOCK();
        }
    }
    LOCK();
    *out = s_st;
    UNLOCK();
}

int ble_svc_get_devices(ble_svc_dev_t *out, int max)
{
    if (s_lock == NULL) {
        return 0;
    }
    LOCK();
    int n = s_st.ndev < max ? s_st.ndev : max;
    memcpy(out, s_devs, n * sizeof(*out));
    UNLOCK();
    return n;
}
