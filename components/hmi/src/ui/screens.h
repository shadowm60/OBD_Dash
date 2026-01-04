#ifndef EEZ_LVGL_UI_SCREENS_H
#define EEZ_LVGL_UI_SCREENS_H

#include <lvgl.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct _objects_t {
    lv_obj_t *startup;
    lv_obj_t *read_dtc;
    lv_obj_t *clear_dtc;
    lv_obj_t *live_data;
    lv_obj_t *freez_frame;
    lv_obj_t *settings;
    lv_obj_t *system;
    lv_obj_t *select_params;
    lv_obj_t *obj0;
    lv_obj_t *obj1;
    lv_obj_t *obj2;
    lv_obj_t *obj3;
    lv_obj_t *obj4;
    lv_obj_t *obj5;
    lv_obj_t *obj6;
    lv_obj_t *obj7;
    lv_obj_t *obj8;
    lv_obj_t *obj9;
    lv_obj_t *log_raw_enabled;
    lv_obj_t *log_params_enabled;
    lv_obj_t *obj10;
    lv_obj_t *obj11;
    lv_obj_t *status_text;
    lv_obj_t *status_text_1;
    lv_obj_t *obj12;
    lv_obj_t *read_dtc_bt;
    lv_obj_t *clear_dtc_bt;
    lv_obj_t *save_dtc_bt;
    lv_obj_t *rpm_gauge;
    lv_obj_t *afr_gauge;
    lv_obj_t *afr_sweet;
    lv_obj_t *obj13;
    lv_obj_t *obj14;
    lv_obj_t *con_method_label;
    lv_obj_t *con_method_select;
    lv_obj_t *rusefi_base_addr_label;
    lv_obj_t *rusefi_base_addr_input;
    lv_obj_t *log_raw_label;
    lv_obj_t *log_data_label;
    lv_obj_t *select_params_label;
    lv_obj_t *sys_auto_brightness;
    lv_obj_t *sys_man_brightness;
} objects_t;

extern objects_t objects;

enum ScreensEnum {
    SCREEN_ID_STARTUP = 1,
    SCREEN_ID_READ_DTC = 2,
    SCREEN_ID_CLEAR_DTC = 3,
    SCREEN_ID_LIVE_DATA = 4,
    SCREEN_ID_FREEZ_FRAME = 5,
    SCREEN_ID_SETTINGS = 6,
    SCREEN_ID_SYSTEM = 7,
    SCREEN_ID_SELECT_PARAMS = 8,
};

void create_screen_startup();
void tick_screen_startup();

void create_screen_read_dtc();
void tick_screen_read_dtc();

void create_screen_clear_dtc();
void tick_screen_clear_dtc();

void create_screen_live_data();
void tick_screen_live_data();

void create_screen_freez_frame();
void tick_screen_freez_frame();

void create_screen_settings();
void tick_screen_settings();

void create_screen_system();
void tick_screen_system();

void create_screen_select_params();
void tick_screen_select_params();

void tick_screen_by_id(enum ScreensEnum screenId);
void tick_screen(int screen_index);

void create_screens();


#ifdef __cplusplus
}
#endif

#endif /*EEZ_LVGL_UI_SCREENS_H*/