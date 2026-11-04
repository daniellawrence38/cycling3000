/* hal_nrf.h: the link to the nRF co-processor, which the stock firmware calls the "SPU".
 *
 * This is where the on-board sensors live. The barometer, the light sensor, the battery gauge and the
 * IMU are NOT on the ESP32's I2C bus (Q36): every I2C transfer in the stock firmware belongs to the
 * touch controller. They arrive as framed messages from the nRF on UART1 (ESP port 2), the same link
 * that carries ANT+.
 *
 * For now this is a listener only. It never transmits, because a passive sniff cannot upset a
 * co-processor that is also holding the ANT+ radio and, on this unit, the buttons. */
#ifndef CC700_HAL_NRF_H
#define CC700_HAL_NRF_H
#include <stdint.h>

/* Starts the listener task. Harmless if the link is silent or the baud rate is wrong: it simply
 * reports that it found nothing. */
void hal_nrf_start(void);

/* The baud rate that produced frames with valid checksums, or 0 while still searching. */
uint32_t hal_nrf_baud(void);

/* Count of frames accepted so far (checksum verified). */
uint32_t hal_nrf_frames(void);
#endif
