/*
 *  ============ uart_comm.c =============
 *  UART0 通信驱动实现 — 中断接收 + ACK 发送。
 *  UART0 communication driver: interrupt-driven RX, immediate ACK TX.
 *
 *  ISR 接收字符后立即发送 '#' ACK，确保 PC 端尽快收到确认。
 *  主循环通过 uart_comm_has_data() / uart_comm_get_char() 消费命令。
 *
 *  ISR sends '#' ACK immediately after RX, ensuring the PC receives
 *  confirmation as fast as possible. The main loop consumes commands
 *  via uart_comm_has_data() / uart_comm_get_char().
 */
#include "uart_comm.h"

/* ----- ISR-to-main 共享状态 (volatile) / shared state (volatile) ----- */
static volatile bool    g_uart_rx_flag = false;
static volatile uint8_t g_uart_rx_char = 0U;

/*
 *  UART0 接收中断处理 — 接收字符，置标志，立即发送 '#' 确认。
 *  UART0 RX interrupt handler: store received char, set flag, send ACK.
 *
 *  ACK 在 ISR 中直接发送，延迟最小。TX 硬件保证空闲：
 *  PC 在收到 ACK 前不会发下一字节，因此不会有 TX 冲突。
 *  ACK is sent directly in the ISR for minimal latency. TX is guaranteed
 *  idle: the PC does not send the next byte until ACK is received.
 */
void UART0_IRQHandler(void)
{
    switch (DL_UART_Main_getPendingInterrupt(UART_COMM_INST)) {
    case DL_UART_MAIN_IIDX_RX:
        g_uart_rx_char = DL_UART_Main_receiveData(UART_COMM_INST);
        g_uart_rx_flag = true;
        /* 立即发送 ACK / Send ACK immediately */
        DL_UART_Main_transmitData(UART_COMM_INST, (uint8_t)UART_ACK_CHAR);
        break;
    default:
        break;
    }
}

void uart_comm_init(void)
{
    /* UART0 外设初始化已在 SYSCFG_DL_UART_init() 中完成
     * (时钟、波特率、8N1、RX 中断使能、GPIO PA10/PA11)。
     * UART0 peripheral init is done in SYSCFG_DL_UART_init()
     * (clock, baud rate, 8N1, RX interrupt enabled, GPIO PA10/PA11). */

    /* 清除挂起中断并使能 NVIC / Clear pending IRQ and enable NVIC */
    NVIC_ClearPendingIRQ(UART_COMM_INT_IRQN);
    NVIC_EnableIRQ(UART_COMM_INT_IRQN);

    /* 清除接收标志 / Clear RX flag */
    g_uart_rx_flag = false;
}

bool uart_comm_has_data(void)
{
    return g_uart_rx_flag;
}

uint8_t uart_comm_get_char(void)
{
    g_uart_rx_flag = false;
    return g_uart_rx_char;
}

void uart_comm_send_char(uint8_t ch)
{
    while (DL_UART_Main_isTXFIFOEmpty(UART_COMM_INST) == false) {
        /* 等待 TX FIFO 有空间 / wait for TX FIFO to have space */
    }
    DL_UART_Main_transmitData(UART_COMM_INST, ch);
}
