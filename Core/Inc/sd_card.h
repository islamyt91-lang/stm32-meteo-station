/**
 * @file  sd_card.h
 * @brief Carte SD (SPI1) : montage FatFs + journalisation des mesures.
 *
 * Fichiers : YYMMDD_R.LOG (ex. 261019_0.LOG), toujours écriture en révision 0.
 * Quand le fichier atteint FILE_MAX_SIZE (settings), il est archivé avec le
 * numéro de révision suivant et un neuf fichier révision 0 est créé.
 * Le nom est en 8.3, compatible _USE_LFN=0.
 */
#ifndef SD_CARD_H
#define SD_CARD_H

#include <stdbool.h>

/** Monte la carte et ouvre/crée le fichier de log courant. Imprimable sur console. */
void SdCard_Init(void);

/**
 * Ajoute une ligne au fichier de log (sans le \n final, ajouté ici).
 * Gère automatiquement l'archivage quand FILE_MAX_SIZE est atteint.
 * @return true si la ligne est écrite sur la carte.
 */
bool SdCard_AppendLine(const char *line);

/** Ferme et démonte le système de fichiers (mode maintenance, retrait sûr). */
void SdCard_Unmount(void);

/** Remonte le système de fichiers (après remplacement de la carte). */
bool SdCard_Remount(void);

/** Vrai si le fichier de log est ouvert et prêt. */
bool SdCard_IsReady(void);

/** Vrai si plus aucun fichier ne peut être écrit (révisions épuisées / disque plein). */
bool SdCard_IsFull(void);

/** Vrai si une erreur d'accès/écriture sur la carte est en cours. */
bool SdCard_Error(void);

#endif /* SD_CARD_H */
