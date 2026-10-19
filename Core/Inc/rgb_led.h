/**
 * @file  rgb_led.h
 * @brief LED RGB sur GPIO (PA0/PA1/PA4) avec PWM logiciel cadencé par SysTick
 *        (1 kHz, période 10 ms) pour obtenir le jaune, l'orange et le blanc.
 */
#ifndef RGB_LED_H
#define RGB_LED_H

#include <stdint.h>

typedef enum {
    RGB_OFF = 0,
    RGB_RED,
    RGB_GREEN,
    RGB_BLUE,
    RGB_YELLOW,
    RGB_ORANGE,
    RGB_WHITE,
    RGB_COLOR_COUNT
} RgbColor;

void RGB_Init(void);

/** Couleur fixe (annule tout clignotement). */
void RGB_SetSolid(RgbColor c);

/** Clignotement 2 couleurs : c1 pendant ms1, puis c2 pendant ms2, en boucle. */
void RGB_SetBlink(RgbColor c1, uint16_t ms1, RgbColor c2, uint16_t ms2);

/** À appeler toutes les 1 ms (SysTick_Handler). */
void RGB_TickISR(void);

#endif /* RGB_LED_H */
