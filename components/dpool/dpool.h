#ifndef _DPOOL_H_
#define _DPOOL_H_

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

typedef struct {
    uint8_t con_status;
}tstDPool_t;


void init_dpool(void);

#endif