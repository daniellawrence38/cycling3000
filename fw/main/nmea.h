/* nmea.h: NMEA 0183 parser for the unit's GNSS receiver (UART0, GPIO 1 TX / GPIO 0 RX, 115200 baud,
 * decoded from the stock firmware's AppDevInit at 0x4200a5dc and FUN_421b75f0; see
 * docs/STOCK-FIRMWARE-FINDINGS.md).
 *
 * No ESP-IDF dependency; host-tested by core/tests/test_nmea.c. Every sentence must carry a valid
 * NMEA XOR checksum (*XX). GGA supplies altitude, satellite count and HDOP; RMC supplies position,
 * speed (knots to m/s), course and UTC timestamp, and emits one hal_gnss_fix_t per RMC sentence. */
#ifndef FW_NMEA_H
#define FW_NMEA_H
#include <stddef.h>
#include <stdint.h>

#include "hal_gnss.h"

#define FW_NMEA_MAX_LINE 128

typedef struct {
    char buf[FW_NMEA_MAX_LINE];
    size_t len;
    int overflow;
    /* Carried from GGA into the epoch's fix */
    float alt_m;
    uint16_t hdop_x10;
    uint8_t sats;
    float last_course_deg;
} fw_nmea_t;

void fw_nmea_init(fw_nmea_t *st);

/* Parses one NUL-terminated NMEA sentence (with or without trailing \r\n).
 * Returns 1 and fills *fix when an RMC sentence completes an epoch; 0 if the sentence was GGA or
 * another valid sentence; -1 if the checksum or format is invalid. */
int fw_nmea_line(fw_nmea_t *st, const char *line, hal_gnss_fix_t *fix);

/* Feeds one byte from the UART stream. Returns 1 and fills *fix when an RMC sentence completes. */
int fw_nmea_feed_byte(fw_nmea_t *st, char c, hal_gnss_fix_t *fix);
#endif
