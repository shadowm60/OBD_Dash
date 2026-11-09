#include "eez_vars.h"
#include "eez-flow.h"

using namespace eez;

extern "C" void eez_var_set_string(uint32_t idx, const char *value)
{
    /* StringValue() copies into EEZ-managed memory, so the caller's buffer
     * may be reused right away. */
    flow::setGlobalVariable(idx, StringValue(value ? value : ""));
}

extern "C" void eez_var_set_int(uint32_t idx, int32_t value)
{
    flow::setGlobalVariable(idx, IntegerValue(value));
}

extern "C" void eez_var_set_float(uint32_t idx, float value)
{
    flow::setGlobalVariable(idx, FloatValue(value));
}

extern "C" void eez_var_set_bool(uint32_t idx, bool value)
{
    flow::setGlobalVariable(idx, BooleanValue(value));
}
