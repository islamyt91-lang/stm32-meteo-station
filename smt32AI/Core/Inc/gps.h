/**
 * @file  gps.h
 * @brief Module GPS sur USART3 (PC10 TX / PC11 RX, 9600 bauds) : réception
 *        par interruption et décodage des trames NMEA $GPRMC / $GNRMC.
 */
#ifndef GPS_H
#define GPS_H

#include "stm32l4xx_hal.h"
#include <stdbool.h>
#include <stdint.h>

typedef struct {
    bool     fix;          /* position valide (statut 'A') */
    int32_t  lat_e6;       /* latitude  en millionièmes de degré (+ = Nord) */
    int32_t  lon_e6;       /* longitude en millionièmes de degré (+ = Est)  */
    uint32_t tick;         /* HAL_GetTick() à la réception de la trame */
} GpsFix;

void GPS_Init(UART_HandleTypeDef *huart);

/** À appeler dans la boucle principale : décode les octets reçus. */
void GPS_Process(void);

/**
 * Dernière trame RMC reçue APRÈS l'instant `sinceTick`.
 * @return false si aucune trame RMC valide (checksum OK) depuis ce moment.
 */
bool GPS_GetSince(uint32_t sinceTick, GpsFix *out);

/** Nombre total de trames NMEA correctes reçues (diagnostic). */
uint32_t GPS_FrameCount(void);

/* Callbacks HAL à relayer (voir main.c) */
void GPS_RxCpltCallback(UART_HandleTypeDef *huart);
void GPS_ErrorCallback(UART_HandleTypeDef *huart);

/** Formate une coordonnée "48.856614" ou "-2.352222". */
void GPS_FormatCoord(char *buf, unsigned n, int32_t v_e6);

#endif /* GPS_H */
