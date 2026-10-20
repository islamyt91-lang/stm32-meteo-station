/**
 * @file  sd_spi.c
 * @brief Carte SD en mode SPI.
 *
 * Séquence d'initialisation (spécification SD simplifiée) :
 *   - >= 74 coups d'horloge avec CS haut, SPI lent (<= 400 kHz)
 *   - CMD0  : passage en mode SPI (réponse 0x01 = idle), jusqu'à 10 essais
 *   - CMD8  : test de tension 0x1AA -> carte SD v2 (sinon v1)
 *   - ACMD41: sortie de l'état idle (bit HCS pour SDHC/SDXC)
 *   - CMD58 : lecture OCR, bit CCS = adressage par bloc (SDHC)
 *   - CMD16 : bloc de 512 octets (cartes à adressage par octet)
 * Puis le SPI passe à 1,25 MHz (marge pour des fils Dupont et une breadboard).
 *
 * Toutes les attentes sont bornées par HAL_GetTick : une carte absente ou
 * mal câblée ne peut jamais bloquer le programme.
 */
#include "sd_spi.h"
#include "www_config.h"
#include <stdio.h>

#define CMD0    0u
#define CMD8    8u
#define CMD9    9u
#define CMD12   12u
#define CMD16   16u
#define CMD17   17u
#define CMD18   18u
#define CMD24   24u
#define CMD25   25u
#define CMD55   55u
#define CMD58   58u
#define ACMD41  (0x80u | 41u)

#define TOKEN_START_SINGLE  0xFEu
#define TOKEN_START_MULTI   0xFCu
#define TOKEN_STOP_MULTI    0xFDu

static SPI_HandleTypeDef *s_hspi;
static bool    s_ready;
static bool    s_blockAddr;     /* SDHC/SDXC : adresse = numéro de secteur */
static const char *s_err = "aucune";
static char    s_msg[160];

/* ------------------------------------------------------------------------ */
static inline void cs_low(void)  { HAL_GPIO_WritePin(SD_CS_PORT, SD_CS_PIN, GPIO_PIN_RESET); }
static inline void cs_high(void) { HAL_GPIO_WritePin(SD_CS_PORT, SD_CS_PIN, GPIO_PIN_SET); }

static uint8_t xchg(uint8_t b)
{
    uint8_t r = 0xFF;
    if (HAL_SPI_TransmitReceive(s_hspi, &b, &r, 1, 10) != HAL_OK) {
        return 0xFF;
    }
    return r;
}

static void set_speed(uint32_t prescaler)
{
    s_hspi->Init.BaudRatePrescaler = prescaler;
    HAL_SPI_Init(s_hspi);
}

static bool wait_ready(uint32_t timeout_ms)
{
    uint32_t t0 = HAL_GetTick();
    do {
        if (xchg(0xFF) == 0xFF) return true;
    } while ((HAL_GetTick() - t0) < timeout_ms);
    return false;
}

static void deselect(void)
{
    cs_high();
    xchg(0xFF);           /* 8 coups d'horloge : la carte libère MISO */
}

static bool select_card(void)
{
    cs_low();
    xchg(0xFF);
    if (wait_ready(500)) return true;
    deselect();
    return false;
}

/** Envoie une commande, retourne la réponse R1 (bit 7 = 1 : pas de réponse). */
static uint8_t send_cmd(uint8_t cmd, uint32_t arg)
{
    uint8_t res;

    if (cmd & 0x80u) {                    /* ACMDn = CMD55 + CMDn */
        cmd &= 0x7Fu;
        res = send_cmd(CMD55, 0);
        if (res > 1u) return res;
    }

    if (cmd == CMD0) {
        /* Avant CMD0 la carte est encore en mode SD natif : sa ligne de
         * données peut rester basse, on n'attend donc pas l'état "prête". */
        deselect();
        cs_low();
        xchg(0xFF);
    } else if (cmd != CMD12) {
        deselect();
        if (!select_card()) return 0xFF;
    }

    xchg((uint8_t)(0x40u | cmd));
    xchg((uint8_t)(arg >> 24));
    xchg((uint8_t)(arg >> 16));
    xchg((uint8_t)(arg >> 8));
    xchg((uint8_t)arg);
    uint8_t crc = 0x01;                   /* CRC ignoré en SPI sauf CMD0/CMD8 */
    if (cmd == CMD0) crc = 0x95;
    if (cmd == CMD8) crc = 0x87;
    xchg(crc);

    if (cmd == CMD12) xchg(0xFF);         /* octet de bourrage */

    uint8_t n = 10;
    do {
        res = xchg(0xFF);
    } while ((res & 0x80u) && --n);
    return res;
}

static uint8_t s_lastToken = 0xFF;

static bool rx_block(uint8_t *buf, uint32_t len)
{
    static const uint8_t ff[512] = { [0 ... 511] = 0xFF };
    uint32_t t0 = HAL_GetTick();
    uint8_t token;

    do {
        token = xchg(0xFF);
    } while (token == 0xFF && (HAL_GetTick() - t0) < 300u);
    s_lastToken = token;
    if (token != TOKEN_START_SINGLE) return false;

    if (HAL_SPI_TransmitReceive(s_hspi, (uint8_t *)ff, buf, (uint16_t)len, 100) != HAL_OK) {
        return false;
    }
    xchg(0xFF);                           /* CRC ignoré */
    xchg(0xFF);
    return true;
}

static bool tx_block(const uint8_t *buf, uint8_t token)
{
    if (!wait_ready(500)) return false;
    xchg(token);
    if (token == TOKEN_STOP_MULTI) return true;

    if (HAL_SPI_Transmit(s_hspi, (uint8_t *)buf, 512, 100) != HAL_OK) return false;
    xchg(0xFF);                           /* CRC factice */
    xchg(0xFF);
    uint8_t resp = xchg(0xFF);
    return (resp & 0x1Fu) == 0x05u;       /* données acceptées */
}

/* ------------------------------------------------------------------------ */
void SD_SPI_Attach(SPI_HandleTypeDef *hspi)
{
    s_hspi = hspi;
    s_ready = false;
    cs_high();
}

static bool sd_init_once(void);

bool SD_SPI_Init(void)
{
    s_ready = false;
    if (s_hspi == NULL) {
        s_err = "SPI non initialise";
        return false;
    }
    /* Jusqu'à 3 tentatives complètes : les cartes bon marché et les fils
     * Dupont donnent parfois un premier essai raté. */
    for (int attempt = 0; attempt < 3; attempt++) {
        if (sd_init_once()) {
            return true;
        }
        cs_high();
        for (int i = 0; i < 20; i++) xchg(0xFF);
        HAL_Delay(100);
    }
    return false;
}

static bool sd_init_once(void)
{
    uint8_t ocr[4];

    HAL_Delay(250);                        /* stabilisation de l'alimentation du module */
    set_speed(SPI_BAUDRATEPRESCALER_256);  /* 80 MHz / 256 = 312 kHz */
    cs_high();
    for (int i = 0; i < 20; i++) xchg(0xFF);  /* 160 coups d'horloge, CS haut */

    /* CMD0 : certaines cartes demandent plusieurs essais */
    uint8_t r0 = 0xFF;
    uint8_t seen_or = 0x00, seen_and = 0xFF;
    for (int tries = 0; tries < 10 && r0 != 0x01; tries++) {
        r0 = send_cmd(CMD0, 0);
        seen_or |= r0;
        seen_and &= r0;
        if (r0 != 0x01) {
            deselect();
            for (int i = 0; i < 10; i++) xchg(0xFF);
            HAL_Delay(10);
        }
    }
    if (r0 != 0x01) {
        deselect();
        if (seen_or == 0x00) {
            s_err = "reponse toujours 0x00 : carte absente, ou MISO (D12) tire a 0 "
                    "(module non alimente en 5V, MISO touche un GND)";
        } else if (seen_and == 0xFF) {
            s_err = "aucune reponse (MISO reste a 1) : carte absente, ou verifier SCK (D13), "
                    "CS (D10), MOSI (D11), VCC 5V et GND";
        } else {
            snprintf(s_msg, sizeof(s_msg), "reponses incoherentes a CMD0 (derniere 0x%02X) : "
                     "verifier MOSI (D11), SCK (D13) et les masses", r0);
            s_err = s_msg;
        }
        return false;
    }

    uint32_t t0 = HAL_GetTick();
    bool sdv2 = false;

    if (send_cmd(CMD8, 0x1AA) == 0x01) {
        for (int i = 0; i < 4; i++) ocr[i] = xchg(0xFF);
        if (ocr[2] != 0x01 || ocr[3] != 0xAA) {   /* tension non supportée */
            deselect();
            s_err = "CMD8 : tension refusee par la carte";
            return false;
        }
        sdv2 = true;
    }

    /* ACMD41 jusqu'à sortie de l'état idle (max 3 s, certaines cartes
     * dépassent la seconde prévue par la norme) */
    uint8_t r;
    do {
        r = send_cmd(ACMD41, sdv2 ? (1uL << 30) : 0);
        if (r != 0x00) HAL_Delay(10);
    } while (r != 0x00 && (HAL_GetTick() - t0) < 3000u);
    if (r != 0x00) {
        deselect();
        s_err = "ACMD41 : la carte ne sort pas de l'etat d'attente (alimentation faible ou carte incompatible)";
        return false;
    }

    s_blockAddr = false;
    if (sdv2) {
        /* CMD58 (lecture OCR) : 3 essais */
        uint8_t r58 = 0xFF;
        for (int k = 0; k < 3 && r58 != 0x00; k++) {
            r58 = send_cmd(CMD58, 0);
            if (r58 != 0x00) { deselect(); HAL_Delay(5); }
        }
        if (r58 == 0x00) {
            for (int i = 0; i < 4; i++) ocr[i] = xchg(0xFF);
            s_blockAddr = (ocr[0] & 0x40u) != 0;  /* bit CCS */
        } else {
            /* Repli : certaines cartes refusent CMD58 en SPI. Le type est
             * alors lu dans le registre CSD (version 2 = SDHC/SDXC). */
            uint8_t csd[16];
            deselect();
            if (send_cmd(CMD9, 0) == 0x00 && rx_block(csd, 16)) {
                s_blockAddr = ((csd[0] >> 6) == 1u);
            } else {
                deselect();
                snprintf(s_msg, sizeof(s_msg), "CMD58 (reponse 0x%02X) et CMD9 en echec", r58);
                s_err = s_msg;
                return false;
            }
        }
    }
    if (!s_blockAddr && send_cmd(CMD16, 512) != 0x00) {
        deselect();
        s_err = "CMD16 en echec";
        return false;
    }
    deselect();
    s_err = "aucune";

    set_speed(SPI_BAUDRATEPRESCALER_64);   /* 1,25 MHz : sûr avec des fils Dupont */
    s_ready = true;
    return true;
}

static bool read_one(uint8_t *buf, uint32_t addr, uint32_t sector)
{
    for (int attempt = 0; attempt < 3; attempt++) {
        uint8_t r = send_cmd(CMD17, addr);
        if (r == 0x00 && rx_block(buf, 512)) {
            deselect();
            return true;
        }
        deselect();
        snprintf(s_msg, sizeof(s_msg), "lecture du secteur %lu en echec (CMD17 0x%02X, jeton 0x%02X) : "
                 "liaison SPI instable, raccourcir les fils", (unsigned long)sector, r, s_lastToken);
        s_err = s_msg;
        HAL_Delay(2);
    }
    return false;
}

bool SD_SPI_Read(uint8_t *buf, uint32_t sector, uint32_t count)
{
    bool ok = true;
    if (!s_ready || count == 0) return false;
    uint32_t step = s_blockAddr ? 1u : 512u;

    /* Secteur par secteur, avec 3 essais chacun */
    for (uint32_t i = 0; i < count; i++) {
        if (!read_one(buf + 512u * i, (sector + i) * step, sector + i)) {
            ok = false;
            break;
        }
    }
    if (!ok) s_ready = false;
    return ok;
}

bool SD_SPI_Write(const uint8_t *buf, uint32_t sector, uint32_t count)
{
    bool ok = true;
    if (!s_ready || count == 0) return false;
    uint32_t step = s_blockAddr ? 1u : 512u;

    for (uint32_t i = 0; i < count && ok; i++) {
        ok = false;
        for (int attempt = 0; attempt < 3 && !ok; attempt++) {
            ok = (send_cmd(CMD24, (sector + i) * step) == 0x00) &&
                 tx_block(buf + 512u * i, TOKEN_START_SINGLE);
            deselect();
        }
        if (!ok) {
            snprintf(s_msg, sizeof(s_msg), "ecriture du secteur %lu en echec : liaison SPI instable",
                     (unsigned long)(sector + i));
            s_err = s_msg;
        }
    }
    if (!ok) s_ready = false;
    return ok;
}

bool SD_SPI_Sync(void)
{
    if (!s_ready) return false;
    bool ok = select_card();
    deselect();
    return ok;
}

uint32_t SD_SPI_SectorCount(void)
{
    uint8_t csd[16];
    uint32_t n = 0;

    if (!s_ready) return 0;
    if (send_cmd(CMD9, 0) == 0x00 && rx_block(csd, 16)) {
        if ((csd[0] >> 6) == 1) {                 /* CSD v2 (SDHC/SDXC) */
            uint32_t c_size = ((uint32_t)(csd[7] & 0x3Fu) << 16) |
                              ((uint32_t)csd[8] << 8) | csd[9];
            n = (c_size + 1u) << 10;
        } else {                                  /* CSD v1 */
            uint32_t c_size = ((uint32_t)(csd[6] & 0x03u) << 10) |
                              ((uint32_t)csd[7] << 2) | (csd[8] >> 6);
            uint32_t mult = ((csd[9] & 0x03u) << 1) | (csd[10] >> 7);
            uint32_t rbl  = csd[5] & 0x0Fu;
            n = (c_size + 1u) << (mult + 2u + rbl - 9u);
        }
    }
    deselect();
    return n;
}

bool SD_SPI_IsReady(void)           { return s_ready; }
void SD_SPI_MarkNotReady(void)      { s_ready = false; }
const char *SD_SPI_LastError(void)  { return s_err; }
