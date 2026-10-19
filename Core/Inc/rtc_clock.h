/**
 * @file  rtc_clock.h
 * @brief RTC interne (LSE 32,768 kHz) : lecture, réglage, détection d'erreur.
 */
#ifndef RTC_CLOCK_H
#define RTC_CLOCK_H

#include "stm32l4xx_hal.h"
#include <stdbool.h>
#include <stdint.h>

typedef struct {
    uint16_t year;     /* 2000..2099 */
    uint8_t  month;    /* 1..12 */
    uint8_t  day;      /* 1..31 */
    uint8_t  weekday;  /* 1 = lundi .. 7 = dimanche */
    uint8_t  hours, minutes, seconds;
} RtcDateTime;

/**
 * @param hrtc     handle initialisé, ou NULL si l'init a échoué
 * La source d'horloge (LSE/LSI) est lue dans g_rtcSource (main.h).
 */
void RtcClock_Init(RTC_HandleTypeDef *hrtc);

/** Lit date/heure. Retourne false si la RTC est en erreur. */
bool RtcClock_Get(RtcDateTime *dt);

bool RtcClock_IsOk(void);

bool RtcClock_SetTime(uint8_t h, uint8_t m, uint8_t s);
bool RtcClock_SetDate(uint8_t month, uint8_t day, uint16_t year);
bool RtcClock_SetWeekday(uint8_t weekday);

const char *RtcClock_WeekdayStr(uint8_t weekday);

#endif /* RTC_CLOCK_H */
