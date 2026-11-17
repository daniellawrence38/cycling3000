/* fw.h: the firmware's internal interfaces (main loop, GATT link, sensor manager). */
#ifndef FW_H
#define FW_H
#include <stddef.h>
#include <stdint.h>

/* One packet received on RX, queued by the NimBLE host task for the core task. */
typedef struct {
    uint16_t len;
    uint8_t data[248];
} fw_pkt_t;

void fw_link_start(void);             /* NimBLE init, GATT service, advertising */
int fw_link_take(fw_pkt_t *p);        /* 1 if a packet was waiting (core task) */
int fw_link_connected(void);
int fw_link_pairing(void);            /* the 60 s pairing window is open */
void fw_link_on_mtu(uint16_t mtu);    /* implemented by main.c */
void fw_link_on_connect(int connected);

void fw_sensors_start(void);          /* after NimBLE sync */
void fw_sensors_tick(uint32_t now_ms);/* core task, 10 Hz: calibration requests */

int hal_gnss_init(void);              /* UART0 receive-only bring-up for NMEA 0183 */
#endif
