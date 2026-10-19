/**
 * @file  buttons.h
 * @brief Bouton rouge / vert : anti-rebond + appui long (5 s), un seul
 *        événement par appui (il faut relâcher pour en générer un autre).
 */
#ifndef BUTTONS_H
#define BUTTONS_H

#include <stdint.h>
#include <stdbool.h>

typedef enum { BTN_RED = 0, BTN_GREEN = 1, BTN_COUNT } ButtonId;

void Buttons_Init(void);

/** À appeler le plus souvent possible dans la boucle principale. */
void Buttons_Update(uint32_t nowMs);

/** Vrai une seule fois quand le bouton a été tenu WWW_LONG_PRESS_MS. */
bool Buttons_TakeLongPress(ButtonId id);

/** État stable (après anti-rebond). */
bool Buttons_IsPressed(ButtonId id);

/** Lecture au démarrage : bouton maintenu de façon stable pendant `ms`. */
bool Buttons_HeldAtBoot(ButtonId id, uint32_t ms);

/** Appelé par HAL_GPIO_EXTI_Callback (PC13) : simple réveil, la logique
 *  reste dans Buttons_Update(). */
void Buttons_OnEdgeISR(void);

#endif /* BUTTONS_H */
