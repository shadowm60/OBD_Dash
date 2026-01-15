#include "eez-flow.h"   // whatever defines setGlobalVariable, Value, StringValue
#include "wrapper.h"

//extern "C" void setGlobalVariable_C(uint32_t idx, const char *str) {
//    eez::flow::setGlobalVariable(idx, eez::StringValue(str));
//}

extern "C" void setGlobalVariable_C(uint32_t idx, EezCValueType type, const void *value, uint32_t count) {
    using namespace eez;

    switch (type) {
        case EEZ_C_STRING:
            flow::setGlobalVariable(idx, StringValue((const char *)value));
            break;

        case EEZ_C_INT:
            flow::setGlobalVariable(idx, eez::IntegerValue(*(const int32_t *)value));
            break;

        case EEZ_C_FLOAT:
            flow::setGlobalVariable(idx, eez::FloatValue(*(const float *)value));
            break;

        case EEZ_C_BOOL:
            flow::setGlobalVariable(idx, eez::BooleanValue(*(const bool *)value));
            break;

        case EEZ_C_STRING_ARRAY: 
            eez::ArrayValue arr;
            const char **items = (const char **)value;

            for (uint32_t i = 0; i < count; i++) {
                arr.append(eez::Value::makeString(items[i]));
            }

            flow::setGlobalVariable(idx, arr);
            break;

        case EEZ_C_BOOL_ARRAY: 
            eez::ArrayValue arr;
            const bool *items = (const bool *)value;

            for (uint32_t i = 0; i < count; i++) {
                arr.append(eez::Value::makeBool(items[i]));
            }
            
            flow::setGlobalVariable(idx, arr);
            break;
    }
}


extern "C" bool setGlobalVariableSafe_C(uint32_t idx, EezCValueType type, const void *value, uint32_t count) {
    using namespace eez;

    const flow::VariableInfo *info = flow::getGlobalVariableInfo(idx);
    if (!info) return false;

    // Validate type compatibility
    switch (type) {
        case EEZ_C_STRING:
            if (info->type != flow::VALUE_TYPE_STRING) return false;
            break;

        case EEZ_C_INT:
            if (info->type != flow::VALUE_TYPE_INT) return false;
            break;

        case EEZ_C_FLOAT:
            if (info->type != flow::VALUE_TYPE_FLOAT) return false;
            break;

        case EEZ_C_BOOL:
            if (info->type != flow::VALUE_TYPE_BOOL) return false;
            break;

        case EEZ_C_STRING_ARRAY:
        case EEZ_C_BOOL_ARRAY:
            if (info->type != flow::VALUE_TYPE_ARRAY) return false;
            break;
    }

    // If type matches, forward to generic setter
    setGlobalVariable_C(idx, type, value, count);
    return true;
}