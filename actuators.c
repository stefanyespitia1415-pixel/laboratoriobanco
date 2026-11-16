/*
 * actuators.c - Motor, servo y bocina.
 * Un hilo genera lo que va "por software": pulso del servo (PB0),
 * PWM del ENA del motor (PB8) y el patron de la bocina (PA8).
 */
#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/pwm.h>
#include "actuators.h"

#define ZU DT_PATH(zephyr_user)

static const struct gpio_dt_spec in1   = GPIO_DT_SPEC_GET(ZU, motor_in1_gpios);
static const struct gpio_dt_spec in2   = GPIO_DT_SPEC_GET(ZU, motor_in2_gpios);
static const struct gpio_dt_spec ena   = GPIO_DT_SPEC_GET(ZU, motor_ena_gpios);
static const struct gpio_dt_spec servo = GPIO_DT_SPEC_GET(ZU, servo_gpios);
static const struct device *pwm_b = DEVICE_DT_GET(DT_NODELABEL(pwm1));   /* bocina PA8 */

static volatile int duty = 0;
static volatile uint32_t servo_us = 600;                 /* ancho del pulso (us) */
static volatile enum bocina_patron patron = BOC_NADA;

void motor_set(int d)
{
	if (d < 0) d = 0;
	if (d > 100) d = 100;
	duty = d;
	gpio_pin_set_dt(&in1, d > 0 ? 1 : 0);
	gpio_pin_set_dt(&in2, 0);
	/* el PWM del ENA lo genera el hilo */
}

int motor_duty(void)
{
	return duty;
}

void servo_set(int grados)
{
	if (grados < 0) grados = 0;
	if (grados > 180) grados = 180;
	servo_us = 600U + 10U * (uint32_t)grados;           /* 600 a 2400 us */
}

void bocina_set(enum bocina_patron p)
{
	patron = p;
}

/* hz = 0 silencia, otro valor = tono a esa frecuencia */
static void tono(uint32_t hz)
{
	if (!device_is_ready(pwm_b)) {
		return;
	}
	if (hz == 0) {
		if (pwm_set(pwm_b, 1, 0, 0, PWM_POLARITY_NORMAL) != 0) {
			pwm_set(pwm_b, 1, 1000000, 0, PWM_POLARITY_NORMAL);
		}
		return;
	}
	uint32_t periodo = 1000000000U / hz;   /* ns */
	pwm_set(pwm_b, 1, periodo, periodo / 2, PWM_POLARITY_NORMAL);
}

#define STACK_ACT 640
K_THREAD_STACK_DEFINE(stack_act, STACK_ACT);
static struct k_thread hilo_act;

static void act_fn(void *a, void *b, void *c)
{
	uint32_t f_prev = 0xFFFFFFFFU;

	while (1) {
		/* BOCINA: cada patron suena distinto */
		uint32_t ms = k_uptime_get_32();
		uint32_t f = 0;

		switch (patron) {
		case BOC_ACCEL: f = ((ms % 1000) < 100) ? 600  : 0; break;
		case BOC_FALLA: f = ((ms % 800)  < 300) ? 1000 : 0; break;
		case BOC_EMERG: f = ((ms % 200)  < 100) ? 2000 : 0; break;
		default:        f = 0; break;
		}
		if (f != f_prev) {
			tono(f);
			f_prev = f;
		}

		/* SERVO en PB0: un pulso por vuelta del bucle (~50 Hz) */
		gpio_pin_set_dt(&servo, 1);
		k_busy_wait(servo_us);
		gpio_pin_set_dt(&servo, 0);

		/* MOTOR en PB8 (ENA): PWM por software, periodo ~20 ms.
		 * duty 5 = 1 ms en alto ... duty 100 = siempre en alto */
		int alto = duty / 5;

		if (alto > 20) alto = 20;
		if (alto > 0) {
			gpio_pin_set_dt(&ena, 1);
			k_msleep(alto);
		}
		if (alto < 20) {
			gpio_pin_set_dt(&ena, 0);
			k_msleep(20 - alto);
		}
	}
}

int actuadores_init(void)
{
	if (!device_is_ready(in1.port) || !device_is_ready(in2.port) ||
	    !device_is_ready(ena.port) || !device_is_ready(servo.port)) {
		return -ENODEV;
	}

	gpio_pin_configure_dt(&ena,   GPIO_OUTPUT_INACTIVE);
	gpio_pin_configure_dt(&in1,   GPIO_OUTPUT_INACTIVE);
	gpio_pin_configure_dt(&in2,   GPIO_OUTPUT_INACTIVE);
	gpio_pin_configure_dt(&servo, GPIO_OUTPUT_INACTIVE);

	tono(0);   /* bocina en silencio desde el arranque */

	k_thread_create(&hilo_act, stack_act, STACK_ACT, act_fn, NULL, NULL, NULL, 5, 0, K_NO_WAIT);
	return 0;
}
