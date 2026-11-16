/* fsm.h - Maquina de estados del banco de pruebas */
#ifndef FSM_H
#define FSM_H

enum fsm_estado { ST_IDLE, ST_ACCEL, ST_STEADY, ST_FALLA, ST_EMERG };

void fsm_init(void);                      /* pone IDLE y crea el hilo */
void fsm_comando(char c);                 /* 'G', 'E' o 'R'. Se puede llamar desde una ISR */
void fsm_pwm(int duty);                   /* velocidad 0..100 % del slider. Se puede llamar desde una ISR */
int fsm_estado(void);
const char *fsm_nombre(int estado);

#endif