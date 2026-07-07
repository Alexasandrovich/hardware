/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file           : main.h
  * @brief          : Header for main.c file.
  *                   This file contains the common defines of the application.
  ******************************************************************************
  * @attention
  *
  * Copyright (c) 2026 STMicroelectronics.
  * All rights reserved.
  *
  * This software is licensed under terms that can be found in the LICENSE file
  * in the root directory of this software component.
  * If no LICENSE file comes with this software, it is provided AS-IS.
  *
  ******************************************************************************
  */
/* USER CODE END Header */

/* Define to prevent recursive inclusion -------------------------------------*/
#ifndef __MAIN_H
#define __MAIN_H

#ifdef __cplusplus
extern "C" {
#endif

/* Includes ------------------------------------------------------------------*/
#include "stm32f4xx_hal.h"

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */

/* USER CODE END Includes */

/* Exported types ------------------------------------------------------------*/
/* USER CODE BEGIN ET */

/* USER CODE END ET */

/* Exported constants --------------------------------------------------------*/
/* USER CODE BEGIN EC */

/* USER CODE END EC */

/* Exported macro ------------------------------------------------------------*/
/* USER CODE BEGIN EM */

/* USER CODE END EM */

void HAL_TIM_MspPostInit(TIM_HandleTypeDef *htim);

/* Exported functions prototypes ---------------------------------------------*/
void Error_Handler(void);

/* USER CODE BEGIN EFP */

/* USER CODE END EFP */

/* Private defines -----------------------------------------------------------*/

/* USER CODE BEGIN Private defines */
#define SELF_TEST              0
/* RPWM только на PB7 (TIM4_CH2), на GND не вешать. REVERSE: LPWM=ШИМ, RPWM=0 с PB7. */
#define MOTOR_DRIVE_REVERSE    1
#define MOTOR_DRIVE_FORWARD    2
#define MOTOR_DRIVE_MODE       MOTOR_DRIVE_REVERSE
#define MOTOR_PWM_PERCENT      100
#define MOTOR_SPIN_CONTINUOUS  1
#define MOTOR_GPIO_FULL_POWER_TEST 0
#define PWM_LIMIT_PERCENT      100
#define COMMAND_TIMEOUT_MS    500
#define RX_LINE_MAX            64
/* NUCLEO-F411RE onboard LD2 (green) — диагностика мотора, см. motor_led_diag_update() */
#define LED_GPIO_PORT          GPIOA
#define LED_GPIO_PIN           GPIO_PIN_5
#define LED_DIAG_CYCLE_MS      2000
#define LED_DIAG_PULSE_MS      120
#define LED_DIAG_GAP_MS        120
/* USER CODE END Private defines */

#ifdef __cplusplus
}
#endif

#endif /* __MAIN_H */
