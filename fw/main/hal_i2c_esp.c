/* hal_i2c_esp.c: the I2C master bus the touch controller and the on-board sensors sit on.
 *
 * The pins come from the stock firmware's own `InitI2CBus` (see board_pins.h for the provenance):
 * port 0, SDA 21, SCL 12, internal pull-ups on. The RAM probe corroborates them, having seen both
 * lines idling high.
 *
 * What is actually on the bus is deliberately not guessed. The stock firmware registers three
 * devices through its `I2CManagerAddDev`, but that function takes the address in a register, so the
 * addresses are not readable from the code the way the display's pins were. Instead this scans the
 * bus once at boot and logs every address that answers, which settles it on the real hardware in a
 * few milliseconds. Identifying the touch controller, the barometer and the light sensor from that
 * list is the next step; writing drivers before knowing what is there would be guesswork.
 */
#include "board_pins.h"
#include "driver/i2c_master.h"
#include "esp_log.h"
#include "hal_i2c.h"

static const char *TAG = "i2c";
static i2c_master_bus_handle_t bus;

i2c_master_bus_handle_t hal_i2c_bus(void) { return bus; }

#if I2C_SCAN_AT_BOOT
/* 7-bit addresses 0x08 to 0x77; the ranges either side are reserved by the I2C specification. */
static void scan(void)
{
    int found = 0;
    char line[128];
    int n = 0;
    for (uint8_t a = 0x08; a <= 0x77; a++) {
        if (i2c_master_probe(bus, a, 50) == ESP_OK) {
            found++;
            if (n < (int)sizeof line - 8)
                n += snprintf(line + n, sizeof line - (size_t)n, "0x%02x ", a);
        }
    }
    if (found)
        ESP_LOGI(TAG, "scan: %d device(s) answered: %s", found, line);
    else
        ESP_LOGW(TAG, "scan: nothing answered on SDA %d / SCL %d", PIN_I2C_SDA, PIN_I2C_SCL);
}
#endif

int hal_i2c_init(void)
{
    if (bus)
        return 0;
    i2c_master_bus_config_t cfg = {
        .i2c_port = I2C_PORT_NUM,
        .sda_io_num = PIN_I2C_SDA,
        .scl_io_num = PIN_I2C_SCL,
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .glitch_ignore_cnt = I2C_GLITCH_IGNORE,
        .flags = {.enable_internal_pullup = 1},
    };
    esp_err_t e = i2c_new_master_bus(&cfg, &bus);
    if (e != ESP_OK) {
        ESP_LOGE(TAG, "bus failed on SDA %d / SCL %d: %s", PIN_I2C_SDA, PIN_I2C_SCL, esp_err_to_name(e));
        bus = NULL;
        return -1;
    }
    ESP_LOGI(TAG, "bus up: port %d, SDA %d, SCL %d", I2C_PORT_NUM, PIN_I2C_SDA, PIN_I2C_SCL);
#if I2C_SCAN_AT_BOOT
    scan();
#endif
    return 0;
}
