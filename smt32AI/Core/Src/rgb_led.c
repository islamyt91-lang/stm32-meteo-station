/**
 * @file  rgb_led.c
 * @brief LED RGB : couleurs du mode + clignotements d'erreur.
 *
 * Les broches sont de simples GPIO (PA4 n'a pas de canal timer), donc le
 * mélange de couleurs se fait par PWM logiciel : 10 pas de 1 ms -> 100 Hz,
 * sans scintillement visible.
 */
#include "rgb_led.h"
#include "www_config.h"

#define PWM_STEPS  10u

/* Rapport cyclique (0..10) de chaque canal pour chaque couleur.
 * À ajuster selon la LED (le vert est souvent bien plus lumineux). */
static const uint8_t kColorTable[RGB_COLOR_COUNT][3] = {
    /*            R   G   B  */
    [RGB_OFF]    = { 0,  0,  0},
    [RGB_RED]    = {10,  0,  0},
    [RGB_GREEN]  = { 0, 10,  0},
    [RGB_BLUE]   = { 0,  0, 10},
    [RGB_YELLOW] = {10,  5,  0},
    [RGB_ORANGE] = {10,  2,  0},
    [RGB_WHITE]  = {10,  7,  7},
};

static volatile RgbColor s_c1 = RGB_OFF, s_c2 = RGB_OFF;
static volatile uint16_t s_ms1 = 0, s_ms2 = 0;   /* s_ms2 == 0 -> couleur fixe */
static volatile uint16_t s_phaseMs = 0;
static volatile uint8_t  s_pwmStep = 0;

static inline void write_channel(GPIO_TypeDef *port, uint16_t pin, uint8_t on)
{
#if RGB_COMMON_ANODE
    on = !on;
#endif
    /* Accès direct au registre BSRR : rapide et sûr depuis une IT */
    port->BSRR = on ? (uint32_t)pin : ((uint32_t)pin << 16);
}

static void apply(RgbColor c, uint8_t step)
{
    const uint8_t *d = kColorTable[c];
    write_channel(RGB_R_PORT, RGB_R_PIN, step < d[0]);
    write_channel(RGB_G_PORT, RGB_G_PIN, step < d[1]);
    write_channel(RGB_B_PORT, RGB_B_PIN, step < d[2]);
}

static void config_pin(GPIO_TypeDef *port, uint16_t pin)
{
    GPIO_InitTypeDef g = {0};
    g.Pin   = pin;
    g.Mode  = GPIO_MODE_OUTPUT_PP;
    g.Pull  = GPIO_NOPULL;
    g.Speed = GPIO_SPEED_FREQ_LOW;
    HAL_GPIO_Init(port, &g);
}

void RGB_Init(void)
{
    /* On configure nous-mêmes les 3 broches en sortie : la LED ne dépend
     * plus de la configuration CubeMX (si PA0 n'y est pas en GPIO_Output,
     * le rouge ne s'allumait jamais). */
    __HAL_RCC_GPIOA_CLK_ENABLE();
    __HAL_RCC_GPIOB_CLK_ENABLE();
    __HAL_RCC_GPIOC_CLK_ENABLE();
    apply(RGB_OFF, 0);
    config_pin(RGB_R_PORT, RGB_R_PIN);
    config_pin(RGB_G_PORT, RGB_G_PIN);
    config_pin(RGB_B_PORT, RGB_B_PIN);
    apply(RGB_OFF, 0);

    RGB_SetSolid(RGB_OFF);
}

void RGB_SetSolid(RgbColor c)
{
    RGB_SetBlink(c, 1000, c, 0);
}

void RGB_SetBlink(RgbColor c1, uint16_t ms1, RgbColor c2, uint16_t ms2)
{
    /* Si le motif n'a pas changé on ne touche à rien (pas de redémarrage
     * de phase quand l'application rappelle la fonction en boucle). */
    if (c1 == s_c1 && c2 == s_c2 && ms1 == s_ms1 && ms2 == s_ms2) {
        return;
    }
    __disable_irq();
    s_c1 = c1; s_c2 = c2;
    s_ms1 = (ms1 == 0) ? 1 : ms1;
    s_ms2 = ms2;
    s_phaseMs = 0;
    __enable_irq();
}

void RGB_TickISR(void)
{
    RgbColor c = s_c1;

    if (s_ms2 != 0) {
        uint32_t period = (uint32_t)s_ms1 + s_ms2;
        if (++s_phaseMs >= period) {
            s_phaseMs = 0;
        }
        c = (s_phaseMs < s_ms1) ? s_c1 : s_c2;
    }

    if (++s_pwmStep >= PWM_STEPS) {
        s_pwmStep = 0;
    }
    apply(c, s_pwmStep);
}
