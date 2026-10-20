/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file    user_diskio.h
  * @brief   Pilote FatFs "User-defined" : carte SD en SPI (voir sd_spi.c)
  ******************************************************************************
  */
/* USER CODE END Header */
#ifndef __USER_DISKIO_H
#define __USER_DISKIO_H

#ifdef __cplusplus
extern "C" {
#endif

#include "ff_gen_drv.h"

extern Diskio_drvTypeDef USER_Driver;

/** Force une réinitialisation de la carte au prochain f_mount
 *  (après un retrait / une erreur). */
void USER_ForceReinit(void);

#ifdef __cplusplus
}
#endif

#endif /* __USER_DISKIO_H */