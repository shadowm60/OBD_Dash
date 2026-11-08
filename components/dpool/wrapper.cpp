#include "eez-flow.h"
#include "wrapper.h"
#include "lvgl_port.h"

extern "C" void setGlobalVariable_C(uint32_t idx, EezCValueType type, const void *value) {
    using namespace eez::flow;

    eez::Value v;

    switch (type) {
    case EEZ_C_STRING:
        v.type = eez::VALUE_TYPE_STRING;
        v.strValue = static_cast<const char *>(value);
        break;

    case EEZ_C_INT:
        v.type = eez::VALUE_TYPE_INT32;
        v.int32Value = *static_cast<const int32_t *>(value);
        break;

    case EEZ_C_FLOAT:
        v.type = eez::VALUE_TYPE_FLOAT;
        v.floatValue = *static_cast<const float *>(value);
        break;

    case EEZ_C_BOOL:
        v.type = eez::VALUE_TYPE_BOOLEAN;
        v.uint8Value = *static_cast<const bool *>(value)? 1 : 0 ;
        break;
    }

    if (lvgl_port_lock(-1)) {
        setGlobalVariable(idx, v);
        lvgl_port_unlock();
    }
}

extern "C" bool setGlobalVariableSafe_C(uint32_t idx, EezCValueType type, const void *value) {
    using namespace eez::flow;
#ifdef check
    const VariableInfo *info = getGlobalVariableInfo(idx);
    if (!info) {
        return false;
    }

    // Type compatibility check
    switch (type) {
    case EEZ_C_STRING:
        if (info->type != VALUE_TYPE_STRING) return false;
        break;

    case EEZ_C_INT:
        if (info->type != VALUE_TYPE_INT) return false;
        break;

    case EEZ_C_FLOAT:
        if (info->type != VALUE_TYPE_FLOAT) return false;
        break;

    case EEZ_C_BOOL:
        if (info->type != VALUE_TYPE_BOOL) return false;
        break;
    }
#endif
    setGlobalVariable_C(idx, type, value);
    return true;
}