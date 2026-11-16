/*
 * comm.c - UART2 hacia el ESP32.
 *   RX: interrupcion; G / E / R se pasan a la FSM (que despierta por semaforo)
 *   TX: hilo que saca de la cola (k_msgq), arma el JSON y le agrega el CRC-16
 */
#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/uart.h>
#include <stdio.h>
#include <stdlib.h>
#include "comm.h"
#include "fsm.h"
#include "sensors.h"

static const struct device *uart = DEVICE_DT_GET(DT_NODELABEL(usart2));

K_MSGQ_DEFINE(cola, sizeof(struct telem), 4, 4);

/* ---------- CRC-16/CCITT (poly 0x1021, inicio 0xFFFF) ---------- */
static uint16_t crc16(const uint8_t *datos, size_t n)
{
	uint16_t crc = 0xFFFF;

	for (size_t i = 0; i < n; i++) {
		crc ^= (uint16_t)datos[i] << 8;
		for (int b = 0; b < 8; b++) {
			if (crc & 0x8000) {
				crc = (uint16_t)((crc << 1) ^ 0x1021);
			} else {
				crc = (uint16_t)(crc << 1);
			}
		}
	}
	return crc;
}

/* ---------- RX por interrupcion ---------- */
static int leyendo_pwm;   /* 1 = llego una 'P' y se estan leyendo los digitos */
static int pwm_val;       /* numero armado con los digitos; -1 = aun no llega ninguno */

static void rx_isr(const struct device *dev, void *user_data)
{
	uint8_t c;

	if (!uart_irq_update(dev)) {
		return;
	}
	while (uart_irq_rx_ready(dev)) {
		if (uart_fifo_read(dev, &c, 1) != 1) {
			break;
		}
		if (c == 'G' || c == 'E' || c == 'R') {
			fsm_comando((char)c);
		} else if (c == 'P') {
			/* empieza "P<numero>\n", por ejemplo P60\n */
			leyendo_pwm = 1;
			pwm_val = -1;
		} else if (leyendo_pwm) {
			if (c >= '0' && c <= '9') {
				pwm_val = (pwm_val < 0 ? 0 : pwm_val) * 10 + (c - '0');
				if (pwm_val > 100) {
					pwm_val = 100;
				}
			} else {
				/* fin de la palabra (\n o \r): si hubo digitos, se pasa a la FSM */
				if ((c == '\n' || c == '\r') && pwm_val >= 0) {
					fsm_pwm(pwm_val);
				}
				leyendo_pwm = 0;
			}
		}
	}
}

/* ---------- HILO TX ---------- */
#define STACK_TX 1280
K_THREAD_STACK_DEFINE(stack_tx, STACK_TX);
static struct k_thread hilo_tx;

static void tx_fn(void *a, void *b, void *c)
{
	struct telem t;
	char linea[240];

	while (1) {
		k_msgq_get(&cola, &t, K_FOREVER);

		int32_t v = (t.ok == 1) ? t.val : 0;
		const char *sg = (v < 0) ? "-" : "";
		int32_t av = abs(v);

		const char *nom = (t.ok == 1) ? "MPU6050" :
				  (t.ok == 2) ? "ERR_LECTURA" : sensor_diag();

		/* 1) cuerpo del JSON (sin la llave final) */
		int n = snprintf(linea, sizeof(linea),
			"{\"nodo\":\"STM32\",\"planta\":\"BANCO\",\"fsm_state\":\"%s\","
			"\"pwm_duty\":%d,\"rpm\":%d,\"i2c_sensor\":\"%s\","
			"\"val_i2c\":%s%d.%02d",
			fsm_nombre(t.est), t.duty, t.rpm, nom, sg, (int)(av / 100), (int)(av % 100));

		if (n < 0 || n >= (int)sizeof(linea) - 24) {
			continue;   /* no cabe: se descarta esta trama */
		}

		/* 2) CRC-16 del cuerpo, y se agrega al final */
		uint16_t crc = crc16((const uint8_t *)linea, (size_t)n);

		n += snprintf(linea + n, sizeof(linea) - n, ",\"crc\":\"%04X\"}\n", crc);

		for (int i = 0; i < n; i++) {
			uart_poll_out(uart, linea[i]);
		}
	}
}

void comm_enviar(const struct telem *t)
{
	k_msgq_put(&cola, t, K_NO_WAIT);
}

int comm_init(void)
{
	if (!device_is_ready(uart)) {
		return -ENODEV;
	}

	uart_irq_callback_user_data_set(uart, rx_isr, NULL);
	uart_irq_rx_enable(uart);

	k_thread_create(&hilo_tx, stack_tx, STACK_TX, tx_fn, NULL, NULL, NULL, 7, 0, K_NO_WAIT);
	return 0;
}