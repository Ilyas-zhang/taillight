/*
 *  ============ main.c =============
 *  MSPM0G3507 + WS2815 taillight application.
 *
 *  UART-driven mode: PC gesture detection app sends digit/OK characters
 *  via UART0; MCU receives, displays on LED chains, and ACKs with '#'.
 *
 *  Protocol:
 *    PC sends character ('0'-'9' or 'K') → MCU receives in UART0 RX ISR
 *    → MCU sends '#' ACK → MCU main loop processes the command.
 */
#include "ti_msp_dl_config.h"
#include "ws2815.h"
#include "taillight_app.h"
#include "uart_comm.h"

/* Busy-wait delay helper (MCLK = 80 MHz, DL_Common_delayCycles is a loop). */
#define LED_DELAY_MS(ms)  DL_Common_delayCycles((uint32_t)(ms) * (CPUCLK_FREQ / 1000U))

int main(void)
{
    /* 时钟 (80 MHz SYSPLL)、GPIO、定时器、DMA、UART0。
     * Clocks (80 MHz SYSPLL), GPIO, timers, DMA, UART0. */
    SYSCFG_DL_init();

    /* WS2815 驱动: DMA 通道 source/dest/size, 中断。*/
    ws2815_init();

    /* UART0 通信驱动: RX 中断, NVIC 使能。*/
    uart_comm_init();

    /* 发送一帧全黑以清除灯带 / Send all-black frame to clear strips */
    ws2815_update();
    LED_DELAY_MS(500);

    /*
     *  UART 命令主循环：接收字符 → 执行 LED 显示动作 → 等待下一命令。
     *  UART command main loop: receive char → execute LED display action
     *  → wait for next command.
     *
     *  无命令时 CPU 进入 WFI 低功耗，由 UART0 RX 或 DMA 中断唤醒。
     *  When idle, CPU sleeps via WFI; wakes on UART0 RX or DMA interrupt.
     */
    while (1) {
        if (uart_comm_has_data()) {
            uint8_t cmd = uart_comm_get_char();
            taillight_process_command(cmd);
        }
        __WFI();
    }
}
