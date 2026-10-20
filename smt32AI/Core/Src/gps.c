/**
 * @file  gps.c
 * @brief GPS NMEA.
 *
 * L'interruption USART3 range chaque octet dans un buffer circulaire ;
 * GPS_Process() (boucle principale) reconstitue les lignes, vérifie le
 * checksum "*hh" et décode $xxRMC :
 *   $GPRMC,hhmmss.ss,A,llll.ll,N,yyyyy.yy,E,vitesse,cap,ddmmyy,,,A*hh
 *           temps    |  latitude    longitude
 *                    statut A = position valide, V = pas de fix
 *
 * Pas de float : les coordonnées sont stockées en millionièmes de degré.
 */
#include "gps.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

#define RX_RING_SIZE  512u        /* puissance de 2 */
#define LINE_MAX      100u

static UART_HandleTypeDef *s_huart;
static uint8_t            s_rxByte;
static volatile uint8_t   s_ring[RX_RING_SIZE];
static volatile uint16_t  s_head, s_tail;

static char     s_line[LINE_MAX];
static uint16_t s_len;

static GpsFix   s_last;           /* dernière trame RMC décodée */
static bool     s_haveLast;
static uint32_t s_frames;

/* ------------------------------------------------------------------------ */
static void start_rx(void)
{
    HAL_UART_Receive_IT(s_huart, &s_rxByte, 1);
}

void GPS_Init(UART_HandleTypeDef *huart)
{
    s_huart = huart;
    s_head = s_tail = 0;
    s_len = 0;
    s_haveLast = false;
    s_frames = 0;
    if (s_huart != NULL) {
        start_rx();
    }
}

void GPS_RxCpltCallback(UART_HandleTypeDef *huart)
{
    if (huart != s_huart) return;
    uint16_t next = (uint16_t)((s_head + 1u) & (RX_RING_SIZE - 1u));
    if (next != s_tail) {
        s_ring[s_head] = s_rxByte;
        s_head = next;
    }
    start_rx();
}

void GPS_ErrorCallback(UART_HandleTypeDef *huart)
{
    if (huart != s_huart) return;
    __HAL_UART_CLEAR_FLAG(huart, UART_CLEAR_OREF | UART_CLEAR_NEF |
                                 UART_CLEAR_FEF  | UART_CLEAR_PEF);
    start_rx();
}

/* ------------------------------------------------------------------------ */
static int hexval(char c)
{
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    return -1;
}

static bool checksum_ok(const char *s)
{
    /* s commence par '$' ; XOR des caractères entre '$' et '*' */
    uint8_t x = 0;
    const char *p = s + 1;
    while (*p && *p != '*') x ^= (uint8_t)*p++;
    if (*p != '*') return false;
    int h = hexval(p[1]), l = hexval(p[2]);
    return h >= 0 && l >= 0 && (uint8_t)((h << 4) | l) == x;
}

/** Découpe sur ',' en conservant les champs vides. */
static int split(char *s, char **fields, int max)
{
    int n = 0;
    fields[n++] = s;
    while (*s && n < max) {
        if (*s == ',' || *s == '*') {
            *s = '\0';
            fields[n++] = s + 1;
        }
        s++;
    }
    return n;
}

/**
 * Convertit "ddmm.mmmm" (ou "dddmm.mmmm") en millionièmes de degré.
 * degrés = dd + mm.mmmm / 60
 */
static bool parse_coord(const char *s, char hemi, int32_t *out)
{
    const char *dot = strchr(s, '.');
    if (s[0] == '\0' || dot == NULL || (dot - s) < 3) return false;

    int degDigits = (int)(dot - s) - 2;      /* 2 (latitude) ou 3 (longitude) */
    int32_t deg = 0;
    for (int i = 0; i < degDigits + 2; i++) {
        if (s[i] < '0' || s[i] > '9') return false;
    }
    for (int i = 0; i < degDigits; i++) {
        deg = deg * 10 + (s[i] - '0');
    }
    int32_t minInt = (s[degDigits] - '0') * 10 + (s[degDigits + 1] - '0');

    /* partie décimale des minutes, ramenée à 5 chiffres */
    int32_t frac = 0;
    int nd = 0;
    for (const char *p = dot + 1; *p && nd < 5; p++, nd++) {
        if (*p < '0' || *p > '9') return false;
        frac = frac * 10 + (*p - '0');
    }
    while (nd < 5) { frac *= 10; nd++; }

    int32_t min_e5 = minInt * 100000 + frac;          /* minutes x 1e5 */
    int32_t v = deg * 1000000 + (int32_t)(((int64_t)min_e5 * 10) / 60);
    if (hemi == 'S' || hemi == 'W') v = -v;
    *out = v;
    return true;
}

static void handle_sentence(char *s)
{
    char *f[20];

    if (s[0] != '$' || !checksum_ok(s)) return;
    s_frames++;

    int n = split(s + 1, f, 20);
    /* f[0] = "GPRMC" / "GNRMC" ... */
    if (n < 7 || strlen(f[0]) != 5 || strcmp(f[0] + 2, "RMC") != 0) return;

    GpsFix fx = {0};
    fx.tick = HAL_GetTick();
    fx.fix = (f[2][0] == 'A');
    if (fx.fix) {
        if (!parse_coord(f[3], f[4][0], &fx.lat_e6) ||
            !parse_coord(f[5], f[6][0], &fx.lon_e6)) {
            fx.fix = false;
        }
    }
    s_last = fx;
    s_haveLast = true;
}

void GPS_Process(void)
{
    if (s_huart == NULL) return;

    /* Réception arrêtée suite à une erreur non signalée : on relance */
    if (s_huart->RxState == HAL_UART_STATE_READY) {
        start_rx();
    }

    while (s_tail != s_head) {
        char c = (char)s_ring[s_tail];
        s_tail = (uint16_t)((s_tail + 1u) & (RX_RING_SIZE - 1u));

        if (c == '$') {                  /* début de trame : on repart à zéro */
            s_len = 0;
        }
        if (c == '\r' || c == '\n') {
            if (s_len > 0) {
                s_line[s_len] = '\0';
                handle_sentence(s_line);
                s_len = 0;
            }
            continue;
        }
        if (s_len < LINE_MAX - 1u) {
            s_line[s_len++] = c;
        } else {
            s_len = 0;                   /* ligne trop longue : ignorée */
        }
    }
}

bool GPS_GetSince(uint32_t sinceTick, GpsFix *out)
{
    if (!s_haveLast || (int32_t)(s_last.tick - sinceTick) < 0) {
        return false;
    }
    if (out != NULL) *out = s_last;
    return true;
}

uint32_t GPS_FrameCount(void)
{
    return s_frames;
}

void GPS_FormatCoord(char *buf, unsigned n, int32_t v)
{
    const char *sign = (v < 0) ? "-" : "";
    if (v < 0) v = -v;
    snprintf(buf, n, "%s%ld.%06ld", sign, (long)(v / 1000000), (long)(v % 1000000));
}
