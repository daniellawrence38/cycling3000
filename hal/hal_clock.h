/* hal_clock.h: monotonic time and UTC (brief 4). */
#ifndef HAL_CLOCK_H
#define HAL_CLOCK_H
#include <stdint.h>

uint32_t hal_clock_monotonic_ms(void);
uint32_t hal_clock_utc_seconds(void); /* 0 if unknown */
void hal_clock_set_utc(uint32_t unix_seconds);
#endif
