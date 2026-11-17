/* fw_sensors.h: the sensor manager's hooks for the shared UI (ui_app_hooks_t) and its status line.
 * All run in the core task (sensors.c). */
#ifndef FW_SENSORS_H
#define FW_SENSORS_H
#include "ui.h"

void fw_sensors_scan(int on);                                    /* Add sensor open (1) or closed (0) */
int fw_sensors_scan_list(ui_scan_item_t *out, int max);         /* rows for Add sensor */
void fw_sensors_pair(const ui_scan_item_t *it);                  /* connect; saved once subscribed */
void fw_sensors_forget(int kind);                                /* 1 power, 2 heart rate */
int fw_sensors_saved(int kind, char *name, int name_cap, char *id4, int id4_cap, int *bars);
void fw_sensors_status_log(const char *when);                    /* one line per sensor, then the radio */
#endif
