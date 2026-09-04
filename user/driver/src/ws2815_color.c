/*
 *  ============ ws2815_color.c =============
 *  WS2815B color buffer management — per-chain LED color storage.
 *
 *  Owns the three color arrays (LIFT / RIGHT / STOP) and provides
 *  set / fill / clear operations.  The DMA engine (ws2815.c) reads
 *  these arrays via ws2815_color.h to build compare-value buffers.
 */
#include <stddef.h>
#include "ws2815_color.h"

/* ---------------- stored GRB888 colors per chain ---------------- */
uint32_t g_color_lift[WS2815_LIFT_LED_NUM];
uint32_t g_color_right[WS2815_RIGHT_LED_NUM];
uint32_t g_color_stop[WS2815_STOP_LED_NUM];

/* ---------------- internal helpers ---------------- */

static void clear_colors(uint32_t *color, uint32_t num)
{
    uint32_t i;
    for (i = 0U; i < num; i++) {
        color[i] = WS2815_GRB888_BLACK;
    }
}

static uint32_t *chain_colors(ws2815_chain_t chain, uint32_t *num)
{
    switch (chain) {
    case WS2815_CHAIN_LIFT:
        *num = WS2815_LIFT_LED_NUM;
        return g_color_lift;
    case WS2815_CHAIN_RIGHT:
        *num = WS2815_RIGHT_LED_NUM;
        return g_color_right;
    case WS2815_CHAIN_STOP:
        *num = WS2815_STOP_LED_NUM;
        return g_color_stop;
    default:
        *num = 0U;
        return NULL;
    }
}

/* ---------------- public API ---------------- */

void ws2815_color_init(void)
{
    clear_colors(g_color_lift, WS2815_LIFT_LED_NUM);
    clear_colors(g_color_right, WS2815_RIGHT_LED_NUM);
    clear_colors(g_color_stop, WS2815_STOP_LED_NUM);
}

void ws2815_set_color(ws2815_chain_t chain, uint8_t index, uint32_t grb)
{
    uint32_t num;
    uint32_t *colors = chain_colors(chain, &num);

    if ((colors != NULL) && (index < num)) {
        colors[index] = grb;
    }
}

void ws2815_fill_color(ws2815_chain_t chain, uint32_t grb)
{
    uint32_t num;
    uint32_t i;
    uint32_t *colors = chain_colors(chain, &num);

    if (colors != NULL) {
        for (i = 0U; i < num; i++) {
            colors[i] = grb;
        }
    }
}

void ws2815_clear_all(void)
{
    clear_colors(g_color_lift, WS2815_LIFT_LED_NUM);
    clear_colors(g_color_right, WS2815_RIGHT_LED_NUM);
    clear_colors(g_color_stop, WS2815_STOP_LED_NUM);
}
