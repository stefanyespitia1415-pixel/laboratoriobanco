/*
 * sensors.c - Sensores de entrada.
 *   - MPU-6050 por I2C1, protegido con mutex
 *   - Encoder en PB14 con interrupcion + semaforo
 */
#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/i2c.h>
#include <stdio.h>
#include "sensors.h"

#define ZU DT_PATH(zephyr_user)

static const struct device *i2c1 = DEVICE_DT_GET(DT_NODELABEL(i2c1));
static const struct gpio_dt_spec enc = GPIO_DT_SPEC_GET(ZU, encoder_gpios);

/* ================= MPU-6050 ================= */
K_MUTEX_DEFINE(i2c_mutex);
static char diag[24] = "SIN_SENSOR";    /* resultado de la sonda I2C */
static volatile int32_t val_centi = 0;  /* aceleracion Z x100 */
static volatile int listo_sensor = 0;

int32_t sensor_valor(void)     { return val_centi; }
int sensor_ok(void)            { return listo_sensor; }
const char *sensor_diag(void)  { return diag; }

/* Pregunta "quien eres" (registro 0x75) en 0x68 y 0x69 */
static void sondear(void)
{
	uint8_t id = 0;

	if (!device_is_ready(i2c1)) {
		snprintf(diag, sizeof(diag), "I2C_NO_LISTO");
		return;
	}
	k_mutex_lock(&i2c_mutex, K_FOREVER);
	int r68 = i2c_reg_read_byte(i2c1, 0x68, 0x75, &id);
	if (r68 == 0) {
		snprintf(diag, sizeof(diag), "ACK68_ID%02X", id);
	} else if (i2c_reg_read_byte(i2c1, 0x69, 0x75, &id) == 0) {
		snprintf(diag, sizeof(diag), "ACK69_ID%02X", id);
	} else {
		snprintf(diag, sizeof(diag), "NACK_68_69");
	}
	k_mutex_unlock(&i2c_mutex);
}

#define STACK_SEN 1280
K_THREAD_STACK_DEFINE(stack_sen, STACK_SEN);
static struct k_thread hilo_sen;

static void sen_fn(void *a, void *b, void *c)
{
	uint8_t d[6] = {0};
	uint8_t dir = 0x68;
	int listo = 0;

	while (1) {
		int r = -1;

		if (device_is_ready(i2c1)) {
			k_mutex_lock(&i2c_mutex, K_FOREVER);
			if (!listo) {
				if (i2c_reg_write_byte(i2c1, 0x68, 0x6B, 0x00) == 0) {
					dir = 0x68;
					listo = 1;
				} else if (i2c_reg_write_byte(i2c1, 0x69, 0x6B, 0x00) == 0) {
					dir = 0x69;
					listo = 1;
				}
				if (listo) {
					k_msleep(100);
				}
			}
			if (listo) {
				r = i2c_burst_read(i2c1, dir, 0x3B, d, 6);
				if (r != 0) {
					listo = 0;
				}
			}
			k_mutex_unlock(&i2c_mutex);
		}

		if (r == 0) {
			int16_t z = (int16_t)((d[4] << 8) | d[5]);

			val_centi = ((int32_t)z * 981) / 16384;   /* m/s^2 x100 */
			listo_sensor = 1;
		} else {
			sondear();
			listo_sensor = 0;
			val_centi = 0;
		}
		k_msleep(200);
	}
}

/* ================= ENCODER ================= */
static atomic_t pulsos = ATOMIC_INIT(0);
K_SEM_DEFINE(enc_sem, 0, 1);
static volatile int64_t t_ultimo_pulso = 0;
static struct gpio_callback enc_cb;

static void enc_isr(const struct device *dev, struct gpio_callback *cb, uint32_t pins)
{
	atomic_inc(&pulsos);
	k_sem_give(&enc_sem);
}

int encoder_leer_pulsos(void)
{
	return (int)atomic_set(&pulsos, 0);
}

#define STACK_ENC 512
K_THREAD_STACK_DEFINE(stack_enc, STACK_ENC);
static struct k_thread hilo_enc;

static void enc_fn(void *a, void *b, void *c)
{
	while (1) {
		k_sem_take(&enc_sem, K_FOREVER);
		t_ultimo_pulso = k_uptime_get();
		k_msleep(1);
	}
}

/* ================= INIT ================= */
int sensores_init(void)
{
	if (!device_is_ready(enc.port)) {
		return -ENODEV;
	}

	/* el pull-down viene del overlay */
	gpio_pin_configure_dt(&enc, GPIO_INPUT);
	gpio_init_callback(&enc_cb, enc_isr, BIT(enc.pin));
	gpio_add_callback(enc.port, &enc_cb);
	gpio_pin_interrupt_configure_dt(&enc, GPIO_INT_EDGE_RISING);

	k_thread_create(&hilo_enc, stack_enc, STACK_ENC, enc_fn, NULL, NULL, NULL, 4, 0, K_NO_WAIT);
	k_thread_create(&hilo_sen, stack_sen, STACK_SEN, sen_fn, NULL, NULL, NULL, 6, 0, K_NO_WAIT);
	return 0;
}
