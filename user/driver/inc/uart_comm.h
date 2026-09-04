/*
 *  ============ uart_comm.h =============
 *  UART0 通信驱动 — PC ↔ MCU 手势字符传输。
 *  UART0 communication driver for PC <-> MCU gesture character transfer.
 *
 *  Protocol:
 *    PC sends character ('0'-'9' or 'K') -> MCU receives in UART0 RX ISR
 *    -> MCU sends '#' ACK -> MCU main loop processes the command.
 *
 *  Hardware: UART0 on PA10 (TX, PINCM21) / PA11 (RX, PINCM22).
 *  Baud rate: 115200, 8N1, UART0 clock = 40 MHz.
 *
 *  外设初始化由 ti_msp_dl_config.c 中的 SYSCFG_DL_UART_init() 完成；
 *  uart_comm_init() 仅使能 NVIC 和清除接收标志。
 *  Peripheral init is done by SYSCFG_DL_UART_init() in ti_msp_dl_config.c;
 *  uart_comm_init() only enables NVIC and clears the RX flag.
 */
#ifndef UART_COMM_H
#define UART_COMM_H

#include "ti_msp_dl_config.h"

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ACK character sent by MCU upon receiving any command */
#define UART_ACK_CHAR   ('#')

/* Initialize UART0 communication: enable NVIC IRQ, clear RX flag.
 * UART0 peripheral init is done by SYSCFG_DL_UART_init(). */
void uart_comm_init(void);

/* Returns true if a new character has been received (not yet consumed). */
bool uart_comm_has_data(void);

/* Consume and return the last received character. Clears the flag.
 * Only call when uart_comm_has_data() returns true. */
uint8_t uart_comm_get_char(void);

/* Transmit a single character (polling TX FIFO empty).
 * Used for debug / direct TX. */
void uart_comm_send_char(uint8_t ch);

#ifdef __cplusplus
}
#endif

#endif /* UART_COMM_H */
