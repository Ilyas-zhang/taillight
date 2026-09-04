/*
 *  ============ ti_msp_dl_config.h =============
 *  MSPM0G3507 + WS2815 LED controller - device configuration declarations
 *
 *  Hand-written (no SysConfig) for the mspm0g3507_ws2815 project.
 *
 *  Three WS2815 chains (physical wiring on the board):
 *    - LIFT : PA12 -> TIMG0_CCP0 -> DMA CH2  (45 LEDs)
 *    - RIGHT: PA28 -> TIMA0_CCP3 -> DMA CH1  (45 LEDs)
 *    - STOP : PB4  -> TIMA0_CCP2 -> DMA CH0  (10 LEDs)
 *
 *  Timing: MCLK = 80 MHz (SYSPLL).
 *    TIMA0 is a PD1 / BUSCLK peripheral  -> BUSCLK = MCLK = 80 MHz.
 *    TIMG0 is a PD0 / ULP-domain peripheral -> ULPCLK = MCLK/2 = 40 MHz
 *    (ULPCLK divider is DIV_2 and ULPCLK is capped at 40 MHz on G-series).
 *  Each timer still produces a 1.25 us (800 kHz) WS2815 bit period, using
 *  the per-timer *_PERIOD_TICKS constants defined below.
 */
#ifndef ti_msp_dl_config_h
#define ti_msp_dl_config_h

#define CONFIG_MSPM0G3507

#if defined(__ti_version__) || defined(__TI_COMPILER_VERSION__)
#define SYSCONFIG_WEAK __attribute__((weak))
#elif defined(__IAR_SYSTEMS_ICC__)
#define SYSCONFIG_WEAK __weak
#elif defined(__GNUC__)
#define SYSCONFIG_WEAK __attribute__((weak))
#endif

#include <ti/devices/msp/msp.h>
#include <ti/driverlib/driverlib.h>
#include <ti/driverlib/m0p/dl_core.h>

#ifdef __cplusplus
extern "C" {
#endif

/* clang-format off */

#define POWER_STARTUP_DELAY                                                (16)

/* MCLK / CPU clock after SYSPLL lock */
#define CPUCLK_FREQ                                                     80000000

/* Defines for SYSPLL_ERR_01 workaround (FCC ratio check) */
#define FLOAT_TO_INT_SCALE                                               (1000U)
#define FCC_EXPECTED_RATIO                                                  1250
#define FCC_UPPER_BOUND                       (FCC_EXPECTED_RATIO * (1 + 0.003))
#define FCC_LOWER_BOUND                       (FCC_EXPECTED_RATIO * (1 - 0.003))

/* ---------- LIFT chain: PA12 -> TIMG0_CCP0 -> DMA CH2 ---------- */
#define WS2815_LIFT_PORT                                                     GPIOA
#define WS2815_LIFT_PIN                                             DL_GPIO_PIN_12
#define WS2815_LIFT_IOMUX                                         (IOMUX_PINCM34)
#define WS2815_LIFT_IOMUX_FUNC                  IOMUX_PINCM34_PF_TIMG0_CCP0

/* ---------- RIGHT chain: PA28 -> TIMA0_CCP3 -> DMA CH1 ---------- */
#define WS2815_RIGHT_PORT                                                    GPIOA
#define WS2815_RIGHT_PIN                                            DL_GPIO_PIN_28
#define WS2815_RIGHT_IOMUX                                          (IOMUX_PINCM3)
#define WS2815_RIGHT_IOMUX_FUNC                  IOMUX_PINCM3_PF_TIMA0_CCP3

/* ---------- STOP chain: PB4 -> TIMA0_CCP2 -> DMA CH0 ---------- */
#define WS2815_STOP_PORT                                                     GPIOB
#define WS2815_STOP_PIN                                              DL_GPIO_PIN_4
#define WS2815_STOP_IOMUX                                          (IOMUX_PINCM17)
#define WS2815_STOP_IOMUX_FUNC                  IOMUX_PINCM17_PF_TIMA0_CCP2

/* ---------- Timers ---------- */
/* TIMA0 drives the two chains on PB4 (CC2, STOP) and PA28 (CC3, RIGHT) */
#define WS2815_TIMA_INST                                                     (TIMA0)
/* TIMG0 drives the chain on PA12 (CC0, LIFT) */
#define WS2815_TIMG_INST                                                     (TIMG0)

/* TIMA0 runs on BUSCLK = MCLK = 80 MHz (divide 1, prescale 0) */
#define WS2815_TIMA_CLK_FREQ                                           80000000
/* WS2815 bit timing @ TIMA0 (1 tick = 12.5 ns) */
#define WS2815_TIMA_PERIOD_TICKS                                          (100)   /* 1.25 us = 800 kHz */
#define WS2815_TIMA_T1H_TICKS                                              (60)   /* logic 1 high = 750 ns */
#define WS2815_TIMA_T0H_TICKS                                              (25)   /* logic 0 high = 312.5 ns */

/* TIMG0 runs on ULPCLK = MCLK / ULPCLK_DIV = 80 / 2 = 40 MHz
 * (PD0 / ULP domain; ULPCLK max = 40 MHz on MSPM0G3507) */
#define WS2815_TIMG_CLK_FREQ                                           40000000
/* WS2815 bit timing @ TIMG0 (1 tick = 25 ns) */
#define WS2815_TIMG_PERIOD_TICKS                                           (50)   /* 1.25 us = 800 kHz */
#define WS2815_TIMG_T1H_TICKS                                              (30)   /* logic 1 high = 750 ns */
#define WS2815_TIMG_T0H_TICKS                                              (12)   /* logic 0 high = 300 ns */

/* Event-bus channel IDs used to route timer LOAD/ZERO events to DMA */
#define WS2815_TIMA_EVENT_CH                                                   (1)
#define WS2815_TIMG_EVENT_CH                                                   (2)

/* ---------- DMA ---------- */
/* Chain -> DMA channel mapping (fixed by the DMA trigger wiring in
 * ti_msp_dl_config.c): CH0/TIMA0_CCP2 = STOP(PB4), CH1/TIMA0_CCP3 = RIGHT,
 * CH2/TIMG0_CCP0 = LIFT(PA12). */
#define WS2815_DMA_CH_LIFT                                                   (2)
#define WS2815_DMA_CH_RIGHT                                                  (1)
#define WS2815_DMA_CH_STOP                                                   (0)

/* ---------- UART0: PA10 (TX, PINCM21) / PA11 (RX, PINCM22) ---------- */
/*  串口通信 — PC 手势检测 ↔ MCU，XDS110 backchannel on LaunchPad。
 *  UART0 for PC gesture detection <-> MCU comm, XDS110 backchannel. */
#define UART_COMM_INST                                                      UART0
#define UART_COMM_INT_IRQN                                          UART0_INT_IRQn
#define UART_COMM_CLOCK_FREQ                                          40000000  /* UART0 实际时钟 / actual clock */
#define UART_COMM_BAUD_RATE                                            115200

/* PA10 -> UART0_TX (PINCM21) */
#define UART_COMM_TX_IOMUX                                          (IOMUX_PINCM21)
#define UART_COMM_TX_IOMUX_FUNC                  IOMUX_PINCM21_PF_UART0_TX

/* PA11 -> UART0_RX (PINCM22) */
#define UART_COMM_RX_IOMUX                                          (IOMUX_PINCM22)
#define UART_COMM_RX_IOMUX_FUNC                  IOMUX_PINCM22_PF_UART0_RX

/* MCU 收到有效命令后发送的 ACK 字符 / ACK character sent by MCU on valid RX */
#define UART_COMM_ACK_CHAR                                                '#'

/* clang-format on */

void SYSCFG_DL_init(void);
void SYSCFG_DL_initPower(void);
void SYSCFG_DL_GPIO_init(void);
void SYSCFG_DL_SYSCTL_init(void);
bool SYSCFG_DL_SYSCTL_SYSPLL_init(void);
void SYSCFG_DL_TIMA_init(void);
void SYSCFG_DL_TIMG_init(void);
void SYSCFG_DL_DMA_init(void);
void SYSCFG_DL_UART_init(void);

#ifdef __cplusplus
}
#endif

#endif /* ti_msp_dl_config_h */
