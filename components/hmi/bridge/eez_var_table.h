/*
 * EEZ Flow global variables and screens by name. The tables are generated at
 * build time from src/ui/vars.h and src/ui/screens.h
 * (tools/gen_eez_var_table.py), so variables and pages added in EEZ Studio are
 * found by the firmware without touching C code.
 */
#ifndef EEZ_VAR_TABLE_H
#define EEZ_VAR_TABLE_H

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    const char *name;           /* enum suffix, e.g. "OIL_PRESS_TEXT", "DASH" */
    uint32_t    index;          /* FLOW_GLOBAL_VARIABLE_* / SCREEN_ID_* value */
} eez_var_entry_t;

extern const eez_var_entry_t eez_var_table[];
extern const unsigned eez_var_table_len;
extern const eez_var_entry_t eez_screen_table[];     /* in page order */
extern const unsigned eez_screen_table_len;

/* Names as written in EEZ Studio vs. the enum names: equal ignoring case and
 * underscores ("oil_press_text" == "OIL_PRESS_TEXT" == "oilPressText"). */
bool eez_name_equal(const char *a, const char *b);

/* Index of the EEZ variable `name` as written in EEZ Studio ("oil_press_text",
 * "oilPress"...): compared ignoring case and underscores. -1 if unknown. */
int32_t eez_var_lookup(const char *name);

#ifdef __cplusplus
}
#endif

#endif /* EEZ_VAR_TABLE_H */
