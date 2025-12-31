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

static tstDPool_t DPool;

const char *con_stat_decode[] = {
    "Not connected",
    "OBD Active",
    "CAN Decoding"
};


static void update_con_status(void) {
    setGlobalVariable_C(FLOW_GLOBAL_VARIABLE_CONN_STATUS, con_stat_decode[DPool.con_status]);
}

void init_dpool(void) {
    DPool.con_status = 1;
    update_con_status();

}