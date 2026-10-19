/**
 * @file  gps.c
 * @brief Pilote GPS NMEA sur USART3 : réception IT, parsing RMC/GGA.
 *
 * Format des trames (extrait) :
 *   $GPRMC,hhmmss.ss,A,llll.ll,a,yyyyy.yy,a,ss.s,ddmmyy,...
 *   $GPGGA,hhmmss.ss,llll.ll,a,yyyyy.yy,a,q,nn,hdop,aa,m,mmm,m,...
 */
#include "gps.h"
#include "www_config.h"

#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#define RX_RING_SIZE  256u        /* puissance de 2 */
#define NMEA_MAX      96u         /* plus longue trame utile */

/* ========================================================================== */
/*  État                                                                      */
/* ========================================================================== */
static UART_HandleTypeDef *s_huart;
static uint8_t            s_rxByte;
static volatile uint8_t   s_ring[RX_RING_SIZE];
static volatile uint16_t  s_head, s_tail;

static char     s_line[NMEA_MAX];
static uint8_t  s_lineLen;
static bool     s_lineOverflow;

static GpsData  s_data;
static bool     s_started;
static uint32_t s_lastRxMs;      /* dernière trame NMEA reçue (fixe ou non) */

/* ========================================================================== */
/*  Outils                                                                    */
/* ========================================================================== */

/** Extrait le champ NMEA d'indice `idx` (0 = nom "$GPRMC") dans `out`. */
static bool field(const char *line, uint8_t idx, char *out, size_t outSize)
{
    const char *p = line;
    uint8_t i = 0;
    size_t n = 0;

    if (*p == '$') p++;                        /* saute '$' */

    while (i < idx) {                          /* avance jusqu'au champ voulu */
        while (*p && *p != ',' && *p != '*') p++;
        if (*p != ',') {
            return false;                      /* champ absent */
        }
        p++;
        i++;
    }

    while (*p && *p != ',' && *p != '*' && n < (outSize - 1u)) {
        out[n++] = *p++;
    }
    out[n] = '\0';
    return n > 0u;
}

/** Vérifie le checksum NMEA ($...*hh). Absent => accepté (certains modules). */
static bool checksum_ok(const char *line)
{
    const char *star = strchr(line, '*');
    unsigned sum = 0;
    char got[3];

    if (star == NULL || star[1] == '\0' || star[2] == '\0') {
        return true;
    }
    for (const char *p = line + 1; p < star; p++) {
        sum ^= (unsigned char)*p;
    }
    got[0] = star[1];
    got[1] = star[2];
    got[2] = '\0';
    return (unsigned)strtoul(got, NULL, 16) == sum;
}

/**
 * Convertit "ddmm.mmmm" (latitude, 2 chiffres) ou "dddmm.mmmm" (longitude,
 * 3 chiffres) + hémisphère en micro-degrés signés.
 * ex. "4851.1234" 'N' -> +48.852057 ; "00220.5678" 'E' -> +2.342797.
 */
static int32_t degmin_to_udeg(const char *s, char hemi)
{
    const char *dot = strchr(s, '.');
    size_t intLen = (dot != NULL) ? (size_t)(dot - s) : strlen(s);
    uint8_t degDigits = (intLen >= 3u) ? (uint8_t)(intLen - 2u) : 2u;
    char ddBuf[5];
    uint32_t deg, minutes_x10000, frac = 0u, f;
    uint8_t fracDigits = 0u;
    const char *p;

    if (intLen < 3u || degDigits > 3u) {
        return 0;
    }

    ddBuf[0] = s[0];
    ddBuf[1] = s[1];
    ddBuf[2] = (degDigits == 3u) ? s[2] : '\0';
    ddBuf[3] = '\0';
    deg = (uint32_t)strtoul(ddBuf, NULL, 10);

    /* minutes : 2 chiffres entiers juste après les degrés */
    ddBuf[0] = s[degDigits];
    ddBuf[1] = s[degDigits + 1u];
    ddBuf[2] = '\0';
    minutes_x10000 = (uint32_t)strtoul(ddBuf, NULL, 10) * 10000u;

    if (dot != NULL) {
        p = dot + 1;
        while (*p != '\0' && fracDigits < 4u) {
            if (*p < '0' || *p > '9') break;
            frac = frac * 10u + (uint32_t)(*p - '0');
            fracDigits++;
            p++;
        }
    }
    while (fracDigits < 4u) {
        frac *= 10u;
        fracDigits++;
    }
    minutes_x10000 += frac;

    /* udeg = deg*1e6 + minutes*1e6/60 ; minutes = minutes_x10000 / 1e4
     *      => deg*1e6 + minutes_x10000 * 10 / 6                        */
    f = (minutes_x10000 * 10u + 3u) / 6u;             /* arrondi */
    if (f >= 1000000u) {
        deg++;
        f -= 1000000u;
    }
    {
        int32_t v = (int32_t)(deg * 1000000u + f);
        if (hemi == 'S' || hemi == 'W') v = -v;
        return v;
    }
}

/** "hhmmss" -> champs; "ddmmyy" -> champs (année 20xx). */
static void parse_hms(const char *s, uint8_t *h, uint8_t *m, uint8_t *sec)
{
    char b[3];
    b[0] = s[0]; b[1] = s[1]; b[2] = '\0';
    *h = (uint8_t)strtoul(b, NULL, 10);
    b[0] = s[2]; b[1] = s[3];
    *m = (uint8_t)strtoul(b, NULL, 10);
    b[0] = s[4]; b[1] = s[5];
    *sec = (uint8_t)strtoul(b, NULL, 10);
}

static void parse_ddmmyy(const char *s, uint8_t *d, uint8_t *mo, uint16_t *y)
{
    char b[3];
    b[0] = s[0]; b[1] = s[1]; b[2] = '\0';
    *d = (uint8_t)strtoul(b, NULL, 10);
    b[0] = s[2]; b[1] = s[3];
    *mo = (uint8_t)strtoul(b, NULL, 10);
    b[0] = s[4]; b[1] = s[5];
    *y = (uint16_t)(2000u + strtoul(b, NULL, 10));
}

/* ========================================================================== */
/*  Parsing d'une trame complète                                              */
/* ========================================================================== */
static void parse_sentence(char *line)
{
    char f[16];

    if (line[0] != '$' || !checksum_ok(line)) {
        return;
    }

    /* ---- RMC : position, vitesse, date, heure, validité */
    if (strncmp(line + 1, "GPRMC", 5) == 0 || strncmp(line + 1, "GNRMC", 5) == 0) {
        char status;

        if (!field(line, 1, f, sizeof(f)) || strlen(f) < 6u) return;
        parse_hms(f, &s_data.hours, &s_data.minutes, &s_data.seconds);
        s_lastRxMs = HAL_GetTick();

        if (!field(line, 2, f, sizeof(f))) return;
        status = f[0];

        if (status == 'A') {
            char lat[16], ns[4], lon[16], ew[4], spd[10], date[10];

            if (!field(line, 3, lat, sizeof(lat)) || !field(line, 4, ns, sizeof(ns)) ||
                !field(line, 5, lon, sizeof(lon)) || !field(line, 6, ew, sizeof(ew))) {
                return;
            }
            s_data.lat_udeg = degmin_to_udeg(lat, ns[0]);
            s_data.lon_udeg = degmin_to_udeg(lon, ew[0]);

            if (field(line, 7, spd, sizeof(spd))) {
                /* "12.5" -> 125 (nœuds x10) */
                const char *dot = strchr(spd, '.');
                uint32_t v = (uint32_t)strtoul(spd, NULL, 10);
                if (dot != NULL && dot[1] != '\0') {
                    char d2[3];
                    d2[0] = dot[1];
                    d2[1] = (dot[2] != '\0') ? dot[2] : '0';
                    d2[2] = '\0';
                    v = v * 10u + (uint32_t)strtoul(d2, NULL, 10);
                } else {
                    v *= 10u;
                }
                s_data.speed_x10 = (uint16_t)v;
            }

            if (field(line, 9, date, sizeof(date)) && strlen(date) == 6u) {
                parse_ddmmyy(date, &s_data.day, &s_data.month, &s_data.year);
            }

            s_data.fixed = true;
            s_data.lastFixMs = HAL_GetTick();
            s_data.sentenceCount++;
        } else {
            s_data.fixed = false;              /* 'V' = void */
            s_data.sentenceCount++;
        }
        return;
    }

    /* ---- GGA : satellites, altitude (informations complémentaires) */
    if (strncmp(line + 1, "GPGGA", 5) == 0 || strncmp(line + 1, "GNGGA", 5) == 0) {
        char qual[4], nsat[6], alt[10];

        s_lastRxMs = HAL_GetTick();
        if (!field(line, 6, qual, sizeof(qual)) || qual[0] == '0') {
            return;                            /* pas de fix */
        }
        if (field(line, 7, nsat, sizeof(nsat))) {
            s_data.satellites = (uint8_t)strtoul(nsat, NULL, 10);
        }
        if (field(line, 9, alt, sizeof(alt))) {
            long a = strtol(alt, NULL, 10);
            if (a >= 0 && a <= 65535) {
                s_data.altitude_m = (uint16_t)a;
            }
        }
        s_data.sentenceCount++;
        return;
    }
}

/* ========================================================================== */
/*  Réception                                                                 */
/* ========================================================================== */
static void start_rx(void)
{
    HAL_UART_Receive_IT(s_huart, &s_rxByte, 1);
}

void Gps_Init(UART_HandleTypeDef *huart)
{
    s_huart = huart;
    s_head = s_tail = 0;
    s_lineLen = 0;
    s_lineOverflow = false;
    memset(&s_data, 0, sizeof(s_data));
    s_started = true;
    s_lastRxMs = HAL_GetTick();
    start_rx();
    printf("[GPS] USART3 (PC10/PC11) activee, %u 8N1. En attente de trames NMEA...\r\n",
           GPS_BAUDRATE);
}

void Gps_RxCpltCallback(UART_HandleTypeDef *huart)
{
    if (huart != s_huart) return;
    {
        uint16_t next = (uint16_t)((s_head + 1u) & (RX_RING_SIZE - 1u));
        if (next != s_tail) {                 /* buffer plein : caractère perdu */
            s_ring[s_head] = s_rxByte;
            s_head = next;
        }
    }
    start_rx();
}

void Gps_ErrorCallback(UART_HandleTypeDef *huart)
{
    if (huart != s_huart) return;
    __HAL_UART_CLEAR_FLAG(huart, UART_CLEAR_OREF | UART_CLEAR_NEF |
                                 UART_CLEAR_FEF  | UART_CLEAR_PEF);
    start_rx();
}

void Gps_Poll(void)
{
    if (!s_started) return;

    /* Sécurité : si la réception s'est arrêtée, on la relance */
    if (s_huart != NULL && s_huart->RxState == HAL_UART_STATE_READY) {
        start_rx();
    }

    while (s_tail != s_head) {
        char c = (char)s_ring[s_tail];
        s_tail = (uint16_t)((s_tail + 1u) & (RX_RING_SIZE - 1u));

        if (c == '$') {                       /* début de trame */
            s_lineLen = 0;
            s_lineOverflow = false;
            s_line[s_lineLen++] = c;
        } else if (c == '\r' || c == '\n') {
            if (s_lineLen > 1u && !s_lineOverflow) {
                s_line[s_lineLen] = '\0';
                parse_sentence(s_line);
            }
            s_lineLen = 0;
        } else if (s_lineLen > 0u) {
            if (s_lineLen < NMEA_MAX - 1u) {
                s_line[s_lineLen++] = c;
            } else {
                s_lineOverflow = true;
            }
        }
    }
}

void Gps_GetLast(GpsData *out)
{
    if (out != NULL) {
        *out = s_data;
    }
}

bool Gps_Error(void)
{
    if (!s_started) return false;
    /* Aucune trame NMEA reçue depuis GPS_ERROR_MS (module absent / RX coupé) */
    return (HAL_GetTick() - s_lastRxMs) > GPS_ERROR_MS;
}
