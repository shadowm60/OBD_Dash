/*
 * EEZ Studio action implementations (declared in src/ui/actions.h).
 * Keep these thin: translate UI events into comsm / obd calls.
 */
#include "actions.h"
#include "screens.h"
#include "comsm.h"
#include "views.h"

extern objects_t objects;

/* Log checkboxes: state is persisted by settings_view (polled, because these
 * actions fire on PRESSED before the checkbox toggles). Logging itself: phase 4. */
void action_log_raw_enabled(lv_event_t *e)
{
}

void action_log_params_enabled(lv_event_t *e)
{
}

/* Settings > Select Params: list is owned by pid_select_view.c */
void action_obd_srv_screen_loaded(lv_event_t *e)
{
    if (lv_event_get_code(e) == LV_EVENT_SCREEN_LOADED) {
        pid_select_view_build();
    }
}

/* Connect / Disconnect / Cancel button */
void action_conn_button_pressed(lv_event_t *e)
{
    comsm_toggle();
}
