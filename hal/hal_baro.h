/* hal_baro.h: barometer. Whether the unit has a working barometer is Unverified (spec 13.1 item 7). */
#ifndef HAL_BARO_H
#define HAL_BARO_H
#include <stdint.h>

int32_t hal_baro_read_pa(void); /* pascals, or -1 */
#endif
