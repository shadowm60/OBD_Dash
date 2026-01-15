#ifndef _COM_SM_H_
#define _COM_SM_H_

#include "esp_log.h"
#include "esp_heap_caps.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

void init_comSM(void);
void init_comSM_task(void);

void con_button_pressed(void);

#endif