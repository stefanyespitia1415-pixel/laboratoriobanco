/*
 * Banco de Pruebas - STM32 Blue Pill (Zephyr)
 *
 * Modulos:
 *   fsm.c       maquina de estados (hilo + semaforo)
 *   actuators.c motor, servo, bocina (hilo)
 *   sensors.c   MPU-6050 (hilo + mutex) y encoder (hilo + semaforo)
 *   comm.c      UART con el ESP32 (RX por interrupcion, TX hilo + cola)
 *   led.c       LED integrado (hilo)
 *
 * Pines: en zephyr/app.overlay (nodo zephyr,user)
 * Comandos por UART desde el ESP32:  G = arrancar, E = emergencia, R = reiniciar
 * MODO AUTOMATICO: IDLE -> ACCEL -> STEADY -> FALLA -> IDLE
 */
#include <zephyr/kernel.h>
#include "fsm.h"
#include "actuators.h"
#include "sensors.h"
#include "comm.h"
#include "led.h"

#define PPV    20     /* ranuras del disco = pulsos por vuelta */
#define N_PROM 3      /* cuantas lecturas de RPM se promedian */

int main(void)
{
	if (actuadores_init() || sensores_init() || led_init()) {
		return 0;
	}
	fsm_init();
	if (comm_init()) {
		return 0;
	}

	/* historial de las ultimas N_PROM lecturas de RPM */
	int hist[N_PROM] = {0};
	int idx = 0;

	while (1) {
		k_msleep(1000);

		int p = encoder_leer_pulsos();   /* pulsos en 1 segundo */

		hist[idx] = (p * 60) / PPV;
		idx = (idx + 1) % N_PROM;

		int suma = 0;

		for (int i = 0; i < N_PROM; i++) {
			suma += hist[i];
		}

		struct telem t = {
			.est  = (uint8_t)fsm_estado(),
			.duty = (uint8_t)motor_duty(),
			.rpm  = (uint16_t)(suma / N_PROM),
			.val  = sensor_valor(),
			.ok   = (uint8_t)sensor_ok(),
		};

		comm_enviar(&t);
	}
	return 0;
}
