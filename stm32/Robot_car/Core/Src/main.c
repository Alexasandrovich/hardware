/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file           : main.c
  * @brief          : Main program body
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
/* Includes ------------------------------------------------------------------*/
#include "main.h"

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
/* USER CODE END Includes */

/* Private typedef -----------------------------------------------------------*/
/* USER CODE BEGIN PTD */

/* USER CODE END PTD */

/* Private define ------------------------------------------------------------*/
/* USER CODE BEGIN PD */

/* USER CODE END PD */

/* Private macro -------------------------------------------------------------*/
/* USER CODE BEGIN PM */

/* USER CODE END PM */

/* Private variables ---------------------------------------------------------*/
TIM_HandleTypeDef htim3;
TIM_HandleTypeDef htim4;

UART_HandleTypeDef huart1;
UART_HandleTypeDef huart2;

/* USER CODE BEGIN PV */
uint16_t  counter_time = 0;
static uint32_t last_command_ms = 0;
static char rx_line[RX_LINE_MAX];
static uint8_t rx_index = 0;

uint8_t rxData[128];
uint8_t TxData[128];
uint8_t counterr = 9;
uint8_t buff[16] = {0,};
/* USER CODE END PV */

/* Private function prototypes -----------------------------------------------*/
void SystemClock_Config(void);
static void MX_GPIO_Init(void);
static void MX_USART2_UART_Init(void);
static void MX_TIM4_Init(void);
static void MX_TIM3_Init(void);
static void MX_USART1_UART_Init(void);
/* USER CODE BEGIN PFP */

/* USER CODE END PFP */

/* Private user code ---------------------------------------------------------*/
/* USER CODE BEGIN 0 */
static int clamp_int(int value, int min, int max)
{
    if (value < min) return min;
    if (value > max) return max;
    return value;
}

static void uart_print(const char *text)
{
    HAL_UART_Transmit(&huart2, (uint8_t *)text, strlen(text), HAL_MAX_DELAY);
}

static uint32_t pwm_from_percent(TIM_HandleTypeDef *htim, int percent)
{
    uint32_t arr = __HAL_TIM_GET_AUTORELOAD(htim);
    int limited = clamp_int(abs(percent), 0, PWM_LIMIT_PERCENT);
    return (uint32_t)((arr * limited) / 100);
}

static int g_commanded_speed = 0;

typedef enum {
    MOTOR_LED_BRAKE = 1,       /* 1 вспышка за цикл */
    MOTOR_LED_REVERSE = 2,     /* 2 вспышки — ШИМ на LPWM (назад) */
    MOTOR_LED_FORWARD = 3,     /* 3 вспышки — ШИМ на RPWM (вперёд) */
    MOTOR_LED_BOTH_PWM = 4,    /* 4 вспышки — оба ШИМ, торможение/ошибка */
    MOTOR_LED_EN_FAULT = 0,    /* горит постоянно — L_EN или R_EN не HIGH */
    MOTOR_LED_PWM_FAULT = 5,   /* 5 вспышек — команда есть, регистры ШИМ = 0 */
} MotorLedPattern;

/*
 * BTS7960 в режиме «2 провода» (robot-kit.ru):
 * L_EN и R_EN постоянно HIGH, скорость — ШИМ на L_PWM или R_PWM.
 * Вперёд:  L_PWM=0, R_PWM=ШИМ.  Назад: L_PWM=ШИМ, R_PWM=0.
 * Драйвер 1: PB6=LPWM (TIM4_CH1), PB7=RPWM (TIM4_CH2), PB0=L_EN, PB1=R_EN.
 * Драйвер 2: PA7=LPWM (TIM3_CH2), PA6=RPWM (TIM3_CH1), PC1=L_EN, PC0=R_EN.
 */
static void set_motor_tim4(int speed);
static void set_motor_tim3(int speed);

static void motor_apply_drive_mode(void)
{
#if MOTOR_GPIO_FULL_POWER_TEST
#if MOTOR_DRIVE_MODE == MOTOR_DRIVE_FORWARD
    HAL_GPIO_WritePin(GPIOB, GPIO_PIN_6, GPIO_PIN_RESET);
    HAL_GPIO_WritePin(GPIOB, GPIO_PIN_7, GPIO_PIN_SET);
    g_commanded_speed = MOTOR_PWM_PERCENT;
#else
    HAL_GPIO_WritePin(GPIOB, GPIO_PIN_6, GPIO_PIN_SET);
    HAL_GPIO_WritePin(GPIOB, GPIO_PIN_7, GPIO_PIN_RESET);
    g_commanded_speed = -MOTOR_PWM_PERCENT;
#endif
#else
#if MOTOR_DRIVE_MODE == MOTOR_DRIVE_FORWARD
    set_motor_tim4(MOTOR_PWM_PERCENT);
    set_motor_tim3(MOTOR_PWM_PERCENT);
#else
    set_motor_tim4(-MOTOR_PWM_PERCENT);
    set_motor_tim3(-MOTOR_PWM_PERCENT);
#endif
#endif
}

static void motor_tim4_start(void)
{
    HAL_TIM_PWM_Start(&htim4, TIM_CHANNEL_1);
    HAL_TIM_PWM_Start(&htim4, TIM_CHANNEL_2);
}

static void motor_tim3_start(void)
{
    HAL_TIM_PWM_Start(&htim3, TIM_CHANNEL_1);
    HAL_TIM_PWM_Start(&htim3, TIM_CHANNEL_2);
}

static void motor_gpio_full_power_start(void)
{
    GPIO_InitTypeDef gpio = {0};

    __HAL_RCC_GPIOB_CLK_ENABLE();

    gpio.Pin = GPIO_PIN_6 | GPIO_PIN_7;
    gpio.Mode = GPIO_MODE_OUTPUT_PP;
    gpio.Pull = GPIO_NOPULL;
    gpio.Speed = GPIO_SPEED_FREQ_LOW;
    HAL_GPIO_Init(GPIOB, &gpio);

    motor_apply_drive_mode();
}

static void set_motor_tim4(int speed)
{
    uint32_t duty = pwm_from_percent(&htim4, speed);

    g_commanded_speed = speed;

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

static void set_motor_tim3(int speed)
{
    uint32_t duty = pwm_from_percent(&htim3, speed);
    if (speed > 0) {
        /* вперёд: LPWM=0, RPWM=ШИМ */
        __HAL_TIM_SET_COMPARE(&htim3, TIM_CHANNEL_1, duty);  /* RPWM PA6 */
        __HAL_TIM_SET_COMPARE(&htim3, TIM_CHANNEL_2, 0);       /* LPWM PA7 */
    } else if (speed < 0) {
        /* назад: LPWM=ШИМ, RPWM=0 */
        __HAL_TIM_SET_COMPARE(&htim3, TIM_CHANNEL_1, 0);
        __HAL_TIM_SET_COMPARE(&htim3, TIM_CHANNEL_2, duty);
    } else {
        __HAL_TIM_SET_COMPARE(&htim3, TIM_CHANNEL_1, 0);
        __HAL_TIM_SET_COMPARE(&htim3, TIM_CHANNEL_2, 0);
    }
}

static void set_left_motor(int speed)
{
    set_motor_tim4(speed);
}

static void set_right_motor(int speed)
{
    set_motor_tim3(speed);
}

static void stop_all_motors(void)
{
    set_left_motor(0);
    set_right_motor(0);
}

static bool parse_command(const char *line, int *left, int *right)
{
    int l = 0;
    int r = 0;

    if (sscanf(line, "L %d R %d", &l, &r) == 2 ||
        sscanf(line, "l %d r %d", &l, &r) == 2) {
        *left = clamp_int(l, -100, 100);
        *right = clamp_int(r, -100, 100);
        return true;
    }

    return false;
}

static void process_serial(void)
{
    uint8_t ch;

    if (HAL_UART_Receive(&huart2, &ch, 1, 10) != HAL_OK) {
        return;
    }

    if (ch == '\r' || ch == '\n') {
        if (rx_index == 0) {
            return;
        }

        rx_line[rx_index] = '\0';

        int left = 0;
        int right = 0;

        if (parse_command(rx_line, &left, &right)) {
            set_left_motor(left);
            set_right_motor(right);
            last_command_ms = HAL_GetTick();
            uart_print("OK\r\n");
        } else {
            uart_print("ERR: use 'L 30 R -30'\r\n");
        }

        rx_index = 0;
        memset(rx_line, 0, sizeof(rx_line));
        return;
    }

    if (rx_index < RX_LINE_MAX - 1) {
        rx_line[rx_index++] = (char)ch;
    } else {
        rx_index = 0;
        memset(rx_line, 0, sizeof(rx_line));
        uart_print("ERR: line too long\r\n");
    }
}

static void led_write(GPIO_PinState state)
{
    HAL_GPIO_WritePin(LED_GPIO_PORT, LED_GPIO_PIN, state);
}

static void led_boot_sequence(void)
{
    for (uint8_t i = 0; i < 3; i++) {
        led_write(GPIO_PIN_SET);
        HAL_Delay(100);
        led_write(GPIO_PIN_RESET);
        HAL_Delay(100);
    }
    HAL_Delay(400);
}

static MotorLedPattern motor_led_read_pattern(void)
{
#if MOTOR_GPIO_FULL_POWER_TEST
    uint32_t lpwm = (HAL_GPIO_ReadPin(GPIOB, GPIO_PIN_6) == GPIO_PIN_SET) ? 1U : 0U;
    uint32_t rpwm = (HAL_GPIO_ReadPin(GPIOB, GPIO_PIN_7) == GPIO_PIN_SET) ? 1U : 0U;
#else
    uint32_t lpwm = __HAL_TIM_GET_COMPARE(&htim4, TIM_CHANNEL_1);
    uint32_t rpwm = __HAL_TIM_GET_COMPARE(&htim4, TIM_CHANNEL_2);
#endif
    GPIO_PinState len = HAL_GPIO_ReadPin(GPIOB, GPIO_PIN_0);
    GPIO_PinState ren = HAL_GPIO_ReadPin(GPIOB, GPIO_PIN_1);

    if (len != GPIO_PIN_SET || ren != GPIO_PIN_SET) {
        return MOTOR_LED_EN_FAULT;
    }
    if (g_commanded_speed != 0 && lpwm == 0 && rpwm == 0) {
        return MOTOR_LED_PWM_FAULT;
    }
    if (lpwm > 0 && rpwm > 0) {
        return MOTOR_LED_BOTH_PWM;
    }
    if (rpwm > 0) {
        return MOTOR_LED_FORWARD;
    }
    if (lpwm > 0) {
        return MOTOR_LED_REVERSE;
    }
    return MOTOR_LED_BRAKE;
}

/*
 * LD2 показывает состояние управления (цикл 2 с):
 *   1 вспышка — торможение (оба PWM = 0)
 *   2 вспышки — назад (LPWM = ШИМ)
 *   3 вспышки — вперёд (RPWM = ШИМ)
 *   4 вспышки — оба ШИМ активны (конфликт)
 *   5 вспышек — команда на мотор есть, но регистры ШИМ пустые
 *   горит без погасаний — L_EN или R_EN не HIGH
 * При включении: 3 быстрых blink = прошивка стартовала.
 */
static void motor_led_diag_update(void)
{
    static uint32_t cycle_start_ms = 0;
    static MotorLedPattern last_pattern = MOTOR_LED_BRAKE;

    uint32_t now = HAL_GetTick();
    MotorLedPattern pattern = motor_led_read_pattern();

    if (pattern == MOTOR_LED_EN_FAULT) {
        led_write(GPIO_PIN_SET);
        return;
    }

    if (cycle_start_ms == 0 || (now - cycle_start_ms) >= LED_DIAG_CYCLE_MS) {
        cycle_start_ms = now;
        last_pattern = pattern;
    }

    uint32_t phase_ms = now - cycle_start_ms;
    uint8_t target_pulses = (uint8_t)last_pattern;
    uint32_t pulse_slot = LED_DIAG_PULSE_MS + LED_DIAG_GAP_MS;
    uint32_t active_window = target_pulses * pulse_slot;

    if (phase_ms >= active_window) {
        led_write(GPIO_PIN_RESET);
        return;
    }

    uint8_t current_pulse = (uint8_t)(phase_ms / pulse_slot);
    uint32_t within_pulse = phase_ms % pulse_slot;

    if (current_pulse >= target_pulses) {
        led_write(GPIO_PIN_RESET);
        return;
    }

    if (within_pulse < LED_DIAG_PULSE_MS) {
        led_write(GPIO_PIN_SET);
    } else {
        led_write(GPIO_PIN_RESET);
    }
}

static void led_init(void)
{
    GPIO_InitTypeDef gpio = {0};
    gpio.Pin = LED_GPIO_PIN;
    gpio.Mode = GPIO_MODE_OUTPUT_PP;
    gpio.Pull = GPIO_NOPULL;
    gpio.Speed = GPIO_SPEED_FREQ_LOW;
    HAL_GPIO_Init(LED_GPIO_PORT, &gpio);
    HAL_GPIO_WritePin(LED_GPIO_PORT, LED_GPIO_PIN, GPIO_PIN_RESET);
}

static void run_self_test(void)
{
    set_left_motor(30);
    set_right_motor(30);
    HAL_Delay(2000);

    stop_all_motors();
    HAL_Delay(1000);

    set_left_motor(-30);
    set_right_motor(-30);
    HAL_Delay(2000);

    stop_all_motors();
    HAL_Delay(1000);
}
/* USER CODE END 0 */

/**
  * @brief  The application entry point.
  * @retval int
  */
int main(void)
{

  /* USER CODE BEGIN 1 */
	TxData[0]=8;
  /* USER CODE END 1 */

  /* MCU Configuration--------------------------------------------------------*/

  /* Reset of all peripherals, Initializes the Flash interface and the Systick. */
  HAL_Init();

  /* USER CODE BEGIN Init */

  /* USER CODE END Init */

  /* Configure the system clock */
  SystemClock_Config();

  /* USER CODE BEGIN SysInit */

  /* USER CODE END SysInit */

  /* Initialize all configured peripherals */
  MX_GPIO_Init();
  MX_USART2_UART_Init();
  MX_TIM4_Init();
  MX_TIM3_Init();
  MX_USART1_UART_Init();
  /* USER CODE BEGIN 2 */
  led_init();
  led_boot_sequence();
  /* 2-wire: оба EN постоянно включены */
  HAL_GPIO_WritePin(GPIOB, GPIO_PIN_0, GPIO_PIN_SET); /* L_EN драйвер 1 */
  HAL_GPIO_WritePin(GPIOB, GPIO_PIN_1, GPIO_PIN_SET); /* R_EN драйвер 1 */
  HAL_GPIO_WritePin(GPIOC, GPIO_PIN_0, GPIO_PIN_SET); /* R_EN драйвер 2 */
  HAL_GPIO_WritePin(GPIOC, GPIO_PIN_1, GPIO_PIN_SET); /* L_EN драйвер 2 */
#if MOTOR_GPIO_FULL_POWER_TEST
  motor_gpio_full_power_start();
#else
  motor_tim4_start();
  motor_tim3_start();
#endif

#if MOTOR_SPIN_CONTINUOUS
  motor_apply_drive_mode();
#else
  set_motor_tim4(0);
#if SELF_TEST
  run_self_test();
#endif
#endif
  last_command_ms = HAL_GetTick();
  /*HAL_StatusTypeDef HAL_USART_Transmit_IT (
USART_HandleTypeDef *  husart, uint8_t * pTxData, uint16_t
Size)  */

  HAL_UART_Receive_IT(&huart1, (uint8_t*)buff, 15);
  /* USER CODE END 2 */

  /* Infinite loop */
  /* USER CODE BEGIN WHILE */
  while (1)
  {
	  process_serial();

#if !MOTOR_SPIN_CONTINUOUS
	  if (HAL_GetTick() - last_command_ms > COMMAND_TIMEOUT_MS) {
		  stop_all_motors();
	  }
#else
	  motor_apply_drive_mode();
#endif

	  motor_led_diag_update();
	  HAL_Delay(10);
	  /*
	   * LPWM HIGH L_EN и R_EN HIGH
	    */

	  /*реализация программного ШИМа
	  HAL_GPIO_WritePin(GPIOA, GPIO_PIN_0, GPIO_PIN_RESET);//LPWM
	  HAL_GPIO_WritePin(GPIOA, GPIO_PIN_1, GPIO_PIN_RESET);
	  HAL_Delay(10);
	  //HAL_GPIO_WritePin(GPIOA, GPIO_PIN_0, GPIO_PIN_SET);
	  HAL_GPIO_WritePin(GPIOA, GPIO_PIN_1, GPIO_PIN_SET);
	  HAL_Delay(10);*/
	  counter_time++;
	  //L_EN и R_EN высокий уровень для вращения моторов
	  //L_PWM и R_PWM - выводы ШИМ
	  /*
	   * //  Движение вперёд на 50% скорости:
	  digitalWrite(L_PWM, LOW );
      digitalWrite(R_PWM, HIGH);
      analogWrite (EN,    127 );
      delay(3000);
	  //  Движение вперёд на 100% скорости:
      digitalWrite(L_PWM, LOW );
      digitalWrite(R_PWM, HIGH);
      analogWrite (EN,    255 );
      delay(3000);
	  //  Свободное вращение:
      digitalWrite(EN,    LOW );
      delay(3000);
	  //  Движение назад на 50% скорости:
      digitalWrite(L_PWM, HIGH);
      digitalWrite(R_PWM, LOW );
      analogWrite (EN,    127 );
      delay(3000);
	  //  Движение назад на 100% скорости:
      digitalWrite(L_PWM, HIGH);
      digitalWrite(R_PWM, LOW );
      digitalWrite(EN,    HIGH);
      delay(3000);                /
	  //  Торможение с силой 50%:
      digitalWrite(L_PWM, HIGH);
      digitalWrite(R_PWM, HIGH);
      analogWrite (EN,    127 );
      delay(3000);
		   */
    /* USER CODE END WHILE */

    /* USER CODE BEGIN 3 */
  }
  /* USER CODE END 3 */
}

/**
  * @brief System Clock Configuration
  * @retval None
  */
void SystemClock_Config(void)
{
  RCC_OscInitTypeDef RCC_OscInitStruct = {0};
  RCC_ClkInitTypeDef RCC_ClkInitStruct = {0};

  /** Configure the main internal regulator output voltage
  */
  __HAL_RCC_PWR_CLK_ENABLE();
  __HAL_PWR_VOLTAGESCALING_CONFIG(PWR_REGULATOR_VOLTAGE_SCALE1);

  /** Initializes the RCC Oscillators according to the specified parameters
  * in the RCC_OscInitTypeDef structure.
  */
  RCC_OscInitStruct.OscillatorType = RCC_OSCILLATORTYPE_HSI;
  RCC_OscInitStruct.HSIState = RCC_HSI_ON;
  RCC_OscInitStruct.HSICalibrationValue = RCC_HSICALIBRATION_DEFAULT;
  RCC_OscInitStruct.PLL.PLLState = RCC_PLL_NONE;
  if (HAL_RCC_OscConfig(&RCC_OscInitStruct) != HAL_OK)
  {
    Error_Handler();
  }

  /** Initializes the CPU, AHB and APB buses clocks
  */
  RCC_ClkInitStruct.ClockType = RCC_CLOCKTYPE_HCLK|RCC_CLOCKTYPE_SYSCLK
                              |RCC_CLOCKTYPE_PCLK1|RCC_CLOCKTYPE_PCLK2;
  RCC_ClkInitStruct.SYSCLKSource = RCC_SYSCLKSOURCE_HSI;
  RCC_ClkInitStruct.AHBCLKDivider = RCC_SYSCLK_DIV1;
  RCC_ClkInitStruct.APB1CLKDivider = RCC_HCLK_DIV2;
  RCC_ClkInitStruct.APB2CLKDivider = RCC_HCLK_DIV1;

  if (HAL_RCC_ClockConfig(&RCC_ClkInitStruct, FLASH_LATENCY_0) != HAL_OK)
  {
    Error_Handler();
  }
}

/**
  * @brief TIM3 Initialization Function
  * @param None
  * @retval None
  */
static void MX_TIM3_Init(void)
{

  /* USER CODE BEGIN TIM3_Init 0 */

  /* USER CODE END TIM3_Init 0 */

  TIM_ClockConfigTypeDef sClockSourceConfig = {0};
  TIM_MasterConfigTypeDef sMasterConfig = {0};
  TIM_OC_InitTypeDef sConfigOC = {0};

  /* USER CODE BEGIN TIM3_Init 1 */

  /* USER CODE END TIM3_Init 1 */
  htim3.Instance = TIM3;
  htim3.Init.Prescaler = 99;
  htim3.Init.CounterMode = TIM_COUNTERMODE_UP;
  htim3.Init.Period = 1599;
  htim3.Init.ClockDivision = TIM_CLOCKDIVISION_DIV1;
  htim3.Init.AutoReloadPreload = TIM_AUTORELOAD_PRELOAD_DISABLE;
  if (HAL_TIM_Base_Init(&htim3) != HAL_OK)
  {
    Error_Handler();
  }
  sClockSourceConfig.ClockSource = TIM_CLOCKSOURCE_INTERNAL;
  if (HAL_TIM_ConfigClockSource(&htim3, &sClockSourceConfig) != HAL_OK)
  {
    Error_Handler();
  }
  if (HAL_TIM_PWM_Init(&htim3) != HAL_OK)
  {
    Error_Handler();
  }
  sMasterConfig.MasterOutputTrigger = TIM_TRGO_RESET;
  sMasterConfig.MasterSlaveMode = TIM_MASTERSLAVEMODE_DISABLE;
  if (HAL_TIMEx_MasterConfigSynchronization(&htim3, &sMasterConfig) != HAL_OK)
  {
    Error_Handler();
  }
  sConfigOC.OCMode = TIM_OCMODE_PWM1;
  sConfigOC.Pulse = 0;
  sConfigOC.OCPolarity = TIM_OCPOLARITY_HIGH;
  sConfigOC.OCFastMode = TIM_OCFAST_DISABLE;
  if (HAL_TIM_PWM_ConfigChannel(&htim3, &sConfigOC, TIM_CHANNEL_1) != HAL_OK)
  {
    Error_Handler();
  }
  if (HAL_TIM_PWM_ConfigChannel(&htim3, &sConfigOC, TIM_CHANNEL_2) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN TIM3_Init 2 */

  /* USER CODE END TIM3_Init 2 */
  HAL_TIM_MspPostInit(&htim3);

}

/**
  * @brief TIM4 Initialization Function
  * @param None
  * @retval None
  */
static void MX_TIM4_Init(void)
{

  /* USER CODE BEGIN TIM4_Init 0 */

  /* USER CODE END TIM4_Init 0 */

  TIM_ClockConfigTypeDef sClockSourceConfig = {0};
  TIM_MasterConfigTypeDef sMasterConfig = {0};
  TIM_OC_InitTypeDef sConfigOC = {0};

  /* USER CODE BEGIN TIM4_Init 1 */

  /* USER CODE END TIM4_Init 1 */
  htim4.Instance = TIM4;
  htim4.Init.Prescaler = 99;
  htim4.Init.CounterMode = TIM_COUNTERMODE_UP;
  htim4.Init.Period = 1599;
  htim4.Init.ClockDivision = TIM_CLOCKDIVISION_DIV1;
  htim4.Init.AutoReloadPreload = TIM_AUTORELOAD_PRELOAD_DISABLE;
  if (HAL_TIM_Base_Init(&htim4) != HAL_OK)
  {
    Error_Handler();
  }
  sClockSourceConfig.ClockSource = TIM_CLOCKSOURCE_INTERNAL;
  if (HAL_TIM_ConfigClockSource(&htim4, &sClockSourceConfig) != HAL_OK)
  {
    Error_Handler();
  }
  if (HAL_TIM_PWM_Init(&htim4) != HAL_OK)
  {
    Error_Handler();
  }
  sMasterConfig.MasterOutputTrigger = TIM_TRGO_RESET;
  sMasterConfig.MasterSlaveMode = TIM_MASTERSLAVEMODE_DISABLE;
  if (HAL_TIMEx_MasterConfigSynchronization(&htim4, &sMasterConfig) != HAL_OK)
  {
    Error_Handler();
  }
  sConfigOC.OCMode = TIM_OCMODE_PWM1;
  sConfigOC.Pulse = 0;
  sConfigOC.OCPolarity = TIM_OCPOLARITY_HIGH;
  sConfigOC.OCFastMode = TIM_OCFAST_DISABLE;
  if (HAL_TIM_PWM_ConfigChannel(&htim4, &sConfigOC, TIM_CHANNEL_1) != HAL_OK)
  {
    Error_Handler();
  }
  if (HAL_TIM_PWM_ConfigChannel(&htim4, &sConfigOC, TIM_CHANNEL_2) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN TIM4_Init 2 */

  /* USER CODE END TIM4_Init 2 */
  HAL_TIM_MspPostInit(&htim4);

}

/**
  * @brief USART1 Initialization Function
  * @param None
  * @retval None
  */
static void MX_USART1_UART_Init(void)
{

  /* USER CODE BEGIN USART1_Init 0 */

  /* USER CODE END USART1_Init 0 */

  /* USER CODE BEGIN USART1_Init 1 */

  /* USER CODE END USART1_Init 1 */
  huart1.Instance = USART1;
  huart1.Init.BaudRate = 115200;
  huart1.Init.WordLength = UART_WORDLENGTH_8B;
  huart1.Init.StopBits = UART_STOPBITS_1;
  huart1.Init.Parity = UART_PARITY_NONE;
  huart1.Init.Mode = UART_MODE_TX_RX;
  huart1.Init.HwFlowCtl = UART_HWCONTROL_NONE;
  huart1.Init.OverSampling = UART_OVERSAMPLING_16;
  if (HAL_UART_Init(&huart1) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN USART1_Init 2 */

  /* USER CODE END USART1_Init 2 */

}

/**
  * @brief USART2 Initialization Function
  * @param None
  * @retval None
  */
static void MX_USART2_UART_Init(void)
{

  /* USER CODE BEGIN USART2_Init 0 */

  /* USER CODE END USART2_Init 0 */

  /* USER CODE BEGIN USART2_Init 1 */

  /* USER CODE END USART2_Init 1 */
  huart2.Instance = USART2;
  huart2.Init.BaudRate = 115200;
  huart2.Init.WordLength = UART_WORDLENGTH_8B;
  huart2.Init.StopBits = UART_STOPBITS_1;
  huart2.Init.Parity = UART_PARITY_NONE;
  huart2.Init.Mode = UART_MODE_TX_RX;
  huart2.Init.HwFlowCtl = UART_HWCONTROL_NONE;
  huart2.Init.OverSampling = UART_OVERSAMPLING_16;
  if (HAL_UART_Init(&huart2) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN USART2_Init 2 */

  /* USER CODE END USART2_Init 2 */

}

/**
  * @brief GPIO Initialization Function
  * @param None
  * @retval None
  */
static void MX_GPIO_Init(void)
{
  GPIO_InitTypeDef GPIO_InitStruct = {0};
  /* USER CODE BEGIN MX_GPIO_Init_1 */
  /* USER CODE END MX_GPIO_Init_1 */

  /* GPIO Ports Clock Enable */
  __HAL_RCC_GPIOC_CLK_ENABLE();
  __HAL_RCC_GPIOA_CLK_ENABLE();
  __HAL_RCC_GPIOB_CLK_ENABLE();

  /*Configure GPIO pin Output Level */
  HAL_GPIO_WritePin(GPIOC, GPIO_PIN_0|GPIO_PIN_1, GPIO_PIN_RESET);

  /*Configure GPIO pin Output Level */
  HAL_GPIO_WritePin(GPIOA, GPIO_PIN_0|GPIO_PIN_1, GPIO_PIN_RESET);

  /*Configure GPIO pin Output Level */
  HAL_GPIO_WritePin(GPIOB, GPIO_PIN_0|GPIO_PIN_1, GPIO_PIN_RESET);

  /*Configure GPIO pins : PC0 PC1 */
  GPIO_InitStruct.Pin = GPIO_PIN_0|GPIO_PIN_1;
  GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
  HAL_GPIO_Init(GPIOC, &GPIO_InitStruct);

  /*Configure GPIO pins : PA0 PA1 */
  GPIO_InitStruct.Pin = GPIO_PIN_0|GPIO_PIN_1;
  GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
  HAL_GPIO_Init(GPIOA, &GPIO_InitStruct);

  /*Configure GPIO pins : PB0 PB1 */
  GPIO_InitStruct.Pin = GPIO_PIN_0|GPIO_PIN_1;
  GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
  HAL_GPIO_Init(GPIOB, &GPIO_InitStruct);

  /* USER CODE BEGIN MX_GPIO_Init_2 */
  /* USER CODE END MX_GPIO_Init_2 */
}

/* USER CODE BEGIN 4 */

/* USER CODE END 4 */

/**
  * @brief  This function is executed in case of error occurrence.
  * @retval None
  */
void Error_Handler(void)
{
  /* USER CODE BEGIN Error_Handler_Debug */
  /* User can add his own implementation to report the HAL error return state */
  __disable_irq();
  while (1)
  {
  }
  /* USER CODE END Error_Handler_Debug */
}
#ifdef USE_FULL_ASSERT
/**
  * @brief  Reports the name of the source file and the source line number
  *         where the assert_param error has occurred.
  * @param  file: pointer to the source file name
  * @param  line: assert_param error line source number
  * @retval None
  */
void assert_failed(uint8_t *file, uint32_t line)
{
  /* USER CODE BEGIN 6 */
  /* User can add his own implementation to report the file name and line number,
     ex: printf("Wrong parameters value: file %s on line %d\r\n", file, line) */
  /* USER CODE END 6 */
}
#endif /* USE_FULL_ASSERT */
