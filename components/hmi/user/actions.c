#include "actions.h"
#include "screens.h"
#include "obd_handler.h"
#include "esp_err.h"
#include "esp_log.h"

static const char *TAG = "hmi_usr";

void action_log_raw_enabled(lv_event_t * e) {

}

void action_log_params_enabled(lv_event_t * e) {

}

extern objects_t objects;

//void event_handler_cb_select_params(lv_event_t * e) {
//}

void action_obd_srv_screen_loaded(lv_event_t * e) {
    lv_event_code_t code = lv_event_get_code(e);

    ESP_LOGI(TAG, "action_obd_srv_screen_loaded");

    if (code == LV_EVENT_SCREEN_LOADED) {

        lv_obj_t *list = objects.obd_srv_table;

        int rows = get_nbr_of_all_services();

        for (int i = 0; i < rows; i++) {

            // Create a list item (a container)
            lv_obj_t *item = lv_list_add_btn(list, NULL, NULL);

            // Create a checkbox inside the item
            char txt[40];
            lv_obj_t *cb = lv_checkbox_create(item);
            get_service_description(i,txt);

            lv_checkbox_set_text(cb, txt);

            // Set checked state
            if (false/*is_row_checked(i)*/)
                lv_obj_add_state(cb, LV_STATE_CHECKED);

            // Set enabled/disabled
            if (false /*!is_row_enabled(i)*/)
                lv_obj_add_state(cb, LV_STATE_DISABLED);

            // Optional: store row index in user data
            lv_obj_set_user_data(cb, (void *)i);

            // Optional: add event handler for checkbox toggle
            //lv_obj_add_event_cb(cb, checkbox_event_handler, LV_EVENT_VALUE_CHANGED, NULL);
        }
    }
}
    
