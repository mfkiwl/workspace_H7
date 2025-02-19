/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file           : main.h
  * @brief          : Header for main.c file.
  *                   This file contains the common defines of the application.
  ******************************************************************************
  * @attention
  *
  * <h2><center>&copy; Copyright (c) 2022 STMicroelectronics.
  * All rights reserved.</center></h2>
  *
  * This software component is licensed by ST under BSD 3-Clause license,
  * the "License"; You may not use this file except in compliance with the
  * License. You may obtain a copy of the License at:
  *                        opensource.org/licenses/BSD-3-Clause
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
#include "stm32h7xx_hal.h"

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

/* Exported functions prototypes ---------------------------------------------*/
void Error_Handler(void);

/* USER CODE BEGIN EFP */

/* USER CODE END EFP */

/* Private defines -----------------------------------------------------------*/
#define QSPI_FLASH_SIZE_2N_OFFSET 0
#define MUX_A_Pin GPIO_PIN_4
#define MUX_A_GPIO_Port GPIOE
#define MUX_B_Pin GPIO_PIN_6
#define MUX_B_GPIO_Port GPIOE
#define SMU_EN_Pin GPIO_PIN_1
#define SMU_EN_GPIO_Port GPIOH
#define SW4_Pin GPIO_PIN_0
#define SW4_GPIO_Port GPIOA
#define SW4_EXTI_IRQn EXTI0_IRQn
#define SW3_Pin GPIO_PIN_1
#define SW3_GPIO_Port GPIOA
#define SW3_EXTI_IRQn EXTI1_IRQn
#define SW2_Pin GPIO_PIN_2
#define SW2_GPIO_Port GPIOA
#define SW2_EXTI_IRQn EXTI2_IRQn
#define SW1_Pin GPIO_PIN_3
#define SW1_GPIO_Port GPIOA
#define SW1_EXTI_IRQn EXTI3_IRQn
#define SCL2_Pin GPIO_PIN_10
#define SCL2_GPIO_Port GPIOB
#define SDA2_Pin GPIO_PIN_11
#define SDA2_GPIO_Port GPIOB
#define I2C1_INT_Pin GPIO_PIN_5
#define I2C1_INT_GPIO_Port GPIOB
#define SDA1_Pin GPIO_PIN_7
#define SDA1_GPIO_Port GPIOB
#define SCL1_Pin GPIO_PIN_8
#define SCL1_GPIO_Port GPIOB
#define MUX_C_Pin GPIO_PIN_0
#define MUX_C_GPIO_Port GPIOE
#define VOUT_EN_Pin GPIO_PIN_1
#define VOUT_EN_GPIO_Port GPIOE
/* USER CODE BEGIN Private defines */

/* USER CODE END Private defines */

#ifdef __cplusplus
}
#endif

#endif /* __MAIN_H */
