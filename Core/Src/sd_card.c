/**
 * @file  sd_card.c
 * @brief Carte SD sur SPI1 : journalisation FatFs avec rotation des fichiers.
 *
 *  Conformément au cahier des charges ("Modes de fonctionnement") :
 *   - un fichier par jour : YYMMDD_0.LOG (révision 0 = fichier courant)
 *   - quand FILE_MAX_SIZE est atteint, le fichier est archivé avec le numéro
 *     de révision suivant, puis la révision 0 recommence à vide
 *   - en mode MAINTENANCE le système de fichiers est démonté pour permettre
 *     un retrait sûr de la carte
 */
#include "sd_card.h"
#include "main.h"
#include "www_config.h"
#include "settings.h"
#include "rtc_clock.h"
#include "fatfs.h"

#include <stdio.h>
#include <string.h>

/* ========================================================================== */
/*  État                                                                      */
/* ========================================================================== */
static bool s_mounted;          /* système de fichiers monté                 */
static bool s_ready;            /* fichier de log ouvert                     */
static bool s_full;             /* plus aucune révision disponible           */
static bool s_error;            /* erreur d'accès / d'écriture               */
static uint32_t s_size;         /* taille courante du fichier (octets)       */
static char s_name[16];         /* nom 8.3 du fichier courant ("YYMMDD_R.LOG") */

/* ========================================================================== */
/*  Brochage (PB6 = SD_CS, déjà configuré en sortie par le pilote)            */
/* ========================================================================== */
static void cs_gpio_init(void)
{
    GPIO_InitTypeDef g = {0};
    __HAL_RCC_GPIOB_CLK_ENABLE();
    HAL_GPIO_WritePin(SD_CS_PORT, SD_CS_PIN, GPIO_PIN_SET);
    g.Pin   = SD_CS_PIN;
    g.Mode  = GPIO_MODE_OUTPUT_PP;
    g.Pull  = GPIO_NOPULL;
    g.Speed = GPIO_SPEED_FREQ_LOW;
    HAL_GPIO_Init(SD_CS_PORT, &g);
}

/* ========================================================================== */
/*  Gestion du fichier de log courant                                         */
/* ========================================================================== */
/** Construit "YYMMDD_0.LOG" (ou "NORTC__0.LOG" si la RTC est absente). */
static void build_name(char *out, size_t outSize, uint8_t rev)
{
    RtcDateTime dt;
    if (rev > 9u) rev = 9u;
    if (RtcClock_Get(&dt)) {
        snprintf(out, outSize, "%02u%02u%02u_%u.LOG",
                 (unsigned)(dt.year % 100u), dt.month, dt.day, rev);
    } else {
        snprintf(out, outSize, "NORTC__%u.LOG", rev);
    }
}

static void close_file(void)
{
    if (s_ready) {
        f_close(&USERFile);
        s_ready = false;
    }
}

/** Ouvre (ou crée) la révision 0 du jour courant. */
static bool open_log_file(void)
{
    FRESULT fr;

    build_name(s_name, sizeof(s_name), 0u);
    fr = f_open(&USERFile, s_name, FA_OPEN_APPEND | FA_WRITE);
    if (fr != FR_OK) {
        fr = f_open(&USERFile, s_name, FA_CREATE_ALWAYS | FA_WRITE);
    }
    if (fr != FR_OK) {
        printf("[SD] Impossible d'ouvrir %s (FatFs %u)\r\n", s_name, (unsigned)fr);
        s_ready = false;
        return false;
    }
    s_size = f_size(&USERFile);
    s_ready = true;
    return true;
}

/**
 * Archive la révision 0 : YYMMDD_0.LOG -> YYMMDD_N.LOG (prochaine révision
 * libre 1..9), puis un nouveau fichier révision 0 est créé.
 */
static bool rotate_file(void)
{
    char dest[16];
    uint8_t rev;

    close_file();

    for (rev = 1u; rev <= 9u; rev++) {
        build_name(dest, sizeof(dest), rev);
        if (f_stat(dest, NULL) != FR_OK) {
            break;                            /* emplacement libre */
        }
    }
    if (rev > 9u) {
        /* Toutes les révisions existent : on écrase la plus ancienne (1) */
        rev = 1u;
        build_name(dest, sizeof(dest), rev);
        f_unlink(dest);
    }

    if (f_rename(s_name, dest) != FR_OK) {
        printf("[ERREUR] SD : archival de %s impossible\r\n", s_name);
        s_error = true;
        /* On repart quand même à zéro sur le fichier courant */
        if (f_open(&USERFile, s_name, FA_CREATE_ALWAYS | FA_WRITE) != FR_OK) {
            s_ready = false;
            return false;
        }
        s_size = 0u;
        s_ready = true;
        return true;
    }

    printf("[SD] Fichier plein : archive %s\r\n", dest);
    return open_log_file();
}

/* ========================================================================== */
/*  API publique                                                              */
/* ========================================================================== */
void SdCard_Init(void)
{
    FRESULT fr;

    s_mounted = s_ready = s_full = s_error = false;
    s_size = 0u;

    cs_gpio_init();
    MX_FATFS_Init();

    fr = f_mount(&USERFatFS, USERPath, 1u);    /* 1 = monter immédiatement */
    if (fr == FR_NO_FILESYSTEM) {
        printf("[SD] Carte non formatee : creation FAT...\r\n");
        static BYTE work[_MAX_SS];
        fr = f_mkfs(USERPath, FM_ANY, 0u, work, sizeof(work));
        if (fr == FR_OK) {
            fr = f_mount(&USERFatFS, USERPath, 1u);
        }
    }
    if (fr != FR_OK) {
        printf("[ERREUR] SD : carte absente ou inaccessible (FatFs %u)\r\n",
               (unsigned)fr);
        s_error = true;
        return;
    }
    s_mounted = true;

    if (open_log_file()) {
        s_ready = true;
        printf("[SD] Carte montee, journal %s (%lu/%lu octets)\r\n",
               s_name, (unsigned long)s_size,
               (unsigned long)g_settings.file_max_size);
    }
}

bool SdCard_AppendLine(const char *line)
{
    FRESULT fr;
    UINT written;
    size_t lineLen, len;
    char crlf[3] = "\r\n";

    if (!s_mounted || !s_ready || s_full) {
        return false;
    }

    lineLen = strlen(line);
    len = lineLen + 2u;                        /* + CRLF */

    /* Rotation si la ligne ferait dépasser FILE_MAX_SIZE */
    if ((s_size + len) > g_settings.file_max_size) {
        if (!rotate_file()) {
            s_error = true;
            s_ready = false;
            return false;
        }
    }

    fr = f_write(&USERFile, line, (UINT)lineLen, &written);
    if ((fr == FR_OK) && (written == (UINT)lineLen)) {
        fr = f_write(&USERFile, crlf, 2u, &written);
    }
    if (fr != FR_OK || written != 2u) {
        if (fr == FR_DENIED) {
            s_full = true;                     /* volume ou répertoire plein */
        }
        s_error = true;
        printf("[ERREUR] SD : ecriture impossible (FatFs %u)\r\n", (unsigned)fr);
        return false;
    }

    s_size += len;
    (void)f_sync(&USERFile);                   /* données sur la carte */
    s_error = false;
    return true;
}

void SdCard_Unmount(void)
{
    if (!s_mounted) {
        return;
    }
    close_file();
    f_mount(NULL, USERPath, 0u);               /* démonter sans forcer */
    s_mounted = false;
    printf("[SD] Systeme de fichiers demonte : carte retiree en securite.\r\n");
}

bool SdCard_Remount(void)
{
    FRESULT fr;

    if (s_mounted) {
        return true;
    }
    fr = f_mount(&USERFatFS, USERPath, 1u);
    if (fr != FR_OK) {
        printf("[ERREUR] SD : re-montage impossible (FatFs %u)\r\n", (unsigned)fr);
        s_error = true;
        return false;
    }
    s_mounted = true;
    s_full = false;
    if (open_log_file()) {
        s_ready = true;
        printf("[SD] Carte remontee, journal %s\r\n", s_name);
        return true;
    }
    s_error = true;
    return false;
}

bool SdCard_IsReady(void)
{
    return s_ready;
}

bool SdCard_IsFull(void)
{
    return s_full;
}

bool SdCard_Error(void)
{
    return s_error;
}
