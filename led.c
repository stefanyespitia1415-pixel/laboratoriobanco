/*
 * led.c - LED integrado en su propio hilo.
 * Lento = sistema vivo, rapido = EMERGENCIA.
 */
#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/gpio.h>
#include "led.h"
#include "fsm.h"

#define LED_NORMAL_MS 500     /* cambia cada 500 ms */
#define LED_EMERG_MS  100     /* en EMERGENCIA cada 100 ms */

static const struct gpio_dt_spec led = GPIO_DT_SPEC_GET(DT_PATH(zephyr_user), led_gpios);

#define STACK_LED 384
K_THREAD_STACK_DEFINE(stack_led, STACK_LED);
static struct k_thread hilo_led;

static void led_fn(void *a, void *b, void *c)
{
	while (1) {
		gpio_pin_toggle_dt(&led);
		k_msleep(fsm_estado() == ST_EMERG ? LED_EMERG_MS : LED_NORMAL_MS);
	}
}

int led_init(void)
{
	if (!device_is_ready(led.port)) {
		return -ENODEV;
	}
	gpio_pin_configure_dt(&led, GPIO_OUTPUT_INACTIVE);   /* apagado */

	k_thread_create(&hilo_led, stack_led, STACK_LED, led_fn, NULL, NULL, NULL, 8, 0, K_NO_WAIT);
	return 0;
}
