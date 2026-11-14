/*
 * nav - pages by EEZ screen name (eez_screen_table, generated from screens.h).
 * The sidebar buttons call the native actions nav_<page> (actions.c).
 * The page shown last is restored at power-up.
 */
#include <string.h>
#include <ctype.h>
#include "lvgl.h"
#include "ui.h"
#include "esp_log.h"
#include "bridge.h"
#include "eez_var_table.h"
#include "settings.h"

static const char *TAG = "nav";

static char s_cur[SETTINGS_PAGE_LEN];

static int32_t screen_id(const char *page)
{
    for (unsigned i = 0; i < eez_screen_table_len; i++) {
        if (eez_name_equal(eez_screen_table[i].name, page)) {
            return (int32_t)eez_screen_table[i].index;
        }
    }
    return -1;
}

static void show_anim(const char *page, int32_t id, lv_scr_load_anim_t anim, uint32_t ms)
{
    eez_flow_set_screen((int16_t)id, anim, ms, 0);
    size_t i = 0;
    for (; page[i] && i < sizeof(s_cur) - 1; i++) {
        s_cur[i] = (char)tolower((unsigned char)page[i]);
    }
    s_cur[i] = '\0';

    app_settings_t cfg;
    settings_get(&cfg);
    if (strcmp(cfg.page, s_cur) != 0) {
        strcpy(cfg.page, s_cur);
        settings_put(&cfg);
    }
}

static void show(const char *page, int32_t id)
{
    show_anim(page, id, LV_SCR_LOAD_ANIM_NONE, 0);
}

void nav_go_anim(const char *page, lv_scr_load_anim_t anim, uint32_t ms)
{
    int32_t id = screen_id(page);
    if (id < 0) {
        ESP_LOGW(TAG, "no screen named '%s'", page);
        return;
    }
    show_anim(page, id, anim, ms);
}

void nav_go(const char *page)
{
    nav_go_anim(page, LV_SCR_LOAD_ANIM_NONE, 0);
}

void nav_init(const char *page)
{
    int32_t id = page && page[0] ? screen_id(page) : -1;
    if (id >= 0) {
        show(page, id);
    } else if (eez_screen_table_len > 0) {
        show(eez_screen_table[0].name, eez_screen_table[0].index);
    }
}

const char *nav_current(void)
{
    return s_cur;
}

bool nav_is(const char *page)
{
    return eez_name_equal(s_cur, page);
}
