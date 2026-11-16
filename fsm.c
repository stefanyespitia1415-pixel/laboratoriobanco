/*
 * fsm.c - FSM con semaforo.
 *
 * El hilo duerme en k_sem_take():
 *   - si el semaforo se da (llego un comando) -> se atiende el comando
 *   - si se acaba el tiempo (timeout)         -> vencio el estado, se pasa al siguiente
 */
#include <zephyr/kernel.h>
#include "fsm.h"
#include "actuators.h"

#define VIB_SIM    0      /* 0 = no hay falla, vuelve a IDLE y repite. 100 = EMERGENCIA */
#define VIB_LIMITE 50

#define T_IDLE_MS    2000
#define T_ACCEL_MS   200     /* cada cuanto sube el PWM +5 % */
#define T_STEADY_MS  5000
#define T_FALLA_MS   2000

static const char *nombres[] = {"IDLE", "ACCEL", "STEADY", "FALLA", "EMERGENCIA"};

static volatile int estado = ST_IDLE;
static int64_t vence = -1;               /* instante (ms) en que vence el estado; -1 = sin limite */

static atomic_t comando = ATOMIC_INIT(0);
static atomic_t pwm_pedido = ATOMIC_INIT(-1);   /* duty pedido por el slider (%); -1 = nada */
static int consigna = 100;                      /* velocidad (%) hasta donde sube ACCEL (solo la toca el hilo FSM) */
K_SEM_DEFINE(fsm_sem, 0, 1);

#define STACK_FSM 768
K_THREAD_STACK_DEFINE(stack_fsm, STACK_FSM);
static struct k_thread hilo_fsm;

/* Entra a un estado nuevo: pone actuadores y su tiempo limite */
static void entrar(int nuevo)
{
	int64_t ahora = k_uptime_get();

	estado = nuevo;
	vence = -1;

	switch (nuevo) {
	case ST_IDLE:
		motor_set(0); servo_set(0); bocina_set(BOC_NADA);
		vence = ahora + T_IDLE_MS;
		break;
	case ST_ACCEL:
		motor_set(0); servo_set(90); bocina_set(BOC_ACCEL);
		vence = ahora + T_ACCEL_MS;
		break;
	case ST_STEADY:
		servo_set(180); bocina_set(BOC_NADA);
		vence = ahora + T_STEADY_MS;
		break;
	case ST_FALLA:
		bocina_set(BOC_FALLA);
		vence = ahora + T_FALLA_MS;
		break;
	case ST_EMERG:
		motor_set(0); servo_set(0); bocina_set(BOC_EMERG);
		/* sin limite: solo sale con la 'R' */
		break;
	}
}

/* Se acabo el tiempo del estado actual */
static void vencio(void)
{
	switch (estado) {
	case ST_IDLE:
		entrar(ST_ACCEL);                 /* AUTOMATICO: arranca solo */
		break;
	case ST_ACCEL:
		if (motor_duty() < consigna) {
			int n = motor_duty() + 5;

			motor_set(n > consigna ? consigna : n);
			vence = k_uptime_get() + T_ACCEL_MS;
		} else {
			entrar(ST_STEADY);
		}
		break;
	case ST_STEADY:
		entrar(ST_FALLA);
		break;
	case ST_FALLA:
		entrar(VIB_SIM > VIB_LIMITE ? ST_EMERG : ST_IDLE);
		break;
	default:
		break;
	}
}

static void fsm_fn(void *a, void *b, void *c)
{
	while (1) {
		k_timeout_t espera = K_FOREVER;

		if (vence >= 0) {
			int64_t resta = vence - k_uptime_get();

			espera = K_MSEC((int32_t)(resta > 0 ? resta : 0));
		}

		if (k_sem_take(&fsm_sem, espera) == 0) {
			/* llego un comando del ESP32 */
			atomic_val_t cmd = atomic_set(&comando, 0);

			if (cmd == 'G' && estado == ST_IDLE) {
				entrar(ST_ACCEL);
			} else if (cmd == 'E') {
				entrar(ST_EMERG);
			} else if (cmd == 'R') {
				entrar(ST_IDLE);
			} else if (cmd == 'P') {
				/* slider: guarda la consigna y, si el motor puede girar, la aplica */
				int d = (int)atomic_set(&pwm_pedido, -1);

				if (d >= 0) {
					consigna = d;
					if (estado == ST_ACCEL || estado == ST_STEADY ||
					    estado == ST_FALLA) {
						motor_set(d);
					}
				}
			}
			/* otro caso: se ignora y se sigue esperando lo que falte del estado */
		} else {
			vencio();
		}
	}
}

void fsm_comando(char c)
{
	atomic_set(&comando, c);
	k_sem_give(&fsm_sem);
}

/* Pide un duty (0..100 %) desde el slider; lo atiende el hilo FSM */
void fsm_pwm(int duty)
{
	if (duty < 0) {
		duty = 0;
	}
	if (duty > 100) {
		duty = 100;
	}
	atomic_set(&pwm_pedido, duty);
	fsm_comando('P');
}

int fsm_estado(void)
{
	return estado;
}

const char *fsm_nombre(int e)
{
	return nombres[e];
}

void fsm_init(void)
{
	entrar(ST_IDLE);
	k_thread_create(&hilo_fsm, stack_fsm, STACK_FSM, fsm_fn, NULL, NULL, NULL, 5, 0, K_NO_WAIT);
}