/*
 *  ============ ws2815.c =============
 *  WS2815B DMA/PWM engine — frame transmission and interrupt handling.
 *
 *  Frame flow:
 *    1. ws2815_update() converts the stored GRB888 colors (from ws2815_color.c)
 *       into compare-value bytes and stores them in per-chain buffers. Trailing
 *       dummy 0-bytes keep the line low after the last real bit. Then the three
 *       DMA channels are re-armed and both timers start.
 *    2. Each timer ZERO event (1.25 us) triggers its DMA channel, which moves
 *       one byte from the buffer into the timer CC register. With
 *       CC_UPDATE_METHOD_ZERO_EVT the byte becomes active one period later,
 *       generating the exact high/low pulse pattern.
 *    3. When all three channels reach transfer count 0, the DMA_IRQHandler
 *       stops both timers and clears the busy flag. The outputs are already
 *       low (dummy bytes), so the >=280 us RESET is produced by
 *       ws2815_wait_idle() before the next frame is armed.
 */
#include "ws2815.h"
#include "ws2815_color.h"

/* ---------------- per-chain DMA channels ---------------- */
#define WS2815_LIFT_DMA_CH    (WS2815_DMA_CH_LIFT)    /* 2 (TIMG0 CC0 / PA12) */
#define WS2815_RIGHT_DMA_CH   (WS2815_DMA_CH_RIGHT)   /* 1 (TIMA0 CC3 / PA28) */
#define WS2815_STOP_DMA_CH    (WS2815_DMA_CH_STOP)    /* 0 (TIMA0 CC2 / PB4) */

/* Trailing dummy 0-bytes: guarantees the output is low when DMA finishes and
 * absorbs the one-bit CC shadow-register latency. */
#define WS2815_DUMMY_BYTES    (3U)

/* RESET pulse duration (>=280 us required). 300 us is a safe margin. */
#define WS2815_RESET_US       (300U)

/* DMA destination registers:
 *   LIFT  -> TIMG0 CC0 = &COUNTERREGS.CC_01[0]
 *   RIGHT -> TIMA0 CC3 = &COUNTERREGS.CC_23[1]
 *   STOP  -> TIMA0 CC2 = &COUNTERREGS.CC_23[0]
 * The CC register is 32-bit, but a BYTE write only touches the low byte; the
 * upper bytes are already 0, so the compare value equals the byte itself.
 */
#define WS2815_LIFT_CC_ADDR   ((uint32_t) &WS2815_TIMG_INST->COUNTERREGS.CC_01[0])
#define WS2815_RIGHT_CC_ADDR  ((uint32_t) &WS2815_TIMA_INST->COUNTERREGS.CC_23[1])
#define WS2815_STOP_CC_ADDR   ((uint32_t) &WS2815_TIMA_INST->COUNTERREGS.CC_23[0])

/* ---------------- buffers ---------------- */
#define WS2815_LIFT_BUF_LEN   (WS2815_LIFT_LED_NUM * 24U + WS2815_DUMMY_BYTES)
#define WS2815_RIGHT_BUF_LEN  (WS2815_RIGHT_LED_NUM * 24U + WS2815_DUMMY_BYTES)
#define WS2815_STOP_BUF_LEN   (WS2815_STOP_LED_NUM * 24U + WS2815_DUMMY_BYTES)

/* Compare-value buffers read by DMA (volatile: written by CPU, read by DMA). */
static volatile uint8_t g_comp_lift[WS2815_LIFT_BUF_LEN];
static volatile uint8_t g_comp_right[WS2815_RIGHT_BUF_LEN];
static volatile uint8_t g_comp_stop[WS2815_STOP_BUF_LEN];

/* Set in DMA_IRQHandler when a channel finishes its buffer. */
static volatile bool g_dma_done[WS2815_CHAIN_MAX];
static volatile bool g_busy;

/* ---------------- internal helpers ---------------- */

/* Build one compare-value byte per WS2815 bit, MSB (G7) first, B0 last.
 * t1h/t0h are the timer-specific high-time tick counts (TIMA0 @ 80 MHz vs
 * TIMG0 @ 40 MHz encode the same 750/300 ns differently). */
static void build_compare_buffer(volatile uint8_t *buf, uint32_t buf_len,
    const uint32_t *color, uint32_t led_num, uint8_t t1h, uint8_t t0h)
{
    uint32_t i, j;

    for (i = 0U; i < led_num; i++) {
        for (j = 0U; j < 24U; j++) {
            buf[i * 24U + j] = ((color[i] << j) & 0x800000U) ? t1h : t0h;
        }
    }
    /* trailing dummy bytes = logic 0 -> output low after the last data bit */
    for (i = led_num * 24U; i < buf_len; i++) {
        buf[i] = 0U;
    }
}

static void arm_dma_channel(uint8_t dma_ch, volatile uint8_t *buf, uint32_t len)
{
    DL_DMA_setSrcAddr(DMA, dma_ch, (uint32_t) buf);
    DL_DMA_setTransferSize(DMA, dma_ch, (uint16_t) len);
    DL_DMA_enableChannel(DMA, dma_ch);
}

/* ---------------- public API ---------------- */

void ws2815_init(void)
{
    uint32_t i;

    /* Source/dest/size for each chain. Dest is fixed; src and size are
     * refreshed every frame (arm_dma_channel) but set once here as well. */
    DL_DMA_setSrcAddr(DMA, WS2815_LIFT_DMA_CH, (uint32_t) g_comp_lift);
    DL_DMA_setDestAddr(DMA, WS2815_LIFT_DMA_CH, WS2815_LIFT_CC_ADDR);
    DL_DMA_setTransferSize(DMA, WS2815_LIFT_DMA_CH, (uint16_t) WS2815_LIFT_BUF_LEN);
    DL_DMA_disableChannel(DMA, WS2815_LIFT_DMA_CH);

    DL_DMA_setSrcAddr(DMA, WS2815_RIGHT_DMA_CH, (uint32_t) g_comp_right);
    DL_DMA_setDestAddr(DMA, WS2815_RIGHT_DMA_CH, WS2815_RIGHT_CC_ADDR);
    DL_DMA_setTransferSize(DMA, WS2815_RIGHT_DMA_CH, (uint16_t) WS2815_RIGHT_BUF_LEN);
    DL_DMA_disableChannel(DMA, WS2815_RIGHT_DMA_CH);

    DL_DMA_setSrcAddr(DMA, WS2815_STOP_DMA_CH, (uint32_t) g_comp_stop);
    DL_DMA_setDestAddr(DMA, WS2815_STOP_DMA_CH, WS2815_STOP_CC_ADDR);
    DL_DMA_setTransferSize(DMA, WS2815_STOP_DMA_CH, (uint16_t) WS2815_STOP_BUF_LEN);
    DL_DMA_disableChannel(DMA, WS2815_STOP_DMA_CH);

    /* Initialize color layer (clears all LED colors to black). */
    ws2815_color_init();

    g_busy = false;
    for (i = 0U; i < WS2815_CHAIN_MAX; i++) {
        g_dma_done[i] = false;
    }

    /* DMA channel-done interrupts were enabled in SYSCFG_DL_DMA_init();
     * this enables the NVIC line itself. */
    NVIC_EnableIRQ(DMA_INT_IRQn);
}

bool ws2815_is_busy(void)
{
    return g_busy;
}

void ws2815_wait_idle(void)
{
    /* Wait for the current frame to be fully shifted out. */
    while (g_busy) {
    }

    /* RESET: hold all outputs low for >=280 us so the LEDs latch the frame. */
    DL_Common_delayCycles(WS2815_RESET_US * (CPUCLK_FREQ / 1000000U));
}

void ws2815_update(void)
{
    uint32_t i;

    /* Never overwrite a buffer while the DMA of the previous frame reads it. */
    ws2815_wait_idle();

    /* LIFT is on TIMG0 (ULPCLK = 40 MHz); RIGHT and STOP are on TIMA0
     * (BUSCLK = 80 MHz), so the high-time tick counts differ per timer. */
    build_compare_buffer(g_comp_lift, WS2815_LIFT_BUF_LEN, g_color_lift,
        WS2815_LIFT_LED_NUM, WS2815_TIMG_T1H_TICKS, WS2815_TIMG_T0H_TICKS);
    build_compare_buffer(g_comp_right, WS2815_RIGHT_BUF_LEN, g_color_right,
        WS2815_RIGHT_LED_NUM, WS2815_TIMA_T1H_TICKS, WS2815_TIMA_T0H_TICKS);
    build_compare_buffer(g_comp_stop, WS2815_STOP_BUF_LEN, g_color_stop,
        WS2815_STOP_LED_NUM, WS2815_TIMA_T1H_TICKS, WS2815_TIMA_T0H_TICKS);

    for (i = 0U; i < WS2815_CHAIN_MAX; i++) {
        g_dma_done[i] = false;
    }
    g_busy = true;

    arm_dma_channel(WS2815_LIFT_DMA_CH, g_comp_lift, WS2815_LIFT_BUF_LEN);
    arm_dma_channel(WS2815_RIGHT_DMA_CH, g_comp_right, WS2815_RIGHT_BUF_LEN);
    arm_dma_channel(WS2815_STOP_DMA_CH, g_comp_stop, WS2815_STOP_BUF_LEN);

    /* Start both timers; CVAE=ZERO loads the counters at 0, so the first
     * period is a clean low (initial CC = 0). */
    DL_TimerA_startCounter(WS2815_TIMA_INST);
    DL_TimerG_startCounter(WS2815_TIMG_INST);
}

/* ---------------- interrupt handler ---------------- */

void DMA_IRQHandler(void)
{
    uint32_t intr = DL_DMA_getPendingInterrupt(DMA);

    switch (intr) {
    case DL_DMA_EVENT_IIDX_DMACH0:   /* CH0 = STOP  (TIMA0 CC2 / PB4) */
        DL_DMA_clearInterruptStatus(DMA, DL_DMA_INTERRUPT_CHANNEL0);
        DL_DMA_disableChannel(DMA, WS2815_STOP_DMA_CH);
        g_dma_done[WS2815_CHAIN_STOP] = true;
        break;
    case DL_DMA_EVENT_IIDX_DMACH1:   /* CH1 = RIGHT (TIMA0 CC3 / PA28) */
        DL_DMA_clearInterruptStatus(DMA, DL_DMA_INTERRUPT_CHANNEL1);
        DL_DMA_disableChannel(DMA, WS2815_RIGHT_DMA_CH);
        g_dma_done[WS2815_CHAIN_RIGHT] = true;
        break;
    case DL_DMA_EVENT_IIDX_DMACH2:   /* CH2 = LIFT  (TIMG0 CC0 / PA12) */
        DL_DMA_clearInterruptStatus(DMA, DL_DMA_INTERRUPT_CHANNEL2);
        DL_DMA_disableChannel(DMA, WS2815_LIFT_DMA_CH);
        g_dma_done[WS2815_CHAIN_LIFT] = true;
        break;
    default:
        DL_DMA_clearInterruptStatus(DMA, DL_DMA_INTERRUPT_CHANNEL0 |
                                         DL_DMA_INTERRUPT_CHANNEL1 |
                                         DL_DMA_INTERRUPT_CHANNEL2);
        break;
    }

    /* The RIGHT/LIFT chains finish together (45 LEDs); the STOP chain
     * (10 LEDs) finishes much earlier. Stop everything only when all three
     * are done. Outputs are already low thanks to the dummy bytes. */
    if (g_dma_done[WS2815_CHAIN_LIFT] && g_dma_done[WS2815_CHAIN_RIGHT] &&
        g_dma_done[WS2815_CHAIN_STOP]) {
        DL_DMA_disableChannel(DMA, WS2815_LIFT_DMA_CH);
        DL_DMA_disableChannel(DMA, WS2815_RIGHT_DMA_CH);
        DL_DMA_disableChannel(DMA, WS2815_STOP_DMA_CH);
        DL_TimerA_stopCounter(WS2815_TIMA_INST);
        DL_TimerG_stopCounter(WS2815_TIMG_INST);
        g_busy = false;
    }
}
