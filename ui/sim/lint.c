/* Layout lint over the LVGL object tree of every scenario (brief 6.5):
 *   touch    interactive objects are at least 44 x 44 px
 *   focus    interactive objects are in the screen's focus order
 *   outside  no child extends outside its parent (nothing here scrolls)
 *   clipped  no text is clipped unless the label ends in an ellipsis
 *   small    no text below 10 px
 * Exceptions: ui/lint-exceptions.txt, one per line: "Board:scenario rule text-or-* # reason". */
#include "lint.h"

#include <stdio.h>
#include <string.h>

#include "app.h"
#include "ui.h"

typedef struct { char where[48], rule[16], what[64]; } exc_t;
static exc_t excs[64];
static int n_exc;
static int problems, excused;
static const char *cur;

static void load_exceptions(const char *path)
{
    FILE *f = fopen(path ? path : "ui/lint-exceptions.txt", "r");
    if (!f) return;
    char line[256];
    while (fgets(line, sizeof line, f) && n_exc < 64) {
        if (line[0] == '#' || line[0] == '\n') continue;
        char *hash = strchr(line, '#');
        if (hash) *hash = 0;
        exc_t *e = &excs[n_exc];
        if (sscanf(line, "%47s %15s %63[^\n]", e->where, e->rule, e->what) >= 2) {
            size_t l = strlen(e->what);
            while (l && e->what[l - 1] == ' ') e->what[--l] = 0;
            n_exc++;
        }
    }
    fclose(f);
}

static int excepted(const char *rule, const char *what)
{
    for (int i = 0; i < n_exc; i++) {
        const exc_t *e = &excs[i];
        int where = !strcmp(e->where, "*") || !strcmp(e->where, cur) ||
                    (strchr(e->where, ':') == NULL && !strncmp(cur, e->where, strlen(e->where)) && cur[strlen(e->where)] == ':');
        if (where && !strcmp(e->rule, rule) && (!e->what[0] || !strcmp(e->what, "*") || strstr(what, e->what)))
            return 1;
    }
    return 0;
}

static void report(const char *rule, const char *what, const lv_area_t *a)
{
    if (excepted(rule, what)) { excused++; return; }
    problems++;
    printf("%-28s %-8s %s at (%d,%d) %dx%d\n", cur, rule, what, (int)a->x1, (int)a->y1, (int)lv_area_get_width(a),
           (int)lv_area_get_height(a));
}

static void describe(lv_obj_t *o, char *out, size_t cap)
{
    if (lv_obj_check_type(o, &lv_label_class)) {
        snprintf(out, cap, "label \"%.40s\"", lv_label_get_text(o));
        return;
    }
    /* an object: name it after its first label, if any */
    for (uint32_t i = 0; i < lv_obj_get_child_count(o); i++) {
        lv_obj_t *c = lv_obj_get_child(o, (int32_t)i);
        if (lv_obj_check_type(c, &lv_label_class)) {
            snprintf(out, cap, "object \"%.40s\"", lv_label_get_text(c));
            return;
        }
    }
    snprintf(out, cap, "object");
}

static void walk(lv_obj_t *o, lv_obj_t *parent)
{
    lv_area_t a, pa;
    char what[96];
    lv_obj_get_coords(o, &a);
    describe(o, what, sizeof what);
    if (parent) {
        lv_obj_get_coords(parent, &pa);
        if (a.x1 < pa.x1 || a.y1 < pa.y1 || a.x2 > pa.x2 || a.y2 > pa.y2)
            report("outside", what, &a);
        if (lv_obj_is_clickable(o)) {
            if (lv_area_get_width(&a) < 44 || lv_area_get_height(&a) < 44)
                report("touch", what, &a);
            if (!lv_obj_get_group(o))
                report("focus", what, &a);
        }
    }
    if (lv_obj_check_type(o, &lv_label_class)) {
        const lv_font_t *f = lv_obj_get_style_text_font(o, 0);
        if (lv_font_get_line_height(f) < lv_font_get_line_height(&font_b700_10)) /* 10 px is the smallest allowed */
            report("small", what, &a);
        if (lv_label_get_long_mode(o) != LV_LABEL_LONG_MODE_DOTS) {
            lv_point_t sz;
            const char *t = lv_label_get_text(o);
            int32_t w = lv_area_get_width(&a);
            lv_text_get_size(&sz, t, f, lv_obj_get_style_text_letter_space(o, 0), lv_obj_get_style_text_line_space(o, 0),
                             w, LV_TEXT_FLAG_NONE);
            if (sz.y > lv_area_get_height(&a) + 1 || (lv_obj_get_style_width(o, 0) != LV_SIZE_CONTENT && sz.x > w + 1))
                report("clipped", what, &a);
        }
    }
    for (uint32_t i = 0; i < lv_obj_get_child_count(o); i++)
        walk(lv_obj_get_child(o, (int32_t)i), o);
}

int lint_all(const char *exceptions_file)
{
    load_exceptions(exceptions_file);
    char name[64];
    for (int i = 0; i < ui_n_scenarios; i++) {
        const ui_scenario_t *s = &ui_scenarios[i];
        snprintf(name, sizeof name, "%s:%s", s->board, s->scenario);
        cur = name;
        lv_obj_t *scr = sim_render_scenario(s);
        lv_obj_update_layout(scr);
        walk(scr, NULL);
    }
    printf("lint: %d scenarios, %d problem(s), %d excused by ui/lint-exceptions.txt\n", ui_n_scenarios, problems, excused);
    return problems ? 1 : 0;
}
