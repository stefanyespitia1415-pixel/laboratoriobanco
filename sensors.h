/* sensors.h - MPU-6050 (I2C) y encoder (PB14) */
#ifndef SENSORS_H
#define SENSORS_H

#include <stdint.h>

int  sensores_init(void);            /* configura el encoder y crea los hilos; 0 = ok */

/* MPU-6050 */
int32_t sensor_valor(void);          /* aceleracion Z en m/s^2 x100 */
int  sensor_ok(void);                /* 1 = lectura buena, 0 = sensor no listo */
const char *sensor_diag(void);       /* resultado de la sonda I2C */

/* Encoder */
int  encoder_leer_pulsos(void);      /* pulsos desde la ultima llamada (y los pone en 0) */

#endif
