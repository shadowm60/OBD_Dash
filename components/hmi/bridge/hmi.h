/*
 * hmi - public entry points of the UI component.
 *
 * main registers these with the board's LVGL port:
 *     lvgl_port_set_ui_hooks(hmi_init, hmi_tick);
 * Both run in the LVGL task with the LVGL lock held.
 */
#ifndef HMI_H
#define HMI_H

#ifdef __cplusplus
extern "C" {
#endif

void hmi_init(void);    /* builds the EEZ UI and starts the data bridge */
void hmi_tick(void);    /* EEZ flow tick */

#ifdef __cplusplus
}
#endif

#endif /* HMI_H */
