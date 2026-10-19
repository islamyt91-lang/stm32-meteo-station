/**
 * @file  bh1750.h
 * @brief Pilote du capteur de luminosité BH1750 (I2C), résultat en lux.
 */
#ifndef BH1750_H
#define BH1750_H

#include "stm32l4xx_hal.h"
#include <stdint.h>

typedef enum {
    BH1750_OK = 0,
    BH1750_NOT_READY,     /* mesure pas encore disponible après init (~180 ms) */
    BH1750_ERR_I2C        /* pas d'ACK, bus occupé, timeout... */
} BH1750_Status;

BH1750_Status BH1750_Init(I2C_HandleTypeDef *hi2c, uint8_t addr7);

/** Lecture en mode continu haute résolution. lux_x10 = lux * 10. */
BH1750_Status BH1750_ReadLux(uint32_t *lux_x10);

/** Code d'erreur HAL de la dernière transaction (HAL_I2C_ERROR_xxx). */
uint32_t BH1750_LastI2CError(void);

#endif /* BH1750_H */
