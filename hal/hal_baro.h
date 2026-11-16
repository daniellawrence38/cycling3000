/* hal_baro.h: barometer.
 * On the unit the barometer is behind the nRF co-processor, not on I2C (Q36); fw/main/hal_nrf_esp.c
 * implements this from the decoded link (docs/NRF-PROTOCOL.md). The decode is confirmed on a bench
 * capture; a height-change test and a ride are still to do. */
#ifndef HAL_BARO_H
#define HAL_BARO_H
#include <stdint.h>

/* The newest reading in pascals, or -1 when there is none or it is more than two seconds old. */
int32_t hal_baro_read_pa(void);
#endif
