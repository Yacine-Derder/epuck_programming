/* Integrate into your STM32F405 Cube project; this is not a complete STM32 image.
 * USART1: PA9 TX / PA10 RX, AF7, 115200, 8N1, no flow control.
 * Enable USART1 global interrupt in CubeMX. Merge callbacks if already present.
 */
#include "main.h"
#include "stm32_example.h"
#include <string.h>
extern UART_HandleTypeDef huart1;
static uint8_t incoming;
static volatile uint8_t ring[256];
static volatile uint16_t head, tail;
static uint32_t last_ping;
volatile uint32_t epuck_rx_bytes, epuck_rx_drops, epuck_pong_count;
char epuck_last_line[256];
static const uint8_t ping[] = "PING\n";

void epuck_link_init(void)
{
    HAL_UART_Receive_IT(&huart1, &incoming, 1);
    last_ping = HAL_GetTick();
}

void HAL_UART_RxCpltCallback(UART_HandleTypeDef *uart)
{
    uint16_t next;
    if (uart->Instance != USART1) return;
    ++epuck_rx_bytes;
    next = (head + 1u) & 255u;
    if (next == tail) ++epuck_rx_drops;
    else { ring[head] = incoming; head = next; }
    HAL_UART_Receive_IT(&huart1, &incoming, 1);
}

/* Integrate this branch into your error callback if the project already has one. */
void HAL_UART_ErrorCallback(UART_HandleTypeDef *uart)
{
    if (uart->Instance == USART1)
        HAL_UART_Receive_IT(&huart1, &incoming, 1);
}

void epuck_link_task(void)
{
    static char line[256];
    static unsigned int length, discard;
    uint8_t byte;
    uint32_t now = HAL_GetTick();
    if ((uint32_t)(now - last_ping) >= 1000) {
        if (HAL_UART_Transmit_IT(&huart1, (uint8_t *)ping, sizeof(ping) - 1) == HAL_OK)
            last_ping = now;
    }
    while (tail != head) {
        byte = ring[tail];
        tail = (tail + 1u) & 255u;
        if (byte == '\r') continue;
        if (byte == '\n') {
            if (!discard && length) {
                line[length] = '\0';
                strcpy(epuck_last_line, line);
                if (!strcmp(line, "PONG")) ++epuck_pong_count;
            }
            length = discard = 0;
        } else if (!discard) {
            if (byte < 32 || byte > 126 || length >= sizeof(line) - 1) discard = 1;
            else line[length++] = (char)byte;
        }
    }
}
