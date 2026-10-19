/**
 * @file    sensor_test.h
 * @brief   Test simple du DHT11 (temperature, humidite) et du BH1750 (luminosite)
 *          avec affichage sur le port serie du ST-Link (USART2, 115200 bauds).
 */
#ifndef SENSOR_TEST_H
#define SENSOR_TEST_H

void SensorTest_Init(void);      /* a appeler une fois (USER CODE 2) */
void SensorTest_Process(void);   /* a appeler dans while(1) (USER CODE 3) */

#endif
