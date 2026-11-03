/* hal_ble.h: BLE peripheral (phone link) and central (sensors). NimBLE on the unit. Throughput, MTU and
 * three concurrent connections on this hardware are Unverified (spec 13.1 item 8). */
#ifndef HAL_BLE_H
#define HAL_BLE_H
#include <stddef.h>
#include <stdint.h>

/* Peripheral */
void hal_ble_advertise(int on);
int hal_ble_notify(const uint8_t *data, size_t len); /* one TX packet */
uint16_t hal_ble_att_mtu(void);
void hal_ble_pairing_window(int open_60s);           /* opens the 60 s window; passkey shown by the UI */
uint32_t hal_ble_passkey(void);                      /* 6-digit passkey while the window is open */
void hal_ble_erase_bond(void);
int hal_ble_bonded(void);

/* Central (sensors): scan and connect are driven by the firmware's sensor manager, which calls the
 * core's cc_on_power_sample, cc_on_hr_sample and cc_on_sensor_state. */
typedef enum { HAL_SENSOR_POWER = 1, HAL_SENSOR_HR = 2 } hal_sensor_kind_t;
void hal_ble_scan(int on);
int hal_ble_connect(hal_sensor_kind_t kind, const uint8_t addr[6], uint8_t addr_type);
void hal_ble_disconnect(hal_sensor_kind_t kind);
#endif
