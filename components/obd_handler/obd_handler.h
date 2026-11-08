#ifndef _OBD_HANDLER_H_
#define _OBD_HANDLER_H_
#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <stdint.h>
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

typedef struct {
    uint32_t valid_mask;
    int32_t rpm;
    int32_t vehicle_speed;
    int32_t coolant_temp;
    int32_t engine_load;
    int32_t timing_advance;
    int32_t intake_temp;
    int32_t maf;
    int32_t fuel_level;
    int32_t battery_voltage_mv;
    int32_t oil_temp;
    int32_t fuel_rate_ml_per_hour;
} obd_live_data_t;

enum obd_live_data_flags {
    OBD_LIVE_RPM = 1u << 0,
    OBD_LIVE_SPEED = 1u << 1,
    OBD_LIVE_COOLANT = 1u << 2,
    OBD_LIVE_LOAD = 1u << 3,
    OBD_LIVE_ADVANCE = 1u << 4,
    OBD_LIVE_IAT = 1u << 5,
    OBD_LIVE_MAF = 1u << 6,
    OBD_LIVE_FUEL_LEVEL = 1u << 7,
    OBD_LIVE_BATTERY = 1u << 8,
    OBD_LIVE_OIL_TEMP = 1u << 9,
    OBD_LIVE_FUEL_RATE = 1u << 10
};


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
const obd_live_data_t *obd_get_live_data(void);
bool obd_pid_is_supported(uint8_t pid);


#endif