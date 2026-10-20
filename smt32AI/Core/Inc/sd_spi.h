/**
 * @file  sd_spi.h
 * @brief Pilote bas niveau carte SD/SDHC en mode SPI (SPI1 + CS sur PB6).
 *        Utilisé par FatFs via FATFS/Target/user_diskio.c.
 */
#ifndef SD_SPI_H
#define SD_SPI_H

#include "stm32l4xx_hal.h"
#include <stdbool.h>
#include <stdint.h>

void SD_SPI_Attach(SPI_HandleTypeDef *hspi);

/** Initialise la carte (CMD0, CMD8, ACMD41...). Retourne true si OK. */
bool SD_SPI_Init(void);

/** Lit/écrit `count` secteurs de 512 octets. */
bool SD_SPI_Read(uint8_t *buf, uint32_t sector, uint32_t count);
bool SD_SPI_Write(const uint8_t *buf, uint32_t sector, uint32_t count);

/** Attend la fin d'une écriture interne de la carte. */
bool SD_SPI_Sync(void);

/** Nombre total de secteurs (lu dans le registre CSD), 0 si erreur. */
uint32_t SD_SPI_SectorCount(void);

bool SD_SPI_IsReady(void);

/** Explication de la derniere erreur d'initialisation (texte). */
const char *SD_SPI_LastError(void);
void SD_SPI_MarkNotReady(void);

#endif /* SD_SPI_H */
