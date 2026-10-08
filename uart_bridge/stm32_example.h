#ifndef STM32_EPUCK_EXAMPLE_H
#define STM32_EPUCK_EXAMPLE_H
#include <stdint.h>
extern volatile uint32_t epuck_rx_bytes, epuck_rx_drops, epuck_pong_count;
extern char epuck_last_line[256];
void epuck_link_init(void);
void epuck_link_task(void);
#endif
