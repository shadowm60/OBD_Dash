#ifndef _DPOOL_H_
#define _DPOOL_H_

#include <stdint.h>
#include <stdbool.h>
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

typedef struct {
    uint8_t con_status;
} tstDPool_t;

void init_dpool(void);
void set_con_status(uint8_t status);
void set_conn_button_text(int id);
void set_read_dtc_txt(char *txt);
void set_rpm_value_txt(int rpm);
void set_afr_value_txt(float afr);

bool dpool_lock(int timeout_ms);
void dpool_unlock(void);

#endif