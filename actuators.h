/* actuators.h - Motor, servo y bocina */
#ifndef ACTUATORS_H
#define ACTUATORS_H

enum bocina_patron { BOC_NADA, BOC_ACCEL, BOC_FALLA, BOC_EMERG };

int  actuadores_init(void);            /* configura pines y crea el hilo; 0 = ok */
void motor_set(int duty);              /* 0 a 100 */
int  motor_duty(void);
void servo_set(int grados);            /* 0 a 180 */
void bocina_set(enum bocina_patron p);

#endif
