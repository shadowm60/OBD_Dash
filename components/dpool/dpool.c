#include "dpool.h"
#include "vars.h"
#include "wrapper.h"

/**
 * Get Variable Value:
 * Use flow::getGlobalVariable(FLOW_GLOBAL_VARIABLE_YOUR_VAR_NAME).getInt() 
 * (or getFloat(), getString(), etc.) to read the value. The enum FLOW_GLOBAL_VARIABLE_YOUR_VAR_NAME is defined in vars.h.
 * Set Variable Value:
 * Use flow::setGlobalVariable(FLOW_GLOBAL_VARIABLE_YOUR_VAR_NAME, IntegerValue(newValue)) 
 * (or FloatValue(), StringValue()) to write a new value.
 */

 /** check for new variables
  * enum FlowGlobalVariables {
  * FLOW_GLOBAL_VARIABLE_CONN_STATUS = 0,
  * FLOW_GLOBAL_VARIABLE_READ_DTC_TEXT = 1,
  * FLOW_GLOBAL_VARIABLE_RPM_VALUE = 2,
  * FLOW_GLOBAL_VARIABLE_AFR_VALUE = 3,
  * FLOW_GLOBAL_VARIABLE_CONN_SELECT = 4,
  * FLOW_GLOBAL_VARIABLE_RUSEFI_BASE_ADDR = 5,
  * FLOW_GLOBAL_VARIABLE_LOG_RAW_MSG = 6,
  * FLOW_GLOBAL_VARIABLE_OBD_PARAMS_LABEL = 7,
  * FLOW_GLOBAL_VARIABLE_OBD_PARAMS_STATUS = 8,
  * FLOW_GLOBAL_VARIABLE_OBD_PARAMS_ENABLED = 9,
  * FLOW_GLOBAL_VARIABLE_CONN_BUTTON_TEXT = 10
  * };
  */

static tstDPool_t DPool;

/* text for FLOW_GLOBAL_VARIABLE_CONN_STATUS */
const char *con_stat_decode[] = {
    "Not connected",
    "OBD Active",
    "CAN Decoding"
};


/* text for FLOW_GLOBAL_VARIABLE_READ_DTC_TEXT */
/* this text is generated dynamically when we read error memory */

/* text for FLOW_GLOBAL_VARIABLE_CONN_SELECT */
const char *conn_select_decode[] = {
    "0 - OBD",
    "1 - rusEFI"
};

/* text for FLOW_GLOBAL_VARIABLE_CONN_BUTTON_TEXT */
const char *conn_button_text[] = {
    "Connect",
    "Connecting...",
    "Disconnect"
};

static void update_con_status(void) {
    setGlobalVariable_C(FLOW_GLOBAL_VARIABLE_CONN_STATUS, EEZ_C_STRING, con_stat_decode[DPool.con_status]);
}

void set_read_dtc_txt(char *txt) {
    setGlobalVariable_C(FLOW_GLOBAL_VARIABLE_READ_DTC_TEXT, EEZ_C_STRING,  txt);
}

void set_rpm_value_txt(int rpm) {
    setGlobalVariable_C(FLOW_GLOBAL_VARIABLE_RPM_VALUE, EEZ_C_INT,  &rpm);
}

void set_afr_value_txt(float afr) {
    setGlobalVariable_C(FLOW_GLOBAL_VARIABLE_AFR_VALUE, EEZ_C_FLOAT, &afr);
}

void set_conn_button_text(int id) {
    setGlobalVariable_C(FLOW_GLOBAL_VARIABLE_CONN_BUTTON_TEXT, EEZ_C_STRING,  conn_button_text[id]);
}

void init_dpool(void) {
    DPool.con_status = 1;
    update_con_status();
    set_conn_button_text(0);

}