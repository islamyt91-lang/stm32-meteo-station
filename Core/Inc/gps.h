/**
 * @file  gps.h
 * @brief GPS NMEA sur USART3 (PC10 = TX vers module, PC11 = RX depuis module).
 *
 * Réception par interruption dans un buffer circulaire, parsing des trames
 * $GPRMC / $GNGGA (compatibles NEO-6M et modules NMEA génériques).
 * Toutes les valeurs sont des entiers (pas de float) pour rester compatibles
 * avec le printf du banc nano.specs.
 */
#ifndef GPS_H
#define GPS_H

#include "stm32l4xx_hal.h"
#include <stdbool.h>
#include <stdint.h>

#define GPS_BAUDRATE   9600u      /* vitesse par défaut des modules NMEA      */
#define GPS_ERROR_MS   10000u     /* sans trame valide -> erreur (LED)        */

typedef struct {
    bool     fixed;               /* position valide (RMC status = 'A')       */
    int32_t  lat_udeg;            /* latitude  micro-degrés (+ = N)           */
    int32_t  lon_udeg;            /* longitude micro-degrés (+ = E)           */
    uint16_t speed_x10;           /* vitesse en nœuds, dixièmes               */
    uint16_t altitude_m;          /* altitude en mètres (GGA, si disponible)  */
    uint8_t  satellites;          /* satellites utilisés (GGA)                */
    /* Horodatage NMEA (UTC) */
    uint8_t  hours, minutes, seconds;
    uint8_t  day, month;
    uint16_t year;
    uint32_t lastFixMs;           /* HAL_GetTick de la dernière trame RMC OK  */
    uint32_t sentenceCount;       /* trames NMEA valides reçues au total      */
} GpsData;

/** Démarre USART3 + réception IT. Imprimable sur console. */
void Gps_Init(UART_HandleTypeDef *huart);

/** À appeler souvent depuis la boucle : consomme le buffer, parse les trames. */
void Gps_Poll(void);

/** Dernière position connue (copie). fixed = false si jamais fixée. */
void Gps_GetLast(GpsData *out);

/** Vrai si le GPS n'a rien donné de valide depuis GPS_ERROR_MS. */
bool Gps_Error(void);

/* Callbacks HAL à relayer (voir main.c) */
void Gps_RxCpltCallback(UART_HandleTypeDef *huart);
void Gps_ErrorCallback(UART_HandleTypeDef *huart);

#endif /* GPS_H */
