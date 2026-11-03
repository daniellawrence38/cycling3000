/* app.h: the live simulator (screen manager on top of the core) and the scripted walk. */
#ifndef CCSIM_APP_H
#define CCSIM_APP_H
#include "ui.h"

void sim_headless_init(void);
int sim_write_png(const char *path);
lv_obj_t *sim_render_scenario(const ui_scenario_t *s);
void sim_render_now(void);

int app_walk(const char *dir, int perf);
int app_interactive(const char *root, const char *script, int scale, int perf);
#endif
