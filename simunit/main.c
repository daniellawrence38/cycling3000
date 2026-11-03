/* simunit: the simulated unit. The core with a directory-backed file system, a scripted sensor and GNSS
 * player, and the protocol server on a localhost TCP socket (brief 2, WP3).
 *
 * TCP framing: every BLE packet (header byte plus data) travels as a u16 little-endian length followed
 * by the packet, in both directions. The negotiated ATT MTU is set with --mtu (default 247).
 *
 * Usage:
 *   simunit --root DIR [--port 7700] [--script FILE.csv] [--speed 1] [--start-at 0] [--finish]
 *           [--exit] [--mtu 247] [--utc UNIX|none] [--tz 480] [--battery 80] [--quiet]
 * --speed 0 runs the script as fast as possible. Without --script the unit idles (Ready) and serves the
 * link until killed. With --exit it stops after the script (and the finish). */
#define _POSIX_C_SOURCE 200809L
#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <poll.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <time.h>
#include <unistd.h>

#include "cc700/core.h"
#include "cc700/link.h"
#include "dirfs.h"
#include "player.h"

static int quiet;
static int client = -1;
static cc_link_t link_srv;
static uint32_t now_ms = 1000;

#define LOG(...) do { if (!quiet) { fprintf(stderr, "simunit: " __VA_ARGS__); fputc('\n', stderr); } } while (0)

static void tx(void *ctx, const uint8_t *pkt, size_t len)
{
    (void)ctx;
    if (client < 0)
        return;
    uint8_t b[2 + 260];
    b[0] = (uint8_t)len;
    b[1] = (uint8_t)(len >> 8);
    memcpy(b + 2, pkt, len);
    size_t off = 0;
    while (off < len + 2) {
        ssize_t n = send(client, b + off, len + 2 - off, MSG_NOSIGNAL);
        if (n <= 0) {
            if (n < 0 && (errno == EAGAIN || errno == EINTR))
                continue;
            close(client);
            client = -1;
            cc_on_phone(0);
            return;
        }
        off += (size_t)n;
    }
}

/* Receive buffer for length-prefixed packets */
static uint8_t rbuf[8192];
static size_t rlen;

static void serve(int lsock, int timeout_ms)
{
    struct pollfd p[2];
    int n = 0;
    if (lsock >= 0) { p[n].fd = lsock; p[n].events = POLLIN; n++; }
    if (client >= 0) { p[n].fd = client; p[n].events = POLLIN; n++; }
    if (n == 0) {
        if (timeout_ms > 0) {
            struct timespec ts = {timeout_ms / 1000, (timeout_ms % 1000) * 1000000L};
            nanosleep(&ts, NULL);
        }
        return;
    }
    if (poll(p, (nfds_t)n, timeout_ms) <= 0)
        return;
    for (int i = 0; i < n; i++) {
        if (!(p[i].revents & (POLLIN | POLLHUP | POLLERR)))
            continue;
        if (p[i].fd == lsock) {
            int c = accept(lsock, NULL, NULL);
            if (c >= 0) {
                if (client >= 0)
                    close(client); /* one phone at a time */
                client = c;
                int one = 1;
                setsockopt(c, IPPROTO_TCP, TCP_NODELAY, &one, sizeof one);
                rlen = 0;
                cc_reasm_init(&link_srv.rx);
                cc_on_phone(1);
                link_srv.ev_state = -1;
                LOG("phone connected");
            }
        } else {
            ssize_t got = recv(client, rbuf + rlen, sizeof rbuf - rlen, 0);
            if (got <= 0) {
                close(client);
                client = -1;
                cc_on_phone(0);
                LOG("phone disconnected");
                continue;
            }
            rlen += (size_t)got;
            size_t off = 0;
            while (rlen - off >= 2) {
                size_t plen = (size_t)rbuf[off] | ((size_t)rbuf[off + 1] << 8);
                if (plen > 260) { /* not our framing: drop the connection */
                    close(client);
                    client = -1;
                    rlen = off = 0;
                    break;
                }
                if (rlen - off < 2 + plen)
                    break;
                cc_link_rx(&link_srv, rbuf + off + 2, plen, now_ms);
                off += 2 + plen;
            }
            if (client >= 0) {
                memmove(rbuf, rbuf + off, rlen - off);
                rlen -= off;
            }
        }
    }
}

static void press(cc_btn_t b, uint32_t ms)
{
    cc_on_button(now_ms, b, 1);
    uint32_t end = now_ms + ms;
    while (now_ms < end) {
        now_ms += 100;
        cc_tick(now_ms);
    }
    cc_on_button(now_ms, b, 0);
    cc_tick(now_ms);
}

static uint32_t last_saved_seq;

static void after_tick(void)
{
    const cc_view_t *v = cc_view();
    cc_link_tick(&link_srv, now_ms);
    if (client >= 0) {
        cc_link_poll_events(&link_srv);
        if (v->saved_seq != last_saved_seq)
            cc_link_event_ride_saved(&link_srv, v->saved_ride_id);
    }
    last_saved_seq = v->saved_seq;
}

static volatile sig_atomic_t stop;
static void on_sig(int s) { (void)s; stop = 1; }

static uint64_t mono_ms(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000u + (uint64_t)ts.tv_nsec / 1000000u;
}

int main(int argc, char **argv)
{
    const char *root = NULL, *script = NULL, *utc_arg = NULL;
    int port = 7700, mtu = 247, finish = 0, exit_after = 0, tz = 480, battery = 80;
    double speed = 1.0;
    int start_at = 0;
    for (int i = 1; i < argc; i++) {
        const char *a = argv[i];
        const char *v = i + 1 < argc ? argv[i + 1] : NULL;
        if (!strcmp(a, "--root") && v) { root = v; i++; }
        else if (!strcmp(a, "--port") && v) { port = atoi(v); i++; }
        else if (!strcmp(a, "--script") && v) { script = v; i++; }
        else if (!strcmp(a, "--speed") && v) { speed = atof(v); i++; }
        else if (!strcmp(a, "--start-at") && v) { start_at = atoi(v); i++; }
        else if (!strcmp(a, "--mtu") && v) { mtu = atoi(v); i++; }
        else if (!strcmp(a, "--utc") && v) { utc_arg = v; i++; }
        else if (!strcmp(a, "--tz") && v) { tz = atoi(v); i++; }
        else if (!strcmp(a, "--battery") && v) { battery = atoi(v); i++; }
        else if (!strcmp(a, "--finish")) finish = 1;
        else if (!strcmp(a, "--exit")) exit_after = 1;
        else if (!strcmp(a, "--quiet")) quiet = 1;
        else { fprintf(stderr, "simunit: unknown argument %s\n", a); return 2; }
    }
    if (!root) {
        fprintf(stderr, "usage: simunit --root DIR [--port N] [--script FILE] [--speed X] [--finish] [--exit]\n");
        return 2;
    }
    signal(SIGINT, on_sig);
    signal(SIGTERM, on_sig);
    signal(SIGPIPE, SIG_IGN);

    hal_fs_t *fs = dirfs_new(root);
    int rec = cc_init(fs, now_ms);
    if (rec)
        LOG("recovered %d ride(s) at boot", rec);
    cc_on_battery(battery, 0);
    cc_on_sensor_state(1, CC_SENS_SEARCHING);
    cc_on_sensor_state(2, CC_SENS_SEARCHING);
    if (!utc_arg || strcmp(utc_arg, "none") != 0)
        cc_set_utc(now_ms, utc_arg ? (uint32_t)strtoul(utc_arg, NULL, 10) : (uint32_t)time(NULL));
    cc_set_tz((int16_t)tz);

    cc_link_host_t host;
    cc_link_host_bind(&host);
    cc_link_init(&link_srv, fs, &host, tx, NULL, (uint16_t)mtu);

    int lsock = -1;
    if (port > 0) {
        lsock = socket(AF_INET, SOCK_STREAM, 0);
        int one = 1;
        setsockopt(lsock, SOL_SOCKET, SO_REUSEADDR, &one, sizeof one);
        struct sockaddr_in sa;
        memset(&sa, 0, sizeof sa);
        sa.sin_family = AF_INET;
        sa.sin_port = htons((uint16_t)port);
        sa.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        if (bind(lsock, (struct sockaddr *)&sa, sizeof sa) != 0 || listen(lsock, 1) != 0) {
            perror("simunit: bind");
            return 1;
        }
        LOG("listening on 127.0.0.1:%d (MTU %d)", port, mtu);
    }

    fx_t fx;
    memset(&fx, 0, sizeof fx);
    if (script && fx_load(&fx, script) != 0) {
        fprintf(stderr, "simunit: cannot read %s\n", script);
        return 1;
    }
    int row = 0, started = 0, done = !script;
    uint32_t base = ((now_ms / 1000u) + 1) * 1000u;
    uint64_t wall0 = mono_ms();
    uint32_t sim0 = now_ms;

    while (!stop) {
        if (!done) {
            int t = row < fx.n ? fx.rows[row].t : -1;
            if (!started && (row >= fx.n || t >= start_at)) {
                cc_cmd_start(now_ms);
                started = 1;
                cc_tick(now_ms);
                LOG("START (ride %u)", cc_view()->ride_id);
            }
            if (row < fx.n) {
                uint32_t due = base + (uint32_t)t * 1000u + 500u;
                if (speed <= 0 || now_ms >= due) {
                    if (now_ms < due)
                        now_ms = due;
                    now_ms = fx_play_row(&fx, row, base);
                    row++;
                    after_tick();
                    serve(lsock, 0);
                    continue;
                }
            } else {
                if (finish && cc_view()->state == CC_RS_RECORDING) {
                    /* the real button path: hold right (pause), left (menu), power (focus), right */
                    press(CC_BTN_R, 1100);
                    press(CC_BTN_L, 100);
                    press(CC_BTN_PWR, 100);
                    press(CC_BTN_R, 100);
                    const cc_view_t *v = cc_view();
                    LOG("saved ride %u: %u s, %u m, avg %u W", v->summary.ride_id, v->summary.sum.timer_s,
                        v->summary.sum.distance_m, v->summary.sum.avg_power);
                    after_tick();
                    press(CC_BTN_R, 100); /* DONE */
                }
                done = 1;
                if (exit_after)
                    break;
            }
        } else if (exit_after) {
            break;
        }
        /* real time: advance the simulated clock with the wall clock (times speed) */
        serve(lsock, 20);
        if (speed > 0) {
            uint32_t target = sim0 + (uint32_t)((double)(mono_ms() - wall0) * speed);
            while (now_ms + 100 <= target) {
                now_ms += 100;
                cc_tick(now_ms);
                after_tick();
            }
        } else {
            now_ms += 100;
            cc_tick(now_ms);
            after_tick();
        }
    }
    if (client >= 0)
        close(client);
    if (lsock >= 0)
        close(lsock);
    fx_free(&fx);
    dirfs_free(fs);
    return 0;
}
