/* comm.h - UART con el ESP32 (RX de comandos, TX de telemetria JSON + CRC) */
#ifndef COMM_H
#define COMM_H

#include <stdint.h>

struct telem {
	uint8_t  est;
	uint8_t  duty;
	uint16_t rpm;
	int32_t  val;
	uint8_t  ok;     /* 1 = bien, 2 = error de lectura, 0 = sensor no listo */
};

int  comm_init(void);                       /* 0 = ok */
void comm_enviar(const struct telem *t);    /* mete la telemetria en la cola */

#endif
