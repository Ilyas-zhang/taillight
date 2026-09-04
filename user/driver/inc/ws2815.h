/*
 *  ============ ws2815.h =============
 *  WS2815B LED strip driver API for MSPM0G3507.
 *
 *  Three independent WS2815 chains, each driven by timer PWM + DMA:
 *    - LIFT : PA12 -> TIMG0_CCP0 -> DMA CH2  (45 LEDs)
 *    - RIGHT: PA28 -> TIMA0_CCP3 -> DMA CH1  (45 LEDs)
 *    - STOP : PB4  -> TIMA0_CCP2 -> DMA CH0  (10 LEDs)
 *
 *  Principle:
 *    Each timer runs in PWM edge-align-up mode at an 800 kHz bit rate:
 *      TIMA0 @ BUSCLK = 80 MHz, period = 100 ticks (1.25 us)
 *      TIMG0 @ ULPCLK = 40 MHz, period = 50 ticks  (1.25 us)
 *    At each counter ZERO event a DMA byte is transferred into the timer CC
 *    register. The byte value IS the PWM compare value, so it encodes one
 *    WS2815 bit:
 *      TIMA0: T1H = 60 (750 ns) / T0H = 25 (312.5 ns)
 *      TIMG0: T1H = 30 (750 ns) / T0H = 12 (300 ns)
 *
 *  When all three DMA channels finish their buffers the DMA_IRQHandler stops
 *  the timers. The trailing dummy bytes of each buffer hold the output low,
 *  and the caller waits >=280 us (RESET) before starting the next frame.
 */
#ifndef WS2815_H
#define WS2815_H

#include "ws2815_types.h"
#include "ti_msp_dl_config.h"

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ---------------- API ---------------- */
void  ws2815_init(void);                          /* configure DMA + color layer */
void  ws2815_set_color(ws2815_chain_t chain, uint8_t index, uint32_t grb);
void  ws2815_fill_color(ws2815_chain_t chain, uint32_t grb);
void  ws2815_clear_all(void);
void  ws2815_update(void);                        /* rebuild compare buffers + start frame */
bool  ws2815_is_busy(void);                       /* frame still being shifted out */
void  ws2815_wait_idle(void);                     /* block until frame done + RESET elapsed */

#ifdef __cplusplus
}
#endif

#endif /* WS2815_H */
