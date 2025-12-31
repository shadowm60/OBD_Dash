#include "eez-flow.h"   // whatever defines setGlobalVariable, Value, StringValue

extern "C" void setGlobalVariable_C(uint32_t idx, const char *str) {
    eez::flow::setGlobalVariable(idx, eez::StringValue(str));
}