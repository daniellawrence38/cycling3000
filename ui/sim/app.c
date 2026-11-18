/* The live simulator: drives the shared screen manager (ui/src/ui_app.c), runs the scripted walk that
 * is WP4's acceptance test, and opens the interactive SDL window. */
#define _POSIX_C_SOURCE 200809L
#include "app.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#include "cc700/nav.h"
#include "cc700/provisional.h"
#include "cc700/store.h"
#include "cc700/zones.h"
#include "dirfs.h"
#include "player.h"

#if CC_LV_USE_SDL
#include <SDL2/SDL.h>
#endif

static hal_fs_t *sim_fs;
static uint32_t sim_now;
static int perf_on;
static double lat[4096];
static int n_lat;
static double pending_input;

static double wall_ms(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec * 1000.0 + (double)ts.tv_nsec / 1e6;
}

/* One 100 ms step of the simulator */
static void step(void)
{
    sim_now += 100;
    cc_tick(sim_now);
    ui_app_step(sim_now);
    if (ui_app_take_input())
        pending_input = wall_ms();
}

static void redraw(void)
{
    ui_app_render();
    lv_refr_now(NULL);
    if (perf_on && pending_input > 0 && n_lat < 4096) {
        lat[n_lat++] = wall_ms() - pending_input;
        pending_input = 0;
    }
}

static int cmpd(const void *a, const void *b)
{
    double x = *(const double *)a, y = *(const double *)b;
    return x < y ? -1 : x > y ? 1 : 0;
}

static void perf_report(void)
{
    if (!perf_on || !n_lat) return;
    qsort(lat, (size_t)n_lat, sizeof lat[0], cmpd);
    printf("perf: input to redraw complete, %d inputs (simulator on this PC, not the unit)\n", n_lat);
    int ps[] = {50, 90, 95, 99, 100};
    for (int i = 0; i < 5; i++) {
        int k = (n_lat - 1) * ps[i] / 100;
        printf("  p%-3d %7.2f ms\n", ps[i], lat[k]);
    }
}

static int app_setup(const char *root)
{
    sim_fs = dirfs_new(root);
    sim_now = 1000;
    cc_init(sim_fs, sim_now);
    cc_on_battery(80, 0);
    cc_on_sensor_state(1, CC_SENS_SEARCHING);
    cc_on_sensor_state(2, CC_SENS_SEARCHING);
    ui_app_init(sim_fs, sim_now, NULL);
    return 0;
}

/* ------------------------------------------------------------------ the scripted walk (WP4) */
static const char *walk_dir;
static int walk_fail;
static int shot_no;

static void shot(const char *name)
{
    char p[512];
    redraw();
    snprintf(p, sizeof p, "%s/%02d_%s.png", walk_dir, ++shot_no, name);
    sim_write_png(p);
}

static void expect(int cond, const char *what)
{
    if (!cond) { printf("walk: FAILED: %s\n", what); walk_fail++; }
    else printf("walk: ok: %s\n", what);
}

static void run_ms(uint32_t ms) { for (uint32_t t = 0; t < ms; t += 100) { step(); } }

static void press(cc_btn_t b, uint32_t ms)
{
    cc_on_button(sim_now, b, 1);
    run_ms(ms);
    cc_on_button(sim_now, b, 0);
    step();
}

/* A tap (down and up at one point), then one step */
static void tap(int16_t x, int16_t y)
{
    ui_app_touch(1, x, y);
    ui_app_touch(0, x, y);
    ui_app_post_touch();
    step();
}

static fx_t fx;
static int fx_row;
static uint32_t fx_base;

static void ride_for(int secs)
{
    for (int s = 0; s < secs && fx_row < fx.n; s++) {
        while (sim_now % 1000 != 400) step();
        sim_now = fx_play_row(&fx, fx_row++, fx_base) - 100;
        step();
    }
}

static int copy_file(const char *from, const char *to)
{
    FILE *a = fopen(from, "rb"), *b = fopen(to, "wb");
    if (!a || !b) { if (a) fclose(a); if (b) fclose(b); return -1; }
    char buf[4096];
    size_t n;
    while ((n = fread(buf, 1, sizeof buf, a)) > 0) fwrite(buf, 1, n, b);
    fclose(a);
    fclose(b);
    return 0;
}

int app_walk(const char *dir, int perf)
{
    walk_dir = dir;
    perf_on = perf;
    mkdir(dir, 0755);
    char root[512], p[700];
    snprintf(root, sizeof root, "%s/CC700", dir);
    snprintf(p, sizeof p, "rm -rf '%s'", root);
    if (system(p) != 0) return 1;
    mkdir(root, 0755);
    snprintf(p, sizeof p, "%s/objects", root);
    mkdir(p, 0755);
    /* a workout and a route already sent by the phone */
    snprintf(p, sizeof p, "%s/objects/1812240e1809c011.cw1", root);
    static const char *w1 = "core/tests/fixtures/threshold-2x20.cw1";
    FILE *ix = fopen("core/tests/fixtures/workouts.txt", "r");
    (void)ix;
    if (ix) fclose(ix);
    /* use the starter workout's own ID for the file name */
    static uint8_t buf[CC_RT_MAX_BYTES];
    FILE *f = fopen(w1, "rb");
    if (!f) { printf("walk: run from the repository root\n"); return 1; }
    size_t n = fread(buf, 1, sizeof buf, f);
    fclose(f);
    static cc_workout_t wo;
    if (cc_cw1_decode(buf, n, &wo) != CC_CW1_OK) return 1;
    cc_object_path(p, sizeof p, wo.id, 1);
    char full[900];
    snprintf(full, sizeof full, "%s/%s", root, p);
    copy_file(w1, full);
    f = fopen("core/tests/fixtures/route.cr1", "rb");
    n = fread(buf, 1, sizeof buf, f);
    fclose(f);
    static cc_route_t r;
    if (cc_cr1_decode(buf, n, &r) != CC_CR1_OK) return 1;
    cc_object_path(p, sizeof p, r.id, 2);
    snprintf(full, sizeof full, "%s/%s", root, p);
    copy_file("core/tests/fixtures/route.cr1", full);

    app_setup(root);
    ui_model_t *m = ui_app_model();
    cc_set_utc(sim_now, 1793000000u);
    cc_set_tz(480);
    cc_on_phone(1);
    m->phone = UI_PHONE_CONNECTED;
    snprintf(m->phone_name, sizeof m->phone_name, "Simulated phone");
    run_ms(1000);
    shot("ready");
    expect(ui_app_top() == SCR_READY, "starts on Ready");

    /* Touch navigation: footer MENU, drag-scroll Menu to Settings, open Settings, drag-scroll Settings,
     * footer CHANGE and footer BACK. */
    ui_app_touch(1, 40, 300);
    ui_app_touch(0, 40, 300);
    ui_app_post_touch();
    step();
    expect(ui_app_top() == SCR_MENU, "touch footer MENU opens Menu");
    ui_app_touch(1, 120, 220);
    ui_app_touch(1, 120, 180);
    ui_app_touch(1, 120, 140);
    ui_app_touch(0, 120, 140);
    ui_app_post_touch();
    step();
    expect(m->scroll == 2 && ui_app_top() == SCR_MENU, "touch drag scrolls Menu to bottom without opening a row");
    ui_app_touch(1, 120, 260); /* row 4 when scroll=2 is index 6 (Settings) */
    ui_app_touch(0, 120, 260);
    ui_app_post_touch();
    step();
    expect(ui_app_top() == SCR_SETTINGS, "touch row opens Settings");
    ui_app_touch(1, 120, 220);
    ui_app_touch(1, 120, 180);
    ui_app_touch(0, 120, 180);
    ui_app_post_touch();
    step();
    expect(m->scroll == 1, "touch drag scrolls Settings down to Firmware");
    ui_app_touch(1, 120, 140);
    ui_app_touch(1, 120, 180);
    ui_app_touch(0, 120, 180);
    ui_app_post_touch();
    step();
    expect(m->scroll == 0, "touch drag scrolls Settings back to top");
    ui_app_touch(1, 40, 300);
    ui_app_touch(0, 40, 300);
    ui_app_post_touch();
    step();
    expect(ui_app_top() == SCR_MENU, "touch footer BACK returns to Menu");
    ui_app_touch(1, 40, 300);
    ui_app_touch(0, 40, 300);
    ui_app_post_touch();
    step();
    expect(ui_app_top() == SCR_READY, "touch footer BACK returns to Ready");

    /* A stuck touch (rain, a wet glove) on a stopped screen holds the once-a-second redraw back for
     * at most 1.5 s, then the screen redraws even though the panel still reports a finger. The
     * release is on the status bar, where a tap does nothing. */
    redraw();
    ui_app_touch(1, 120, 10);
    run_ms(1200);
    expect(!ui_app_dirty(), "a touch held 1.2 s on Ready holds the redraw back");
    run_ms(500);
    expect(ui_app_dirty(), "a touch held past 1.5 s on Ready no longer holds the redraw back");
    ui_app_touch(0, 120, 10);
    ui_app_post_touch();
    step();
    expect(ui_app_top() == SCR_READY, "releasing the stuck touch on the status bar does nothing");

    /* Load the workout and the route through the menus, with buttons only */
    press(CC_BTN_L, 100);
    expect(ui_app_top() == SCR_MENU, "left opens the Menu");
    shot("menu");
    press(CC_BTN_R, 100);
    expect(ui_app_top() == SCR_WORKOUTS, "Workouts list");
    shot("workouts");
    press(CC_BTN_R, 100);
    expect(ui_app_top() == SCR_WORKOUT_DETAIL, "workout detail");
    shot("workout_detail");
    press(CC_BTN_R, 100);
    expect(ui_app_top() == SCR_READY && cc_view()->workout_loaded, "LOAD returns to Ready with the workout loaded");
    press(CC_BTN_L, 100);
    press(CC_BTN_PWR, 100);
    expect(m->focus == 1, "power moves the focus to Routes");
    press(CC_BTN_R, 100);
    expect(ui_app_top() == SCR_ROUTES, "Routes list");
    press(CC_BTN_R, 100);
    expect(ui_app_top() == SCR_ROUTE_DETAIL, "route detail");
    shot("route_detail");
    press(CC_BTN_R, 100);
    expect(ui_app_top() == SCR_READY && cc_view()->route_loaded, "LOAD returns to Ready with the route loaded");
    shot("ready_loaded");

    /* Max heart rate and the heart-rate zones (Daniel's request of 18 November 2026, QUESTIONS Q47):
     * Menu, Profile, Max heart rate, edited with the buttons (BACK discards) and then by touch (SAVE
     * on the footer saves whichever stepper was touched last); the same footer fix on FTP. */
    press(CC_BTN_L, 100);
    for (int i = 0; i < 3; i++) press(CC_BTN_PWR, 100);
    press(CC_BTN_R, 100);
    expect(ui_app_top() == SCR_PROFILE_MENU && cc_view()->max_hr == CC_MAXHR_OFF,
           "Menu, Profile opens the Profile menu, max HR not set");
    shot("profile_menu_off");
    press(CC_BTN_PWR, 100);
    press(CC_BTN_R, 100);
    expect(ui_app_top() == SCR_MAX_HR && m->maxhr_edit == CC_MAXHR_OFF, "Max heart rate opens its editor, off");
    press(CC_BTN_R, 100);
    expect(m->maxhr_edit == CC_MAXHR_START_BPM - 5, "the first step from off starts at 180 (-5: 175)");
    for (int i = 0; i < 3; i++) press(CC_BTN_PWR, 100);
    press(CC_BTN_R, 100);
    press(CC_BTN_R, 100);
    expect(m->focus == 3 && m->maxhr_edit == 185, "power moves to +5, right steps it: 185");
    shot("max_hr_changed");
    press(CC_BTN_L, 100);
    expect(ui_app_top() == SCR_PROFILE_MENU && cc_view()->max_hr == CC_MAXHR_OFF && m->focus == 1,
           "BACK discards the change, back on the Max heart rate row");
    press(CC_BTN_R, 100);
    for (int i = 0; i < 4; i++) press(CC_BTN_PWR, 100);
    press(CC_BTN_R, 100);
    expect(ui_app_top() == SCR_PROFILE_MENU && cc_view()->max_hr == CC_MAXHR_OFF, "DONE with no change keeps off");
    tap(120, 58 + 46 + 23);
    expect(ui_app_top() == SCR_MAX_HR, "a tap on the Max heart rate row opens the editor");
    tap(8 + 3 * 58 + 25, 200);
    tap(8 + 3 * 58 + 25, 200);
    tap(8 + 1 * 58 + 25, 200);
    expect(m->maxhr_edit == 189, "taps on the steppers: off, +5 (185), +5 (190), -1 (189)");
    tap(200, 300);
    expect(ui_app_top() == SCR_PROFILE_MENU && cc_view()->max_hr == 189, "a tap on footer SAVE saves 189");
    tap(120, 58 + 46 + 23);
    for (int i = 0; i < 89; i++) tap(8 + 1 * 58 + 25, 200);
    expect(m->maxhr_edit == CC_MAXHR_MIN_BPM, "-1 stops at 100");
    tap(8 + 1 * 58 + 25, 200);
    expect(m->maxhr_edit == CC_MAXHR_OFF, "one more step down turns the zones off");
    shot("max_hr_turn_off");
    tap(20, 40);
    expect(ui_app_top() == SCR_PROFILE_MENU && cc_view()->max_hr == 189, "the header back discards: still 189");
    press(CC_BTN_R, 100);
    for (int i = 0; i < 3; i++) press(CC_BTN_PWR, 100);
    for (int i = 0; i < 4; i++) press(CC_BTN_R, 100);
    expect(m->maxhr_edit == 209, "right four times on +5: 209");
    press(CC_BTN_L, 100);
    expect(ui_app_top() == SCR_PROFILE_MENU && cc_view()->max_hr == 189, "left on the editor is BACK: still 189");
    press(CC_BTN_R, 100);
    press(CC_BTN_R, 100); /* focus starts on -5: 184 */
    for (int i = 0; i < 4; i++) press(CC_BTN_PWR, 100);
    press(CC_BTN_R, 100); /* SAVE */
    expect(ui_app_top() == SCR_PROFILE_MENU && cc_view()->max_hr == 184, "the buttons save: 189 - 5 = 184");
    shot("profile_menu_set");
    tap(120, 58 + 23);
    expect(ui_app_top() == SCR_PROFILE && m->ftp_edit == cc_view()->ftp, "a tap on the FTP row opens the FTP editor");
    {
        int f0 = cc_view()->ftp;
        tap(8 + 3 * 58 + 25, 200);
        tap(200, 300);
        expect(ui_app_top() == SCR_PROFILE_MENU && cc_view()->ftp == f0 + 5, "FTP: +5 then footer SAVE saves (not +5 again)");
        tap(120, 58 + 23);
        tap(8 + 25, 200);
        tap(200, 300);
        expect(ui_app_top() == SCR_PROFILE_MENU && cc_view()->ftp == f0, "FTP: -5 then footer SAVE restores it");
    }
    press(CC_BTN_L, 100);
    press(CC_BTN_L, 100);
    expect(ui_app_top() == SCR_READY, "back to Ready");

    /* Ride: START, then the four pages */
    if (fx_load(&fx, "core/tests/fixtures/route_ride.csv") != 0) return 1;
    fx_row = 0;
    fx_base = (sim_now / 1000 + 1) * 1000;
    press(CC_BTN_R, 100);
    expect(cc_view()->state == CC_RS_RECORDING, "START begins recording");
    ride_for(30);
    expect(cc_view()->ring_n == 4, "four pages in the ring with a workout and a route");
    expect(ui_app_visible() == SCR_MAIN, "first page is Main");
    expect(cc_view()->hr_ok && cc_view()->hr_zone >= 1 && cc_view()->hr_zone == cc_zone_of_hr(cc_view()->hr, 184),
           "the HEART RATE field has its zone at max HR 184");
    shot("page_main");
    static const ui_screen_id_t pages[3] = {SCR_WORKOUT, SCR_CLIMB, SCR_NAV};
    static const char *names[3] = {"page_workout", "page_climb", "page_nav"};
    for (int i = 0; i < 3; i++) {
        press(CC_BTN_L, 100);
        ride_for(2);
        expect(ui_app_visible() == pages[i], names[i]);
        shot(names[i]);
    }
    press(CC_BTN_L, 100);
    expect(ui_app_visible() == SCR_MAIN, "the ring wraps to Main");
    cc_on_button(sim_now, CC_BTN_L, 1);
    run_ms(1100);
    cc_on_button(sim_now, CC_BTN_L, 0);
    step();
    expect(ui_app_visible() == SCR_NAV, "hold left goes to the previous page");
    press(CC_BTN_R, 100);
    run_ms(200);
    expect(cc_view()->banner.id == CC_AL_LAP, "right short is a lap: LAP banner");
    shot("lap_banner");
    ride_for(60);

    /* While recording a stuck touch never holds the redraw back (the timer, speed and power must keep
     * moving), and a horizontal swipe does not change the page (spec 4: touch has no job while
     * riding). The redraw check can pass for another reason, such as a banner, so it is a floor. */
    redraw();
    ui_app_touch(1, 200, 150);
    ride_for(2);
    expect(ui_app_dirty(), "a touch held while recording does not freeze the ride page");
    int page_before = m->page;
    ui_app_touch(1, 120, 150);
    ui_app_touch(1, 40, 150);
    ui_app_touch(0, 40, 150);
    ui_app_post_touch();
    ride_for(1);
    expect(m->page == page_before && cc_view()->state == CC_RS_RECORDING,
           "a swipe while recording does not change the page");

    /* The heart-rate graph (Daniel's request of 18 November 2026, QUESTIONS): a tap on HEART RATE
     * on Main opens it; left (short or held) or a tap anywhere closes it back to Main without
     * changing the page; right keeps its ride meaning; a pause closes it. */
    press(CC_BTN_L, 100);
    ride_for(1);
    expect(ui_app_visible() == SCR_MAIN && m->page == 0, "back on Main for the heart-rate graph");
    tap(200, 165);
    ride_for(1);
    expect(ui_app_visible() == SCR_MAIN, "a tap on CADENCE does nothing");
    tap(60, 165);
    expect(ui_app_visible() == SCR_HR_GRAPH, "a tap on HEART RATE opens the heart-rate graph");
    ride_for(2);
    {
        const cc_hrhist_t *h = &cc_view()->hr_hist;
        expect(h->n >= 90 && cc_view()->hr_ok && cc_hrhist_at(h, 0) == (int)cc_view()->hr,
               "the graph holds the ride's seconds and ends at the HEART RATE value");
    }
    shot("hr_graph");
    press(CC_BTN_R, 100);
    run_ms(200);
    /* lap_s restarts at a lap (the LAP banner may queue behind a workout banner, so it is not the check) */
    expect(ui_app_visible() == SCR_HR_GRAPH && cc_view()->lap_s <= 1,
           "right on the graph is still a lap, and the graph stays open");
    shot("hr_graph_lap");
    press(CC_BTN_L, 100);
    expect(ui_app_visible() == SCR_MAIN && m->page == 0, "left closes the graph to Main, not the next page");
    tap(60, 165);
    ride_for(1);
    tap(120, 220);
    expect(ui_app_visible() == SCR_MAIN, "a tap anywhere on the graph closes it");
    tap(60, 165);
    cc_on_button(sim_now, CC_BTN_L, 1);
    run_ms(1100);
    cc_on_button(sim_now, CC_BTN_L, 0);
    step();
    expect(ui_app_visible() == SCR_MAIN && m->page == 0, "hold left closes the graph to Main, not the previous page");
    tap(60, 165);
    cc_on_button(sim_now, CC_BTN_R, 1);
    run_ms(600);
    expect(ui_app_visible() == SCR_HR_GRAPH && cc_view()->hold.active, "the hold bar shows over the graph");
    shot("hr_graph_hold");
    run_ms(500);
    cc_on_button(sim_now, CC_BTN_R, 0);
    step();
    expect(cc_view()->state == CC_RS_PAUSED && ui_app_visible() == SCR_PAUSED, "hold right on the graph pauses");
    run_ms(2000);
    press(CC_BTN_R, 100);
    ride_for(1);
    expect(cc_view()->state == CC_RS_RECORDING && ui_app_visible() == SCR_MAIN,
           "after the resume the graph is closed and Main shows");

    /* Manual pause, the pause menu moved with the power key only, Finish and save */
    cc_on_button(sim_now, CC_BTN_R, 1);
    run_ms(600);
    expect(cc_view()->hold.active, "hold bar while the right button is held");
    shot("hold_bar");
    run_ms(500);
    cc_on_button(sim_now, CC_BTN_R, 0);
    step();
    expect(cc_view()->state == CC_RS_PAUSED && !cc_view()->pause_auto, "hold right pauses (manual)");
    shot("paused");
    press(CC_BTN_L, 100);
    expect(cc_view()->state == CC_RS_PAUSE_MENU && cc_view()->focus == CC_PM_RESUME, "pause menu, focus on Resume");
    shot("pause_menu");
    press(CC_BTN_PWR, 100);
    expect(cc_view()->focus == CC_PM_FINISH, "power moves the focus to Finish and save");
    shot("pause_menu_finish");
    press(CC_BTN_R, 100);
    expect(cc_view()->state == CC_RS_SUMMARY, "Finish and save shows the Summary");
    shot("summary");
    press(CC_BTN_R, 100);
    expect(cc_view()->state == CC_RS_READY && ui_app_top() == SCR_READY, "DONE returns to Ready");
    expect(m->rides_total == 1, "the ride is in History");
    shot("ready_after");

    /* Second ride with no workout and no route: two-page ring */
    cc_cmd_unload_workout();
    cc_cmd_unload_route();
    run_ms(200);
    press(CC_BTN_R, 100);
    fx_row = 0;
    fx_base = (sim_now / 1000 + 1) * 1000;
    ride_for(5);
    expect(cc_view()->ring_n == 2 && cc_view()->ring[0] == CC_PAGE_MAIN && cc_view()->ring[1] == CC_PAGE_CLIMB,
            "no workout and no route: the ring is Main, Climb");
    shot("ring2_main");
    press(CC_BTN_L, 100);
    ride_for(1);
    expect(ui_app_visible() == SCR_CLIMB, "second page is Climb");
    shot("ring2_climb");
    press(CC_BTN_L, 100);
    ride_for(1);
    expect(ui_app_visible() == SCR_MAIN, "and back to Main");
    fx_free(&fx);
    perf_report();
    printf("walk: %d screenshot(s) in %s, %d failure(s)\n", shot_no, dir, walk_fail);
    return walk_fail ? 1 : 0;
}

/* ------------------------------------------------------------------ interactive (SDL) */
#if CC_LV_USE_SDL
static int key_watch(void *ud, SDL_Event *e)
{
    (void)ud;
    if (e->type != SDL_KEYDOWN && e->type != SDL_KEYUP) return 0;
    if (e->type == SDL_KEYDOWN && e->key.repeat) return 0;
    int down = e->type == SDL_KEYDOWN;
    cc_btn_t b;
    switch (e->key.keysym.sym) {
    case SDLK_p: b = CC_BTN_PWR; break;
    case SDLK_a: b = CC_BTN_L; break;
    case SDLK_l: b = CC_BTN_R; break;
    default: return 0;
    }
    if (down) pending_input = wall_ms();
    cc_on_button(sim_now, b, down);
    return 0;
}
#endif

int app_interactive(const char *root, const char *script, int scale, int perf)
{
#if CC_LV_USE_SDL
    perf_on = perf;
    lv_init();
    lv_display_t *d = lv_sdl_window_create(240, 320);
    lv_sdl_window_set_zoom(d, (float)(scale > 0 ? scale : 1));
    lv_sdl_window_set_title(d, "CC700 simulator (P power, A left, L right)");
    lv_sdl_mouse_create();
    SDL_AddEventWatch(key_watch, NULL);
    char tmp[256];
    if (!root) {
        snprintf(tmp, sizeof tmp, "/tmp/ccsim-%d", (int)getpid());
        mkdir(tmp, 0755);
        root = tmp;
    }
    app_setup(root);
    cc_set_utc(sim_now, (uint32_t)time(NULL));
    cc_set_tz(480);
    if (script && fx_load(&fx, script) == 0) fx_base = (sim_now / 1000 + 1) * 1000;
    double t0 = wall_ms();
    uint32_t sim0 = sim_now;
    for (;;) {
        uint32_t target = sim0 + (uint32_t)(wall_ms() - t0);
        while (sim_now + 100 <= target) {
            if (script && fx.n && cc_view()->state == CC_RS_RECORDING && fx_row < fx.n && sim_now % 1000 == 400) {
                sim_now = fx_play_row(&fx, fx_row++, fx_base) - 100;
            }
            step();
        }
        ui_app_render();
        lv_timer_handler();
        if (perf_on && pending_input > 0 && n_lat < 4096) { lat[n_lat++] = wall_ms() - pending_input; pending_input = 0; }
        if (cc_view()->power_off_request) break;
        SDL_Delay(30);
    }
    perf_report();
    return 0;
#else
    (void)root; (void)script; (void)scale; (void)perf;
    fprintf(stderr, "ccsim: built without SDL2; use --shot, --shots, --lint or --walk\n");
    return 2;
#endif
}
