#include <string.h>

#include "screens.h"
#include "images.h"
#include "fonts.h"
#include "actions.h"
#include "vars.h"
#include "styles.h"
#include "ui.h"

#include <string.h>

objects_t objects;
lv_obj_t *tick_value_change_obj;

static void event_handler_cb_startup_obj0(lv_event_t *e) {
    lv_event_code_t event = lv_event_get_code(e);
    void *flowState = lv_event_get_user_data(e);
    (void)flowState;
    
    if (event == LV_EVENT_RELEASED) {
        e->user_data = (void *)0;
        flowPropagateValueLVGLEvent(flowState, 0, 0, e);
    }
}

static void event_handler_cb_startup_obj1(lv_event_t *e) {
    lv_event_code_t event = lv_event_get_code(e);
    void *flowState = lv_event_get_user_data(e);
    (void)flowState;
    
    if (event == LV_EVENT_RELEASED) {
        e->user_data = (void *)0;
        flowPropagateValueLVGLEvent(flowState, 3, 0, e);
    }
}

static void event_handler_cb_startup_obj2(lv_event_t *e) {
    lv_event_code_t event = lv_event_get_code(e);
    void *flowState = lv_event_get_user_data(e);
    (void)flowState;
    
    if (event == LV_EVENT_RELEASED) {
        e->user_data = (void *)0;
        flowPropagateValueLVGLEvent(flowState, 5, 0, e);
    }
}

static void event_handler_cb_startup_obj3(lv_event_t *e) {
    lv_event_code_t event = lv_event_get_code(e);
    void *flowState = lv_event_get_user_data(e);
    (void)flowState;
    
    if (event == LV_EVENT_PRESSED) {
        e->user_data = (void *)0;
        flowPropagateValueLVGLEvent(flowState, 7, 0, e);
    }
}

static void event_handler_cb_startup_obj4(lv_event_t *e) {
    lv_event_code_t event = lv_event_get_code(e);
    void *flowState = lv_event_get_user_data(e);
    (void)flowState;
    
    if (event == LV_EVENT_RELEASED) {
        e->user_data = (void *)0;
        flowPropagateValueLVGLEvent(flowState, 9, 0, e);
    }
}

static void event_handler_cb_startup_obj5(lv_event_t *e) {
    lv_event_code_t event = lv_event_get_code(e);
    void *flowState = lv_event_get_user_data(e);
    (void)flowState;
    
    if (event == LV_EVENT_PRESSED) {
        e->user_data = (void *)0;
        flowPropagateValueLVGLEvent(flowState, 11, 0, e);
    }
}

static void event_handler_cb_read_dtc_read_dtc(lv_event_t *e) {
    lv_event_code_t event = lv_event_get_code(e);
    void *flowState = lv_event_get_user_data(e);
    (void)flowState;
    
    if (event == LV_EVENT_GESTURE) {
        e->user_data = (void *)0;
        flowPropagateValueLVGLEvent(flowState, 1, 0, e);
    }
}

static void event_handler_cb_clear_dtc_clear_dtc(lv_event_t *e) {
    lv_event_code_t event = lv_event_get_code(e);
    void *flowState = lv_event_get_user_data(e);
    (void)flowState;
    
    if (event == LV_EVENT_GESTURE) {
        e->user_data = (void *)0;
        flowPropagateValueLVGLEvent(flowState, 1, 0, e);
    }
}

static void event_handler_cb_live_data_live_data(lv_event_t *e) {
    lv_event_code_t event = lv_event_get_code(e);
    void *flowState = lv_event_get_user_data(e);
    (void)flowState;
    
    if (event == LV_EVENT_GESTURE) {
        e->user_data = (void *)0;
        flowPropagateValueLVGLEvent(flowState, 1, 0, e);
    }
}

static void event_handler_cb_freez_frame_freez_frame(lv_event_t *e) {
    lv_event_code_t event = lv_event_get_code(e);
    void *flowState = lv_event_get_user_data(e);
    (void)flowState;
    
    if (event == LV_EVENT_GESTURE) {
        e->user_data = (void *)0;
        flowPropagateValueLVGLEvent(flowState, 1, 0, e);
    }
}

static void event_handler_cb_settings_settings(lv_event_t *e) {
    lv_event_code_t event = lv_event_get_code(e);
    void *flowState = lv_event_get_user_data(e);
    (void)flowState;
    
    if (event == LV_EVENT_GESTURE) {
        e->user_data = (void *)0;
        flowPropagateValueLVGLEvent(flowState, 1, 0, e);
    }
}

static void event_handler_cb_system_system(lv_event_t *e) {
    lv_event_code_t event = lv_event_get_code(e);
    void *flowState = lv_event_get_user_data(e);
    (void)flowState;
    
    if (event == LV_EVENT_GESTURE) {
        e->user_data = (void *)0;
        flowPropagateValueLVGLEvent(flowState, 1, 0, e);
    }
}

void create_screen_startup() {
    void *flowState = getFlowState(0, 0);
    (void)flowState;
    lv_obj_t *obj = lv_obj_create(0);
    objects.startup = obj;
    lv_obj_set_pos(obj, 0, 0);
    lv_obj_set_size(obj, 800, 480);
    lv_obj_set_style_bg_color(obj, lv_color_hex(0xff000000), LV_PART_MAIN | LV_STATE_DEFAULT);
    {
        lv_obj_t *parent_obj = obj;
        {
            lv_obj_t *obj = lv_btn_create(parent_obj);
            objects.obj0 = obj;
            lv_obj_set_pos(obj, 21, 14);
            lv_obj_set_size(obj, 334, 88);
            lv_obj_add_event_cb(obj, event_handler_cb_startup_obj0, LV_EVENT_ALL, flowState);
            {
                lv_obj_t *parent_obj = obj;
                {
                    lv_obj_t *obj = lv_label_create(parent_obj);
                    lv_obj_set_pos(obj, 0, 0);
                    lv_obj_set_size(obj, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
                    lv_obj_set_style_align(obj, LV_ALIGN_CENTER, LV_PART_MAIN | LV_STATE_DEFAULT);
                    lv_obj_set_style_text_font(obj, &lv_font_montserrat_30, LV_PART_MAIN | LV_STATE_DEFAULT);
                    lv_label_set_text(obj, "Read DTCs");
                }
            }
        }
        {
            lv_obj_t *obj = lv_btn_create(parent_obj);
            objects.obj1 = obj;
            lv_obj_set_pos(obj, 437, 13);
            lv_obj_set_size(obj, 334, 88);
            lv_obj_add_event_cb(obj, event_handler_cb_startup_obj1, LV_EVENT_ALL, flowState);
            {
                lv_obj_t *parent_obj = obj;
                {
                    lv_obj_t *obj = lv_label_create(parent_obj);
                    lv_obj_set_pos(obj, 0, 0);
                    lv_obj_set_size(obj, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
                    lv_obj_set_style_align(obj, LV_ALIGN_CENTER, LV_PART_MAIN | LV_STATE_DEFAULT);
                    lv_obj_set_style_text_font(obj, &lv_font_montserrat_30, LV_PART_MAIN | LV_STATE_DEFAULT);
                    lv_label_set_text(obj, "Clear DTCs");
                }
            }
        }
        {
            lv_obj_t *obj = lv_btn_create(parent_obj);
            objects.obj2 = obj;
            lv_obj_set_pos(obj, 20, 144);
            lv_obj_set_size(obj, 334, 88);
            lv_obj_add_event_cb(obj, event_handler_cb_startup_obj2, LV_EVENT_ALL, flowState);
            {
                lv_obj_t *parent_obj = obj;
                {
                    lv_obj_t *obj = lv_label_create(parent_obj);
                    lv_obj_set_pos(obj, 0, 0);
                    lv_obj_set_size(obj, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
                    lv_obj_set_style_align(obj, LV_ALIGN_CENTER, LV_PART_MAIN | LV_STATE_DEFAULT);
                    lv_obj_set_style_text_font(obj, &lv_font_montserrat_30, LV_PART_MAIN | LV_STATE_DEFAULT);
                    lv_label_set_text(obj, "Live Data");
                }
            }
        }
        {
            lv_obj_t *obj = lv_btn_create(parent_obj);
            objects.obj3 = obj;
            lv_obj_set_pos(obj, 437, 143);
            lv_obj_set_size(obj, 334, 88);
            lv_obj_add_event_cb(obj, event_handler_cb_startup_obj3, LV_EVENT_ALL, flowState);
            {
                lv_obj_t *parent_obj = obj;
                {
                    lv_obj_t *obj = lv_label_create(parent_obj);
                    lv_obj_set_pos(obj, 0, 0);
                    lv_obj_set_size(obj, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
                    lv_obj_set_style_align(obj, LV_ALIGN_CENTER, LV_PART_MAIN | LV_STATE_DEFAULT);
                    lv_obj_set_style_text_font(obj, &lv_font_montserrat_30, LV_PART_MAIN | LV_STATE_DEFAULT);
                    lv_label_set_text(obj, "Freez Fame");
                }
            }
        }
        {
            lv_obj_t *obj = lv_btn_create(parent_obj);
            objects.obj4 = obj;
            lv_obj_set_pos(obj, 21, 287);
            lv_obj_set_size(obj, 334, 88);
            lv_obj_add_event_cb(obj, event_handler_cb_startup_obj4, LV_EVENT_ALL, flowState);
            {
                lv_obj_t *parent_obj = obj;
                {
                    lv_obj_t *obj = lv_label_create(parent_obj);
                    lv_obj_set_pos(obj, 0, 0);
                    lv_obj_set_size(obj, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
                    lv_obj_set_style_align(obj, LV_ALIGN_CENTER, LV_PART_MAIN | LV_STATE_DEFAULT);
                    lv_obj_set_style_text_font(obj, &lv_font_montserrat_30, LV_PART_MAIN | LV_STATE_DEFAULT);
                    lv_label_set_text(obj, "Settings");
                }
            }
        }
        {
            lv_obj_t *obj = lv_btn_create(parent_obj);
            objects.obj5 = obj;
            lv_obj_set_pos(obj, 438, 287);
            lv_obj_set_size(obj, 334, 88);
            lv_obj_add_event_cb(obj, event_handler_cb_startup_obj5, LV_EVENT_ALL, flowState);
            {
                lv_obj_t *parent_obj = obj;
                {
                    lv_obj_t *obj = lv_label_create(parent_obj);
                    lv_obj_set_pos(obj, 0, 0);
                    lv_obj_set_size(obj, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
                    lv_obj_set_style_align(obj, LV_ALIGN_CENTER, LV_PART_MAIN | LV_STATE_DEFAULT);
                    lv_obj_set_style_text_font(obj, &lv_font_montserrat_30, LV_PART_MAIN | LV_STATE_DEFAULT);
                    lv_label_set_text(obj, "System");
                }
            }
        }
        {
            // status_text
            lv_obj_t *obj = lv_label_create(parent_obj);
            objects.status_text = obj;
            lv_obj_set_pos(obj, 38, 413);
            lv_obj_set_size(obj, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
            lv_obj_set_style_text_color(obj, lv_color_hex(0xff00ff00), LV_PART_MAIN | LV_STATE_DEFAULT);
            lv_obj_set_style_text_font(obj, &lv_font_montserrat_26, LV_PART_MAIN | LV_STATE_DEFAULT);
            lv_label_set_text(obj, "Status: ");
        }
        {
            // status_text_1
            lv_obj_t *obj = lv_label_create(parent_obj);
            objects.status_text_1 = obj;
            lv_obj_set_pos(obj, 136, 413);
            lv_obj_set_size(obj, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
            lv_obj_set_style_text_color(obj, lv_color_hex(0xff00ff00), LV_PART_MAIN | LV_STATE_DEFAULT);
            lv_obj_set_style_text_font(obj, &lv_font_montserrat_26, LV_PART_MAIN | LV_STATE_DEFAULT);
            lv_label_set_text(obj, "");
        }
    }
    
    tick_screen_startup();
}

void tick_screen_startup() {
    void *flowState = getFlowState(0, 0);
    (void)flowState;
    {
        const char *new_val = evalTextProperty(flowState, 14, 3, "Failed to evaluate Text in Label widget");
        const char *cur_val = lv_label_get_text(objects.status_text_1);
        if (strcmp(new_val, cur_val) != 0) {
            tick_value_change_obj = objects.status_text_1;
            lv_label_set_text(objects.status_text_1, new_val);
            tick_value_change_obj = NULL;
        }
    }
}

void create_screen_read_dtc() {
    void *flowState = getFlowState(0, 1);
    (void)flowState;
    lv_obj_t *obj = lv_obj_create(0);
    objects.read_dtc = obj;
    lv_obj_set_pos(obj, 0, 0);
    lv_obj_set_size(obj, 800, 480);
    lv_obj_add_event_cb(obj, event_handler_cb_read_dtc_read_dtc, LV_EVENT_ALL, flowState);
    lv_obj_set_style_bg_color(obj, lv_color_hex(0xff000000), LV_PART_MAIN | LV_STATE_DEFAULT);
    
    tick_screen_read_dtc();
}

void tick_screen_read_dtc() {
    void *flowState = getFlowState(0, 1);
    (void)flowState;
}

void create_screen_clear_dtc() {
    void *flowState = getFlowState(0, 2);
    (void)flowState;
    lv_obj_t *obj = lv_obj_create(0);
    objects.clear_dtc = obj;
    lv_obj_set_pos(obj, 0, 0);
    lv_obj_set_size(obj, 800, 480);
    lv_obj_add_event_cb(obj, event_handler_cb_clear_dtc_clear_dtc, LV_EVENT_ALL, flowState);
    lv_obj_set_style_bg_color(obj, lv_color_hex(0xff000000), LV_PART_MAIN | LV_STATE_DEFAULT);
    
    tick_screen_clear_dtc();
}

void tick_screen_clear_dtc() {
    void *flowState = getFlowState(0, 2);
    (void)flowState;
}

void create_screen_live_data() {
    void *flowState = getFlowState(0, 3);
    (void)flowState;
    lv_obj_t *obj = lv_obj_create(0);
    objects.live_data = obj;
    lv_obj_set_pos(obj, 0, 0);
    lv_obj_set_size(obj, 800, 480);
    lv_obj_add_event_cb(obj, event_handler_cb_live_data_live_data, LV_EVENT_ALL, flowState);
    lv_obj_set_style_bg_color(obj, lv_color_hex(0xff000000), LV_PART_MAIN | LV_STATE_DEFAULT);
    
    tick_screen_live_data();
}

void tick_screen_live_data() {
    void *flowState = getFlowState(0, 3);
    (void)flowState;
}

void create_screen_freez_frame() {
    void *flowState = getFlowState(0, 4);
    (void)flowState;
    lv_obj_t *obj = lv_obj_create(0);
    objects.freez_frame = obj;
    lv_obj_set_pos(obj, 0, 0);
    lv_obj_set_size(obj, 800, 480);
    lv_obj_add_event_cb(obj, event_handler_cb_freez_frame_freez_frame, LV_EVENT_ALL, flowState);
    lv_obj_set_style_bg_color(obj, lv_color_hex(0xff000000), LV_PART_MAIN | LV_STATE_DEFAULT);
    
    tick_screen_freez_frame();
}

void tick_screen_freez_frame() {
    void *flowState = getFlowState(0, 4);
    (void)flowState;
}

void create_screen_settings() {
    void *flowState = getFlowState(0, 5);
    (void)flowState;
    lv_obj_t *obj = lv_obj_create(0);
    objects.settings = obj;
    lv_obj_set_pos(obj, 0, 0);
    lv_obj_set_size(obj, 800, 480);
    lv_obj_add_event_cb(obj, event_handler_cb_settings_settings, LV_EVENT_ALL, flowState);
    lv_obj_set_style_bg_color(obj, lv_color_hex(0xff000000), LV_PART_MAIN | LV_STATE_DEFAULT);
    
    tick_screen_settings();
}

void tick_screen_settings() {
    void *flowState = getFlowState(0, 5);
    (void)flowState;
}

void create_screen_system() {
    void *flowState = getFlowState(0, 6);
    (void)flowState;
    lv_obj_t *obj = lv_obj_create(0);
    objects.system = obj;
    lv_obj_set_pos(obj, 0, 0);
    lv_obj_set_size(obj, 800, 480);
    lv_obj_add_event_cb(obj, event_handler_cb_system_system, LV_EVENT_ALL, flowState);
    lv_obj_set_style_bg_color(obj, lv_color_hex(0xff000000), LV_PART_MAIN | LV_STATE_DEFAULT);
    
    tick_screen_system();
}

void tick_screen_system() {
    void *flowState = getFlowState(0, 6);
    (void)flowState;
}


static const char *screen_names[] = { "startup", "readDTC", "clearDTC", "liveData", "freezFrame", "settings", "system" };
static const char *object_names[] = { "startup", "read_dtc", "clear_dtc", "live_data", "freez_frame", "settings", "system", "obj0", "obj1", "obj2", "obj3", "obj4", "obj5", "status_text", "status_text_1" };


typedef void (*tick_screen_func_t)();
tick_screen_func_t tick_screen_funcs[] = {
    tick_screen_startup,
    tick_screen_read_dtc,
    tick_screen_clear_dtc,
    tick_screen_live_data,
    tick_screen_freez_frame,
    tick_screen_settings,
    tick_screen_system,
};
void tick_screen(int screen_index) {
    tick_screen_funcs[screen_index]();
}
void tick_screen_by_id(enum ScreensEnum screenId) {
    tick_screen_funcs[screenId - 1]();
}

void create_screens() {
    eez_flow_init_screen_names(screen_names, sizeof(screen_names) / sizeof(const char *));
    eez_flow_init_object_names(object_names, sizeof(object_names) / sizeof(const char *));
    
    lv_disp_t *dispp = lv_disp_get_default();
    lv_theme_t *theme = lv_theme_default_init(dispp, lv_palette_main(LV_PALETTE_BLUE), lv_palette_main(LV_PALETTE_RED), false, LV_FONT_DEFAULT);
    lv_disp_set_theme(dispp, theme);
    
    create_screen_startup();
    create_screen_read_dtc();
    create_screen_clear_dtc();
    create_screen_live_data();
    create_screen_freez_frame();
    create_screen_settings();
    create_screen_system();
}
