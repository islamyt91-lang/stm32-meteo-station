/**
 * @file  sdlog.c
 * @brief Journal des mesures sur carte SD.
 *
 * Règles du sujet :
 *  - nom : AAMMJJ_0.LOG (ex. 261013_0.LOG), on écrit toujours dans la
 *    révision 0 ;
 *  - quand le fichier atteint FILE_MAX_SIZE, il est archivé sous le premier
 *    numéro libre (AAMMJJ_1.LOG, _2...) et l'écriture reprend dans un _0 vide.
 *
 * Chaque ligne est écrite par ouverture / écriture / fermeture : en cas de
 * coupure ou de retrait, au pire la dernière ligne est perdue.
 */
#include "sdlog.h"
#include "fatfs.h"
#include "sd_spi.h"

void USER_ForceReinit(void);   /* FATFS/Target/user_diskio.c */
#include <stdio.h>
#include <string.h>

/* Noms longs activés dans CubeMX ? Sinon (8.3) révisions 1 à 9 seulement. */
#if (defined(_USE_LFN) && (_USE_LFN != 0)) || (defined(FF_USE_LFN) && (FF_USE_LFN != 0))
#define MAX_REVISION  999u
#else
#define MAX_REVISION  9u
#endif

#define MIN_FREE_BYTES  (8u * 1024u)   /* en dessous : carte considérée pleine */

static bool        s_mounted;
static SdLogStatus s_last = SDLOG_NOT_MOUNTED;
static FRESULT     s_lastFr = FR_OK;
static char        s_detail[160];

static SdLogStatus fail(SdLogStatus st)
{
    s_last = st;
    if (st == SDLOG_WRITE_ERROR || st == SDLOG_NOT_MOUNTED) {
        /* La carte a peut-être été retirée : on démonte, et le prochain
         * SDLog_Mount() réinitialisera complètement la carte. */
        f_mount(NULL, USERPath, 0);
        USER_ForceReinit();
        s_mounted = false;
    }
    return st;
}

static bool free_space_ok(void)
{
    FATFS *fs;
    DWORD freeClust;
    if (f_getfree(USERPath, &freeClust, &fs) != FR_OK) {
        return true;                     /* inconnu : on tente quand même */
    }
    uint64_t freeBytes = (uint64_t)freeClust * fs->csize * 512u;
    return freeBytes >= MIN_FREE_BYTES;
}

SdLogStatus SDLog_Mount(void)
{
    if (s_mounted) return SDLOG_OK;

    USER_ForceReinit();
    s_lastFr = f_mount(&USERFatFS, USERPath, 1);
    if (s_lastFr != FR_OK) {
        return fail(SDLOG_NOT_MOUNTED);
    }
    s_mounted = true;
    if (!free_space_ok()) {
        return s_last = SDLOG_FULL;
    }
    return s_last = SDLOG_OK;
}

void SDLog_Unmount(void)
{
    if (s_mounted) {
        f_mount(NULL, USERPath, 0);
    }
    USER_ForceReinit();
    s_mounted = false;
    s_last = SDLOG_NOT_MOUNTED;
}

bool SDLog_IsMounted(void)
{
    return s_mounted;
}

static void make_name(char *buf, size_t n, uint8_t yy, uint8_t mm, uint8_t dd, unsigned rev)
{
    snprintf(buf, n, "%s%02u%02u%02u_%u.LOG", USERPath, yy, mm, dd, rev);
}

/** Archive AAMMJJ_0.LOG sous le premier numéro libre. */
static FRESULT archive(uint8_t yy, uint8_t mm, uint8_t dd)
{
    char src[24], dst[24];
    FILINFO fi;
    FRESULT fr;

    make_name(src, sizeof(src), yy, mm, dd, 0);
    for (unsigned rev = 1; rev <= MAX_REVISION; rev++) {
        make_name(dst, sizeof(dst), yy, mm, dd, rev);
        fr = f_stat(dst, &fi);
        if (fr == FR_NO_FILE) {
            return f_rename(src, dst);
        }
        if (fr != FR_OK) {
            return fr;
        }
    }
    /* Tous les numéros sont pris : on remplace le plus ancien (_1)
     * en décalant les autres. */
    make_name(dst, sizeof(dst), yy, mm, dd, 1);
    f_unlink(dst);
    for (unsigned rev = 2; rev <= MAX_REVISION; rev++) {
        char a[24], b[24];
        make_name(a, sizeof(a), yy, mm, dd, rev);
        make_name(b, sizeof(b), yy, mm, dd, rev - 1u);
        f_rename(a, b);
    }
    make_name(dst, sizeof(dst), yy, mm, dd, MAX_REVISION);
    return f_rename(src, dst);
}

SdLogStatus SDLog_Append(uint8_t yy, uint8_t mm, uint8_t dd, const char *line,
                         const char *header, uint32_t max_size)
{
    char name[24];
    UINT bw;
    FRESULT fr;
    FILINFO fi;

    if (!s_mounted && SDLog_Mount() != SDLOG_OK && !s_mounted) {
        return s_last;
    }

    uint32_t lineLen = (uint32_t)strlen(line);
    make_name(name, sizeof(name), yy, mm, dd, 0);

    /* Archivage si la ligne ferait dépasser FILE_MAX_SIZE */
    fr = f_stat(name, &fi);
    if (fr == FR_OK && fi.fsize > 0 && (fi.fsize + lineLen) > max_size) {
        fr = archive(yy, mm, dd);
        if (fr == FR_DENIED) return fail(SDLOG_FULL);
        if (fr != FR_OK)     return fail(SDLOG_WRITE_ERROR);
        fr = FR_NO_FILE;
    } else if (fr != FR_OK && fr != FR_NO_FILE) {
        return fail(SDLOG_WRITE_ERROR);
    }
    bool newFile = (fr == FR_NO_FILE);

    fr = f_open(&USERFile, name, FA_OPEN_APPEND | FA_WRITE);
    if (fr == FR_DENIED) return fail(SDLOG_FULL);
    if (fr != FR_OK)     return fail(SDLOG_WRITE_ERROR);

    if (newFile && header != NULL) {
        UINT hl = (UINT)strlen(header);
        fr = f_write(&USERFile, header, hl, &bw);
        if (fr == FR_OK && bw < hl) { f_close(&USERFile); return fail(SDLOG_FULL); }
    }
    if (fr == FR_OK) {
        fr = f_write(&USERFile, line, (UINT)lineLen, &bw);
        if (fr == FR_OK && bw < lineLen) { f_close(&USERFile); return fail(SDLOG_FULL); }
    }
    FRESULT frc = f_close(&USERFile);
    if (fr != FR_OK || frc != FR_OK) {
        return fail(SDLOG_WRITE_ERROR);
    }

    /* Carte presque pleine : signalé avant le blocage complet */
    if (!free_space_ok()) {
        return s_last = SDLOG_FULL;
    }
    return s_last = SDLOG_OK;
}

SdLogStatus SDLog_LastStatus(void)
{
    return s_last;
}

const char *SDLog_StatusStr(SdLogStatus s)
{
    switch (s) {
    case SDLOG_OK:          return "OK";
    case SDLOG_NOT_MOUNTED: return "carte absente ou non FAT32";
    case SDLOG_FULL:        return "carte pleine";
    case SDLOG_WRITE_ERROR: return "erreur d'ecriture";
    default:                return "?";
    }
}

const char *SDLog_Detail(void)
{
    switch (s_lastFr) {
    case FR_OK:
        snprintf(s_detail, sizeof(s_detail), "aucun");
        break;
    case FR_NOT_READY:
    case FR_DISK_ERR:
        snprintf(s_detail, sizeof(s_detail), "FatFs %d : %s",
                 (int)s_lastFr, SD_SPI_LastError());
        break;
    case FR_NO_FILESYSTEM:
        snprintf(s_detail, sizeof(s_detail),
                 "FatFs %d, la carte repond mais n'est pas en FAT32 : la formater en FAT32",
                 (int)s_lastFr);
        break;
    default:
        snprintf(s_detail, sizeof(s_detail), "FatFs code %d", (int)s_lastFr);
        break;
    }
    return s_detail;
}
