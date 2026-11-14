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

static const char *TAG = "web_svc";

#define BLOCK       8192
#define NAME_MAX_   64

/* log file prefixes (datalog, diag_view); nothing else on the card is served */
static const char *const PREFIXES[] = { "can_", "par_", "evt_", "dtc_" };
#define NPREFIXES (sizeof(PREFIXES) / sizeof(PREFIXES[0]))

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
"</style></head><body><h1>OBD Dash &ndash; log files</h1><div id=card></div>"
"<div class=bar><button onclick=load()>Refresh</button>"
"<button class=del id=dsel onclick=delsel() disabled>Delete selected</button><span id=msg></span></div>"
"<div id=list></div><script>"
"const G=[['can_','CAN traces (can_)'],['par_','Parameter logs (par_)'],['evt_','Event logs (evt_)'],"
"['dtc_','DTC reports (dtc_)']];"
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

/* "/dl/can_20261114_101002.log" after the prefix -> decoded, checked file name */
static bool parse_target(const char *uri, const char *prefix, char *name, size_t len)
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
    /* plain log names only: no path, no control characters */
    if (n == 0 || strpbrk(name, "/\\:*?\"<>|") != NULL || strstr(name, "..") != NULL) return false;
    for (size_t i = 0; i < n; i++) {
        if ((unsigned char)name[i] < 0x20) return false;
    }
    return is_log_name(name);
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
    DIR *d = sd ? opendir(MOUNT_POINT) : NULL;
    int k = 0;
    struct dirent *e;
    while (d && (e = readdir(d)) != NULL) {
        if (e->d_type == DT_DIR || !is_log_name(e->d_name) || strlen(e->d_name) >= NAME_MAX_) continue;
        char path[sizeof(MOUNT_POINT) + NAME_MAX_ + 1];
        struct stat st;
        snprintf(path, sizeof(path), MOUNT_POINT "/%s", e->d_name);
        bool ok = stat(path, &st) == 0;
        httpd_resp_sendstr_chunk(req, k++ ? ",{\"n\":" : "{\"n\":");
        json_str(req, e->d_name);
        snprintf(b, sizeof(b), ",\"s\":%ld,\"t\":%lld,\"open\":%s}", ok ? (long)st.st_size : 0L,
                 ok ? (long long)st.st_mtime : 0LL, datalog_is_open(e->d_name) ? "true" : "false");
        httpd_resp_sendstr_chunk(req, b);
    }
    if (d) closedir(d);
    httpd_resp_sendstr_chunk(req, "]}");
    return httpd_resp_send_chunk(req, NULL, 0);
}

static esp_err_t dl_get(httpd_req_t *req)
{
    char name[NAME_MAX_], path[sizeof(MOUNT_POINT) + NAME_MAX_ + 1];
    if (!parse_target(req->uri, "/dl/", name, sizeof(name))) return fail(req, "400 Bad Request", "bad file name");
    snprintf(path, sizeof(path), MOUNT_POINT "/%s", name);
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
    snprintf(disp, sizeof(disp), "attachment; filename=\"%s\"", name);
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
    char name[NAME_MAX_], path[sizeof(MOUNT_POINT) + NAME_MAX_ + 1];
    if (!parse_target(req->uri, "/del/", name, sizeof(name))) return fail(req, "400 Bad Request", "bad file name");
    if (datalog_is_open(name)) return fail(req, "409 Conflict", "being recorded, stop logging first");
    snprintf(path, sizeof(path), MOUNT_POINT "/%s", name);
    if (unlink(path) != 0) return fail(req, "404 Not Found", "cannot delete (not there?)");
    ESP_LOGI(TAG, "deleted %s", name);
    return httpd_resp_sendstr(req, "ok");
}

/* ---------------------------------------------------------------- API */

esp_err_t web_svc_init(void)
{
    httpd_config_t cfg = HTTPD_DEFAULT_CONFIG();
    cfg.stack_size = 6144;
    cfg.task_caps = MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT;    /* SD + sockets only, never writes flash */
    cfg.max_open_sockets = 4;
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
