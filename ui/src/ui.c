/* ui.c: screen table and dispatch. */
#include <string.h>

#include "ui.h"

static const struct { const char *name; void (*build)(lv_obj_t *, const ui_model_t *); } SCREENS[SCR_COUNT] = {
    [SCR_MAIN] = {"Main", scr_main},
    [SCR_WORKOUT] = {"Workout", scr_workout},
    [SCR_CLIMB] = {"Climb", scr_climb},
    [SCR_NAV] = {"Nav", scr_nav},
    [SCR_WORKOUTS] = {"Workouts", scr_workouts},
    [SCR_WORKOUT_DETAIL] = {"WorkoutDetail", scr_workout_detail},
    [SCR_READY] = {"Ready", scr_ready},
    [SCR_MENU] = {"Menu", scr_menu},
    [SCR_SENSORS] = {"Sensors", scr_sensors},
    [SCR_ADD_SENSOR] = {"AddSensor", scr_add_sensor},
    [SCR_POWER_METER] = {"PowerMeter", scr_power_meter},
    [SCR_PROFILE] = {"Profile", scr_profile},
    [SCR_ROUTES] = {"Routes", scr_routes},
    [SCR_ROUTE_DETAIL] = {"RouteDetail", scr_route_detail},
    [SCR_HISTORY] = {"History", scr_history},
    [SCR_SUMMARY] = {"Summary", scr_summary},
    [SCR_PAUSED] = {"Paused", scr_paused},
    [SCR_PAUSE_MENU] = {"PauseMenu", scr_pause_menu},
    [SCR_CONFIRM] = {"Confirm", scr_confirm},
    [SCR_ALERTS] = {"Alerts", scr_alerts},
    [SCR_EMPTY] = {"Empty", scr_empty},
    [SCR_HEART_RATE] = {"HeartRate", scr_heart_rate},
    [SCR_PHONE] = {"Phone", scr_phone},
    [SCR_SETTINGS] = {"Settings", scr_settings},
    [SCR_HR_GRAPH] = {"HrGraph", scr_hr_graph},
    [SCR_PROFILE_MENU] = {"ProfileMenu", scr_profile_menu},
    [SCR_MAX_HR] = {"MaxHr", scr_max_hr},
};

const char *ui_screen_name(ui_screen_id_t id) { return id < SCR_COUNT ? SCREENS[id].name : "?"; }

int ui_screen_by_name(const char *name)
{
    for (int i = 0; i < SCR_COUNT; i++)
        if (strcmp(SCREENS[i].name, name) == 0) return i;
    return -1;
}

void ui_build(ui_screen_id_t id, lv_obj_t *root, const ui_model_t *m)
{
    lv_obj_clean(root);
    lv_obj_set_style_bg_color(root, lv_color_hex(C_BG), 0);
    lv_obj_set_style_bg_opa(root, LV_OPA_COVER, 0);
    lv_obj_set_style_text_color(root, lv_color_hex(C_TEXT), 0);
    lv_obj_set_scrollable(root, false);
    if (id < SCR_COUNT && SCREENS[id].build)
        SCREENS[id].build(root, m);
}
