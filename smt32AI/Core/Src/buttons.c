/**
 * @file  buttons.c
 * @brief Anti-rebond temporel + détection d'appui long.
 *
 * Principe : la lecture brute doit rester identique WWW_DEBOUNCE_MS avant
 * d'être acceptée comme état stable. Quand l'état stable est "appuyé" depuis
 * WWW_LONG_PRESS_MS, on lève UN événement ; le flag `fired` empêche tout
 * nouvel événement tant que le bouton n'est pas relâché.
 */
#include "buttons.h"
#include "www_config.h"

typedef struct {
    GPIO_TypeDef *port;
    uint16_t      pin;
    bool     rawLast;
    uint32_t rawChangeMs;
    bool     stable;        /* true = appuyé */
    uint32_t pressStartMs;
    bool     fired;         /* appui long déjà signalé pour cet appui */
    volatile bool event;    /* événement en attente de lecture */
} Button;

static Button s_btn[BTN_COUNT] = {
    [BTN_RED]   = { .port = BTN_RED_PORT,   .pin = BTN_RED_PIN   },
    [BTN_GREEN] = { .port = BTN_GREEN_PORT, .pin = BTN_GREEN_PIN },
};

static inline bool read_raw(const Button *b)
{
    /* Actif à l'état bas (pull-up) */
    return HAL_GPIO_ReadPin(b->port, b->pin) == GPIO_PIN_RESET;
}

void Buttons_Init(void)
{
    uint32_t now = HAL_GetTick();
    for (int i = 0; i < BTN_COUNT; i++) {
        Button *b = &s_btn[i];
        b->rawLast = read_raw(b);
        b->stable = b->rawLast;
        b->rawChangeMs = now;
        b->pressStartMs = now;
        /* Un bouton déjà enfoncé au démarrage (entrée en mode configuration)
         * ne doit pas déclencher un appui long ensuite : il faut le relâcher. */
        b->fired = b->stable;
        b->event = false;
    }
}

void Buttons_Update(uint32_t now)
{
    for (int i = 0; i < BTN_COUNT; i++) {
        Button *b = &s_btn[i];
        bool raw = read_raw(b);

        if (raw != b->rawLast) {
            b->rawLast = raw;
            b->rawChangeMs = now;
        }

        if (raw != b->stable && (now - b->rawChangeMs) >= WWW_DEBOUNCE_MS) {
            b->stable = raw;
            if (raw) {
                b->pressStartMs = now;
            } else {
                b->fired = false;          /* relâché : réarmement */
            }
        }

        if (b->stable && !b->fired &&
            (now - b->pressStartMs) >= WWW_LONG_PRESS_MS) {
            b->fired = true;
            b->event = true;
        }
    }
}

bool Buttons_TakeLongPress(ButtonId id)
{
    if (id >= BTN_COUNT || !s_btn[id].event) {
        return false;
    }
    s_btn[id].event = false;
    return true;
}

bool Buttons_IsPressed(ButtonId id)
{
    return (id < BTN_COUNT) && s_btn[id].stable;
}

bool Buttons_HeldAtBoot(ButtonId id, uint32_t ms)
{
    if (id >= BTN_COUNT) {
        return false;
    }
    uint32_t start = HAL_GetTick();
    while ((HAL_GetTick() - start) < ms) {
        if (!read_raw(&s_btn[id])) {
            return false;
        }
    }
    return true;
}

void Buttons_OnEdgeISR(void)
{
    /* Rien à faire : l'anti-rebond est géré par scrutation dans la boucle.
     * L'interruption EXTI13 reste utile pour sortir d'un éventuel mode
     * basse consommation (WFI) dans une version future. */
}
