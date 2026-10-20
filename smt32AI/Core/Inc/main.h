/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file           : main.h
  * @brief          : Header for main.c file.
  ******************************************************************************
  */
/* USER CODE END Header */
#ifndef __MAIN_H
#define __MAIN_H

#ifdef __cplusplus
extern "C" {
#endif

#include "stm32l4xx_hal.h"

/* USER CODE BEGIN Includes */
#include <stdbool.h>
/* USER CODE END Includes */

void Error_Handler(void);

/* USER CODE BEGIN EFP */
/* Source d'horloge de la RTC choisie au démarrage */
typedef enum { RTC_SRC_NONE = 0, RTC_SRC_LSE, RTC_SRC_LSI } RtcSource;
extern RtcSource g_rtcSource;
extern const char *g_rtcDiag;
/* USER CODE END EFP */

/* Private defines -----------------------------------------------------------*/
#define B1_Pin GPIO_PIN_13
#define B1_GPIO_Port GPIOC
#define BTN2_Pin GPIO_PIN_0
#define BTN2_GPIO_Port GPIOC
#define LED_R_Pin GPIO_PIN_0
#define LED_R_GPIO_Port GPIOA
#define LED_G_Pin GPIO_PIN_1
#define LED_G_GPIO_Port GPIOA
#define USART_TX_Pin GPIO_PIN_2
#define USART_TX_GPIO_Port GPIOA
#define USART_RX_Pin GPIO_PIN_3
#define USART_RX_GPIO_Port GPIOA
#define LED_B_Pin GPIO_PIN_4
#define LED_B_GPIO_Port GPIOA
#define DHT11_Pin GPIO_PIN_0
#define DHT11_GPIO_Port GPIOB
#define TMS_Pin GPIO_PIN_13
#define TMS_GPIO_Port GPIOA
#define TCK_Pin GPIO_PIN_14
#define TCK_GPIO_Port GPIOA
#define SWO_Pin GPIO_PIN_3
#define SWO_GPIO_Port GPIOB
#define SD_CS_Pin GPIO_PIN_6
#define SD_CS_GPIO_Port GPIOB

#ifdef __cplusplus
}
#endif

#endif /* __MAIN_H */
