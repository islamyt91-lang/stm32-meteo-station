/**
 * @file    sensor_test.c
 * @brief   Test DHT11 (PB0) + BH1750 (I2C1, PB8/PB9) -> UART ST-Link (USART2).
 *
 * Broches (fiche du projet) :
 *   DHT11  : PB0
 *   BH1750 : PB8 (SCL), PB9 (SDA), adresse 0x23 (ADDR a GND)
 *   Serie  : USART2 (PA2 TX / PA3 RX) = port COM virtuel du ST-Link
 */
#include "sensor_test.h"
#include "main.h"
#include <stdarg.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

/* ------------------------------------------------------------------ */
/*  Configuration                                                      */
/* ------------------------------------------------------------------ */
#define DHT11_PORT         GPIOB
#define DHT11_PIN          GPIO_PIN_0
#define BH1750_ADDR        ((uint16_t)(0x23U << 1))
#define TEST_PERIOD_MS     2000U    /* DHT11 : au moins 1 a 2 s entre deux mesures */

extern I2C_HandleTypeDef hi2c1;
UART_HandleTypeDef huart2;          /* USART2 initialise ici (pas par CubeMX) */

/* ------------------------------------------------------------------ */
/*  Sortie serie                                                       */
/* ------------------------------------------------------------------ */
static void out(const char *fmt, ...)
{
    char buf[200];
    va_list ap;
    va_start(ap, fmt);
    int n = vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    if (n <= 0) { return; }
    if ((size_t)n >= sizeof(buf)) { n = (int)sizeof(buf) - 1; }
    (void)HAL_UART_Transmit(&huart2, (uint8_t *)buf, (uint16_t)n, 200U);
}

static void uart2_init(void)
{
    /* PA2/PA3 sont deja en AF7 (MX_GPIO_Init) ; on le refait par securite. */
    __HAL_RCC_GPIOA_CLK_ENABLE();
    GPIO_InitTypeDef g = {0};
    g.Pin       = GPIO_PIN_2 | GPIO_PIN_3;
    g.Mode      = GPIO_MODE_AF_PP;
    g.Pull      = GPIO_NOPULL;
    g.Speed     = GPIO_SPEED_FREQ_VERY_HIGH;
    g.Alternate = GPIO_AF7_USART2;
    HAL_GPIO_Init(GPIOA, &g);

    __HAL_RCC_USART2_CLK_ENABLE();
    huart2.Instance        = USART2;
    huart2.Init.BaudRate   = 115200;
    huart2.Init.WordLength = UART_WORDLENGTH_8B;
    huart2.Init.StopBits   = UART_STOPBITS_1;
    huart2.Init.Parity     = UART_PARITY_NONE;
    huart2.Init.Mode       = UART_MODE_TX_RX;
    huart2.Init.HwFlowCtl  = UART_HWCONTROL_NONE;
    huart2.Init.OverSampling = UART_OVERSAMPLING_16;
    huart2.Init.OneBitSampling = UART_ONE_BIT_SAMPLE_DISABLE;
    huart2.AdvancedInit.AdvFeatureInit = UART_ADVFEATURE_NO_INIT;
    if (HAL_UART_Init(&huart2) != HAL_OK) { Error_Handler(); }
}

/* ------------------------------------------------------------------ */
/*  Temps en microsecondes (compteur de cycles DWT du Cortex-M4)       */
/* ------------------------------------------------------------------ */
static uint32_t s_cyc_per_us = 80U;

static void dwt_init(void)
{
    CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;
    DWT->CYCCNT = 0U;
    DWT->CTRL  |= DWT_CTRL_CYCCNTENA_Msk;
    s_cyc_per_us = SystemCoreClock / 1000000U;
    if (s_cyc_per_us == 0U) { s_cyc_per_us = 1U; }
}

/* ------------------------------------------------------------------ */
/*  DHT11                                                              */
/* ------------------------------------------------------------------ */
#define DHT_LOW()   (DHT11_PORT->BSRR = ((uint32_t)DHT11_PIN << 16U))
#define DHT_HIGH()  (DHT11_PORT->BSRR = (uint32_t)DHT11_PIN)
#define DHT_READ()  ((DHT11_PORT->IDR & DHT11_PIN) != 0U)

typedef enum { DHT_OK = 0, DHT_NO_RESPONSE, DHT_TIMEOUT, DHT_CHECKSUM, DHT_RANGE } DhtResult_t;

static void dht_gpio_init(void)
{
    __HAL_RCC_GPIOB_CLK_ENABLE();
    GPIO_InitTypeDef g = {0};
    g.Pin   = DHT11_PIN;
    g.Mode  = GPIO_MODE_OUTPUT_OD;      /* open-drain : ecrire 1 = relacher la ligne */
    g.Pull  = GPIO_PULLUP;              /* secours : le module a normalement sa pull-up */
    g.Speed = GPIO_SPEED_FREQ_HIGH;
    HAL_GPIO_Init(DHT11_PORT, &g);
    DHT_HIGH();
}

/* Attend un niveau ; false si le delai (en us) est depasse. Boucle bornee. */
static bool wait_level(bool level, uint32_t timeout_us, uint32_t *elapsed_us)
{
    const uint32_t limit = timeout_us * s_cyc_per_us;
    const uint32_t start = DWT->CYCCNT;
    while (DHT_READ() != level) {
        if ((DWT->CYCCNT - start) > limit) { return false; }
    }
    if (elapsed_us != NULL) { *elapsed_us = (DWT->CYCCNT - start) / s_cyc_per_us; }
    return true;
}

static DhtResult_t dht_read(int *temp_c, int *hum_pct, uint8_t raw[5])
{
    for (int i = 0; i < 5; i++) { raw[i] = 0U; }

    /* 1) Signal de depart : ligne a 0 pendant 20 ms (>= 18 ms exiges) */
    DHT_LOW();
    HAL_Delay(20);

    /* 2) Lecture de la trame : interruptions coupees (~5 ms max), attentes bornees */
    uint32_t primask = __get_PRIMASK();
    __disable_irq();
    DHT_HIGH();

    DhtResult_t r = DHT_OK;
    uint32_t t = 0U;
    if (!wait_level(false, 100U, &t))      { r = DHT_NO_RESPONSE; }
    else if (!wait_level(true, 120U, &t))  { r = DHT_TIMEOUT; }
    else if (!wait_level(false, 120U, &t)) { r = DHT_TIMEOUT; }
    else {
        for (uint32_t i = 0U; i < 40U; i++) {
            if (!wait_level(true, 100U, &t))  { r = DHT_TIMEOUT; break; }   /* ~50 us bas */
            if (!wait_level(false, 120U, &t)) { r = DHT_TIMEOUT; break; }   /* haut : 26 us = 0, 70 us = 1 */
            raw[i / 8U] = (uint8_t)(raw[i / 8U] << 1U);
            if (t > 40U) { raw[i / 8U] |= 1U; }
        }
    }
    __set_PRIMASK(primask);
    DHT_HIGH();
    if (r != DHT_OK) { return r; }

    /* 3) Verification du checksum et de la plausibilite */
    if ((uint8_t)(raw[0] + raw[1] + raw[2] + raw[3]) != raw[4]) { return DHT_CHECKSUM; }
    int t_c = (int)raw[2];
    if ((raw[3] & 0x80U) != 0U) { t_c = -t_c; }
    if (raw[0] > 100U || t_c < -40 || t_c > 80) { return DHT_RANGE; }

    *hum_pct = (int)raw[0];
    *temp_c  = t_c;
    return DHT_OK;
}

static const char *dht_err_text(DhtResult_t r)
{
    switch (r) {
    case DHT_NO_RESPONSE: return "pas de reponse (verifier PB0, alimentation 3,3 V, pull-up)";
    case DHT_TIMEOUT:     return "trame incomplete (timeout)";
    case DHT_CHECKSUM:    return "checksum incorrect (fils trop longs ? parasites ?)";
    case DHT_RANGE:       return "valeurs impossibles";
    default:              return "?";
    }
}

/* ------------------------------------------------------------------ */
/*  BH1750                                                             */
/* ------------------------------------------------------------------ */
static bool s_bh_ok = false;

static bool bh1750_init(void)
{
    uint8_t cmd = 0x01U;                                   /* power on */
    if (HAL_I2C_Master_Transmit(&hi2c1, BH1750_ADDR, &cmd, 1U, 50U) != HAL_OK) { return false; }
    cmd = 0x10U;                                           /* mesure continue, resolution 1 lx */
    if (HAL_I2C_Master_Transmit(&hi2c1, BH1750_ADDR, &cmd, 1U, 50U) != HAL_OK) { return false; }
    HAL_Delay(200);                                        /* 1re mesure : ~180 ms max */
    return true;
}

static bool bh1750_read(uint16_t *raw, uint32_t *lux_x10)
{
    uint8_t b[2] = {0U, 0U};
    if (HAL_I2C_Master_Receive(&hi2c1, BH1750_ADDR, b, 2U, 50U) != HAL_OK) { return false; }
    uint16_t r = (uint16_t)(((uint16_t)b[0] << 8U) | b[1]);
    *raw = r;
    *lux_x10 = ((uint32_t)r * 100U) / 12U;                 /* lux = brut / 1,2 ; resultat x10 */
    return true;
}

static void i2c_scan(void)
{
    out("[I2C] Scan du bus I2C1 :\r\n");
    int found = 0;
    for (uint16_t a = 0x08U; a <= 0x77U; a++) {
        if (HAL_I2C_IsDeviceReady(&hi2c1, (uint16_t)(a << 1U), 1U, 5U) == HAL_OK) {
            out("[I2C]   peripherique trouve en 0x%02X%s\r\n", (unsigned)a,
                (a == 0x23U || a == 0x5CU) ? "  (BH1750)" : "");
            found++;
        }
    }
    if (found == 0) { out("[I2C]   AUCUN peripherique (verifier SDA=PB9, SCL=PB8, alimentation)\r\n"); }
}

/* ------------------------------------------------------------------ */
/*  Programme de test                                                  */
/* ------------------------------------------------------------------ */
static uint32_t s_last = 0U;
static uint32_t s_count = 0U;

void SensorTest_Init(void)
{
    uart2_init();
    dwt_init();
    dht_gpio_init();

    out("\r\n========================================\r\n");
    out(" TEST CAPTEURS : DHT11 (PB0) + BH1750 (I2C1)\r\n");
    out("========================================\r\n");

    HAL_Delay(50);
    out("[DHT11] Ligne au repos : %s\r\n",
        DHT_READ() ? "HAUT (normal)" : "BAS (anormal : verifier cablage / pull-up)");

    i2c_scan();
    s_bh_ok = bh1750_init();
    out("[BH1750] Initialisation : %s\r\n", s_bh_ok ? "OK" : "ECHEC (pas de reponse a 0x23)");
    out("----------------------------------------\r\n");

    s_last = HAL_GetTick();
}

void SensorTest_Process(void)
{
    uint32_t now = HAL_GetTick();
    if ((now - s_last) < TEST_PERIOD_MS) { return; }
    s_last = now;
    s_count++;

    out("--- Mesure %lu ---\r\n", (unsigned long)s_count);
    bool all_ok = true;

    /* ----- DHT11 ----- */
    int t = 0, h = 0;
    uint8_t raw[5];
    DhtResult_t r = dht_read(&t, &h, raw);
    if (r == DHT_OK) {
        out("[DHT11]  Temperature : %d C | Humidite : %d %%   (trame %02X %02X %02X %02X %02X)\r\n",
            t, h, raw[0], raw[1], raw[2], raw[3], raw[4]);
        if (t < 0 || t > 50 || h < 20 || h > 90) {
            out("[DHT11]  note : hors plage garantie du capteur (0-50 C, 20-90 %%)\r\n");
        }
    } else {
        all_ok = false;
        out("[DHT11]  ERREUR : %s\r\n", dht_err_text(r));
    }

    /* ----- BH1750 ----- */
    uint16_t braw = 0U;
    uint32_t lux10 = 0U;
    if (!s_bh_ok) { s_bh_ok = bh1750_init(); }            /* nouvelle tentative */
    if (s_bh_ok && bh1750_read(&braw, &lux10)) {
        out("[BH1750] Luminosite  : %lu.%lu lux   (brut %u)\r\n",
            (unsigned long)(lux10 / 10U), (unsigned long)(lux10 % 10U), (unsigned)braw);
    } else {
        all_ok = false;
        s_bh_ok = false;
        out("[BH1750] ERREUR I2C : pas de reponse (SDA=PB9, SCL=PB8, ADDR a GND, 3,3 V)\r\n");
    }

    /* LED verte de la carte (LD2, PA5) : clignote court = tout OK, long = erreur */
    HAL_GPIO_WritePin(GPIOA, GPIO_PIN_5, GPIO_PIN_SET);
    HAL_Delay(all_ok ? 80U : 400U);
    HAL_GPIO_WritePin(GPIOA, GPIO_PIN_5, GPIO_PIN_RESET);
}
