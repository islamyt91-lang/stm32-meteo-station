/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file    stm32l4xx_it.c
  * @brief   Interrupt Service Routines.
  ******************************************************************************
  */
/* USER CODE END Header */
#include "main.h"
#include "stm32l4xx_it.h"
/* USER CODE BEGIN Includes */
#include "rgb_led.h"
/* USER CODE END Includes */

extern UART_HandleTypeDef huart2;
extern UART_HandleTypeDef huart3;

/******************************************************************************/
/*           Cortex-M4 Processor Interruption and Exception Handlers          */
/******************************************************************************/
void NMI_Handler(void)
{
  while (1)
  {
  }
}

void HardFault_Handler(void)
{
  while (1)
  {
  }
}

void MemManage_Handler(void)
{
  while (1)
  {
  }
}

void BusFault_Handler(void)
{
  while (1)
  {
  }
}

void UsageFault_Handler(void)
{
  while (1)
  {
  }
}

void SVC_Handler(void)
{
}

void DebugMon_Handler(void)
{
}

void PendSV_Handler(void)
{
}

/**
  * @brief This function handles System tick timer (1 ms).
  */
void SysTick_Handler(void)
{
  /* USER CODE BEGIN SysTick_IRQn 0 */

  /* USER CODE END SysTick_IRQn 0 */
  HAL_IncTick();
  /* USER CODE BEGIN SysTick_IRQn 1 */
  RGB_TickISR();          /* PWM logiciel + clignotements de la LED RGB */
  /* USER CODE END SysTick_IRQn 1 */
}

/******************************************************************************/
/* STM32L4xx Peripheral Interrupt Handlers                                    */
/******************************************************************************/

/**
  * @brief This function handles USART2 global interrupt (réception console).
  */
void USART2_IRQHandler(void)
{
  HAL_UART_IRQHandler(&huart2);
}

/**
  * @brief This function handles EXTI line[15:10] interrupts (PC13).
  */
void EXTI15_10_IRQHandler(void)
{
  HAL_GPIO_EXTI_IRQHandler(GPIO_PIN_13);
}

/**
  * @brief This function handles USART3 global interrupt (GPS).
  */
void USART3_IRQHandler(void)
{
  HAL_UART_IRQHandler(&huart3);
}
