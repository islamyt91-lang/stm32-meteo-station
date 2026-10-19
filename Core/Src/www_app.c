/**
 * @file  www_app.c
 * @brief Worldwide Weather Watcher - logique applicative.
 *
 *  Modes (cf. "Modes de fonctionnement") :
 *   STANDARD      : acquisition toutes les LOG_INTERVAL, LED verte
 *   CONFIGURATION : démarrage bouton rouge appuyé, commandes UART,
 *                   acquisition arrêtée, LED jaune, retour STANDARD après
 *                   30 min sans activité
 *   ECONOMIQUE    : depuis STANDARD (vert 5 s), intervalle x2, LED bleue
 *   MAINTENANCE   : depuis STANDARD/ECO (rouge 5 s), mesures affichées en
 *                   direct, LED orange, rouge 5 s -> mode précédent
 *
 *  L'acquisition est NON BLOQUANTE : elle démarre, interroge chaque capteur
 *  jusqu'à réussite ou TIMEOUT, puis écrit UNE ligne horodatée. Pendant ce
 *  temps les boutons et la console continuent d'être traités.
 *
 *  GPS (USART3) et carte SD (SPI1/FatFS) sont intégrés :
 *   - la position GPS est ajoutée à la ligne de mesure (modes standard/éco)
 *   - la ligne est écrite dans le fichier .LOG de la carte SD
 *   - en mode maintenance la carte SD est démontée (retrait sûr)
 *   - en mode économique le GPS n'est lu qu'une mesure sur deux
 */
#include "www_app.h"
#include "main.h"
#include "www_config.h"
#include "settings.h"
#include "console.h"
#include "rgb_led.h"
#include "buttons.h"
#include "dht11.h"
#include "bh1750.h"
#include "rtc_clock.h"
#include "gps.h"
#include "sd_card.h"

#include <stdio.h>
#include <string.h>
#include <ctype.h>
#include <stdlib.h>

/* ========================================================================== */
/*  Types                                                                     */
/* ========================================================================== */
typedef enum { MODE_STANDARD, MODE_CONFIG, MODE_ECO, MODE_MAINT } Mode;

typedef enum { SENS_OFF, SENS_PENDING, SENS_DONE, SENS_TIMEOUT } SensState;

typedef struct {
    bool     active;
    uint32_t startMs;
    /* DHT11 (température + humidité) */
    SensState dhtState;
    DHT11_Data dht;
    DHT11_Status dhtLastErr;
    /* BH1750 */
    SensState luxState;
    uint32_t lux_x10;
    uint32_t luxNextTryMs;
} Acquisition;

/* ========================================================================== */
/*  État global                                                               */
/* ========================================================================== */
static Mode     s_mode;
static Mode     s_maintReturn = MODE_STANDARD;
static uint32_t s_nextAcqMs;
static uint32_t s_configLastActivityMs;
static uint32_t s_lastRtcCheckMs;
static Acquisition s_acq;

/* Compteurs de timeouts consécutifs (2 -> capteur en erreur) */
static uint8_t s_dhtTimeouts, s_luxTimeouts;
static bool    s_errSensorAccess;   /* rouge/vert 1:1 */
static bool    s_errIncoherent;     /* rouge/vert 1:2 */
static bool    s_rtcErrReported;

/* ========================================================================== */
/*  Outils                                                                    */
/* ========================================================================== */
static const char *mode_name(Mode m)
{
    switch (m) {
    case MODE_STANDARD: return "STANDARD";
    case MODE_CONFIG:   return "CONFIGURATION";
    case MODE_ECO:      return "ECONOMIQUE";
    case MODE_MAINT:    return "MAINTENANCE";
    default:            return "?";
    }
}

/** Écrit une valeur en dixièmes sous forme "-12.3". */
static void fmt_x10(char *buf, size_t n, int32_t v)
{
    const char *sign = (v < 0) ? "-" : "";
    if (v < 0) v = -v;
    snprintf(buf, n, "%s%ld.%ld", sign, (long)(v / 10), (long)(v % 10));
}

/** Écrit une position en micro-degrés sous forme "48.852057". */
static void fmt_udeg(char *buf, size_t n, int32_t v)
{
    const char *sign = (v < 0) ? "-" : "";
    uint32_t a = (v < 0) ? (uint32_t)(-v) : (uint32_t)v;
    snprintf(buf, n, "%s%lu.%06lu", sign,
             (unsigned long)(a / 1000000u), (unsigned long)(a % 1000000u));
}

static uint32_t current_period_ms(void)
{
    uint32_t p = g_settings.log_interval_s * 1000u;
    if (s_mode == MODE_ECO)   p *= 2u;
    if (s_mode == MODE_MAINT) p = WWW_MAINT_PERIOD_MS;
    return p;
}

static bool acquisition_mode(Mode m)
{
    return m == MODE_STANDARD || m == MODE_ECO || m == MODE_MAINT;
}

/* ========================================================================== */
/*  LED                                                                       */
/* ========================================================================== */
static void update_led(void)
{
    /* Priorité : RTC > GPS > capteur > incohérent > carte SD > couleur du mode */
    if (!RtcClock_IsOk()) {
        RGB_SetBlink(RGB_RED, 500, RGB_BLUE, 500);
        return;
    }
    if (acquisition_mode(s_mode)) {
        if (Gps_Error())        { RGB_SetBlink(RGB_RED, 500, RGB_YELLOW, 500); return; }
        if (SdCard_IsFull())    { RGB_SetBlink(RGB_RED, 500, RGB_WHITE, 500);  return; }
        if (SdCard_Error())     { RGB_SetBlink(RGB_RED, 500, RGB_WHITE, 1000); return; }
        if (s_errSensorAccess)  { RGB_SetBlink(RGB_RED, 500, RGB_GREEN, 500);  return; }
        if (s_errIncoherent)    { RGB_SetBlink(RGB_RED, 333, RGB_GREEN, 667);  return; }
    }
    switch (s_mode) {
    case MODE_STANDARD: RGB_SetSolid(RGB_GREEN);  break;
    case MODE_CONFIG:   RGB_SetSolid(RGB_YELLOW); break;
    case MODE_ECO:      RGB_SetSolid(RGB_BLUE);   break;
    case MODE_MAINT:    RGB_SetSolid(RGB_ORANGE); break;
    }
}

/* ========================================================================== */
/*  Acquisition                                                               */
/* ========================================================================== */
static void print_header(void)
{
    printf("DATE_HEURE;MODE;TEMP_AIR_C;HYGR_PCT;LUMIN_LUX;LUMIN_NIVEAU;"
           "GPS_LAT;GPS_LON;GPS_VIT_NOEUD\r\n");
}

static void acq_start(uint32_t now)
{
    memset(&s_acq, 0, sizeof(s_acq));
    s_acq.active  = true;
    s_acq.startMs = now;
    s_acq.dhtState = (g_settings.temp_air || g_settings.hygr) ? SENS_PENDING : SENS_OFF;
    s_acq.luxState = g_settings.lumin ? SENS_PENDING : SENS_OFF;
    s_acq.dhtLastErr = DHT11_OK;
    s_acq.luxNextTryMs = now;
}

static void acq_poll(uint32_t now)
{
    uint32_t timeoutMs = (uint32_t)g_settings.timeout_s * 1000u;
    bool expired = (now - s_acq.startMs) >= timeoutMs;

    /* --- DHT11 : une tentative toutes les 2 s minimum (géré par le pilote) */
    if (s_acq.dhtState == SENS_PENDING) {
        DHT11_Status st = DHT11_Read(&s_acq.dht);
        if (st == DHT11_OK) {
            s_acq.dhtState = SENS_DONE;
        } else if (st != DHT11_BUSY) {
            s_acq.dhtLastErr = st;
        }
        if (s_acq.dhtState == SENS_PENDING && expired) {
            s_acq.dhtState = SENS_TIMEOUT;
        }
    }

    /* --- BH1750 : une tentative toutes les 200 ms */
    if (s_acq.luxState == SENS_PENDING && (int32_t)(now - s_acq.luxNextTryMs) >= 0) {
        if (BH1750_ReadLux(&s_acq.lux_x10) == BH1750_OK) {
            s_acq.luxState = SENS_DONE;
        } else {
            s_acq.luxNextTryMs = now + 200u;
        }
    }
    if (s_acq.luxState == SENS_PENDING && expired) {
        s_acq.luxState = SENS_TIMEOUT;
    }
}

static bool acq_finished(void)
{
    return s_acq.dhtState != SENS_PENDING && s_acq.luxState != SENS_PENDING;
}

static void acq_finish(void)
{
    char ts[32], tStr[12], hStr[12], lStr[16];
    char gpsLat[16], gpsLon[16], gpsSpd[12];
    char line[192];
    const char *lvl = "NA";
    bool incoherent = false;
    RtcDateTime dt;
    GpsData gps;

    s_acq.active = false;
    strcpy(tStr, "NA");
    strcpy(hStr, "NA");
    strcpy(lStr, "NA");
    strcpy(gpsLat, "NA");
    strcpy(gpsLon, "NA");
    strcpy(gpsSpd, "NA");

    /* ---- Horodatage */
    if (RtcClock_Get(&dt)) {
        snprintf(ts, sizeof(ts), "%04u-%02u-%02u %02u:%02u:%02u",
                 dt.year, dt.month, dt.day, dt.hours, dt.minutes, dt.seconds);
    } else {
        strcpy(ts, "NA");
    }

    /* ---- Température / humidité */
    if (s_acq.dhtState == SENS_DONE) {
        s_dhtTimeouts = 0;
        int16_t t = s_acq.dht.temp_x10;

        if (g_settings.temp_air) {
            if (t < g_settings.min_temp_air * 10 || t > g_settings.max_temp_air * 10) {
                char v[12];
                fmt_x10(v, sizeof(v), t);
                printf("[ALERTE] Temperature %s C hors plage [%d ; %d] : "
                       "verification materielle requise\r\n",
                       v, g_settings.min_temp_air, g_settings.max_temp_air);
                incoherent = true;
            } else {
                fmt_x10(tStr, sizeof(tStr), t);
            }
        }
        if (g_settings.hygr) {
            /* Hygrométrie ignorée hors de [HYGR_MINT ; HYGR_MAXT] (pas une erreur) */
            if (t >= g_settings.hygr_mint * 10 && t <= g_settings.hygr_maxt * 10) {
                if (s_acq.dht.hum_x10 > 1000) {
                    printf("[ALERTE] Humidite %d %% impossible\r\n", s_acq.dht.hum_x10 / 10);
                    incoherent = true;
                } else {
                    snprintf(hStr, sizeof(hStr), "%d", s_acq.dht.hum_x10 / 10);
                }
            }
        }
    } else if (s_acq.dhtState == SENS_TIMEOUT) {
        if (s_dhtTimeouts < 255) s_dhtTimeouts++;
        printf("[ERREUR] DHT11 sans reponse apres %u s (%s), timeout %u/2\r\n",
               g_settings.timeout_s, DHT11_StatusStr(s_acq.dhtLastErr),
               s_dhtTimeouts > 2 ? 2 : s_dhtTimeouts);
    } else {
        s_dhtTimeouts = 0;          /* capteur désactivé */
    }

    /* ---- Luminosité (seuils en lux) */
    if (s_acq.luxState == SENS_DONE) {
        s_luxTimeouts = 0;
        uint32_t lux = s_acq.lux_x10 / 10u;
        fmt_x10(lStr, sizeof(lStr), (int32_t)s_acq.lux_x10);
        if (lux < g_settings.lumin_low)       lvl = "FAIBLE";
        else if (lux > g_settings.lumin_high) lvl = "FORTE";
        else                                  lvl = "MOYENNE";
    } else if (s_acq.luxState == SENS_TIMEOUT) {
        if (s_luxTimeouts < 255) s_luxTimeouts++;
        printf("[ERREUR] BH1750 sans reponse apres %u s (erreur I2C 0x%02lX), timeout %u/2\r\n",
               g_settings.timeout_s, (unsigned long)BH1750_LastI2CError(),
               s_luxTimeouts > 2 ? 2 : s_luxTimeouts);
    } else {
        s_luxTimeouts = 0;
    }

    /* ---- GPS : en mode économique, une mesure sur deux seulement */
    {
        static uint8_t s_ecoCpt;
        bool doGps = (s_mode != MODE_ECO) || ((s_ecoCpt++ & 1u) == 0u);

        if (doGps) {
            Gps_GetLast(&gps);
            if (gps.fixed) {
                fmt_udeg(gpsLat, sizeof(gpsLat), gps.lat_udeg);
                fmt_udeg(gpsLon, sizeof(gpsLon), gps.lon_udeg);
                fmt_x10(gpsSpd, sizeof(gpsSpd), (int32_t)gps.speed_x10);
            } else {
                printf("[INFO] GPS sans position (fix) pour l'instant\r\n");
            }
        }
    }

    /* ---- Une seule ligne par acquisition (console + carte SD) */
    snprintf(line, sizeof(line), "%s;%s;%s;%s;%s;%s;%s;%s;%s",
             ts, mode_name(s_mode), tStr, hStr, lStr, lvl,
             gpsLat, gpsLon, gpsSpd);
    printf("%s\r\n", line);

    /* ---- Journal sur carte SD (jamais en mode maintenance) */
    if (s_mode != MODE_MAINT) {
        if (!SdCard_AppendLine(line)) {
            printf("[INFO] Mesure non enregistree sur la carte SD\r\n");
        }
    }

    /* ---- Mise à jour des erreurs */
    s_errSensorAccess = (s_dhtTimeouts >= 2) || (s_luxTimeouts >= 2);
    s_errIncoherent   = incoherent;
}

static void acq_abort(void)
{
    s_acq.active = false;
}

/* ========================================================================== */
/*  Changement de mode                                                        */
/* ========================================================================== */
static void print_help(void);
static void print_settings(void);

static void enter_mode(Mode m)
{
    uint32_t now = HAL_GetTick();
    Mode old = s_mode;

    acq_abort();

    /* Sortie du mode maintenance : on remonte la carte SD */
    if (old == MODE_MAINT && m != MODE_MAINT) {
        SdCard_Remount();
    }

    s_mode = m;
    printf("\r\n=== Mode %s ===\r\n", mode_name(m));

    switch (m) {
    case MODE_CONFIG:
        s_configLastActivityMs = now;
        printf("Acquisition arretee. Retour automatique en STANDARD apres %lu min "
               "sans activite.\r\n", (unsigned long)(WWW_CONFIG_IDLE_TIMEOUT_MS / 60000u));
        print_help();
        print_settings();
        break;
    case MODE_MAINT:
        s_maintReturn = old;
        SdCard_Unmount();          /* carte SD demontee : retrait sans risque */
        printf("Mesures en direct toutes les %lu s. La carte SD peut etre retiree.\r\n"
               "Rouge 5 s : retour en mode %s.\r\n",
               (unsigned long)(WWW_MAINT_PERIOD_MS / 1000u), mode_name(old));
        print_header();
        break;
    case MODE_ECO:
    case MODE_STANDARD:
        printf("Intervalle de mesure : %lu s\r\n", (unsigned long)(current_period_ms() / 1000u));
        print_header();
        break;
    }
    /* Première mesure immédiate dans les modes d'acquisition */
    s_nextAcqMs = now;
    update_led();
}

static void handle_buttons(void)
{
    bool red   = Buttons_TakeLongPress(BTN_RED);
    bool green = Buttons_TakeLongPress(BTN_GREEN);

    switch (s_mode) {
    case MODE_STANDARD:
        if (red)        enter_mode(MODE_MAINT);
        else if (green) enter_mode(MODE_ECO);
        break;
    case MODE_ECO:
#if WWW_ECO_RED_GOES_TO_MAINT
        if (red) enter_mode(MODE_MAINT);
#else
        if (red) enter_mode(MODE_STANDARD);
#endif
        break;
    case MODE_MAINT:
        if (red) enter_mode(s_maintReturn);
        break;
    case MODE_CONFIG:
        /* Les boutons n'ont pas d'action en configuration */
        break;
    }
}

/* ========================================================================== */
/*  Console de configuration                                                  */
/* ========================================================================== */
typedef enum { P_U8, P_U16, P_U32, P_I16 } PType;

typedef struct {
    const char *name;
    PType       type;
    void       *ptr;
    long        min, max;
    const char *unit;
} Param;

static const Param kParams[] = {
    {"FILE_MAX_SIZE", P_U32, &g_settings.file_max_size, 512, 65536, "octets (reserve carte SD)"},
    {"TIMEOUT",       P_U16, &g_settings.timeout_s, TIMEOUT_MIN_S, TIMEOUT_MAX_S, "s"},
    {"LUMIN",         P_U8,  &g_settings.lumin, 0, 1, ""},
    {"LUMIN_LOW",     P_U32, &g_settings.lumin_low, 0, LUMIN_MAX_LUX, "lux"},
    {"LUMIN_HIGH",    P_U32, &g_settings.lumin_high, 0, LUMIN_MAX_LUX, "lux"},
    {"TEMP_AIR",      P_U8,  &g_settings.temp_air, 0, 1, ""},
    {"MIN_TEMP_AIR",  P_I16, &g_settings.min_temp_air, TEMP_MIN_C, TEMP_MAX_C, "C"},
    {"MAX_TEMP_AIR",  P_I16, &g_settings.max_temp_air, TEMP_MIN_C, TEMP_MAX_C, "C"},
    {"HYGR",          P_U8,  &g_settings.hygr, 0, 1, ""},
    {"HYGR_MINT",     P_I16, &g_settings.hygr_mint, TEMP_MIN_C, TEMP_MAX_C, "C"},
    {"HYGR_MAXT",     P_I16, &g_settings.hygr_maxt, TEMP_MIN_C, TEMP_MAX_C, "C"},
};
#define N_PARAMS (sizeof(kParams) / sizeof(kParams[0]))

static long param_get(const Param *p)
{
    switch (p->type) {
    case P_U8:  return *(uint8_t *)p->ptr;
    case P_U16: return *(uint16_t *)p->ptr;
    case P_U32: return (long)*(uint32_t *)p->ptr;
    case P_I16: return *(int16_t *)p->ptr;
    }
    return 0;
}

static void param_set(const Param *p, long v)
{
    switch (p->type) {
    case P_U8:  *(uint8_t *)p->ptr  = (uint8_t)v;  break;
    case P_U16: *(uint16_t *)p->ptr = (uint16_t)v; break;
    case P_U32: *(uint32_t *)p->ptr = (uint32_t)v; break;
    case P_I16: *(int16_t *)p->ptr  = (int16_t)v;  break;
    }
}

/** Conversion stricte d'un entier signé. */
static bool parse_long(const char *s, long *out)
{
    char *end;
    if (s == NULL || *s == '\0') return false;
    long v = strtol(s, &end, 10);
    if (*end != '\0') return false;
    *out = v;
    return true;
}

static void print_help(void)
{
    printf("Commandes (NOM=VALEUR, Entree pour valider) :\r\n"
           "  LOG_INTERVAL=10        intervalle en minutes (1-1440)\r\n"
           "  LOG_INTERVAL_S=10      intervalle en secondes (tests rapides, >= 5)\r\n"
           "  TIMEOUT=30             abandon d'une mesure (s)\r\n"
           "  FILE_MAX_SIZE=2048     taille max fichier log (reserve carte SD)\r\n"
           "  LUMIN=1  LUMIN_LOW=500  LUMIN_HIGH=10000      (seuils en lux)\r\n"
           "  TEMP_AIR=1  MIN_TEMP_AIR=-10  MAX_TEMP_AIR=60 (C)\r\n"
           "  HYGR=1  HYGR_MINT=0  HYGR_MAXT=50             (C)\r\n"
           "  CLOCK=HH:MM:SS   DATE=MM,JJ,AAAA   DAY=MON..SUN\r\n"
           "  RESET  VERSION  SHOW  HELP  EXIT\r\n");
}

static void print_settings(void)
{
    RtcDateTime dt;
    printf("Parametres actuels :\r\n");
    if (g_settings.log_interval_s % 60u == 0) {
        printf("  LOG_INTERVAL=%lu min\r\n", (unsigned long)(g_settings.log_interval_s / 60u));
    } else {
        printf("  LOG_INTERVAL_S=%lu s\r\n", (unsigned long)g_settings.log_interval_s);
    }
    for (size_t i = 0; i < N_PARAMS; i++) {
        printf("  %s=%ld %s\r\n", kParams[i].name, param_get(&kParams[i]), kParams[i].unit);
    }
    if (RtcClock_Get(&dt)) {
        printf("  RTC : %s %02u/%02u/%04u %02u:%02u:%02u\r\n",
               RtcClock_WeekdayStr(dt.weekday), dt.day, dt.month, dt.year,
               dt.hours, dt.minutes, dt.seconds);
    } else {
        printf("  RTC : ERREUR\r\n");
    }
}

static void save_and_report(void)
{
    if (Settings_Save()) {
        printf("OK (sauvegarde)\r\n");
    } else {
        printf("OK mais ERREUR d'ecriture Flash : valeur perdue au redemarrage\r\n");
    }
}

static bool cross_checks_ok(void)
{
    if (g_settings.lumin_low > g_settings.lumin_high) {
        printf("Erreur : LUMIN_LOW doit etre <= LUMIN_HIGH\r\n");
        return false;
    }
    if (g_settings.min_temp_air > g_settings.max_temp_air) {
        printf("Erreur : MIN_TEMP_AIR doit etre <= MAX_TEMP_AIR\r\n");
        return false;
    }
    if (g_settings.hygr_mint > g_settings.hygr_maxt) {
        printf("Erreur : HYGR_MINT doit etre <= HYGR_MAXT\r\n");
        return false;
    }
    return true;
}

static void cmd_clock(const char *v)
{
    unsigned h, m, s;
    char extra;
    if (sscanf(v, "%u:%u:%u%c", &h, &m, &s, &extra) != 3 || h > 23 || m > 59 || s > 59) {
        printf("Format : CLOCK=HH:MM:SS (00-23:00-59:00-59)\r\n");
        return;
    }
    printf(RtcClock_SetTime((uint8_t)h, (uint8_t)m, (uint8_t)s) ? "OK\r\n"
                                                                 : "Erreur RTC\r\n");
}

static void cmd_date(const char *v)
{
    unsigned mo, d, y;
    char extra;
    if (sscanf(v, "%u,%u,%u%c", &mo, &d, &y, &extra) != 3) {
        printf("Format : DATE=MM,JJ,AAAA (ex. DATE=10,12,2026)\r\n");
        return;
    }
    if (!RtcClock_SetDate((uint8_t)mo, (uint8_t)d, (uint16_t)y)) {
        printf("Date invalide (mois 1-12, jour du mois, annee 2000-2099)\r\n");
        return;
    }
    printf("OK (jour de la semaine calcule automatiquement)\r\n");
}

static void cmd_day(const char *v)
{
    static const char *days[] = {"MON", "TUE", "WED", "THU", "FRI", "SAT", "SUN"};
    for (uint8_t i = 0; i < 7; i++) {
        if (strcmp(v, days[i]) == 0) {
            printf(RtcClock_SetWeekday((uint8_t)(i + 1)) ? "OK\r\n" : "Erreur RTC\r\n");
            return;
        }
    }
    printf("Valeurs : MON TUE WED THU FRI SAT SUN\r\n");
}

static void handle_command(char *line)
{
    /* Majuscules + suppression des espaces */
    char *w = line;
    for (char *r = line; *r; r++) {
        if (*r != ' ' && *r != '\t') *w++ = (char)toupper((unsigned char)*r);
    }
    *w = '\0';
    if (line[0] == '\0') return;

    char *eq  = strchr(line, '=');
    char *val = NULL;
    if (eq != NULL) { *eq = '\0'; val = eq + 1; }

    /* --- Commandes sans valeur */
    if (val == NULL) {
        if (strcmp(line, "HELP") == 0)    { print_help(); return; }
        if (strcmp(line, "SHOW") == 0 || strcmp(line, "STATUS") == 0) { print_settings(); return; }
        if (strcmp(line, "VERSION") == 0) {
            printf("Worldwide Weather Watcher v%s - lot %s (compile le %s %s)\r\n",
                   WWW_VERSION, WWW_LOT, __DATE__, __TIME__);
            return;
        }
        if (strcmp(line, "RESET") == 0) {
            Settings_Defaults(&g_settings);
            printf("Parametres remis aux valeurs par defaut. ");
            save_and_report();
            return;
        }
        if (strcmp(line, "EXIT") == 0) { enter_mode(MODE_STANDARD); return; }
    }

    /* --- Réglage de la RTC */
    if (val != NULL && strcmp(line, "CLOCK") == 0) { cmd_clock(val); return; }
    if (val != NULL && strcmp(line, "DATE") == 0)  { cmd_date(val);  return; }
    if (val != NULL && strcmp(line, "DAY") == 0)   { cmd_day(val);   return; }

    /* --- Intervalle de mesure */
    if (strcmp(line, "LOG_INTERVAL") == 0 || strcmp(line, "LOG_INTERVALL") == 0) {
        long v;
        if (val == NULL) {
            printf("LOG_INTERVAL = %lu s\r\n", (unsigned long)g_settings.log_interval_s);
        } else if (!parse_long(val, &v) || v < (long)LOG_INTERVAL_MIN_MIN || v > (long)LOG_INTERVAL_MAX_MIN) {
            printf("Valeur invalide : %u a %u minutes\r\n", LOG_INTERVAL_MIN_MIN, LOG_INTERVAL_MAX_MIN);
        } else {
            g_settings.log_interval_s = (uint32_t)v * 60u;
            save_and_report();
        }
        return;
    }
    if (strcmp(line, "LOG_INTERVAL_S") == 0) {
        long v;
        if (val == NULL) {
            printf("LOG_INTERVAL = %lu s\r\n", (unsigned long)g_settings.log_interval_s);
        } else if (!parse_long(val, &v) || v < (long)LOG_INTERVAL_MIN_S ||
                   v > (long)(LOG_INTERVAL_MAX_MIN * 60u)) {
            printf("Valeur invalide : %u a %u secondes\r\n", LOG_INTERVAL_MIN_S,
                   LOG_INTERVAL_MAX_MIN * 60u);
        } else {
            g_settings.log_interval_s = (uint32_t)v;
            save_and_report();
        }
        return;
    }

    /* --- Capteur de pression : absent de cette version */
    if (strncmp(line, "PRESSURE", 8) == 0) {
        printf("Capteur de pression non installe dans cette version\r\n");
        return;
    }

    /* --- Paramètres numériques génériques */
    for (size_t i = 0; i < N_PARAMS; i++) {
        const Param *p = &kParams[i];
        if (strcmp(line, p->name) != 0) continue;

        if (val == NULL) {
            printf("%s = %ld %s\r\n", p->name, param_get(p), p->unit);
            return;
        }
        long v;
        if (!parse_long(val, &v) || v < p->min || v > p->max) {
            printf("Valeur invalide pour %s : %ld a %ld\r\n", p->name, p->min, p->max);
            return;
        }
        long old = param_get(p);
        param_set(p, v);
        if (!cross_checks_ok()) {
            param_set(p, old);           /* on annule */
            return;
        }
        save_and_report();
        return;
    }

    printf("Commande inconnue. Tapez HELP.\r\n");
}

static void handle_console(uint32_t now)
{
    char line[CONSOLE_LINE_MAX];

    if (Console_TakeActivity()) {
        s_configLastActivityMs = now;
    }
    while (Console_GetLine(line, sizeof(line))) {
        if (s_mode == MODE_CONFIG) {
            handle_command(line);
            if (s_mode != MODE_CONFIG) break;   /* EXIT */
            printf("> ");
        } else {
            printf("Commandes disponibles uniquement en mode CONFIGURATION "
                   "(redemarrer en maintenant le bouton rouge).\r\n");
        }
    }
}

/* ========================================================================== */
/*  API                                                                       */
/* ========================================================================== */
void App_Init(I2C_HandleTypeDef *hi2c, UART_HandleTypeDef *huart,
              RTC_HandleTypeDef *hrtc)
{
    Console_Init(huart);
    RGB_Init();
    Buttons_Init();

    printf("\r\n========================================\r\n"
           " Worldwide Weather Watcher v%s\r\n"
           "========================================\r\n", WWW_VERSION);

    if (!Settings_Load()) {
        printf("[INFO] Aucun parametre en Flash : valeurs par defaut.\r\n");
    }

    RtcClock_Init(hrtc);
    if (!RtcClock_IsOk()) {
        printf("[ERREUR] Horloge RTC inaccessible.\r\n");
        s_rtcErrReported = true;
    } else if (g_rtcSource == RTC_SRC_LSI) {
        printf("[ATTENTION] Quartz 32,768 kHz (LSE) absent : RTC sur oscillateur "
               "interne LSI (derive possible, heure perdue hors tension).\r\n");
    }

    DHT11_Init();
    if (BH1750_Init(hi2c, BH1750_ADDR_7BIT) != BH1750_OK) {
        printf("[ERREUR] BH1750 absent a l'adresse 0x%02X (nouvel essai a chaque mesure).\r\n",
               BH1750_ADDR_7BIT);
    }

    /* Bouton rouge maintenu au démarrage -> mode configuration */
    s_mode = MODE_STANDARD;
    enter_mode(Buttons_HeldAtBoot(BTN_RED, 50) ? MODE_CONFIG : MODE_STANDARD);
    if (s_mode == MODE_CONFIG) {
        printf("> ");
    }
}

void App_Loop(void)
{
    uint32_t now = HAL_GetTick();

    Gps_Poll();                /* consomme les trames NMEA reçues (USART3) */
    Buttons_Update(now);
    handle_buttons();
    handle_console(now);

    /* Surveillance de la RTC toutes les secondes */
    if ((now - s_lastRtcCheckMs) >= 1000u) {
        s_lastRtcCheckMs = now;
        bool ok = RtcClock_Get(NULL);
        if (!ok && !s_rtcErrReported) {
            printf("[ERREUR] Horloge RTC inaccessible.\r\n");
            s_rtcErrReported = true;
        } else if (ok && s_rtcErrReported) {
            printf("[INFO] Horloge RTC de nouveau disponible.\r\n");
            s_rtcErrReported = false;
        }
    }

    switch (s_mode) {
    case MODE_CONFIG:
        if ((now - s_configLastActivityMs) >= WWW_CONFIG_IDLE_TIMEOUT_MS) {
            printf("\r\n[INFO] Pas d'activite : retour en mode STANDARD.\r\n");
            enter_mode(MODE_STANDARD);
        }
        break;

    case MODE_STANDARD:
    case MODE_ECO:
    case MODE_MAINT:
        if (!s_acq.active && (int32_t)(now - s_nextAcqMs) >= 0) {
            acq_start(now);
            s_nextAcqMs = now + current_period_ms();
        }
        if (s_acq.active) {
            acq_poll(now);
            if (acq_finished()) {
                acq_finish();
            }
        }
        break;
    }

    update_led();
}
