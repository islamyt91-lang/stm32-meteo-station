/* USER CODE BEGIN Header */
/**
 ******************************************************************************
  * @file    user_diskio.c
  * @brief   Diskio driver for SD card over SPI1 (PA5 SCK / PA6 MISO / PA7 MOSI,
  *          CS = PB6 "SD_CS").
  ******************************************************************************
  * @attention
  *
  * Copyright (c) 2026 STMicroelectronics.
  * All rights reserved.
  *
  * This software is licensed under terms that can be found in the LICENSE file
  * in the root directory of this software component.
  * If no LICENSE file comes with it, it is provided AS-IS.
  *
  ******************************************************************************
  */
 /* USER CODE END Header */

#ifdef USE_OBSOLETE_USER_CODE_SECTION_0
/*
 * Warning: the user section 0 is no more in use (starting from CubeMx version 4.16.0)
 * To be suppressed in the future.
 * Kept to ensure backward compatibility with previous CubeMx versions when
 * migrating projects.
 * User code previously added there should be copied in the new user sections before
 * the section contents can be deleted.
 */
/* USER CODE BEGIN 0 */
/* USER CODE END 0 */
#endif

/* USER CODE BEGIN DECL */

/* Includes ------------------------------------------------------------------*/
#include <string.h>
#include "ff_gen_drv.h"
#include "main.h"

/* Private typedef -----------------------------------------------------------*/
/* Private define ------------------------------------------------------------*/
#define SD_CS_PORT          GPIOB
#define SD_CS_PIN           GPIO_PIN_6          /* "SD_CS" du .ioc */

#define SD_dummy            0xFFu
#define SD_START_TOKEN      0xFEu               /* données : CMD17/CMD24 */

#define SD_TIMEOUT_MS       500u
#define SD_SPI_BAUD_INIT_HZ 200000u             /* <= 400 kHz au démarrage */
#define SD_SPI_BAUD_DATA_HZ 4000000u            /* après identification */

/* Instructions (bit 6 = start, bit 5 = direction) */
#define SD_CMD0             0x40u               /* GO_IDLE_STATE            */
#define SD_CMD8             0x48u               /* SEND_IF_COND (v2)        */
#define SD_CMD9             0x49u               /* SEND_CSD                 */
#define SD_CMD16            0x50u               /* SET_BLOCKLEN             */
#define SD_CMD17            0x51u               /* READ_SINGLE_BLOCK        */
#define SD_CMD24            0x58u               /* WRITE_BLOCK              */
#define SD_CMD58            0x7Au               /* READ_OCR                 */
#define SD_CMD55            0x77u               /* APP_CMD                  */
#define SD_ACMD41           0x69u               /* SD_SEND_OP_COND          */

/* Réponses */
#define SD_R1_IDLE          0x01u
#define SD_R1_ILLEGAL       0x04u

/* Private variables ---------------------------------------------------------*/
extern SPI_HandleTypeDef hspi1;

/* Disk status */
static volatile DSTATUS Stat = STA_NOINIT;
static volatile BYTE    s_cardType;             /* 0 = SDSC, 1 = SDHC/SDXC */

/* USER CODE END DECL */

/* Private function prototypes -----------------------------------------------*/
DSTATUS USER_initialize (BYTE pdrv);
DSTATUS USER_status (BYTE pdrv);
DRESULT USER_read (BYTE pdrv, BYTE *buff, DWORD sector, UINT count);
#if _USE_WRITE == 1
  DRESULT USER_write (BYTE pdrv, const BYTE *buff, DWORD sector, UINT count);
#endif /* _USE_WRITE == 1 */
#if _USE_IOCTL == 1
  DRESULT USER_ioctl (BYTE pdrv, BYTE cmd, void *buff);
#endif /* _USE_IOCTL == 1 */

Diskio_drvTypeDef  USER_Driver =
{
  USER_initialize,
  USER_status,
  USER_read,
#if  _USE_WRITE
  USER_write,
#endif  /* _USE_WRITE == 1 */
#if _USE_IOCTL == 1
  USER_ioctl,
#endif /* _USE_IOCTL == 1 */
};

/* Private functions ---------------------------------------------------------*/

/* ---- Bas niveau SPI / CS --------------------------------------------------*/

static inline void cs_low(void)
{
  HAL_GPIO_WritePin(SD_CS_PORT, SD_CS_PIN, GPIO_PIN_RESET);
}

static inline void cs_high(void)
{
  HAL_GPIO_WritePin(SD_CS_PORT, SD_CS_PIN, GPIO_PIN_SET);
}

static BYTE xchg(BYTE data)
{
  BYTE rx = SD_dummy;
  HAL_SPI_TransmitReceive(&hspi1, &data, &rx, 1, 20);
  return rx;
}

static void dummy(BYTE n)
{
  while (n--) {
    (void)xchg(SD_dummy);
  }
}

/** Change le débit SPI (MSB/s). */
static void set_spi_speed(uint32_t hz)
{
  HAL_SPI_DeInit(&hspi1);
  hspi1.Init.BaudRatePrescaler = SPI_BAUDRATEPRESCALER_256;   /* ~312 kHz @ 80 MHz */
  if (hz > 2000000u) {
    hspi1.Init.BaudRatePrescaler = SPI_BAUDRATEPRESCALER_16;  /* ~5 MHz  */
  } else if (hz > 1000000u) {
    hspi1.Init.BaudRatePrescaler = SPI_BAUDRATEPRESCALER_32;  /* ~2.5 MHz */
  } else if (hz > 400000u) {
    hspi1.Init.BaudRatePrescaler = SPI_BAUDRATEPRESCALER_64;  /* ~1.25 MHz */
  } else {
    hspi1.Init.BaudRatePrescaler = SPI_BAUDRATEPRESCALER_256; /* ~312 kHz */
  }
  HAL_SPI_Init(&hspi1);
}

/**
 * Envoie une commande cartes SD. CS est déjà bas.
 * @return réponse R1 (0xFF = timeout).
 */
static BYTE sd_cmd(BYTE cmd, DWORD arg)
{
  BYTE crc, r1;
  uint32_t t0;

  dummy(1);                                  /* une tempo avant la commande */

  xchg((BYTE)(cmd | 0x40u));
  xchg((BYTE)(arg >> 24));
  xchg((BYTE)(arg >> 16));
  xchg((BYTE)(arg >> 8));
  xchg((BYTE)arg);

  crc = 0x01u;                               /* CRC par défaut */
  if (cmd == SD_CMD0) crc = 0x95u;           /* reset     : 0x40 0x00 0x00 0x00 0x00 0x95 */
  if (cmd == SD_CMD8) crc = 0x87u;           /* 2.7-3.6V  : 0x48 0x00 0x00 0x01 0xAA 0x87 */
  xchg(crc);

  /* R1 : réponse dans les 8 premiers octets (CMD12 peut en demander plus) */
  t0 = HAL_GetTick();
  do {
    r1 = xchg(SD_dummy);
    if ((r1 & 0x80u) == 0u) {
      return r1;
    }
  } while ((HAL_GetTick() - t0) < SD_TIMEOUT_MS);

  return 0xFFu;
}

/** CMD58 : lit l'OCR, retourne true si la carte est identifiée (SDHC => CCS). */
static bool sd_read_ocr(bool *is_sdhc)
{
  BYTE r1, i, b;
  uint32_t t0;

  r1 = sd_cmd(SD_CMD58, 0u);
  if (r1 != 0x00u) {
    cs_high();
    dummy(1);
    return false;
  }
  for (i = 0; i < 4u; i++) {
    b = xchg(SD_dummy);
    if (i == 0u) {
      *is_sdhc = (b & 0x40u) != 0u;           /* CCS (bit 30) */
    }
  }
  /* attendre que la carte sorte de busy après ACMD41 */
  t0 = HAL_GetTick();
  do {
    if (xchg(SD_dummy) == 0xFFu) {
      cs_high();
      dummy(1);
      return true;
    }
  } while ((HAL_GetTick() - t0) < SD_TIMEOUT_MS);

  cs_high();
  dummy(1);
  return false;
}

/** Identifie et initialise la carte SD (SPI mode). */
static DSTATUS sd_init_card(void)
{
  BYTE r1 = 0xFFu;
  uint32_t t0;
  bool v2 = false, sdhc = false;
  BYTE i;

  Stat = STA_NOINIT;
  s_cardType = 0;

  cs_high();
  set_spi_speed(SD_SPI_BAUD_INIT_HZ);
  dummy(10u);                                /* >= 74 horloges avec CS haut */

  /* --- CMD0 : GO_IDLE_STATE, attendre R1 = 0x01 */
  cs_low();
  t0 = HAL_GetTick();
  do {
    r1 = sd_cmd(SD_CMD0, 0u);
  } while (r1 != SD_R1_IDLE && (HAL_GetTick() - t0) < SD_TIMEOUT_MS);
  if (r1 != SD_R1_IDLE) {
    cs_high();
    dummy(1);
    return Stat;                             /* aucune carte / carte non SD */
  }

  /* --- CMD8 : SEND_IF_COND (v2 uniquement) */
  r1 = sd_cmd(SD_CMD8, 0x000001AAu);
  if (r1 == SD_R1_IDLE) {
    BYTE echo[4];
    for (i = 0; i < 4u; i++) echo[i] = xchg(SD_dummy);
    v2 = (echo[2] == 0x01u && echo[3] == 0xAAu);
  } else if ((r1 & SD_R1_ILLEGAL) == 0u) {
    cs_high();
    dummy(1);
    return Stat;
  }
  /* r1 illégal (0x05) => carte v1 (MMC/SD1) : non supportée pour un projet A2 */

  /* --- ACMD41 : attendre que la carte sorte de l'état idle */
  t0 = HAL_GetTick();
  do {
    sd_cmd(SD_CMD55, 0u);                    /* APP_CMD */
    r1 = sd_cmd(SD_ACMD41, v2 ? 0x40000000u : 0u);  /* HCS */
    if (r1 == 0x00u) {
      break;
    }
  } while ((HAL_GetTick() - t0) < SD_TIMEOUT_MS);
  if (r1 != 0x00u) {
    cs_high();
    dummy(1);
    return Stat;
  }

  /* --- CMD58 : OCR, détection SDHC/SDXC (CCS) */
  if (!sd_read_ocr(&sdhc)) {
    return Stat;
  }

  /* --- CMD16 : bloc de 512 octets (SDSC uniquement, inoffensif sinon) */
  if (!sdhc) {
    cs_low();
    r1 = sd_cmd(SD_CMD16, 512u);
    cs_high();
    dummy(1);
    if (r1 != 0x00u) {
      return Stat;
    }
  }

  s_cardType = sdhc ? 1u : 0u;
  Stat = 0;
  set_spi_speed(SD_SPI_BAUD_DATA_HZ);
  return Stat;
}

/** Lit `count` secteurs de 512 octets (LBA). */
static DRESULT sd_read_sectors(BYTE *buff, DWORD sector, UINT count)
{
  uint32_t t0;
  BYTE r1;

  while (count--) {
    cs_low();
    r1 = sd_cmd(SD_CMD17, s_cardType ? sector : sector * 512u);
    if (r1 != 0x00u) {
      cs_high();
      dummy(1);
      return RES_ERROR;
    }
    /* attendre le token de données 0xFE */
    t0 = HAL_GetTick();
    do {
      r1 = xchg(SD_dummy);
    } while (r1 != SD_START_TOKEN && r1 != 0xFFu &&
             (HAL_GetTick() - t0) < SD_TIMEOUT_MS);
    if (r1 != SD_START_TOKEN) {
      cs_high();
      dummy(1);
      return RES_ERROR;
    }
    if (HAL_SPI_Receive(&hspi1, buff, 512, 200) != HAL_OK) {
      cs_high();
      dummy(1);
      return RES_ERROR;
    }
    dummy(2u);                               /* CRC */
    cs_high();
    dummy(1);
    buff += 512u;
    sector++;
  }
  return RES_OK;
}

#if _USE_WRITE == 1
/** Écrit `count` secteurs de 512 octets (LBA). */
static DRESULT sd_write_sectors(const BYTE *buff, DWORD sector, UINT count)
{
  uint32_t t0;
  BYTE r1;

  while (count--) {
    cs_low();
    r1 = sd_cmd(SD_CMD24, s_cardType ? sector : sector * 512u);
    if (r1 != 0x00u) {
      cs_high();
      dummy(1);
      return RES_ERROR;
    }
    xchg(SD_START_TOKEN);                    /* token de données */
    if (HAL_SPI_Transmit(&hspi1, (BYTE *)buff, 512, 200) != HAL_OK) {
      cs_high();
      dummy(1);
      return RES_ERROR;
    }
    dummy(2u);                               /* CRC (ignoré en SPI) */
    r1 = xchg(SD_dummy);                     /* réponse de données */
    if ((r1 & 0x1Fu) != 0x05u) {             /* 0x05 = accepted */
      cs_high();
      dummy(1);
      return RES_ERROR;
    }
    /* attendre la fin du busy (octets à 0x00, puis 0xFF) */
    t0 = HAL_GetTick();
    do {
      r1 = xchg(SD_dummy);
    } while (r1 != 0xFFu && (HAL_GetTick() - t0) < (SD_TIMEOUT_MS * 4u));
    cs_high();
    dummy(1);
    if (r1 != 0xFFu) {
      return RES_ERROR;
    }
    buff += 512u;
    sector++;
  }
  return RES_OK;
}
#endif /* _USE_WRITE == 1 */

/* USER CODE BEGIN INIT */
/**
  * @brief  Initializes a Drive
  * @param  pdrv: Physical drive number (0..)
  * @retval DSTATUS: Operation status
  */
DSTATUS USER_initialize (
	BYTE pdrv           /* Physical drive nmuber to identify the drive */
)
{
  (void)pdrv;
  return sd_init_card();
  /* USER CODE END INIT */
}

/**
  * @brief  Gets Disk Status
  * @param  pdrv: Physical drive number (0..)
  * @retval DSTATUS: Operation status
  */
DSTATUS USER_status (
	BYTE pdrv       /* Physical drive number to identify the drive */
)
{
  (void)pdrv;
  /* USER CODE BEGIN STATUS */
  return Stat;
  /* USER CODE END STATUS */
}

/**
  * @brief  Reads Sector(s)
  * @param  pdrv: Physical drive number (0..)
  * @param  *buff: Data buffer to store read data
  * @param  sector: Sector address (LBA)
  * @param  count: Number of sectors to read (1..128)
  * @retval DRESULT: Operation result
  */
DRESULT USER_read (
	BYTE pdrv,      /* Physical drive nmuber to identify the drive */
	BYTE *buff,     /* Data buffer to store read data */
	DWORD sector,   /* Sector address in LBA */
	UINT count      /* Number of sectors to read */
)
{
  (void)pdrv;
  /* USER CODE BEGIN READ */
  if (Stat & STA_NOINIT) return RES_NOTRDY;
  return sd_read_sectors(buff, sector, count);
  /* USER CODE END READ */
}

/**
  * @brief  Writes Sector(s)
  * @param  pdrv: Physical drive number (0..)
  * @param  *buff: Data to be written
  * @param  sector: Sector address (LBA)
  * @param  count: Number of sectors to write (1..128)
  * @retval DRESULT: Operation result
  */
#if _USE_WRITE == 1
DRESULT USER_write (
	BYTE pdrv,          /* Physical drive nmuber to identify the drive */
	const BYTE *buff,   /* Data to be written */
	DWORD sector,       /* Sector address in LBA */
	UINT count          /* Number of sectors to write */
)
{
  (void)pdrv;
  /* USER CODE BEGIN WRITE */
  if (Stat & STA_NOINIT) return RES_NOTRDY;
  return sd_write_sectors(buff, sector, count);
  /* USER CODE END WRITE */
}
#endif /* _USE_WRITE == 1 */

/**
  * @brief  I/O control operation
  * @param  pdrv: Physical drive number (0..)
  * @param  cmd: Control code
  * @param  *buff: Buffer to send/receive control data
  * @retval DRESULT: Operation result
  */
#if _USE_IOCTL == 1
DRESULT USER_ioctl (
	BYTE pdrv,      /* Physical drive number (0..) */
	BYTE cmd,       /* Control code */
	void *buff      /* Buffer to send/receive control data */
)
{
  (void)pdrv;
  /* USER CODE BEGIN IOCTL */
  DRESULT res = RES_ERROR;
  BYTE csd[16];

  if (Stat & STA_NOINIT) {
    return RES_NOTRDY;
  }

  switch (cmd) {
  case CTRL_SYNC:
    res = RES_OK;
    break;

  case GET_SECTOR_COUNT:
    /* CSD (CMD9) : capacité = (C_SIZE+1) * 512 Ko pour CSD v2 (SDHC),
     * formule classique  (C_SIZE+1)*2^(C_SIZE_MULT+2)*2^READ_BL_LEN pour v1 */
    cs_low();
    if (sd_cmd(SD_CMD9, 0u) == 0x00u) {
      uint32_t t0 = HAL_GetTick();
      BYTE t;
      do {
        t = xchg(SD_dummy);
      } while (t != SD_START_TOKEN && (HAL_GetTick() - t0) < SD_TIMEOUT_MS);
      if (t == SD_START_TOKEN) {
        if (HAL_SPI_Receive(&hspi1, csd, 16, 200) == HAL_OK) {
          dummy(2u);
          if (s_cardType) {                 /* CSD v2 (SDHC/SDXC) */
            uint32_t c_size = ((uint32_t)(csd[7] & 0x3Fu) << 16)
                            | ((uint32_t)csd[8] << 8)
                            |  csd[9];
            *(DWORD *)buff = (c_size + 1u) * 1024u;   /* 512-octet sectors */
            res = RES_OK;
          } else {                          /* CSD v1 (SDSC) */
            uint32_t read_bl_len = csd[5] & 0x0Fu;
            uint32_t c_size = ((uint32_t)(csd[6] & 0x03u) << 10)
                            | ((uint32_t)csd[7] << 2)
                            | ((uint32_t)csd[8] >> 6);
            uint32_t c_size_mult = ((csd[6] >> 7) & 0x01u)
                                 | ((csd[8] >> 5) & 0x03u) << 1;
            *(DWORD *)buff = (c_size + 1u)
                           * (1u << (c_size_mult + 2u))
                           * (1u << (read_bl_len - 9u));
            res = RES_OK;
          }
        }
      }
    }
    cs_high();
    dummy(1);
    break;

  case GET_BLOCK_SIZE:
    *(DWORD *)buff = 1u;                     /* taille d'effacement = 1 secteur */
    res = RES_OK;
    break;

  default:
    res = RES_PARERR;
    break;
  }
  return res;
  /* USER CODE END IOCTL */
}
#endif /* _USE_IOCTL == 1 */
