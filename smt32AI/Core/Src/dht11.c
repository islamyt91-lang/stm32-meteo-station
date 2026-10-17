/**
 * @file  dht11.c
 * @brief Pilote DHT11.
 *
 * La broche est configurée en sortie OPEN-DRAIN avec pull-up : écrire 0 tire
 * la ligne à la masse, écrire 1 la relâche, et le registre IDR permet de lire
 * le niveau imposé par le capteur sans changer le mode de la broche.
 *
 * Trame (datasheet) :
 *   MCU : niveau bas >= 18 ms, puis relâche la ligne
 *   DHT : 80 µs bas + 80 µs haut (réponse)
 *   40 bits : 50 µs bas puis haut de 26-28 µs ("0") ou ~70 µs ("1")
 *   Octets : HR entier, HR décimal, T entier, T décimal, checksum
 *
 * Chaque attente de front est bornée (timeout en µs mesuré par DWT) : la
 * fonction ne peut jamais bloquer plus de ~25 ms.
 */
#include "dht11.h"
#include "www_config.h"

static uint32_t     s_lastAttemptMs;
static DHT11_Status s_lastStatus = DHT11_ERR_NO_RESPONSE;

/* ------------------------------------------------------------------------ */
static void dwt_init(void)
{
    CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;
    DWT->CYCCNT = 0;
    DWT->CTRL |= DWT_CTRL_CYCCNTENA_Msk;
}

static inline uint32_t cycles_per_us(void)
{
    return SystemCoreClock / 1000000u;
}

void DWT_DelayUs(uint32_t us)
{
    uint32_t start = DWT->CYCCNT;
    uint32_t ticks = us * cycles_per_us();
    while ((DWT->CYCCNT - start) < ticks) { }
}

static inline uint32_t line_level(void)
{
    return (DHT11_PORT->IDR & DHT11_PIN) ? 1u : 0u;
}

static inline void line_low(void)     { DHT11_PORT->BSRR = (uint32_t)DHT11_PIN << 16; }
static inline void line_release(void) { DHT11_PORT->BSRR = (uint32_t)DHT11_PIN; }

/**
 * Attend que la ligne prenne la valeur `level`.
 * @return durée d'attente en µs, ou -1 si timeout.
 */
static int32_t wait_level(uint32_t level, uint32_t timeout_us)
{
    uint32_t start = DWT->CYCCNT;
    uint32_t limit = timeout_us * cycles_per_us();
    while (line_level() != level) {
        if ((DWT->CYCCNT - start) > limit) {
            return -1;
        }
    }
    return (int32_t)((DWT->CYCCNT - start) / cycles_per_us());
}

/* ------------------------------------------------------------------------ */
void DHT11_Init(void)
{
    GPIO_InitTypeDef g = {0};

    dwt_init();

    line_release();
    g.Pin   = DHT11_PIN;
    g.Mode  = GPIO_MODE_OUTPUT_OD;
    g.Pull  = GPIO_PULLUP;          /* en plus de la résistance du module */
    g.Speed = GPIO_SPEED_FREQ_HIGH;
    HAL_GPIO_Init(DHT11_PORT, &g);

    /* Le capteur demande ~1 s de stabilisation après mise sous tension :
     * première lecture autorisée 1 s après l'init. */
    s_lastAttemptMs = HAL_GetTick() - (DHT11_MIN_INTERVAL_MS - 1000u);
}

DHT11_Status DHT11_Read(DHT11_Data *out)
{
    uint8_t  b[5] = {0};
    uint32_t now = HAL_GetTick();

    if ((now - s_lastAttemptMs) < DHT11_MIN_INTERVAL_MS) {
        return DHT11_BUSY;
    }
    s_lastAttemptMs = now;

    /* 1. Signal de start : 20 ms à l'état bas */
    line_low();
    HAL_Delay(20);
    line_release();

    /* 2. Réponse du capteur : bas ~80 µs puis haut ~80 µs */
    if (wait_level(0, 200) < 0) { return s_lastStatus = DHT11_ERR_NO_RESPONSE; }
    if (wait_level(1, 150) < 0) { return s_lastStatus = DHT11_ERR_NO_RESPONSE; }
    if (wait_level(0, 150) < 0) { return s_lastStatus = DHT11_ERR_TIMEOUT; }

    /* 3. 40 bits de données */
    for (int i = 0; i < 40; i++) {
        if (wait_level(1, 100) < 0) {           /* fin des 50 µs à l'état bas */
            return s_lastStatus = DHT11_ERR_TIMEOUT;
        }
        int32_t high_us = wait_level(0, 120);   /* durée de l'état haut */
        if (high_us < 0) {
            return s_lastStatus = DHT11_ERR_TIMEOUT;
        }
        b[i / 8] <<= 1;
        if (high_us > 45) {                     /* 26-28 µs -> 0, ~70 µs -> 1 */
            b[i / 8] |= 1u;
        }
    }

    /* 4. Checksum */
    if ((uint8_t)(b[0] + b[1] + b[2] + b[3]) != b[4]) {
        return s_lastStatus = DHT11_ERR_CHECKSUM;
    }

    if (out != NULL) {
        out->hum_x10 = (int16_t)(b[0] * 10 + (b[1] % 10));
        /* Certains DHT11 récents fournissent le dixième de degré dans b[3]
         * (bit 7 = signe négatif). Les anciens envoient 0 : sans effet. */
        int16_t t = (int16_t)(b[2] * 10 + (b[3] & 0x0F) % 10);
        out->temp_x10 = (b[3] & 0x80) ? (int16_t)-t : t;
    }
    return s_lastStatus = DHT11_OK;
}

const char *DHT11_StatusStr(DHT11_Status s)
{
    switch (s) {
    case DHT11_OK:              return "OK";
    case DHT11_BUSY:            return "occupe (<2 s)";
    case DHT11_ERR_NO_RESPONSE: return "pas de reponse";
    case DHT11_ERR_TIMEOUT:     return "trame interrompue";
    case DHT11_ERR_CHECKSUM:    return "checksum faux";
    default:                    return "?";
    }
}
