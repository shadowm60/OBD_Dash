#ifndef _OBD_HANDLER_H_
#define _OBD_HANDLER_H_
#include <stdio.h>
#include <stdlib.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "esp_err.h"
#include "esp_log.h"
#include "obd_handler_types.h"
#include "can.h"

#define PID_PARAM_NOT_USED      0xff
#define SIZE_PARAM_NOT_USED     0xffff
#define MAX_PID_COUNT           0xD0

#define READ_TIMEOUT            100 //ms


//steps needed to get and handle PID datas
typedef enum {
    STANDBY = 0,
    READ_SUPPORTED_PIDS = 1,
    CYCLIC_READ = 2,
}obd_handler_pid_states_t;

typedef enum {
    UNCONNECTED = 0,
    CURRENT_DATA_READ = 1,
    READ_DTCS = 2,
    CLEAR_DTC = 3,
    OBD_TEST_O2 = 4,
    VEH_INFO = 5,
}obd_handler_state_t;


void init_obd_handler( void );
int get_nbr_of_all_services(void);
void get_service_description(int idx, char *text);
bool is_row_checked(int idx);
bool is_row_enabled(int idx);
void set_row_enabled_value(int idx, bool val);
void set_row_checked_value(int idx, bool val);


#endif