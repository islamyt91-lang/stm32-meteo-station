/**
 * @file  bh1750.c
 * @brief Pilote BH1750.
 *
 * Commandes utilisées (datasheet) :
 *   0x01 Power On, 0x07 Reset, 0x10 mode continu haute résolution (1 lx,
 *   120 ms typique / 180 ms max). Valeur lue sur 16 bits, lux = brut / 1.2.
 *
 * Gestion d'erreurs : toute transaction I2C a un timeout court (pas de
 * HAL_MAX_DELAY). Après une erreur, le pilote se marque "non initialisé" ; à
 * la lecture suivante il réinitialise le périphérique I2C (débloque un bus
 * resté occupé) puis le capteur.
 */
#include "bh1750.h"

#define BH1750_CMD_POWER_ON      0x01u
#define BH1750_CMD_RESET         0x07u
#define BH1750_CMD_CONT_HRES     0x10u
#define BH1750_MEAS_TIME_MS      180u
#define BH1750_I2C_TIMEOUT_MS    20u

static I2C_HandleTypeDef *s_hi2c;
static uint16_t s_addr8;          /* adresse décalée pour la HAL */
static uint8_t  s_ready;          /* capteur configuré */
static uint32_t s_initTick;
static uint32_t s_lastErr;

static HAL_StatusTypeDef send_cmd(uint8_t cmd)
{
    HAL_StatusTypeDef st = HAL_I2C_Master_Transmit(s_hi2c, s_addr8, &cmd, 1,
                                                   BH1750_I2C_TIMEOUT_MS);
    if (st != HAL_OK) {
        s_lastErr = HAL_I2C_GetError(s_hi2c);
    }
    return st;
}

static void recover_bus(void)
{
    /* Réinitialise le périphérique I2C (les paramètres de hi2c->Init sont
     * conservés), utile après un blocage SDA ou un état HAL_BUSY. */
    HAL_I2C_DeInit(s_hi2c);
    HAL_I2C_Init(s_hi2c);
}

static BH1750_Status configure(void)
{
    s_ready = 0;
    if (HAL_I2C_IsDeviceReady(s_hi2c, s_addr8, 2, BH1750_I2C_TIMEOUT_MS) != HAL_OK) {
        s_lastErr = HAL_I2C_GetError(s_hi2c);
        return BH1750_ERR_I2C;
    }
    if (send_cmd(BH1750_CMD_POWER_ON) != HAL_OK) return BH1750_ERR_I2C;
    if (send_cmd(BH1750_CMD_RESET)    != HAL_OK) return BH1750_ERR_I2C;
    if (send_cmd(BH1750_CMD_CONT_HRES) != HAL_OK) return BH1750_ERR_I2C;
    s_ready = 1;
    s_initTick = HAL_GetTick();
    return BH1750_OK;
}

BH1750_Status BH1750_Init(I2C_HandleTypeDef *hi2c, uint8_t addr7)
{
    s_hi2c  = hi2c;
    s_addr8 = (uint16_t)(addr7 << 1);
    s_lastErr = 0;
    return configure();
}

BH1750_Status BH1750_ReadLux(uint32_t *lux_x10)
{
    uint8_t rx[2];

    if (s_hi2c == NULL) {
        return BH1750_ERR_I2C;
    }
    if (!s_ready) {
        recover_bus();
        if (configure() != BH1750_OK) {
            return BH1750_ERR_I2C;
        }
    }
    if ((HAL_GetTick() - s_initTick) < BH1750_MEAS_TIME_MS) {
        return BH1750_NOT_READY;
    }

    if (HAL_I2C_Master_Receive(s_hi2c, s_addr8, rx, 2, BH1750_I2C_TIMEOUT_MS) != HAL_OK) {
        s_lastErr = HAL_I2C_GetError(s_hi2c);
        s_ready = 0;            /* reconfiguration à la prochaine lecture */
        return BH1750_ERR_I2C;
    }

    uint32_t raw = ((uint32_t)rx[0] << 8) | rx[1];
    if (lux_x10 != NULL) {
        *lux_x10 = (raw * 100u) / 12u;   /* lux*10 = brut*10/1.2 */
    }
    return BH1750_OK;
}

uint32_t BH1750_LastI2CError(void)
{
    return s_lastErr;
}
