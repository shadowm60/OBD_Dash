#include "comSM.h"
#include "dpool.h"

static const char *TAG = "comSM:";

void init_comSM(void) {
    ESP_LOGI(TAG, "Init comSM");

}

void init_comSM_task(void) {

}

void con_button_pressed(void) {
    static uint8_t state = 0;
    if (state == 2) {
        state = 0;
    } else {
        state++;
    }
    set_conn_button_text(state);
}