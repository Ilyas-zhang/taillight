/*
 *  ============ ws2815_color.h =============
 *  Internal header — color buffer layer shared between ws2815_color.c
 *  and ws2815.c (DMA engine).
 *
 *  NOT part of the public API.  Only included by driver source files.
 */
#ifndef WS2815_COLOR_H
#define WS2815_COLOR_H

#include "ws2815_types.h"

/* Color arrays — written by ws2815_color.c, read by ws2815.c */
extern uint32_t g_color_lift[WS2815_LIFT_LED_NUM];
extern uint32_t g_color_right[WS2815_RIGHT_LED_NUM];
extern uint32_t g_color_stop[WS2815_STOP_LED_NUM];

/* Initialize color layer — clears all color arrays to black.
 * Called by ws2815_init(). */
void ws2815_color_init(void);

#endif /* WS2815_COLOR_H */
