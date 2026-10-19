/**
 * @file  settings.h
 * @brief Paramètres modifiables en mode configuration, sauvegardés en Flash.
 *
 * Le STM32L476 n'a pas d'EEPROM : on émule l'EEPROM du sujet avec la dernière
 * page de Flash (2 Ko, banque 2, page 255 -> 0x080FF800), protégée par un
 * mot magique et un CRC. Rien n'est écrit tant qu'aucun paramètre ne change.
 */
#ifndef SETTINGS_H
#define SETTINGS_H

#include <stdint.h>
#include <stdbool.h>

typedef struct {
    uint32_t log_interval_s;   /* LOG_INTERVAL (min) ou LOG_INTERVAL_S (s)  */
    uint32_t file_max_size;    /* FILE_MAX_SIZE (octets) - réservé carte SD */
    uint16_t timeout_s;        /* TIMEOUT : abandon d'une acquisition       */

    uint8_t  lumin;            /* LUMIN {0,1}                               */
    uint32_t lumin_low;        /* LUMIN_LOW  (lux)                          */
    uint32_t lumin_high;       /* LUMIN_HIGH (lux)                          */

    uint8_t  temp_air;         /* TEMP_AIR {0,1}                            */
    int16_t  min_temp_air;     /* MIN_TEMP_AIR (°C)                         */
    int16_t  max_temp_air;     /* MAX_TEMP_AIR (°C)                         */

    uint8_t  hygr;             /* HYGR {0,1}                                */
    int16_t  hygr_mint;        /* HYGR_MINT (°C)                            */
    int16_t  hygr_maxt;        /* HYGR_MAXT (°C)                            */
} Settings;

/* Domaines de définition */
#define LOG_INTERVAL_MIN_MIN    1u
#define LOG_INTERVAL_MAX_MIN    1440u
#define LOG_INTERVAL_MIN_S      5u        /* >= 2 s du DHT11, marge comprise */
#define TIMEOUT_MIN_S           1u
#define TIMEOUT_MAX_S           600u
#define LUMIN_MAX_LUX           65535u    /* pleine échelle du BH1750 */
#define TEMP_MIN_C              (-40)
#define TEMP_MAX_C              85

extern Settings g_settings;

void Settings_Defaults(Settings *s);
/** Charge depuis la Flash. Retourne false (et applique les défauts) si vide/corrompue. */
bool Settings_Load(void);
/** Écrit en Flash. Retourne false en cas d'erreur d'effacement/programmation. */
bool Settings_Save(void);

#endif /* SETTINGS_H */
