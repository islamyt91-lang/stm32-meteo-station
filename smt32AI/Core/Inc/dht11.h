/**
 * @file  dht11.h
 * @brief Pilote DHT11 (température + humidité) sur une broche, protocole
 *        1 fil propriétaire. Temporisation µs par le compteur de cycles DWT.
 */
#ifndef DHT11_H
#define DHT11_H

#include <stdint.h>

#define DHT11_MIN_INTERVAL_MS  2000u   /* délai minimal entre deux lectures */

typedef enum {
    DHT11_OK = 0,
    DHT11_BUSY,            /* < 2 s depuis la dernière lecture : réessayer plus tard */
    DHT11_ERR_NO_RESPONSE, /* le capteur ne répond pas au signal de start */
    DHT11_ERR_TIMEOUT,     /* trame interrompue */
    DHT11_ERR_CHECKSUM     /* somme de contrôle fausse */
} DHT11_Status;

typedef struct {
    int16_t temp_x10;      /* température en dixièmes de °C */
    int16_t hum_x10;       /* humidité en dixièmes de % */
} DHT11_Data;

void DHT11_Init(void);
DHT11_Status DHT11_Read(DHT11_Data *out);
const char *DHT11_StatusStr(DHT11_Status s);

/* Attente active en microsecondes (DWT), réutilisable ailleurs. */
void DWT_DelayUs(uint32_t us);

#endif /* DHT11_H */
