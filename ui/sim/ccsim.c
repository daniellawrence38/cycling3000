/* ccsim: the CC700 simulator (brief 6.5).
 *
 *   ccsim --shot Board:scenario --out file.png    render one scenario to PNG (headless)
 *   ccsim --shots DIR                             render every scenario to DIR/Board__scenario.png
 *   ccsim --lint [--exceptions FILE]              layout lint over every scenario (brief 6.5)
 *   ccsim --walk DIR                              scripted ride walk (WP4 acceptance), screenshots in DIR
 *   ccsim [--root DIR] [--script FILE.csv] [--scale 2]   interactive window (SDL builds only):
 *                                                 P power, A bottom left, L bottom right, mouse = touch
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>

#include "app.h"
#include "lodepng/lodepng.h"
#include "lint.h"
#include "ui.h"

static uint8_t fb[240 * 320 * 2];
static lv_display_t *disp;
static uint32_t fake_ms;

static uint32_t tick_cb(void) { return fake_ms; }

static void flush_cb(lv_display_t *d, const lv_area_t *a, uint8_t *px)
{
    (void)a;
    (void)px;
    lv_display_flush_ready(d);
}

void sim_headless_init(void)
{
    lv_init();
    lv_tick_set_cb(tick_cb);
    disp = lv_display_create(240, 320);
    lv_display_set_color_format(disp, LV_COLOR_FORMAT_RGB565);
    lv_display_set_buffers(disp, fb, NULL, sizeof fb, LV_DISPLAY_RENDER_MODE_DIRECT);
    lv_display_set_flush_cb(disp, flush_cb);
}

int sim_write_png(const char *path)
{
    static uint8_t rgb[240 * 320 * 3];
    for (int i = 0; i < 240 * 320; i++) {
        uint16_t c = (uint16_t)(fb[2 * i] | (fb[2 * i + 1] << 8));
        uint8_t r = (uint8_t)((c >> 11) & 0x1F), g = (uint8_t)((c >> 5) & 0x3F), b = (uint8_t)(c & 0x1F);
        rgb[3 * i] = (uint8_t)((r << 3) | (r >> 2));
        rgb[3 * i + 1] = (uint8_t)((g << 2) | (g >> 4));
        rgb[3 * i + 2] = (uint8_t)((b << 3) | (b >> 2));
    }
    return lodepng_encode24_file(path, rgb, 240, 320) == 0 ? 0 : -1;
}

/* Builds a scenario on a fresh screen and renders it. Returns the screen (kept until the next call). */
lv_obj_t *sim_render_scenario(const ui_scenario_t *s)
{
    static ui_model_t m;
    static lv_obj_t *old;
    static lv_group_t *grp;
    s->fill(&m);
    lv_obj_t *scr = lv_obj_create(NULL);
    if (grp) lv_group_delete(grp);
    grp = lv_group_create();
    ui_focus_group = grp;
    ui_build(s->id, scr, &m);
    lv_screen_load(scr);
    if (old) lv_obj_delete(old);
    old = scr;
    fake_ms += 1000;
    lv_refr_now(disp);
    return scr;
}

void sim_render_now(void)
{
    fake_ms += 100;
    lv_refr_now(disp);
}

static const ui_scenario_t *find(const char *spec)
{
    char b[64];
    snprintf(b, sizeof b, "%s", spec);
    char *c = strchr(b, ':');
    const char *sc = c ? c + 1 : "only";
    if (c) *c = 0;
    for (int i = 0; i < ui_n_scenarios; i++)
        if (!strcmp(ui_scenarios[i].board, b) && !strcmp(ui_scenarios[i].scenario, sc)) return &ui_scenarios[i];
    return NULL;
}

int main(int argc, char **argv)
{
    const char *shot = NULL, *out = NULL, *shots = NULL, *exc = NULL, *walk = NULL, *root = NULL, *script = NULL;
    int lint = 0, scale = 2, perf = 0;
    for (int i = 1; i < argc; i++) {
        const char *a = argv[i], *v = i + 1 < argc ? argv[i + 1] : NULL;
        if (!strcmp(a, "--shot") && v) { shot = v; i++; }
        else if (!strcmp(a, "--out") && v) { out = v; i++; }
        else if (!strcmp(a, "--shots") && v) { shots = v; i++; }
        else if (!strcmp(a, "--lint")) lint = 1;
        else if (!strcmp(a, "--exceptions") && v) { exc = v; i++; }
        else if (!strcmp(a, "--walk") && v) { walk = v; i++; }
        else if (!strcmp(a, "--root") && v) { root = v; i++; }
        else if (!strcmp(a, "--script") && v) { script = v; i++; }
        else if (!strcmp(a, "--scale") && v) { scale = atoi(v); i++; }
        else if (!strcmp(a, "--perf")) perf = 1;
        else { fprintf(stderr, "ccsim: unknown argument %s\n", a); return 2; }
    }
    if (shot || shots || lint || walk) {
        sim_headless_init();
        if (shot) {
            const ui_scenario_t *s = find(shot);
            if (!s) { fprintf(stderr, "ccsim: no scenario %s\n", shot); return 2; }
            sim_render_scenario(s);
            return sim_write_png(out ? out : "shot.png") ? 1 : 0;
        }
        if (shots) {
            mkdir(shots, 0755);
            char p[512];
            snprintf(p, sizeof p, "%s/index.json", shots);
            FILE *ix = fopen(p, "w");
            fprintf(ix, "[\n");
            for (int i = 0; i < ui_n_scenarios; i++) {
                const ui_scenario_t *s = &ui_scenarios[i];
                sim_render_scenario(s);
                snprintf(p, sizeof p, "%s/%s__%s.png", shots, s->board, s->scenario);
                if (sim_write_png(p)) { fprintf(stderr, "ccsim: cannot write %s\n", p); return 1; }
                fprintf(ix, " {\"board\": \"%s\", \"scenario\": \"%s\", \"png\": \"%s__%s.png\"}%s\n", s->board, s->scenario,
                        s->board, s->scenario, i + 1 < ui_n_scenarios ? "," : "");
            }
            fprintf(ix, "]\n");
            fclose(ix);
            printf("%d scenarios rendered to %s\n", ui_n_scenarios, shots);
            return 0;
        }
        if (lint)
            return lint_all(exc);
        return app_walk(walk, perf);
    }
    return app_interactive(root, script, scale, perf);
}
