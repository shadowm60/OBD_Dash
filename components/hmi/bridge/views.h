/* Internal to hmi/bridge: per-feature view controllers driven by ui_bridge. */
#ifndef HMI_VIEWS_H
#define HMI_VIEWS_H

/* afr_value is published as AFR x this, see live_view.c */
#define AFR_GAUGE_SCALE 100

/* Called once after ui_init() (LVGL lock held). */
void settings_view_init(void);
void live_view_init(void);
void diag_view_init(void);

/* Called from the bridge timer (LVGL task). */
void settings_view_refresh(void);
void diag_view_refresh(void);
void pid_select_view_refresh(void);
void live_values_refresh(void);

/* Modal message box with an OK button (LVGL task only). */
void view_show_message(const char *title, const char *text);

/* Settings > Select Params list, (re)built on screen load. */
void pid_select_view_build(void);

#endif
