/**
 * @file  sdlog.h
 * @brief Journal des mesures sur carte SD (FatFs) : fichiers AAMMJJ_N.LOG,
 *        archivage quand FILE_MAX_SIZE est atteint, détection des erreurs.
 */
#ifndef SDLOG_H
#define SDLOG_H

#include <stdbool.h>
#include <stdint.h>

typedef enum {
    SDLOG_OK = 0,
    SDLOG_NOT_MOUNTED,   /* carte absente / non formatée FAT32 */
    SDLOG_FULL,          /* plus de place */
    SDLOG_WRITE_ERROR    /* erreur d'accès ou d'écriture */
} SdLogStatus;

/** Monte la carte. Retourne SDLOG_OK si la carte est utilisable. */
SdLogStatus SDLog_Mount(void);

/** Démonte proprement (mode maintenance : retrait possible). */
void SDLog_Unmount(void);

bool SDLog_IsMounted(void);

/**
 * Ajoute une ligne au fichier du jour (AAMMJJ_0.LOG). Si la taille dépasse
 * max_size, le fichier est archivé en AAMMJJ_N.LOG et un nouveau _0 est créé.
 * @param yy,mm,dd  date du jour (0,0,0 si RTC en erreur -> 000000_0.LOG)
 * @param header    ligne d'en-tête écrite au début de chaque nouveau fichier
 */
SdLogStatus SDLog_Append(uint8_t yy, uint8_t mm, uint8_t dd, const char *line,
                         const char *header, uint32_t max_size);

/** Dernier statut (pour la LED d'erreur). */
SdLogStatus SDLog_LastStatus(void);

const char *SDLog_StatusStr(SdLogStatus s);

/** Detail technique de la derniere erreur (code FatFs + cause probable). */
const char *SDLog_Detail(void);

#endif /* SDLOG_H */
