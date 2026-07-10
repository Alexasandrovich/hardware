#include "rbot_platform.h"
#include "rbot_app.h"
#include "main.h"

#include <stdlib.h>
#include <string.h>

extern TIM_HandleTypeDef htim3;
extern TIM_HandleTypeDef htim4;
extern UART_HandleTypeDef huart2;

#define RBOT_PWM_MAX_DEFAULT 60

static int clamp_int(int v, int lo, int hi)
{
    if (v < lo) return lo;
    if (v > hi) return hi;
    return v;
}

static void hal_uart_write(const char *data)
{
    size_t len = strlen(data);
    if (len == 0U) {
        return;
    }
    HAL_UART_Transmit(&huart2, (uint8_t *)data, (uint16_t)len, 100U);
}

static uint32_t hal_millis(void)
{
    return HAL_GetTick();
}

static uint32_t pwm_from_percent(TIM_HandleTypeDef *htim, int percent)
{
    uint32_t arr = __HAL_TIM_GET_AUTORELOAD(htim);
    int limited = clamp_int(abs(percent), 0, 100);
    return (uint32_t)((arr * (uint32_t)limited) / 100U);
}

/* TIM4: PB6=LPWM CH1, PB7=RPWM CH2 */
static void set_left_motor(int speed)
{
    uint32_t duty = pwm_from_percent(&htim4, speed);

    if (speed > 0) {
        __HAL_TIM_SET_COMPARE(&htim4, TIM_CHANNEL_1, 0);
        __HAL_TIM_SET_COMPARE(&htim4, TIM_CHANNEL_2, duty);
    } else if (speed < 0) {
        __HAL_TIM_SET_COMPARE(&htim4, TIM_CHANNEL_1, duty);
        __HAL_TIM_SET_COMPARE(&htim4, TIM_CHANNEL_2, 0);
    } else {
        __HAL_TIM_SET_COMPARE(&htim4, TIM_CHANNEL_1, 0);
        __HAL_TIM_SET_COMPARE(&htim4, TIM_CHANNEL_2, 0);
    }
}

/* TIM3: PA6=RPWM CH1, PA7=LPWM CH2 */
static void set_right_motor(int speed)
{
    uint32_t duty = pwm_from_percent(&htim3, speed);

    if (speed > 0) {
        __HAL_TIM_SET_COMPARE(&htim3, TIM_CHANNEL_1, duty);
        __HAL_TIM_SET_COMPARE(&htim3, TIM_CHANNEL_2, 0);
    } else if (speed < 0) {
        __HAL_TIM_SET_COMPARE(&htim3, TIM_CHANNEL_1, 0);
        __HAL_TIM_SET_COMPARE(&htim3, TIM_CHANNEL_2, duty);
    } else {
        __HAL_TIM_SET_COMPARE(&htim3, TIM_CHANNEL_1, 0);
        __HAL_TIM_SET_COMPARE(&htim3, TIM_CHANNEL_2, 0);
    }
}

static void hal_motor_set(int left, int right)
{
    set_left_motor(left);
    set_right_motor(right);
}

static void motors_hw_init(void)
{
    HAL_TIM_PWM_Start(&htim4, TIM_CHANNEL_1);
    HAL_TIM_PWM_Start(&htim4, TIM_CHANNEL_2);
    HAL_TIM_PWM_Start(&htim3, TIM_CHANNEL_1);
    HAL_TIM_PWM_Start(&htim3, TIM_CHANNEL_2);
    HAL_GPIO_WritePin(GPIOB, GPIO_PIN_0, GPIO_PIN_SET);
    HAL_GPIO_WritePin(GPIOB, GPIO_PIN_1, GPIO_PIN_SET);
    HAL_GPIO_WritePin(GPIOC, GPIO_PIN_0, GPIO_PIN_SET);
    HAL_GPIO_WritePin(GPIOC, GPIO_PIN_1, GPIO_PIN_SET);
}

void rbot_platform_init(void)
{
    motors_hw_init();
    rbot_hal_t hal = {
        .uart_write = hal_uart_write,
        .motor_set = hal_motor_set,
        .millis = hal_millis,
        .pwm_max = RBOT_PWM_MAX_DEFAULT,
        .wd_ms = 500,
    };
    rbot_app_init(&hal);
}
