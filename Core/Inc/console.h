/**
 * @file  console.h
 * @brief Console série sur USART2 (ST-LINK VCP, 115200 8N1) : printf en
 *        émission, réception par interruption dans un buffer circulaire.
 */
#ifndef CONSOLE_H
#define CONSOLE_H

#include "stm32l4xx_hal.h"
#include <stdbool.h>
#include <stddef.h>

#define CONSOLE_LINE_MAX  64

void Console_Init(UART_HandleTypeDef *huart);

/**
 * Récupère une ligne complète tapée par l'utilisateur (sans le \r\n).
 * Fait l'écho des caractères et gère la touche retour arrière.
 * @return true si une ligne est disponible dans `line`.
 */
bool Console_GetLine(char *line, size_t size);

/** Vrai si au moins un caractère a été reçu depuis le dernier appel. */
bool Console_TakeActivity(void);

/* Callbacks HAL à relayer (voir main.c) */
void Console_RxCpltCallback(UART_HandleTypeDef *huart);
void Console_ErrorCallback(UART_HandleTypeDef *huart);

#endif /* CONSOLE_H */
