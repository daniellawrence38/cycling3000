/* hal_nrf.h: the link to the nRF co-processor.
 *
 * This is where the on-board sensors live. The barometer, the light sensor, the battery gauge and the
 * IMU are NOT on the ESP32's I2C bus (Q36): every I2C transfer in the stock firmware belongs to the
 * touch controller. They arrive as framed messages from the nRF on UART1 (ESP port 2), the same link
 * that carries ANT+.
 *
 * It is NOT receive-only any more. What it transmits is exactly the table Daniel approved in
 * docs/QUESTIONS.md Q38 (amendment of 17 November 2026): five frames, all kind 2, 16 bytes (payloads
 * in docs/NRF-PROTOCOL.md, "The senders, exactly" and "Additional outbound commands on id 16"), each
 * pinned whole in core/tests/test_nrf_proto.c. fw/main/hal_nrf_esp.c's header has the detail.
 *   SendQuerySlaveModeCmd id 1,  24 01 ff ff 00 ff ff ff  boot handshake: every 200 ms from the first
 *                                                        frame heard until answered, at most 10 s
 *   SendPowerOnCmd        id 16, e2 02 00 00 00 01 00 00  boot handshake, after an "in application"
 *                                                        reply: every 100 ms until the nRF's power
 *                                                        event (or sensors after 1 s), at most 5 s
 *   GNSS power-on         id 16, e2 02 07 00 00 01 00 00  once per boot, when the handshake is RUNNING
 *                                                        and the sensor side is known to be on
 *   GNSS reset            id 16, e2 02 07 00 00 02 00 00  hal_nrf_gnss_reset(): the silence watchdog,
 *                                                        5.1 s without a byte, at most 3 per boot
 *   SendPowerOffCmd       id 16, e2 02 00 00 00 00 00 00  hal_power_off(), after the files are closed:
 *                                                        every 1.5 s until power drops, at most 30
 * Nothing else is sent: no GNSS power-off, no SendCheckPowerOnReasonCmd, no buzzer, nothing to the ANT+
 * side, no co-processor upgrade (a "bootloader" reply stops all transmitting), and nothing to the GNSS
 * receiver on UART0. */
#ifndef CC700_HAL_NRF_H
#define CC700_HAL_NRF_H
#include <stdint.h>

/* Starts the link task, which searches for the baud rate and runs the boot handshake above from the
 * first frame it hears. If the link is silent or the rate is not found it reports that it found
 * nothing, sends nothing, and leaves the port at 115200. */
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

/* 1 while the nRF has started this boot only because the charger is connected (power event 4) and has
 * not yet reported a manual power-on (5 or 6) or sent sensor frames: the unit is "off and charging".
 * Seen on the unit (outputs/handshake-2.txt): on USB, a power-off or the nRF's own long-press cut
 * restarts the ESP in this state, with no sensors. The stock firmware shows its charging screen here;
 * this firmware keeps the screen dark and ignores the buttons. Pressing power makes the nRF send
 * event 5, which ends it. */
int hal_nrf_charge_only(void);

/* 1 and the send time in *at_ms once the boot handshake has sent the GNSS power-on, else 0. The GNSS
 * silence watchdog arms from that moment: before it the receiver's rail may be off. */
int hal_nrf_gnss_on_sent(uint32_t *at_ms);

/* Sends the GNSS hardware reset (e2 02 07 00 00 02). Returns 0, or -1 if it was not sent (power-on not
 * sent yet, a power-off in progress, or the port not at 115200). For the silence watchdog only. */
int hal_nrf_gnss_reset(void);
#endif
