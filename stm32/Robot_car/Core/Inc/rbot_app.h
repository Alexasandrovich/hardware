#ifndef RBOT_APP_H
#define RBOT_APP_H

#include <stdint.h>

typedef enum {
    RBOT_DISARMED = 0,
    RBOT_ARMED,
    RBOT_FAULT
} rbot_state_t;

typedef struct {
    void (*uart_write)(const char *data);
    void (*motor_set)(int left, int right);
    uint32_t (*millis)(void);
    int pwm_max;
    int wd_ms;
} rbot_hal_t;

void rbot_app_init(const rbot_hal_t *hal);
void rbot_app_on_byte(uint8_t ch);
void rbot_app_tick(void);

rbot_state_t rbot_app_state(void);
int rbot_app_left(void);
int rbot_app_right(void);

#endif
