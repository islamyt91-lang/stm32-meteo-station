/**
 * @file  settings.c
 * @brief Paramètres + émulation d'EEPROM dans la dernière page de Flash.
 */
#include "settings.h"
#include "www_config.h"
#include <string.h>

#define SETTINGS_FLASH_ADDR   0x080FF800u   /* dernière page de 2 Ko */
#define SETTINGS_FLASH_BANK   FLASH_BANK_2
#define SETTINGS_FLASH_PAGE   255u
#define SETTINGS_MAGIC        0x57575731u   /* "WWW1" */
#define SETTINGS_LAYOUT       1u            /* à incrémenter si Settings change */

typedef struct {
    uint32_t magic;
    uint32_t layout;
    Settings data;
    uint32_t crc;
} Record;

/* Taille arrondie au multiple de 8 octets (programmation par double mot) */
#define RECORD_DWORDS  ((sizeof(Record) + 7u) / 8u)

Settings g_settings;

void Settings_Defaults(Settings *s)
{
    s->log_interval_s = WWW_DEFAULT_LOG_INTERVAL_S;
    s->file_max_size  = 2048u;
    s->timeout_s      = 30u;

    /* Seuils en LUX (BH1750 : 0 à 65535 lx). Repères : bureau ~300-500 lx,
     * ciel couvert ~1000-10000 lx, plein soleil > 30000 lx. */
    s->lumin      = 1u;
    s->lumin_low  = 500u;
    s->lumin_high = 10000u;

    s->temp_air     = 1u;
    s->min_temp_air = -10;
    s->max_temp_air = 60;

    s->hygr      = 1u;
    s->hygr_mint = 0;
    s->hygr_maxt = 50;
}

static uint32_t crc32(const uint8_t *p, size_t n)
{
    uint32_t crc = 0xFFFFFFFFu;
    while (n--) {
        crc ^= *p++;
        for (int k = 0; k < 8; k++) {
            crc = (crc >> 1) ^ (0xEDB88320u & (uint32_t)-(int32_t)(crc & 1u));
        }
    }
    return ~crc;
}

static bool valid(const Settings *s)
{
    return s->log_interval_s >= LOG_INTERVAL_MIN_S &&
           s->log_interval_s <= LOG_INTERVAL_MAX_MIN * 60u &&
           s->timeout_s >= TIMEOUT_MIN_S && s->timeout_s <= TIMEOUT_MAX_S &&
           s->lumin <= 1u && s->temp_air <= 1u && s->hygr <= 1u &&
           s->lumin_low <= s->lumin_high && s->lumin_high <= LUMIN_MAX_LUX &&
           s->min_temp_air <= s->max_temp_air &&
           s->hygr_mint <= s->hygr_maxt;
}

bool Settings_Load(void)
{
    const Record *r = (const Record *)SETTINGS_FLASH_ADDR;

    if (r->magic == SETTINGS_MAGIC && r->layout == SETTINGS_LAYOUT &&
        r->crc == crc32((const uint8_t *)&r->data, sizeof(Settings)) &&
        valid(&r->data)) {
        g_settings = r->data;
        return true;
    }
    Settings_Defaults(&g_settings);
    return false;
}

bool Settings_Save(void)
{
    uint64_t buf[RECORD_DWORDS];
    Record   rec;
    FLASH_EraseInitTypeDef erase = {0};
    uint32_t pageErr = 0;
    bool ok = true;

    memset(&rec, 0, sizeof(rec));
    rec.magic  = SETTINGS_MAGIC;
    rec.layout = SETTINGS_LAYOUT;
    rec.data   = g_settings;
    rec.crc    = crc32((const uint8_t *)&rec.data, sizeof(Settings));

    memset(buf, 0xFF, sizeof(buf));
    memcpy(buf, &rec, sizeof(rec));

    HAL_FLASH_Unlock();
    __HAL_FLASH_CLEAR_FLAG(FLASH_FLAG_ALL_ERRORS);

    erase.TypeErase = FLASH_TYPEERASE_PAGES;
    erase.Banks     = SETTINGS_FLASH_BANK;
    erase.Page      = SETTINGS_FLASH_PAGE;
    erase.NbPages   = 1;
    if (HAL_FLASHEx_Erase(&erase, &pageErr) != HAL_OK) {
        ok = false;
    }

    for (uint32_t i = 0; ok && i < RECORD_DWORDS; i++) {
        if (HAL_FLASH_Program(FLASH_TYPEPROGRAM_DOUBLEWORD,
                              SETTINGS_FLASH_ADDR + i * 8u, buf[i]) != HAL_OK) {
            ok = false;
        }
    }
    HAL_FLASH_Lock();

    /* Vérification de relecture */
    if (ok && memcmp((const void *)SETTINGS_FLASH_ADDR, &rec, sizeof(rec)) != 0) {
        ok = false;
    }
    return ok;
}
