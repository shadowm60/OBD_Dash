/*
 * Typed setters for EEZ Flow global variables (indices from vars.h).
 *
 * Must be called from the LVGL task (e.g. an lv_timer or event callback):
 * the EEZ flow engine is not thread safe. Strings are copied.
 */
#ifndef EEZ_VARS_H
#define EEZ_VARS_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* true after any setter call; cleared by the bridge after the EEZ tick */
extern bool eez_vars_dirty;

void eez_var_set_string(uint32_t idx, const char *value);
void eez_var_set_int(uint32_t idx, int32_t value);
void eez_var_set_float(uint32_t idx, float value);
void eez_var_set_bool(uint32_t idx, bool value);

int32_t eez_var_get_int(uint32_t idx);
bool    eez_var_get_bool(uint32_t idx);
/* Copies the string value (e.g. a textarea two-way bound to it) into `buf`. */
void    eez_var_get_string(uint32_t idx, char *buf, size_t len);

#ifdef __cplusplus
}
#endif

#endif /* EEZ_VARS_H */
