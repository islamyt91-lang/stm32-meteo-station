/**
 * @file  console.c
 * @brief Console série USART2.
 */
#include "console.h"
#include <stdio.h>

#define RX_RING_SIZE 128u   /* puissance de 2 */

static UART_HandleTypeDef *s_huart;
static uint8_t           s_rxByte;
static volatile uint8_t  s_ring[RX_RING_SIZE];
static volatile uint16_t s_head, s_tail;
static volatile bool     s_activity;

static char   s_line[CONSOLE_LINE_MAX];
static size_t s_len;

/* ---- printf -> USART2 (appelé par _write() de syscalls.c) ---------------- */
int __io_putchar(int ch)
{
    uint8_t c = (uint8_t)ch;
    if (s_huart != NULL) {
        HAL_UART_Transmit(s_huart, &c, 1, 10);
    }
    return ch;
}

static void tx(const char *s, uint16_t n)
{
    HAL_UART_Transmit(s_huart, (uint8_t *)s, n, 50);
}

static void start_rx(void)
{
    HAL_UART_Receive_IT(s_huart, &s_rxByte, 1);
}

void Console_Init(UART_HandleTypeDef *huart)
{
    s_huart = huart;
    s_head = s_tail = 0;
    s_len = 0;
    setvbuf(stdout, NULL, _IONBF, 0);   /* pas de tampon : affichage immédiat */
    start_rx();
}

void Console_RxCpltCallback(UART_HandleTypeDef *huart)
{
    if (huart != s_huart) return;
    uint16_t next = (uint16_t)((s_head + 1u) & (RX_RING_SIZE - 1u));
    if (next != s_tail) {            /* buffer plein : caractère perdu */
        s_ring[s_head] = s_rxByte;
        s_head = next;
    }
    s_activity = true;
    start_rx();
}

void Console_ErrorCallback(UART_HandleTypeDef *huart)
{
    if (huart != s_huart) return;
    /* Overrun / bruit : on efface les flags et on relance la réception,
     * sinon la console resterait muette. */
    __HAL_UART_CLEAR_FLAG(huart, UART_CLEAR_OREF | UART_CLEAR_NEF |
                                 UART_CLEAR_FEF  | UART_CLEAR_PEF);
    start_rx();
}

bool Console_TakeActivity(void)
{
    bool a = s_activity;
    s_activity = false;
    return a;
}

bool Console_GetLine(char *line, size_t size)
{
    /* Sécurité : si la réception s'est arrêtée (erreur non signalée), relance */
    if (s_huart != NULL && s_huart->RxState == HAL_UART_STATE_READY) {
        start_rx();
    }

    while (s_tail != s_head) {
        char c = (char)s_ring[s_tail];
        s_tail = (uint16_t)((s_tail + 1u) & (RX_RING_SIZE - 1u));

        if (c == '\r' || c == '\n') {
            if (s_len == 0) {
                continue;                 /* ignore \n après \r, lignes vides */
            }
            tx("\r\n", 2);
            size_t n = (s_len < size - 1) ? s_len : size - 1;
            for (size_t i = 0; i < n; i++) line[i] = s_line[i];
            line[n] = '\0';
            s_len = 0;
            return true;
        }
        if (c == '\b' || c == 0x7F) {      /* retour arrière */
            if (s_len > 0) {
                s_len--;
                tx("\b \b", 3);
            }
            continue;
        }
        if (c >= 0x20 && c < 0x7F && s_len < CONSOLE_LINE_MAX - 1) {
            s_line[s_len++] = c;
            tx(&c, 1);                    /* écho */
        }
    }
    return false;
}
