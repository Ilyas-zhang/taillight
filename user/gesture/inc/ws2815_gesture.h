/*
 *  ============ ws2815_gesture.h =============
 *  WS2815B gesture definition — per-chain color palette data.
 *
 *  A gesture provides three independent palettes (LIFT / RIGHT / STOP),
 *  one per LED chain.  palette.colors[0] is the chain's primary color,
 *  used by scan and breathe effects.
 *
 *  Usage:
 *    - Define a gesture instance with three ws2815_chain_palette_t.
 *    - Pass a pointer to the taillight_app effect functions.
 */
#ifndef WS2815_GESTURE_H
#define WS2815_GESTURE_H

#include "ws2815_types.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ---------------- per-chain palette ---------------- */
typedef struct {
    const uint32_t *colors;   /* GRB888 color array */
    uint8_t count;            /* number of colors in the array */
} ws2815_chain_palette_t;

/* ---------------- gesture: palettes for all three chains ---------------- */
typedef struct {
    ws2815_chain_palette_t lift;
    ws2815_chain_palette_t right;
    ws2815_chain_palette_t stop;
} ws2815_gesture_t;

/* Default gesture instance (defined in ws2815_gesture_default.c) */
extern const ws2815_gesture_t ws2815_gesture_default;

/* ---------------- digit mask: which chain indices to light ---------------- */
typedef struct {
    const uint8_t *indices;  /* array of chain indices (0-based) */
    uint8_t count;           /* number of indices in the array */
} ws2815_digit_mask_t;

/* ---------------- digit display side ---------------- */
typedef enum {
    WS2815_DIGIT_SIDE_LEFT  = 0,   /* LIFT chain (left side of PCB) */
    WS2815_DIGIT_SIDE_RIGHT = 1,   /* RIGHT chain (right side of PCB) */
} ws2815_digit_side_t;

/* ---------------- digit mask pair: left + right ---------------- */

/*
 *  数字掩码对 — 左侧 (LIFT) 和右侧 (RIGHT) 各自的 LED 掩码。
 *  Digit mask pair: separate masks for LIFT (left) and RIGHT (right) chains.
 *
 *  两侧 LED 在 PCB 上位置不同（螺旋方向相反），因此需要独立定义。
 *  Left and right LEDs occupy different PCB positions (opposing spiral
 *  directions), so each side needs its own mask definition.
 */
typedef struct {
    ws2815_digit_mask_t left;   /* LIFT chain indices (left side) */
    ws2815_digit_mask_t right;  /* RIGHT chain indices (right side) */
} ws2815_digit_mask_pair_t;

/*
 *  10 个数字掩码对 (0–9)。
 *  10 digit mask pairs (digits 0–9).
 *
 *  每个数字包含左侧和右侧各自的掩码，通过 taillight_show_digit()
 *  选择在 LIFT（左）或 RIGHT（右）链上显示。
 *  Each digit has independent left and right masks.
 *  Use taillight_show_digit() to select which side to display on.
 */
extern const ws2815_digit_mask_pair_t ws2815_digit_masks[10];

/* OK gesture mask pair (defined in ws2815_gesture_ok.c) */
extern const ws2815_digit_mask_pair_t ws2815_gesture_ok;

/* ---------------- digit display gesture: palettes + masks ---------------- */

/*
 *  数字显示手势 — 调色板 + LED 掩码，统一管理。
 *  Digit display gesture: palettes + LED index masks, managed together.
 *
 *  gesture:   LIFT=RED, RIGHT=GREEN, STOP=RED
 *  lift_mask:  which LIFT chain indices form digit "1"
 *  right_mask: which RIGHT chain indices form digit "2"
 */
typedef struct {
    ws2815_gesture_t gesture;
    ws2815_digit_mask_t lift_mask;
    ws2815_digit_mask_t right_mask;
} ws2815_gesture_digits_t;

/* Digits gesture instance (defined in ws2815_gesture_digits.c) */
extern const ws2815_gesture_digits_t ws2815_gesture_digits;

#ifdef __cplusplus
}
#endif

#endif /* WS2815_GESTURE_H */
