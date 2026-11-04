/* hal_i2c.h: the firmware's single I2C master bus (touch and the on-board sensors).
 *
 * Firmware-local for now rather than part of hal/, because the portable core has no notion of I2C:
 * it sees sensors through hal_baro.h and friends. If a second board ever needs this it can move. */
#ifndef CC700_HAL_I2C_H
#define CC700_HAL_I2C_H
#include "driver/i2c_master.h"

/* Creates the bus and, if I2C_SCAN_AT_BOOT, logs every address that answers. 0 on success. */
int hal_i2c_init(void);

/* The bus handle, or NULL if hal_i2c_init has not succeeded. */
i2c_master_bus_handle_t hal_i2c_bus(void);

#endif
