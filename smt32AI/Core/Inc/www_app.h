/**
 * @file  www_app.h
 * @brief Application Worldwide Weather Watcher : machine à états des modes,
 *        acquisition, console de configuration, gestion des erreurs.
 */
#ifndef WWW_APP_H
#define WWW_APP_H

#include "stm32l4xx_hal.h"
#include <stdbool.h>

/**
 * @param hi2c   I2C1 (BH1750)
 * @param huart     USART2 (console)
 * @param huartGps  USART3 (GPS)
 * @param hspiSd    SPI1 (carte SD)
 * @param hrtc      RTC initialisée, ou NULL si son init a échoué
 */
void App_Init(I2C_HandleTypeDef *hi2c, UART_HandleTypeDef *huart,
              UART_HandleTypeDef *huartGps, SPI_HandleTypeDef *hspiSd,
              RTC_HandleTypeDef *hrtc);

/** À appeler en boucle dans while(1). Ne bloque jamais plus de ~25 ms. */
void App_Loop(void);

#endif /* WWW_APP_H */
