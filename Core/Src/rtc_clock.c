/**
 * @file  rtc_clock.c
 * @brief RTC interne.
 *
 * - Au premier démarrage (registre de sauvegarde vide), l'heure est réglée
 *   sur la date/heure de compilation ; ensuite elle n'est plus écrasée à
 *   chaque reset (elle survit au reset, et à la coupure si VBAT est alimenté).
 * - Erreur RTC si : aucun oscillateur 32 kHz, échec d'init, échec de lecture HAL, ou
 *   horloge figée (les secondes n'avancent plus alors que le temps passe).
 */
#include "rtc_clock.h"
#include "main.h"
#include <string.h>

#define RTC_BKP_MAGIC  0x32F2u

static RTC_HandleTypeDef *s_hrtc;
static bool     s_ok;
static uint32_t s_lastSecOfDay = 0xFFFFFFFFu;
static uint32_t s_lastChangeTick;

static uint8_t weekday_of(uint16_t y, uint8_t m, uint8_t d)
{
    /* Algorithme de Sakamoto : 0 = dimanche */
    static const uint8_t t[] = {0, 3, 2, 5, 0, 3, 5, 1, 4, 6, 2, 4};
    if (m < 3) y--;
    uint8_t w = (uint8_t)((y + y / 4 - y / 100 + y / 400 + t[m - 1] + d) % 7);
    return (w == 0) ? 7 : w;      /* 1 = lundi .. 7 = dimanche */
}

static void set_from_build_time(void)
{
    static const char months[] = "JanFebMarAprMayJunJulAugSepOctNovDec";
    const char *date = __DATE__;  /* "Oct 12 2026" */
    const char *time = __TIME__;  /* "14:25:00"    */
    uint8_t month = 1;

    for (uint8_t i = 0; i < 12; i++) {
        if (strncmp(date, &months[i * 3], 3) == 0) { month = (uint8_t)(i + 1); break; }
    }
    uint8_t  day  = (uint8_t)(((date[4] == ' ') ? 0 : (date[4] - '0')) * 10 + (date[5] - '0'));
    uint16_t year = (uint16_t)((date[7] - '0') * 1000 + (date[8] - '0') * 100 +
                               (date[9] - '0') * 10 + (date[10] - '0'));
    uint8_t h = (uint8_t)((time[0] - '0') * 10 + (time[1] - '0'));
    uint8_t m = (uint8_t)((time[3] - '0') * 10 + (time[4] - '0'));
    uint8_t s = (uint8_t)((time[6] - '0') * 10 + (time[7] - '0'));

    RtcClock_SetTime(h, m, s);
    RtcClock_SetDate(month, day, year);
}

void RtcClock_Init(RTC_HandleTypeDef *hrtc)
{
    s_hrtc = hrtc;
    s_ok = (hrtc != NULL) && (g_rtcSource != RTC_SRC_NONE);
    s_lastChangeTick = HAL_GetTick();

    if (!s_ok) {
        return;
    }
    if (HAL_RTCEx_BKUPRead(s_hrtc, RTC_BKP_DR0) != RTC_BKP_MAGIC) {
        set_from_build_time();
        HAL_RTCEx_BKUPWrite(s_hrtc, RTC_BKP_DR0, RTC_BKP_MAGIC);
    }
}

bool RtcClock_IsOk(void)
{
    return s_ok;
}

bool RtcClock_Get(RtcDateTime *dt)
{
    RTC_TimeTypeDef t = {0};
    RTC_DateTypeDef d = {0};

    if (s_hrtc == NULL) {
        s_ok = false;
        return false;
    }
    /* Oscillateur 32 kHz toujours vivant ? */
    uint32_t rdy = (g_rtcSource == RTC_SRC_LSE) ? RCC_FLAG_LSERDY : RCC_FLAG_LSIRDY;
    if (g_rtcSource == RTC_SRC_NONE || __HAL_RCC_GET_FLAG(rdy) == RESET) {
        s_ok = false;
        return false;
    }
    /* GetTime puis GetDate obligatoirement (déverrouille les registres) */
    if (HAL_RTC_GetTime(s_hrtc, &t, RTC_FORMAT_BIN) != HAL_OK ||
        HAL_RTC_GetDate(s_hrtc, &d, RTC_FORMAT_BIN) != HAL_OK) {
        s_ok = false;
        return false;
    }

    /* Horloge figée ? */
    uint32_t sod = (uint32_t)t.Hours * 3600u + t.Minutes * 60u + t.Seconds;
    uint32_t now = HAL_GetTick();
    if (sod != s_lastSecOfDay) {
        s_lastSecOfDay = sod;
        s_lastChangeTick = now;
        s_ok = true;
    } else if ((now - s_lastChangeTick) > 3000u) {
        s_ok = false;
        return false;
    }

    if (dt != NULL) {
        dt->year = (uint16_t)(2000u + d.Year);
        dt->month = d.Month;
        dt->day = d.Date;
        dt->weekday = d.WeekDay;
        dt->hours = t.Hours;
        dt->minutes = t.Minutes;
        dt->seconds = t.Seconds;
    }
    return true;
}

bool RtcClock_SetTime(uint8_t h, uint8_t m, uint8_t s)
{
    RTC_TimeTypeDef t = {0};
    if (s_hrtc == NULL || h > 23 || m > 59 || s > 59) return false;
    t.Hours = h;
    t.Minutes = m;
    t.Seconds = s;
    t.DayLightSaving = RTC_DAYLIGHTSAVING_NONE;
    t.StoreOperation = RTC_STOREOPERATION_RESET;
    if (HAL_RTC_SetTime(s_hrtc, &t, RTC_FORMAT_BIN) != HAL_OK) return false;
    s_lastSecOfDay = 0xFFFFFFFFu;     /* repart de zéro pour la détection "figée" */
    s_lastChangeTick = HAL_GetTick();
    return true;
}

static uint8_t days_in_month(uint8_t m, uint16_t y)
{
    static const uint8_t dm[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    if (m == 2 && ((y % 4 == 0 && y % 100 != 0) || y % 400 == 0)) return 29;
    return dm[m - 1];
}

bool RtcClock_SetDate(uint8_t month, uint8_t day, uint16_t year)
{
    RTC_DateTypeDef d = {0};
    if (s_hrtc == NULL || month < 1 || month > 12 || year < 2000 || year > 2099 ||
        day < 1 || day > days_in_month(month, year)) {
        return false;
    }
    d.Month = month;
    d.Date = day;
    d.Year = (uint8_t)(year - 2000u);
    d.WeekDay = weekday_of(year, month, day);   /* calculé automatiquement */
    return HAL_RTC_SetDate(s_hrtc, &d, RTC_FORMAT_BIN) == HAL_OK;
}

bool RtcClock_SetWeekday(uint8_t weekday)
{
    RTC_TimeTypeDef t;
    RTC_DateTypeDef d;
    if (s_hrtc == NULL || weekday < 1 || weekday > 7) return false;
    if (HAL_RTC_GetTime(s_hrtc, &t, RTC_FORMAT_BIN) != HAL_OK ||
        HAL_RTC_GetDate(s_hrtc, &d, RTC_FORMAT_BIN) != HAL_OK) {
        return false;
    }
    d.WeekDay = weekday;
    return HAL_RTC_SetDate(s_hrtc, &d, RTC_FORMAT_BIN) == HAL_OK;
}

const char *RtcClock_WeekdayStr(uint8_t w)
{
    static const char *names[] = {"?", "MON", "TUE", "WED", "THU", "FRI", "SAT", "SUN"};
    return (w >= 1 && w <= 7) ? names[w] : names[0];
}
