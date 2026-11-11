#ifndef EEZ_LVGL_UI_SCREENS_H
#define EEZ_LVGL_UI_SCREENS_H

#include <lvgl.h>

#ifdef __cplusplus
extern "C" {
#endif

// Screens

enum ScreensEnum {
    _SCREEN_ID_FIRST = 1,
    SCREEN_ID_STARTUP = 1,
    SCREEN_ID_READ_DTC = 2,
    SCREEN_ID_CLEAR_DTC = 3,
    SCREEN_ID_DRIVE = 4,
    SCREEN_ID_LIVE_DATA = 5,
    SCREEN_ID_TUNE = 6,
    SCREEN_ID_FREEZ_FRAME = 7,
    SCREEN_ID_SETTINGS = 8,
    SCREEN_ID_SYSTEM = 9,
    SCREEN_ID_SELECT_PARAMS = 10,
    _SCREEN_ID_LAST = 10
};

typedef struct _objects_t {
    lv_obj_t *startup;
    lv_obj_t *read_dtc;
    lv_obj_t *clear_dtc;
    lv_obj_t *drive;
    lv_obj_t *live_data;
    lv_obj_t *tune;
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
    lv_obj_t *status_text;
    lv_obj_t *status_text_1;
    lv_obj_t *conn_button;
    lv_obj_t *conn_button_text;
    lv_obj_t *obj6;
    lv_obj_t *read_dtc_bt;
    lv_obj_t *clear_dtc_bt;
    lv_obj_t *save_dtc_bt;
    lv_obj_t *obj7;
    lv_obj_t *obj8;
    lv_obj_t *drive_mil;
    lv_obj_t *obj9;
    lv_obj_t *drive_rpm_gauge;
    lv_obj_t *obj10;
    lv_obj_t *obj11;
    lv_obj_t *obj12;
    lv_obj_t *obj13;
    lv_obj_t *obj14;
    lv_obj_t *obj15;
    lv_obj_t *obj16;
    lv_obj_t *obj17;
    lv_obj_t *obj18;
    lv_obj_t *obj19;
    lv_obj_t *obj20;
    lv_obj_t *obj21;
    lv_obj_t *obj22;
    lv_obj_t *obj23;
    lv_obj_t *obj24;
    lv_obj_t *obj25;
    lv_obj_t *obj26;
    lv_obj_t *rpm_gauge;
    lv_obj_t *afr_gauge;
    lv_obj_t *afr_sweet;
    lv_obj_t *obj27;
    lv_obj_t *obj28;
    lv_obj_t *obj29;
    lv_obj_t *race_mil;
    lv_obj_t *obj30;
    lv_obj_t *obj31;
    lv_obj_t *obj32;
    lv_obj_t *obj33;
    lv_obj_t *obj34;
    lv_obj_t *obj35;
    lv_obj_t *obj36;
    lv_obj_t *obj37;
    lv_obj_t *obj38;
    lv_obj_t *obj39;
    lv_obj_t *obj40;
    lv_obj_t *obj41;
    lv_obj_t *obj42;
    lv_obj_t *obj43;
    lv_obj_t *obj44;
    lv_obj_t *obj45;
    lv_obj_t *obj46;
    lv_obj_t *obj47;
    lv_obj_t *obj48;
    lv_obj_t *tune_mil;
    lv_obj_t *obj49;
    lv_obj_t *obj50;
    lv_obj_t *obj51;
    lv_obj_t *obj52;
    lv_obj_t *obj53;
    lv_obj_t *obj54;
    lv_obj_t *obj55;
    lv_obj_t *obj56;
    lv_obj_t *obj57;
    lv_obj_t *obj58;
    lv_obj_t *obj59;
    lv_obj_t *obj60;
    lv_obj_t *obj61;
    lv_obj_t *obj62;
    lv_obj_t *obj63;
    lv_obj_t *obj64;
    lv_obj_t *obj65;
    lv_obj_t *obj66;
    lv_obj_t *obj67;
    lv_obj_t *obj68;
    lv_obj_t *obj69;
    lv_obj_t *obj70;
    lv_obj_t *obj71;
    lv_obj_t *obj72;
    lv_obj_t *obj73;
    lv_obj_t *obj74;
    lv_obj_t *obj75;
    lv_obj_t *obj76;
    lv_obj_t *obj77;
    lv_obj_t *obj78;
    lv_obj_t *obj79;
    lv_obj_t *obj80;
    lv_obj_t *obj81;
    lv_obj_t *obj82;
    lv_obj_t *obj83;
    lv_obj_t *obj84;
    lv_obj_t *obj85;
    lv_obj_t *obj86;
    lv_obj_t *obj87;
    lv_obj_t *obj88;
    lv_obj_t *obj89;
    lv_obj_t *obj90;
    lv_obj_t *obj91;
    lv_obj_t *obj92;
    lv_obj_t *obj93;
    lv_obj_t *obj94;
    lv_obj_t *obj95;
    lv_obj_t *obj96;
    lv_obj_t *con_method_label;
    lv_obj_t *con_method_select;
    lv_obj_t *rusefi_base_addr_label;
    lv_obj_t *rusefi_base_addr_input;
    lv_obj_t *obj97;
    lv_obj_t *obj98;
    lv_obj_t *obj99;
    lv_obj_t *log_raw_label;
    lv_obj_t *log_raw_enabled;
    lv_obj_t *log_data_label;
    lv_obj_t *log_params_enabled;
    lv_obj_t *select_params_label;
    lv_obj_t *obj100;
    lv_obj_t *log_all_traffic;
    lv_obj_t *sys_auto_brightness;
    lv_obj_t *sys_man_brightness;
    lv_obj_t *obj101;
    lv_obj_t *sys_gauge_anim;
    lv_obj_t *sys_read_speed_label;
    lv_obj_t *sys_read_speed;
    lv_obj_t *sys_read_speed_value;
    lv_obj_t *obj102;
    lv_obj_t *obd_srv_table;
    lv_obj_t *obj103;
} objects_t;

extern objects_t objects;

typedef struct {
    lv_meter_scale_t *scale;
    lv_meter_indicator_t *indicator;
} screen_drive_state_t;

typedef struct {
    lv_meter_scale_t *scale;
    lv_meter_indicator_t *indicator;
    lv_meter_scale_t *scale1;
    lv_meter_indicator_t *indicator1;
} screen_live_data_state_t;

extern screen_drive_state_t screen_drive_state;
extern screen_live_data_state_t screen_live_data_state;

void create_screen_startup();
void tick_screen_startup();

void create_screen_read_dtc();
void tick_screen_read_dtc();

void create_screen_clear_dtc();
void tick_screen_clear_dtc();

void create_screen_drive();
void tick_screen_drive();

void create_screen_live_data();
void tick_screen_live_data();

void create_screen_tune();
void tick_screen_tune();

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