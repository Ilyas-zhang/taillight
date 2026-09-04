/*
 *  ============ ws2815_types.h =============
 *  WS2815B type definitions, color macros, and chain identifiers.
 *
 *  Pure data — no hardware register dependencies.
 *  Include this when you only need the types (e.g., gesture layer).
 *  Include ws2815.h when you need the driver API.
 */
#ifndef WS2815_TYPES_H
#define WS2815_TYPES_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ---------------- LED counts per chain ---------------- */
#define WS2815_LIFT_LED_NUM        (45U)
#define WS2815_RIGHT_LED_NUM       (45U)
#define WS2815_STOP_LED_NUM        (10U)
#define WS2815_TOTAL_LED_NUM       (100U)

/*
 * GRB888 colors. WS2815 samples green, red, blue in that order, so the
 * packed value layout is: bits[23:16]=G, bits[15:8]=R, bits[7:0]=B.
 */
#define WS2815_GRB888_BLACK        (0x000000U)
#define WS2815_GRB888_RED          (0x00FF00U)
#define WS2815_GRB888_GREEN        (0xFF0000U)
#define WS2815_GRB888_BLUE         (0x0000FFU)
#define WS2815_GRB888_WHITE        (0xFFFFFFU)
#define WS2815_GRB888_YELLOW       (0xFFFF00U)
#define WS2815_GRB888_IRED         (0x5CCD5CU)   /* light green */
#define WS2815_GRB888_ORANGE       (0xA5FF00U)
#define WS2815_GRB888_PURPLE       (0x008080U)
#define WS2815_GRB888_PINK         (0xB6FFC1U)   /* light red */
#define WS2815_GRB888_CYAN         (0xFF00FFU)
#define WS2815_GRB888_PBLUE        (0x80008CU)   /* peacock blue */
#define WS2815_GRB888_VIOLET       (0x008BFFU)   /* blue violet */

/* ---------------- chain identifier ---------------- */
typedef enum {
    WS2815_CHAIN_LIFT = 0,   /* DMA CH2 (TIMG0 CC0 / PA12) */
    WS2815_CHAIN_RIGHT,      /* DMA CH1 (TIMA0 CC3 / PA28) */
    WS2815_CHAIN_STOP,       /* DMA CH0 (TIMA0 CC2 / PB4) */
    WS2815_CHAIN_MAX
} ws2815_chain_t;

#ifdef __cplusplus
}
#endif

#endif /* WS2815_TYPES_H */
