#include "web_svc.h"

#include <stdio.h>
#include <string.h>
#include <strings.h>
#include <ctype.h>
#include <dirent.h>
#include <sys/stat.h>
#include <unistd.h>
#include "esp_log.h"
#include "esp_heap_caps.h"
#include "esp_http_server.h"
#include "esp_vfs_fat.h"
#include "sd_card.h"
#include "datalog.h"
#include "esp_random.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "comsm.h"

static const char *TAG = "web_svc";

#define BLOCK       8192
#define NAME_MAX_   64

/* log file prefixes (datalog, diag_view); nothing else on the card is served */
static const char *const PREFIXES[] = { "can_", "par_", "evt_", "dtc_" };
#define NPREFIXES (sizeof(PREFIXES) / sizeof(PREFIXES[0]))

/* Database folders: "dbc/<name>.dbc" (Dash page) and "diag/<name>.txt" (Diag page)
 * in the API, uploadable. */
#define DBC_DIR     MOUNT_POINT "/db/dbc"
#define DBC_KEY     "dbc/"
#define DIAG_DIR    MOUNT_POINT "/db/diag"
#define DIAG_KEY    "diag/"
#define MAX_UPLOAD  (1024 * 1024)

static httpd_handle_t s_srv;

/* ---------------------------------------------------------------- page */

static const char PAGE[] =
"<!doctype html><html><head><meta charset=utf-8>"
"<meta name=viewport content='width=device-width,initial-scale=1'>"
"<title>OBD Dash - logs</title><style>"
"body{font:15px system-ui,sans-serif;background:#0b0f14;color:#dde3ea;margin:0 auto;padding:16px;max-width:900px}"
"h1{font-size:20px;margin:0 0 4px}h2{font-size:16px;margin:24px 0 8px;color:#8a96a3}"
"table{width:100%;border-collapse:collapse}td{padding:6px 8px;border-bottom:1px solid #253041}"
"td.s{text-align:right;color:#8a96a3;white-space:nowrap;width:1%}td.c{width:1%}"
"a{color:#5aa2ff;text-decoration:none;word-break:break-all}button{background:#22303f;color:#dde3ea;"
"border:1px solid #253041;border-radius:6px;padding:4px 10px;cursor:pointer}button.del{background:#5a2020}"
"button:disabled{opacity:.4;cursor:default}.bar{position:sticky;top:0;background:#0b0f14;padding:8px 0;"
"display:flex;gap:8px;align-items:center;flex-wrap:wrap;border-bottom:1px solid #253041}"
"#msg{color:#3ecf6e;min-height:20px}.open{color:#e0b341}"
".up{margin:16px 0;padding:12px;border:1px dashed #253041;border-radius:8px}"
"</style></head><body><h1>OBD Dash &ndash; log files</h1><div id=card></div>"
"<div class=bar><button onclick=load()>Refresh</button>"
"<button class=del id=dsel onclick=delsel() disabled>Delete selected</button><span id=msg></span></div>"
"<div id=list></div><div class=up>Upload a database file: .dbc for the Dash page, .txt for the Diag page "
"(docs/diag_db.md): <input type=file id=f accept='.dbc,.txt'> <button onclick=up()>Upload</button></div><script>"
"const G=[['can_','CAN traces (can_)'],['par_','Parameter logs (par_)'],['evt_','Event logs (evt_)'],"
"['dtc_','DTC reports (dtc_)'],['dbc/','DBC files for the Dash page (db/dbc/)'],"
"['diag/','Diagnostic databases for the Diag page (db/diag/)']];"
"function sz(b){return b<1024?b+' B':b<1048576?(b/1024).toFixed(1)+' kB':(b/1048576).toFixed(1)+' MB'}"
"function esc(s){return s.replace(/[&<>\"']/g,c=>'&#'+c.charCodeAt(0)+';')}"
"function tm(t){if(t<1700000000)return'';let d=new Date(t*1000);return d.toLocaleString()}"
"function msg(t){document.getElementById('msg').textContent=t}"
"function sel(){return[...document.querySelectorAll('input.f:checked')].map(e=>e.value)}"
"function upd(){let n=sel().length,b=document.getElementById('dsel');b.disabled=!n;"
"b.textContent=n?'Delete selected ('+n+')':'Delete selected'}"
"function gsel(p,on){document.querySelectorAll('input.f[data-g=\"'+p+'\"]').forEach(e=>e.checked=on);upd()}"
"async function load(){let j;try{j=await(await fetch('/api/list')).json()}catch(e){msg('Dash not reachable');return}"
"document.getElementById('card').textContent=j.sd?('SD card: '+sz(j.free)+' free of '+sz(j.total)):'No SD card';"
"j.files.sort((a,b)=>a.n<b.n?1:-1);let h='';"
"for(const[p,t]of G){const fs=j.files.filter(f=>f.n.startsWith(p));let tot=fs.reduce((a,f)=>a+f.s,0);"
"h+='<h2>'+t+' &ndash; '+fs.length+' files, '+sz(tot)+'</h2><table>';"
"if(!fs.length)h+='<tr><td>(none)</td></tr>';"
"else h+='<tr><td class=c><input type=checkbox onchange=\"gsel(\\''+p+'\\',this.checked)\"></td><td>select all</td><td></td><td></td><td></td></tr>';"
"for(const f of fs){let u=encodeURIComponent(f.n);"
"h+='<tr><td class=c>'+(f.open?'':'<input type=checkbox class=f data-g='+p+' value=\"'+esc(f.n)+'\" onchange=upd()>')+'</td>'+"
"'<td><a href=\"/dl/'+u+'\" download>'+esc(f.n)+'</a>'+(f.open?' <span class=open>(recording)</span>':'')+'</td>'+"
"'<td class=s>'+tm(f.t)+'</td><td class=s>'+sz(f.s)+'</td><td class=s>'+"
"(f.open?'':'<button class=del data-n=\"'+esc(f.n)+'\" onclick=del1(this.dataset.n)>Delete</button>')+'</td></tr>'}"
"h+='</table>'}document.getElementById('list').innerHTML=h;upd()}"
"async function rm(n){let r=await fetch('/del/'+encodeURIComponent(n),{method:'POST'});"
"if(!r.ok)msg('Delete of '+n+' failed: '+await r.text());return r.ok}"
"async function del1(n){if(!confirm('Delete '+n+'?'))return;if(await rm(n))msg('Deleted '+n);load()}"
"async function delsel(){let s=sel();if(!s.length||!confirm('Delete '+s.length+' file(s)?'))return;"
"let k=0;for(const n of s){msg('Deleting '+n+' ...');if(!await rm(n))break;k++}"
"if(k==s.length)msg(k+' file(s) deleted');load()}"
"async function up(){let f=document.getElementById('f').files[0];if(!f)return;msg('Uploading '+f.name+' ...');"
"let d=f.name.toLowerCase().endsWith('.dbc')?'dbc/':'diag/';"
"let r=await fetch('/up/'+d+encodeURIComponent(f.name),{method:'POST',body:f});"
"msg(r.ok?f.name+' uploaded':'Upload failed: '+await r.text());document.getElementById('f').value='';load()}"
"load()</script></body></html>";

static esp_err_t page_get(httpd_req_t *req)
{
    httpd_resp_set_type(req, "text/html");
    return httpd_resp_send(req, PAGE, sizeof(PAGE) - 1);
}

/* ---------------------------------------------------------------- helpers */

static int hexv(char c)
{
    if (c >= '0' && c <= '9') return c - '0';
    c = (char)tolower((unsigned char)c);
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    return -1;
}

static bool is_log_name(const char *name)
{
    for (size_t i = 0; i < NPREFIXES; i++) {
        if (strncasecmp(name, PREFIXES[i], strlen(PREFIXES[i])) == 0) {
            return true;
        }
    }
    return false;
}

static bool is_diag_name(const char *name)
{
    size_t n = strlen(name);
    return n > 4 && strcasecmp(name + n - 4, ".txt") == 0;
}

static bool is_dbc_name(const char *name)
{
    size_t n = strlen(name);
    return n > 4 && strcasecmp(name + n - 4, ".dbc") == 0;
}

/* "/dl/can_20261114_101002.log" or "/dl/dbc/rusefi.dbc" after the prefix ->
 * decoded, checked name (as in the API) and the path on the card. */
static bool parse_target(const char *uri, const char *prefix, char *name, size_t len, char *path, size_t plen)
{
    size_t n = 0;
    for (const char *s = uri + strlen(prefix); *s && *s != '?'; s++) {
        char c = *s;
        if (c == '%' && hexv(s[1]) >= 0 && hexv(s[2]) >= 0) {
            c = (char)(hexv(s[1]) << 4 | hexv(s[2]));
            s += 2;
        } else if (c == '+') {
            c = ' ';
        }
        if (n + 1 >= len) return false;
        name[n++] = c;
    }
    name[n] = '\0';
    bool dbc = strncmp(name, DBC_KEY, strlen(DBC_KEY)) == 0;
    bool diag = strncmp(name, DIAG_KEY, strlen(DIAG_KEY)) == 0;
    const char *file = dbc ? name + strlen(DBC_KEY) : diag ? name + strlen(DIAG_KEY) : name;
    /* plain names only: no path, no control characters */
    if (file[0] == '\0' || strpbrk(file, "/\\:*?\"<>|") != NULL || strstr(file, "..") != NULL) return false;
    for (const char *c = file; *c; c++) {
        if ((unsigned char)*c < 0x20) return false;
    }
    if (dbc ? !is_dbc_name(file) : diag ? !is_diag_name(file) : !is_log_name(file)) return false;
    snprintf(path, plen, "%s/%s", dbc ? DBC_DIR : diag ? DIAG_DIR : MOUNT_POINT, file);
    return true;
}

static esp_err_t fail(httpd_req_t *req, const char *status, const char *msg)
{
    httpd_resp_set_status(req, status);
    httpd_resp_set_type(req, "text/plain");
    return httpd_resp_sendstr(req, msg);
}

/* JSON string with escapes (file names) */
static void json_str(httpd_req_t *req, const char *s)
{
    char buf[NAME_MAX_ * 2 + 4];
    size_t n = 0;
    buf[n++] = '"';
    for (; *s && n < sizeof(buf) - 8; s++) {
        unsigned char c = (unsigned char)*s;
        if (c == '"' || c == '\\') { buf[n++] = '\\'; buf[n++] = (char)c; }
        else if (c < 0x20)         n += snprintf(buf + n, sizeof(buf) - n, "\\u%04x", c);
        else                       buf[n++] = (char)c;
    }
    buf[n++] = '"';
    httpd_resp_send_chunk(req, buf, n);
}

/* ---------------------------------------------------------------- handlers */

static void list_dir(httpd_req_t *req, const char *dir, const char *key, bool (*want)(const char *), int *k)
{
    DIR *d = opendir(dir);
    struct dirent *e;
    while (d && (e = readdir(d)) != NULL) {
        if (e->d_type == DT_DIR || !want(e->d_name) || strlen(e->d_name) >= NAME_MAX_ - 8) continue;
        char path[sizeof(DBC_DIR) + NAME_MAX_ + 1], name[NAME_MAX_ + 8], b[96];
        struct stat st;
        snprintf(path, sizeof(path), "%s/%s", dir, e->d_name);
        snprintf(name, sizeof(name), "%s%s", key, e->d_name);
        bool ok = stat(path, &st) == 0;
        httpd_resp_sendstr_chunk(req, (*k)++ ? ",{\"n\":" : "{\"n\":");
        json_str(req, name);
        snprintf(b, sizeof(b), ",\"s\":%ld,\"t\":%lld,\"open\":%s}", ok ? (long)st.st_size : 0L,
                 ok ? (long long)st.st_mtime : 0LL, datalog_is_open(name) ? "true" : "false");
        httpd_resp_sendstr_chunk(req, b);
    }
    if (d) closedir(d);
}

static esp_err_t list_get(httpd_req_t *req)
{
    httpd_resp_set_type(req, "application/json");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    char b[160];
    bool sd = waveshare_sd_card_mounted();
    uint64_t total = 0, free_b = 0;
    if (sd) esp_vfs_fat_info(MOUNT_POINT, &total, &free_b);
    snprintf(b, sizeof(b), "{\"sd\":%s,\"total\":%llu,\"free\":%llu,\"files\":[", sd ? "true" : "false",
             (unsigned long long)total, (unsigned long long)free_b);
    httpd_resp_sendstr_chunk(req, b);
    int k = 0;
    if (sd) {
        list_dir(req, MOUNT_POINT, "", is_log_name, &k);
        list_dir(req, DBC_DIR, DBC_KEY, is_dbc_name, &k);
        list_dir(req, DIAG_DIR, DIAG_KEY, is_diag_name, &k);
    }
    httpd_resp_sendstr_chunk(req, "]}");
    return httpd_resp_send_chunk(req, NULL, 0);
}

static esp_err_t dl_get(httpd_req_t *req)
{
    char name[NAME_MAX_], path[sizeof(DBC_DIR) + NAME_MAX_ + 1];
    if (!parse_target(req->uri, "/dl/", name, sizeof(name), path, sizeof(path))) return fail(req, "400 Bad Request", "bad file name");
    FILE *f = fopen(path, "r");
    if (f == NULL) return fail(req, "404 Not Found", "no such file");
    char *buf = heap_caps_malloc(BLOCK, MALLOC_CAP_SPIRAM);
    if (buf == NULL) {
        fclose(f);
        return fail(req, "500 Internal Server Error", "out of memory");
    }
    const char *ext = strrchr(name, '.');
    httpd_resp_set_type(req, ext && strcasecmp(ext, ".csv") == 0 ? "text/csv"
                             : ext && strcasecmp(ext, ".txt") == 0 ? "text/plain" : "application/octet-stream");
    char disp[NAME_MAX_ + 48];
    const char *base = strrchr(name, '/');
    snprintf(disp, sizeof(disp), "attachment; filename=\"%s\"", base ? base + 1 : name);
    httpd_resp_set_hdr(req, "Content-Disposition", disp);
    size_t r;
    esp_err_t err = ESP_OK;
    while ((r = fread(buf, 1, BLOCK, f)) > 0) {
        if ((err = httpd_resp_send_chunk(req, buf, r)) != ESP_OK) break;
    }
    fclose(f);
    heap_caps_free(buf);
    if (err == ESP_OK) err = httpd_resp_send_chunk(req, NULL, 0);
    ESP_LOGI(TAG, "download %s%s", name, err == ESP_OK ? "" : " aborted");
    return err;
}

static esp_err_t del_post(httpd_req_t *req)
{
    char name[NAME_MAX_], path[sizeof(DBC_DIR) + NAME_MAX_ + 1];
    if (!parse_target(req->uri, "/del/", name, sizeof(name), path, sizeof(path))) return fail(req, "400 Bad Request", "bad file name");
    if (datalog_is_open(name)) return fail(req, "409 Conflict", "being recorded, stop logging first");
    if (unlink(path) != 0) return fail(req, "404 Not Found", "cannot delete (not there?)");
    ESP_LOGI(TAG, "deleted %s", name);
    return httpd_resp_sendstr(req, "ok");
}

/* Upload: only database files (dbc/, diag/), written to a temp file and renamed when complete. */
static esp_err_t up_post(httpd_req_t *req)
{
    char name[NAME_MAX_], path[sizeof(DBC_DIR) + NAME_MAX_ + 1], tmp[sizeof(DBC_DIR) + 16];
    if (!parse_target(req->uri, "/up/", name, sizeof(name), path, sizeof(path))) return fail(req, "400 Bad Request", "bad file name");
    bool diag = strncmp(name, DIAG_KEY, strlen(DIAG_KEY)) == 0;
    if (!diag && strncmp(name, DBC_KEY, strlen(DBC_KEY)) != 0) {
        return fail(req, "403 Forbidden", "only .dbc (Dash) and .txt (Diag) database files can be uploaded");
    }
    const char *dir = diag ? DIAG_DIR : DBC_DIR;
    if (!waveshare_sd_card_mounted()) return fail(req, "503 Service Unavailable", "no SD card");
    if (req->content_len > MAX_UPLOAD) return fail(req, "413 Payload Too Large", "file too big (1 MB max)");
    mkdir(MOUNT_POINT "/db", 0775);
    mkdir(dir, 0775);
    snprintf(tmp, sizeof(tmp), "%s/upload.tmp", dir);
    FILE *f = fopen(tmp, "w");
    if (f == NULL) return fail(req, "500 Internal Server Error", "cannot create the file");
    char *buf = heap_caps_malloc(BLOCK, MALLOC_CAP_SPIRAM);
    if (buf == NULL) {
        fclose(f);
        return fail(req, "500 Internal Server Error", "out of memory");
    }
    size_t left = req->content_len;
    bool ok = true;
    while (left > 0) {
        int r = httpd_req_recv(req, buf, left < BLOCK ? left : BLOCK);
        if (r == HTTPD_SOCK_ERR_TIMEOUT) continue;
        if (r <= 0 || fwrite(buf, 1, r, f) != (size_t)r) {
            ok = false;
            break;
        }
        left -= r;
    }
    heap_caps_free(buf);
    ok = fclose(f) == 0 && ok;
    if (ok) {
        unlink(path);                   /* replace a file of that name */
        ok = rename(tmp, path) == 0;
    }
    if (!ok) {
        unlink(tmp);
        return fail(req, "500 Internal Server Error", "write failed (card full?)");
    }
    ESP_LOGI(TAG, "upload %s, %u bytes", name, (unsigned)req->content_len);
    return httpd_resp_sendstr(req, "ok");
}

/* ---------------------------------------------------------------- PC tool API
 *   GET  /api/status                 {"obd":"idle","pin":true}   (no PIN)
 *   POST /api/uds?pin=&tx=&rx=&req=  one UDS request (hex), read services only:
 *        {"state":"done"|"negative"|"timeout"|"error","nrc":"31","data":"62F190...","error":"..."}
 * The PIN (4 digits, new at every boot) is shown on the dash (Wi-Fi / Diag
 * page). Wrong PIN: 1 s delay; after 10 wrong ones the API is locked until
 * the next boot. */

static char     s_pin[5];
static int      s_pin_fails;

const char *web_svc_pin(void)
{
    return s_pin;
}

/* Read-only services used by UDS and KWP diagnostics. */
static bool allowed(const uint8_t *req, int len)
{
    switch (req[0]) {
    case 0x3E: return len == 2;
    case 0x10: return len == 2 && (req[1] == 0x01 || req[1] == 0x03 || req[1] == 0x81);
    case 0x1A: return len == 2;
    case 0x22: return len >= 3 && (len - 1) % 2 == 0;
    case 0x19: return len >= 2;
    case 0x18: return len >= 2 && len <= 7;
    default:   return false;
    }
}

static esp_err_t json(httpd_req_t *req, const char *status, const char *body)
{
    httpd_resp_set_status(req, status);
    httpd_resp_set_type(req, "application/json");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    return httpd_resp_sendstr(req, body);
}

static esp_err_t status_get(httpd_req_t *req)
{
    comsm_status_t cs;
    comsm_get_status(&cs);
    char b[160];
    snprintf(b, sizeof(b), "{\"obd\":\"%s\",\"pin\":true,\"locked\":%s,\"session\":\"%s\"}",
             comsm_state_name(cs.state), s_pin_fails >= 10 ? "true" : "false", "");
    if (cs.session_open) {
        snprintf(b, sizeof(b), "{\"obd\":\"%s\",\"pin\":true,\"locked\":%s,\"session\":\"%02X on %03X\"}",
                 comsm_state_name(cs.state), s_pin_fails >= 10 ? "true" : "false", cs.session_type, cs.session_tx);
    }
    return json(req, "200 OK", b);
}

static int hexbytes(const char *s, uint8_t *out, int max)
{
    int n = 0;
    while (*s && n < max) {
        if (*s == ' ') { s++; continue; }
        if (hexv(s[0]) < 0 || hexv(s[1]) < 0) return -1;
        out[n++] = (uint8_t)(hexv(s[0]) << 4 | hexv(s[1]));
        s += 2;
    }
    return *s ? -1 : n;
}

static esp_err_t uds_post(httpd_req_t *req)
{
    char q[160], pin[8] = "", tx[8] = "", rx[8] = "", rq[40] = "";
    if (httpd_req_get_url_query_str(req, q, sizeof(q)) != ESP_OK) {
        return json(req, "400 Bad Request", "{\"error\":\"pin, tx, rx and req are needed\"}");
    }
    httpd_query_key_value(q, "pin", pin, sizeof(pin));
    httpd_query_key_value(q, "tx", tx, sizeof(tx));
    httpd_query_key_value(q, "rx", rx, sizeof(rx));
    httpd_query_key_value(q, "req", rq, sizeof(rq));
    if (s_pin_fails >= 10) {
        return json(req, "403 Forbidden", "{\"error\":\"locked after 10 wrong PINs: restart the dash\"}");
    }
    if (strcmp(pin, s_pin) != 0) {
        s_pin_fails++;
        vTaskDelay(pdMS_TO_TICKS(1000));
        ESP_LOGW(TAG, "API: wrong PIN (%d)", s_pin_fails);
        return json(req, "403 Forbidden", "{\"error\":\"wrong PIN (shown on the dash: Wi-Fi or Diag page)\"}");
    }
    char *e1, *e2;
    unsigned long t = strtoul(tx, &e1, 16), r = strtoul(rx, &e2, 16);
    uint8_t data[7];
    int n = hexbytes(rq, data, sizeof(data));
    if (!tx[0] || !rx[0] || *e1 || *e2 || t > 0x7FF || r > 0x7FF || n <= 0) {
        return json(req, "400 Bad Request", "{\"error\":\"tx, rx: 11-bit hex IDs; req: 1..7 hex bytes\"}");
    }
    if (!allowed(data, n)) {
        return json(req, "403 Forbidden",
                    "{\"error\":\"only read requests: UDS 22/19 and KWP 1A/18, plus 3E and diagnostic sessions\"}");
    }
    /* the Diag page may be using the link: wait up to 2 s for a free slot */
    esp_err_t err = ESP_ERR_INVALID_STATE;
    comsm_uds_result_t res;
    comsm_uds_get(&res);
    uint32_t seq = res.seq;
    for (int i = 0; i < 40 && (err = comsm_uds_request(t, r, data, (uint8_t)n)) == ESP_ERR_INVALID_STATE; i++) {
        comsm_status_t cs;
        comsm_get_status(&cs);
        if (cs.state != COMSM_IDLE) {
            return json(req, "409 Conflict", "{\"error\":\"OBD is connected: disconnect it on the dash first\"}");
        }
        vTaskDelay(pdMS_TO_TICKS(50));
        comsm_uds_get(&res);
        seq = res.seq;
    }
    if (err != ESP_OK) {
        return json(req, "409 Conflict", "{\"error\":\"the dash is busy, try again\"}");
    }
    int64_t end = esp_timer_get_time() + 8000000;       /* longer than response pending (5 s) */
    do {
        vTaskDelay(pdMS_TO_TICKS(10));
        comsm_uds_get(&res);
    } while ((res.state == COMSM_UDS_BUSY || res.seq == seq) && esp_timer_get_time() < end);
    static const char *const ST[] = { "idle", "busy", "done", "negative", "timeout", "error" };
    char *b = heap_caps_malloc(COMSM_UDS_MAX * 2 + 160, MALLOC_CAP_SPIRAM);
    if (b == NULL) {
        return json(req, "500 Internal Server Error", "{\"error\":\"out of memory\"}");
    }
    int o = snprintf(b, 160, "{\"state\":\"%s\",\"nrc\":\"%02X\",\"error\":\"%s\",\"data\":\"",
                     res.seq == seq ? "timeout" : ST[res.state], res.nrc, res.error);
    for (int i = 0; i < res.len && res.seq != seq; i++) {
        o += sprintf(b + o, "%02X", res.data[i]);
    }
    strcpy(b + o, "\"}");
    ESP_LOGI(TAG, "API: %03lX %s -> %s", t, rq, ST[res.state]);
    esp_err_t e = json(req, "200 OK", b);
    heap_caps_free(b);
    return e;
}

/* ---------------------------------------------------------------- API */

esp_err_t web_svc_init(void)
{
    snprintf(s_pin, sizeof(s_pin), "%04lu", (unsigned long)(esp_random() % 10000));
    httpd_config_t cfg = HTTPD_DEFAULT_CONFIG();
    cfg.stack_size = 6144;
    cfg.task_caps = MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT;    /* SD + sockets only, never writes flash */
    cfg.max_open_sockets = 4;
    cfg.max_uri_handlers = 12;
    cfg.lru_purge_enable = true;
    cfg.uri_match_fn = httpd_uri_match_wildcard;
    cfg.core_id = 0;                /* CAN / OBD run on core 1 */
    cfg.task_priority = 2;          /* below datalog: logging wins over downloads */
    esp_err_t err = httpd_start(&s_srv, &cfg);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "httpd_start: %s", esp_err_to_name(err));
        s_srv = NULL;
        return err;
    }
    const httpd_uri_t uris[] = {
        { .uri = "/",         .method = HTTP_GET,  .handler = page_get },
        { .uri = "/api/list", .method = HTTP_GET,  .handler = list_get },
        { .uri = "/dl/*",     .method = HTTP_GET,  .handler = dl_get },
        { .uri = "/del/*",    .method = HTTP_POST, .handler = del_post },
        { .uri = "/up/*",     .method = HTTP_POST, .handler = up_post },
        { .uri = "/api/status", .method = HTTP_GET, .handler = status_get },
        { .uri = "/api/uds",  .method = HTTP_POST, .handler = uds_post },
    };
    for (size_t i = 0; i < sizeof(uris) / sizeof(uris[0]); i++) {
        httpd_register_uri_handler(s_srv, &uris[i]);
    }
    ESP_LOGI(TAG, "log file manager on port %d", cfg.server_port);
    return ESP_OK;
}

bool web_svc_running(void)
{
    return s_srv != NULL;
}
