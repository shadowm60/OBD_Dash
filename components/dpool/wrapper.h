#ifdef __cplusplus
extern "C" {
#endif

#include "stdint.h"

enum EezCValueType {
    EEZ_C_STRING,
    EEZ_C_INT,
    EEZ_C_FLOAT,
    EEZ_C_BOOL
};

/*
 * int x = 42;
 * setGlobalVariable_C(3, EEZ_C_INT, &x, 0);
 *
 * float afr = 14.7f;
 * setGlobalVariable_C(4, EEZ_C_FLOAT, &afr, 0);
 *
 * const char *name = "test";
 * setGlobalVariable_C(5, EEZ_C_STRING, name, 0);
 *
 * bool flag = true;
 * setGlobalVariable_C(6, EEZ_C_BOOL, &flag, 0);
 */

void setGlobalVariable_C(uint32_t idx, enum EezCValueType type, const void *value);
bool setGlobalVariableSafe_C(uint32_t idx, enum EezCValueType type, const void *value);

#ifdef __cplusplus
}
#endif