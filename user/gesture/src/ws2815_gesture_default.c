/*
 *  ============ ws2815_gesture_default.c =============
 *  Default gesture instance — per-chain color palettes.
 *
 *  Each chain has its own 12-color palette.  The first element is the
 *  chain's primary color, used by scan and breathe effects:
 *    LIFT  -> RED    (tail / position light)
 *    RIGHT -> GREEN  (turn signal)
 *    STOP  -> BLUE   (brake light)
 */
#include "ws2815_gesture.h"

#define DEFAULT_PALETTE_SIZE  (12U)

/* LIFT palette: RED as primary, followed by warm → cool progression */
static const uint32_t g_default_palette_lift[DEFAULT_PALETTE_SIZE] = {
    WS2815_GRB888_RED,    WS2815_GRB888_ORANGE,  WS2815_GRB888_YELLOW,
    WS2815_GRB888_GREEN,  WS2815_GRB888_CYAN,    WS2815_GRB888_BLUE,
    WS2815_GRB888_VIOLET, WS2815_GRB888_PURPLE,  WS2815_GRB888_PINK,
    WS2815_GRB888_IRED,   WS2815_GRB888_PBLUE,   WS2815_GRB888_WHITE
};

/* RIGHT palette: GREEN as primary, followed by cool → warm progression */
static const uint32_t g_default_palette_right[DEFAULT_PALETTE_SIZE] = {
    WS2815_GRB888_GREEN,  WS2815_GRB888_CYAN,    WS2815_GRB888_BLUE,
    WS2815_GRB888_VIOLET, WS2815_GRB888_PURPLE,  WS2815_GRB888_PINK,
    WS2815_GRB888_RED,    WS2815_GRB888_ORANGE,  WS2815_GRB888_YELLOW,
    WS2815_GRB888_IRED,   WS2815_GRB888_PBLUE,   WS2815_GRB888_WHITE
};

/* STOP palette: BLUE as primary, followed by blue tones → warm progression */
static const uint32_t g_default_palette_stop[DEFAULT_PALETTE_SIZE] = {
    WS2815_GRB888_BLUE,   WS2815_GRB888_VIOLET,  WS2815_GRB888_PBLUE,
    WS2815_GRB888_CYAN,   WS2815_GRB888_PURPLE,  WS2815_GRB888_PINK,
    WS2815_GRB888_RED,    WS2815_GRB888_ORANGE,  WS2815_GRB888_YELLOW,
    WS2815_GRB888_GREEN,  WS2815_GRB888_IRED,    WS2815_GRB888_WHITE
};

const ws2815_gesture_t ws2815_gesture_default = {
    .lift  = { g_default_palette_lift,   DEFAULT_PALETTE_SIZE },
    .right = { g_default_palette_right,  DEFAULT_PALETTE_SIZE },
    .stop  = { g_default_palette_stop,   DEFAULT_PALETTE_SIZE },
};
