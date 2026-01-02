#ifndef EEZ_LVGL_UI_VARS_H
#define EEZ_LVGL_UI_VARS_H

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

// enum declarations



// Flow global variables

enum FlowGlobalVariables {
    FLOW_GLOBAL_VARIABLE_CONN_STATUS = 0,
    FLOW_GLOBAL_VARIABLE_READ_DTC_TEXT = 1,
    FLOW_GLOBAL_VARIABLE_RPM_VALUE = 2,
    FLOW_GLOBAL_VARIABLE_AFR_VALUE = 3,
    FLOW_GLOBAL_VARIABLE_COOLANT = 4,
    FLOW_GLOBAL_VARIABLE_TPS_VAL = 5,
    FLOW_GLOBAL_VARIABLE_IGN_VAL = 6,
    FLOW_GLOBAL_VARIABLE_IAT_VAL = 7,
    FLOW_GLOBAL_VARIABLE_BATT_VAL = 8
};

// Native global variables



#ifdef __cplusplus
}
#endif

#endif /*EEZ_LVGL_UI_VARS_H*/