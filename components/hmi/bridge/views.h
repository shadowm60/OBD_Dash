/* Internal to hmi/bridge: per-feature view controllers driven by ui_bridge. */
#ifndef HMI_VIEWS_H
#define HMI_VIEWS_H

/* Called once after ui_init() (LVGL lock held). */
void settings_view_init(void);
void diag_view_init(void);

/* Called from the bridge timer (LVGL task). */
void settings_view_refresh(void);
void diag_view_refresh(void);

#endif
