/* hal_input.h: button and touch events. Hold detection is in core (brief 6.4), not here. */
#ifndef HAL_INPUT_H
#define HAL_INPUT_H
#include <stdint.h>

typedef enum {
    HAL_BTN_L_DOWN, HAL_BTN_L_UP,
    HAL_BTN_R_DOWN, HAL_BTN_R_UP,
    HAL_PWR_DOWN, HAL_PWR_UP,
    HAL_TOUCH,
} hal_input_kind_t;

typedef struct {
    hal_input_kind_t kind;
    int16_t x, y;  /* HAL_TOUCH only */
    uint8_t down;  /* HAL_TOUCH only */
} hal_input_event_t;

/* Returns 1 and fills ev if an event is queued. */
int hal_input_poll(hal_input_event_t *ev);
#endif
