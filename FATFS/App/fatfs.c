/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file   fatfs.c
  * @brief  Code for fatfs applications
  ******************************************************************************
  * @attention
  *
  * Copyright (c) 2026 STMicroelectronics.
  * All rights reserved.
  *
  * This software is licensed under terms that can be found in the LICENSE file
  * in the root directory of this software component.
  * If no LICENSE file comes with this software, it is provided AS-IS.
  *
  ******************************************************************************
  */
/* USER CODE END Header */
#include "fatfs.h"

/* USER CODE BEGIN Includes */
#include "rtc_clock.h"
/* USER CODE END Includes */

uint8_t retUSER;    /* Return value for USER */
char USERPath[4];   /* USER logical drive path */
FATFS USERFatFS;    /* File system object for USER logical drive */
FIL USERFile;       /* File object for USER */

/* USER CODE BEGIN Variables */

/* USER CODE END Variables */

void MX_FATFS_Init(void)
{
  /*## FatFS: Link the USER driver ###########################*/
  retUSER = FATFS_LinkDriver(&USER_Driver, USERPath);

  /* USER CODE BEGIN Init */
  /* additional user code for init */
  /* USER CODE END Init */
}

/**
  * @brief  Gets Time from RTC
  * @param  None
  * @retval Time in DWORD
  */
DWORD get_fattime(void)
{
  /* USER CODE BEGIN get_fattime */
  RtcDateTime dt;
  if (!RtcClock_Get(&dt)) {
    return 0;                      /* horodatage inconnu (RTC absente) */
  }
  if (dt.year < 1980u) {
    return 0;
  }
  return ((DWORD)(dt.year - 1980u) << 25)
       | ((DWORD)(dt.month & 0x0Fu)   << 21)
       | ((DWORD)(dt.day   & 0x1Fu)   << 16)
       | ((DWORD)(dt.hours & 0x1Fu)   << 11)
       | ((DWORD)(dt.minutes & 0x3Fu) << 5)
       | ((DWORD)(dt.seconds / 2u) & 0x1Fu);
  /* USER CODE END get_fattime */
}

/* USER CODE BEGIN Application */

/* USER CODE END Application */
