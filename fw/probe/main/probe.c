/* probe.c: read-only hardware probe, run from RAM (BRINGUP.md step 3).
 *
 * It only reads. It prints the chip, then watches the input level of every GPIO that is not reserved
 * and prints each change for 120 s while Daniel presses the buttons and touches the screen one at a
 * time, which maps buttons and the touch interrupt to pins. It never drives a pin, never changes a pull,
 * never touches the USB (19, 20), flash and PSRAM (26 to 37) pins, and has no flash driver at all.
 * Enabling a pin's input buffer does not change what drives it. */
#include <inttypes.h>
#include <stdio.h>

#include "driver/gpio.h"
#include "esp_chip_info.h"
#include "esp_rom_sys.h"
#include "esp_system.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static int reserved(int pin)
{
    return pin == 19 || pin == 20 || (pin >= 26 && pin <= 37) || (pin >= 22 && pin <= 25) || pin > 48;
}

void app_main(void)
{
    /* Give the serial monitor time to attach after load-ram (BRINGUP.md step 3). */
    for (int i = 10; i > 0; i -= 2) {
        printf("CC700 probe running from RAM; starting in %d s\n", i);
        vTaskDelay(pdMS_TO_TICKS(2000));
    }
    esp_chip_info_t c;
    esp_chip_info(&c);
    printf("\nCC700 probe (RAM only, read only)\n");
    printf("chip: model %d, revision v%d.%d, %d cores, features 0x%08" PRIx32 "\n", (int)c.model,
           c.revision / 100, c.revision % 100, c.cores, c.features);
    printf("reset reason: %d\n", (int)esp_reset_reason());

    int level[49];
    for (int p = 0; p <= 48; p++) {
        level[p] = -1;
        if (reserved(p) || !GPIO_IS_VALID_GPIO(p))
            continue;
        gpio_input_enable((gpio_num_t)p);
        level[p] = gpio_get_level((gpio_num_t)p);
    }
    printf("initial levels:");
    for (int p = 0; p <= 48; p++)
        if (level[p] >= 0)
            printf(" %d=%d", p, level[p]);
    printf("\nwatching for 120 s: press each button, touch the screen, plug and unplug the charger\n");

    for (int t = 0; t < 120000; t += 5) {
        for (int p = 0; p <= 48; p++) {
            if (level[p] < 0)
                continue;
            int v = gpio_get_level((gpio_num_t)p);
            if (v != level[p]) {
                printf("%7d ms  GPIO %2d  %d -> %d\n", t, p, level[p], v);
                level[p] = v;
            }
        }
        vTaskDelay(pdMS_TO_TICKS(5));
    }
    printf("probe finished; reset the unit (RESET, or power off and on) to return to the stock firmware\n");
    for (;;)
        vTaskDelay(pdMS_TO_TICKS(1000));
}
