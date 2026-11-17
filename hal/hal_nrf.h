/* hal_nrf.h: the link to the nRF co-processor.
 *
 * This is where the on-board sensors live. The barometer, the light sensor, the battery gauge and the
 * IMU are NOT on the ESP32's I2C bus (Q36): every I2C transfer in the stock firmware belongs to the
 * touch controller. They arrive as framed messages from the nRF on UART1 (ESP port 2), the same link
 * that carries ANT+.
 *
 * It is NOT receive-only any more (it was until Q38 was answered). The firmware transmits five
 * command frames on it (three of them the GNSS modes), all kind 2, id 16, each 16 bytes (payloads in
 * docs/NRF-PROTOCOL.md, "The
 * senders, exactly" and "Additional outbound commands on id 16"); fw/main/hal_nrf_esp.c says exactly
 * when each is sent:
 *   SendPowerOnCmd    e2 02 00 00 00 01 00 00   at boot, retried while no barometer reading has
 *                                                arrived, and on a wake after power-off
 *   GNSS power        e2 02 07 00 00 <m> 00 00   m = 0 off, 1 on, 2 reset (hal_nrf_gnss_power)
 *   SendPowerOffCmd   e2 02 00 00 00 00 00 00   from hal_power_off() only
 * Nothing else is sent: no queries, no buzzer, nothing to the ANT+ side. */
#ifndef CC700_HAL_NRF_H
#define CC700_HAL_NRF_H
#include <stdint.h>

/* Starts the link task, which sends the boot frames above and then searches for the baud rate. If the
 * link is silent or the rate is not found it reports that it found nothing, and leaves the port at
 * 115200 for transmitting. */
void hal_nrf_start(void);

/* The baud rate that produced frames with valid checksums, or 0 while still searching. */
uint32_t hal_nrf_baud(void);

/* Count of frames accepted so far (checksum verified). */
uint32_t hal_nrf_frames(void);

/* The barometer's own temperature in hundredths of a degree Celsius. Returns 0, or -1 when there is no
 * fresh barometer reading. hal_baro_read_pa() (hal_baro.h) is implemented on this link too. */
int hal_nrf_baro_temp_c100(int16_t *out);

/* The battery's millivolts from id 0 / 0x52. Returns 0, or -1 when there is no fresh battery reading.
 * hal_power_battery_pct(), hal_power_ambient_lux() and hal_power_off() (hal_power.h) are implemented
 * on this link too. */
int hal_nrf_battery_mv(uint16_t *out);

/* Sends the GNSS power/reset command on id 16 (0xE2 0x02 0x07, mode: 0 off, 1 on, 2 reset). */
void hal_nrf_gnss_power(uint8_t mode);
#endif
