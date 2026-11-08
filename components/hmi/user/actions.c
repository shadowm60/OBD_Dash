#include "actions.h"
#include <stdint.h>
#include "screens.h"
#include "obd_handler.h"
#include "esp_err.h"
#include "esp_log.h"
#include "comSM.h"

static const char *TAG = "hmi_usr";

void action_log_raw_enabled(lv_event_t * e) {

}

void action_log_params_enabled(lv_event_t * e) {

}

extern objects_t objects;

static void checkbox_event_handler(lv_event_t * e)
{
    lv_obj_t * cb = lv_event_get_target(e);

    bool checked = lv_obj_has_state(cb, LV_STATE_CHECKED);
    int index = (int)(intptr_t)lv_obj_get_user_data(cb);

    set_row_checked_value(index, checked);

}

void action_obd_srv_screen_loaded(lv_event_t * e) {
    lv_event_code_t code = lv_event_get_code(e);

    ESP_LOGI(TAG, "action_obd_srv_screen_loaded");

    if (code == LV_EVENT_SCREEN_LOADED) {

        lv_obj_t *list = objects.obd_srv_table;

        lv_obj_clean(list);

        int rows = get_nbr_of_all_services();

        for (int i = 0; i < rows; i++) {
            // Create a list item (a container)
            lv_obj_t *item = lv_list_add_btn(list, NULL, NULL);

            // Create a checkbox inside the item
            char txt[40];
            lv_obj_t *cb = lv_checkbox_create(item);
            get_service_description(i,txt);

            lv_checkbox_set_text(cb, txt);
            lv_obj_set_style_text_font(cb, &lv_font_montserrat_20, LV_PART_MAIN | LV_STATE_DEFAULT);

            // Set checked state
            if (is_row_checked(i))
                lv_obj_add_state(cb, LV_STATE_CHECKED);

            // Set enabled/disabled
            if (!is_row_enabled(i))
                lv_obj_add_state(cb, LV_STATE_DISABLED);

            // Optional: store row index in user data
            lv_obj_set_user_data(cb, (void *)(intptr_t)i);

            // Optional: add event handler for checkbox toggle
            lv_obj_add_event_cb(cb, checkbox_event_handler, LV_EVENT_VALUE_CHANGED, NULL);
        }
    }
}
    
/* Connect/Disconnect button */
void action_conn_button_pressed(lv_event_t * e) {
    con_button_pressed();
}