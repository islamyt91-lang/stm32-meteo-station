/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file    user_diskio.c
  * @brief   Interface FatFs <-> carte SD en SPI (sd_spi.c).
  *          Ce fichier REMPLACE celui généré par CubeMX.
  ******************************************************************************
  */
/* USER CODE END Header */
#include <string.h>
#include "ff_gen_drv.h"
#include "user_diskio.h"
#include "sd_spi.h"

static volatile DSTATUS Stat = STA_NOINIT;

/* Table interne du gestionnaire de pilotes ST (ff_gen_drv.c) */
extern Disk_drvTypeDef disk;

static DSTATUS USER_initialize(BYTE pdrv);
static DSTATUS USER_status(BYTE pdrv);
static DRESULT USER_read(BYTE pdrv, BYTE *buff, DWORD sector, UINT count);
static DRESULT USER_write(BYTE pdrv, const BYTE *buff, DWORD sector, UINT count);
static DRESULT USER_ioctl(BYTE pdrv, BYTE cmd, void *buff);

/* L'ordre des champs suit ff_gen_drv.h (FatFs R0.12c et R0.15) */
Diskio_drvTypeDef USER_Driver =
{
  USER_initialize,
  USER_status,
  USER_read,
#if !defined(_USE_WRITE) || (_USE_WRITE == 1)
  USER_write,
#endif
#if !defined(_USE_IOCTL) || (_USE_IOCTL == 1)
  USER_ioctl,
#endif
};

void USER_ForceReinit(void)
{
  Stat = STA_NOINIT;
  SD_SPI_MarkNotReady();
  disk.is_initialized[0] = 0;
}

static DSTATUS USER_initialize(BYTE pdrv)
{
  (void)pdrv;
  Stat = SD_SPI_Init() ? 0 : STA_NOINIT;
  return Stat;
}

static DSTATUS USER_status(BYTE pdrv)
{
  (void)pdrv;
  if (!SD_SPI_IsReady()) Stat |= STA_NOINIT;
  return Stat;
}

static DRESULT USER_read(BYTE pdrv, BYTE *buff, DWORD sector, UINT count)
{
  (void)pdrv;
  if (Stat & STA_NOINIT) return RES_NOTRDY;
  if (SD_SPI_Read(buff, (uint32_t)sector, count)) return RES_OK;
  Stat = STA_NOINIT;
  return RES_ERROR;
}

static DRESULT USER_write(BYTE pdrv, const BYTE *buff, DWORD sector, UINT count)
{
  (void)pdrv;
  if (Stat & STA_NOINIT) return RES_NOTRDY;
  if (SD_SPI_Write(buff, (uint32_t)sector, count)) return RES_OK;
  Stat = STA_NOINIT;
  return RES_ERROR;
}

static DRESULT USER_ioctl(BYTE pdrv, BYTE cmd, void *buff)
{
  (void)pdrv;
  if (Stat & STA_NOINIT) return RES_NOTRDY;

  switch (cmd)
  {
  case CTRL_SYNC:
    return SD_SPI_Sync() ? RES_OK : RES_ERROR;
  case GET_SECTOR_COUNT:
  {
    uint32_t n = SD_SPI_SectorCount();
    if (n == 0) return RES_ERROR;
    *(DWORD *)buff = n;
    return RES_OK;
  }
  case GET_SECTOR_SIZE:
    *(WORD *)buff = 512;
    return RES_OK;
  case GET_BLOCK_SIZE:
    *(DWORD *)buff = 1;   /* taille d'effacement inconnue */
    return RES_OK;
  default:
    return RES_PARERR;
  }
}