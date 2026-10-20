/**
 * @file    www_config.h
 * @brief   Worldwide Weather Watcher - réglages de compilation et brochage.
 *
 * Tout ce qui se règle AVANT compilation est ici. Les paramètres modifiables
 * à l'exécution (LOG_INTERVAL, TIMEOUT, seuils...) sont dans settings.h.
 */
#ifndef WWW_CONFIG_H
#define WWW_CONFIG_H

#include "stm32l4xx_hal.h"

/* ----------------------------------------------------------------------------
 * Version / traçabilité (commande VERSION)
 * ------------------------------------------------------------------------- */
#define WWW_VERSION              "1.2.0"
#define WWW_LOT                  "LOT-2026-A2-001"

/* ----------------------------------------------------------------------------
 * TESTS RAPIDES
 *  0 : intervalle par défaut = 10 minutes (comportement du cahier des charges)
 *  1 : intervalle par défaut = 10 secondes, mode configuration quitté au bout
 *      de 60 s d'inactivité au lieu de 30 min (pour les démos / tests).
 * À l'exécution on peut aussi taper LOG_INTERVAL_S=10 en mode configuration.
 * ------------------------------------------------------------------------- */
#ifndef WWW_FAST_TEST
#define WWW_FAST_TEST            0
#endif

#if WWW_FAST_TEST
#define WWW_DEFAULT_LOG_INTERVAL_S   10u
#define WWW_CONFIG_IDLE_TIMEOUT_MS   (60u * 1000u)
#else
#define WWW_DEFAULT_LOG_INTERVAL_S   (10u * 60u)
#define WWW_CONFIG_IDLE_TIMEOUT_MS   (30u * 60u * 1000u)
#endif

/* ----------------------------------------------------------------------------
 * ACTIVATION DE LA CARTE SD ET DU GPS
 *  0 : désactivé -> comportement identique à la v2 (6 colonnes, aucune
 *      erreur SD ou GPS, LED verte en mode standard)
 *  1 : activé    -> enregistrement sur carte SD / colonnes LATITUDE;LONGITUDE
 * Activer l'un puis l'autre, en vérifiant le montage à chaque fois.
 * ------------------------------------------------------------------------- */
#define WWW_ENABLE_SD                1
#define WWW_ENABLE_GPS               0

/* Période d'affichage en mode maintenance (>= 2 s à cause du DHT11) */
#define WWW_MAINT_PERIOD_MS          5000u

/* Durée d'un appui long (cahier des charges : 5 s) */
#define WWW_LONG_PRESS_MS            5000u
#define WWW_DEBOUNCE_MS              30u

/* Ambiguïté du sujet : depuis le mode économique, un appui long sur le bouton
 * rouge mène soit au mode standard (paragraphe "économique"), soit au mode
 * maintenance (paragraphe "maintenance").
 *  0 : ECO + rouge 5 s -> STANDARD   (choix par défaut)
 *  1 : ECO + rouge 5 s -> MAINTENANCE (retour en ECO en sortant)            */
#define WWW_ECO_RED_GOES_TO_MAINT    0

/* ----------------------------------------------------------------------------
 * Brochage (NUCLEO-L476RG)
 * ------------------------------------------------------------------------- */
/* LED RGB : PA0 = rouge, PA1 = vert, PA4 = bleu */
#define RGB_R_PORT    GPIOA
#define RGB_R_PIN     GPIO_PIN_0
#define RGB_G_PORT    GPIOA
#define RGB_G_PIN     GPIO_PIN_1
#define RGB_B_PORT    GPIOA
#define RGB_B_PIN     GPIO_PIN_4
/* 0 = cathode commune (1 logique = allumé), 1 = anode commune (logique inversée) */
#define RGB_COMMON_ANODE   1

/* Boutons (actifs à l'état bas, pull-up) :
 *  - bouton ROUGE = B1 bleu de la carte (PC13)
 *  - bouton VERT  = bouton externe (PC0 = A5)
 * Inverser ici si votre montage est différent. */
#define BTN_RED_PORT    GPIOC
#define BTN_RED_PIN     GPIO_PIN_13
#define BTN_GREEN_PORT  GPIOC
#define BTN_GREEN_PIN   GPIO_PIN_0

/* DHT11 : PB0 (open-drain + pull-up) */
#define DHT11_PORT      GPIOB
#define DHT11_PIN       GPIO_PIN_0

/* BH1750 : adresse 7 bits 0x23 (ADDR à GND) ou 0x5C (ADDR à VCC) */
#define BH1750_ADDR_7BIT   0x23u

/* Carte SD : SPI1 (PA5 SCK = D13, PA6 MISO = D12, PA7 MOSI = D11),
 * CS sur PB6 (D10) */
#define SD_CS_PORT      GPIOB
#define SD_CS_PIN       GPIO_PIN_6

/* GPS : USART3 (PC10 TX -> RX du GPS, PC11 RX <- TX du GPS), 9600 bauds */

#endif /* WWW_CONFIG_H */
