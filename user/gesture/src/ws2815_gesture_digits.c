/*
 *  ============ ws2815_gesture_digits.c =============
 *  数字显示手势 — 10 个数字 LED 掩码对 (0–9)，左/右独立定义。
 *
 *  Digit display gesture: 10 digit mask pairs (0–9), with independent
 *  left (LIFT) and right (RIGHT) mask definitions.
 *
 *  Via taillight_show_digit(), any digit can be displayed on either the
 *  LIFT (left) or RIGHT (right) chain, using the side-appropriate mask.
 *
 *  掩码索引为 WS2815 链内索引（0–44），由用户提供的全局编号转换而来：
 *    左侧 (LIFT):  chain_index = 45 - global_number   (global 1–45)
 *    右侧 (RIGHT): chain_index = global_number - 46    (global 46–90)
 *
 *  全局编号来源: user/gesture/index.txt
 */
#include "ws2815_gesture.h"

/* ================================================================
 *  数字 0
 *  0(left)  = 8/9/10/11/12/13/14/15/16/17/18/19
 *  0(right) = 72/73/74/75/76/77/78/79/80/81/82/83
 * ================================================================ */
static const uint8_t g_digit0_left[] = {
    /* 8  9  10  11  12  13  14  15  16  17  18  19 */
    37, 36, 35, 34, 33, 32, 31, 30, 29, 28, 27, 26,
};
#define DIGIT0_LEFT_SIZE  (sizeof(g_digit0_left) / sizeof(g_digit0_left[0]))

static const uint8_t g_digit0_right[] = {
    /* 72  73  74  75  76  77  78  79  80  81  82  83 */
    26, 27, 28, 29, 30, 31, 32, 33, 34, 35, 36, 37,
};
#define DIGIT0_RIGHT_SIZE  (sizeof(g_digit0_right) / sizeof(g_digit0_right[0]))

/* ================================================================
 *  数字 1
 *  1(left)  = 沿用旧版定义 (LIFT chain indices)
 *  1(right) = 56/73/83/72/84/90/87/78/77/79
 * ================================================================ */
static const uint8_t g_digit1_left[] = {
    /* 旧版 LIFT 掩码: #15 #14 #13 #5 #1 #2 #8 #20 #19 #9 */
    30, 31, 32, 40, 44, 43, 37, 25, 26, 36,
};
#define DIGIT1_LEFT_SIZE  (sizeof(g_digit1_left) / sizeof(g_digit1_left[0]))

static const uint8_t g_digit1_right[] = {
    /* 56  73  83  72  84  90  87  78  77  79 */
    10, 27, 37, 26, 38, 44, 41, 32, 31, 33,
};
#define DIGIT1_RIGHT_SIZE  (sizeof(g_digit1_right) / sizeof(g_digit1_right[0]))

/* ================================================================
 *  数字 2
 *  2(left)  = 18/19/8/9/10/11/4/5/15/30/14/26
 *  2(right) = 沿用旧版定义 (RIGHT chain indices)
 * ================================================================ */
static const uint8_t g_digit2_left[] = {
    /* 18  19  8   9   10  11  4   5   15  30  14  26 */
    27, 26, 37, 36, 35, 34, 41, 40, 30, 15, 31, 19,
};
#define DIGIT2_LEFT_SIZE  (sizeof(g_digit2_left) / sizeof(g_digit2_left[0]))

static const uint8_t g_digit2_right[] = {
    /* 旧版 RIGHT 掩码: #74 #73 #72 #83 #82 #81 #88 #87 #77 #62 #78 #79 #66 */
    28, 27, 26, 37, 36, 35, 42, 41, 31, 16, 32, 33, 20,
};
#define DIGIT2_RIGHT_SIZE  (sizeof(g_digit2_right) / sizeof(g_digit2_right[0]))

/* ================================================================
 *  数字 3
 *  3(left)  = 18/19/8/9/10/11/4/6/12/26/27/28/29/30
 *  3(right) = 74/73/72/83/82/81/88/86/80/66/65/64/63/62
 * ================================================================ */
static const uint8_t g_digit3_left[] = {
    /* 18  19  8   9   10  11  4   6   12  26  27  28  29  30 */
    27, 26, 37, 36, 35, 34, 41, 39, 33, 19, 18, 17, 16, 15,
};
#define DIGIT3_LEFT_SIZE  (sizeof(g_digit3_left) / sizeof(g_digit3_left[0]))

static const uint8_t g_digit3_right[] = {
    /* 74  73  72  83  82  81  88  86  80  66  65  64  63  62 */
    28, 27, 26, 37, 36, 35, 42, 40, 34, 20, 19, 18, 17, 16,
};
#define DIGIT3_RIGHT_SIZE  (sizeof(g_digit3_right) / sizeof(g_digit3_right[0]))

/* ================================================================
 *  数字 4
 *  4(left)  = 8/7/16/5/12/25/3/4/13/27
 *  4(right) = 72/85/76/87/80/67/89/88/79/65
 * ================================================================ */
static const uint8_t g_digit4_left[] = {
    /* 8   7   16  5   12  25  3   4   13  27 */
    37, 38, 29, 40, 33, 20, 42, 41, 32, 18,
};
#define DIGIT4_LEFT_SIZE  (sizeof(g_digit4_left) / sizeof(g_digit4_left[0]))

static const uint8_t g_digit4_right[] = {
    /* 72  85  76  87  80  67  89  88  79  65 */
    26, 39, 30, 41, 34, 21, 43, 42, 33, 19,
};
#define DIGIT4_RIGHT_SIZE  (sizeof(g_digit4_right) / sizeof(g_digit4_right[0]))

/* ================================================================
 *  数字 5
 *  5(left)  = 34/8/19/9/18/17/1/4/13/14/15
 *  5(right) = 58/73/72/83/74/75/90/88/80/77/78/79
 * ================================================================ */
static const uint8_t g_digit5_left[] = {
    /* 34  8   19  9   18  17  1   4   13  14  15 */
    11, 37, 26, 36, 27, 28, 44, 41, 32, 31, 30,
};
#define DIGIT5_LEFT_SIZE  (sizeof(g_digit5_left) / sizeof(g_digit5_left[0]))

static const uint8_t g_digit5_right[] = {
    /* 58  73  72  83  74  75  90  88  80  77  78  79 */
    12, 27, 26, 37, 28, 29, 44, 42, 34, 31, 32, 33,
};
#define DIGIT5_RIGHT_SIZE  (sizeof(g_digit5_right) / sizeof(g_digit5_right[0]))

/* ================================================================
 *  数字 6
 *  6(left)  = 20/19/18/17/16/15/14/13/12/11/3/7
 *  6(right) = 58/73/74/75/76/77/78/79/80/81/89/85
 * ================================================================ */
static const uint8_t g_digit6_left[] = {
    /* 20  19  18  17  16  15  14  13  12  11  3   7 */
    25, 26, 27, 28, 29, 30, 31, 32, 33, 34, 42, 38,
};
#define DIGIT6_LEFT_SIZE  (sizeof(g_digit6_left) / sizeof(g_digit6_left[0]))

static const uint8_t g_digit6_right[] = {
    /* 56  73  74  75  76  77  78  79  80  81  89  85 */
    10, 27, 28, 29, 30, 31, 32, 33, 34, 35, 43, 39,
};
#define DIGIT6_RIGHT_SIZE  (sizeof(g_digit6_right) / sizeof(g_digit6_right[0]))

/* ================================================================
 *  数字 7
 *  7(left)  = 34/8/22/10/4/14/28
 *  7(right) = 58/72/70/82/88/78/64
 * ================================================================ */
static const uint8_t g_digit7_left[] = {
    /* 34  8   22  10  4   14  28 */
    11, 37, 23, 35, 41, 31, 17,
};
#define DIGIT7_LEFT_SIZE  (sizeof(g_digit7_left) / sizeof(g_digit7_left[0]))

static const uint8_t g_digit7_right[] = {
    /* 58  72  70  82  88  78  64 */
    12, 26, 24, 36, 42, 32, 18,
};
#define DIGIT7_RIGHT_SIZE  (sizeof(g_digit7_right) / sizeof(g_digit7_right[0]))

/* ================================================================
 *  数字 8
 *  8(left)  = 1/7/3/19/8/9/6/4/16/12/29/27/28
 *  8(right) = 85/73/72/83/89/90/86/76/63/64/65/80/88
 * ================================================================ */
static const uint8_t g_digit8_left[] = {
    /* 1   7   3   19  8   9   6   4   16  12  29  27  28 */
    44, 38, 42, 26, 37, 36, 39, 41, 29, 33, 16, 18, 17,
};
#define DIGIT8_LEFT_SIZE  (sizeof(g_digit8_left) / sizeof(g_digit8_left[0]))

static const uint8_t g_digit8_right[] = {
    /* 85  73  72  83  89  90  86  76  63  64  65  80  88 */
    39, 27, 26, 37, 43, 44, 40, 30, 17, 18, 19, 34, 42,
};
#define DIGIT8_RIGHT_SIZE  (sizeof(g_digit8_right) / sizeof(g_digit8_right[0]))

/* ================================================================
 *  数字 9
 *  9(left)  = 4/6/17/18/19/8/9/10/11/12/13/14/29
 *  9(right) = 88/86/75/74/73/72/71/70/69/68/67/66/65/64/63
 * ================================================================ */
static const uint8_t g_digit9_left[] = {
    /* 4   6   17  18  19  8   9   10  11  12  13  14  29 */
    41, 39, 28, 27, 26, 37, 36, 35, 34, 33, 32, 31, 16,
};
#define DIGIT9_LEFT_SIZE  (sizeof(g_digit9_left) / sizeof(g_digit9_left[0]))

static const uint8_t g_digit9_right[] = {
    /* 88  86  75  74  73  72  83  82  81  80  79  78  63 */
    42, 40, 29, 28, 27, 26, 37, 36, 35, 34, 33, 32, 17,
};
#define DIGIT9_RIGHT_SIZE  (sizeof(g_digit9_right) / sizeof(g_digit9_right[0]))

/* ================================================================
 *  10 个数字掩码对数组 (digits 0–9)
 * ================================================================ */
const ws2815_digit_mask_pair_t ws2815_digit_masks[10] = {
    { { g_digit0_left,  (uint8_t)DIGIT0_LEFT_SIZE  },
      { g_digit0_right, (uint8_t)DIGIT0_RIGHT_SIZE } },
    { { g_digit1_left,  (uint8_t)DIGIT1_LEFT_SIZE  },
      { g_digit1_right, (uint8_t)DIGIT1_RIGHT_SIZE } },
    { { g_digit2_left,  (uint8_t)DIGIT2_LEFT_SIZE  },
      { g_digit2_right, (uint8_t)DIGIT2_RIGHT_SIZE } },
    { { g_digit3_left,  (uint8_t)DIGIT3_LEFT_SIZE  },
      { g_digit3_right, (uint8_t)DIGIT3_RIGHT_SIZE } },
    { { g_digit4_left,  (uint8_t)DIGIT4_LEFT_SIZE  },
      { g_digit4_right, (uint8_t)DIGIT4_RIGHT_SIZE } },
    { { g_digit5_left,  (uint8_t)DIGIT5_LEFT_SIZE  },
      { g_digit5_right, (uint8_t)DIGIT5_RIGHT_SIZE } },
    { { g_digit6_left,  (uint8_t)DIGIT6_LEFT_SIZE  },
      { g_digit6_right, (uint8_t)DIGIT6_RIGHT_SIZE } },
    { { g_digit7_left,  (uint8_t)DIGIT7_LEFT_SIZE  },
      { g_digit7_right, (uint8_t)DIGIT7_RIGHT_SIZE } },
    { { g_digit8_left,  (uint8_t)DIGIT8_LEFT_SIZE  },
      { g_digit8_right, (uint8_t)DIGIT8_RIGHT_SIZE } },
    { { g_digit9_left,  (uint8_t)DIGIT9_LEFT_SIZE  },
      { g_digit9_right, (uint8_t)DIGIT9_RIGHT_SIZE } },
};

/* ================================================================
 *  旧版兼容：ws2815_gesture_digits 组合实例
 *  Legacy compat: combined gesture + mask instance
 *  (digit "1" on LIFT, digit "2" on RIGHT, STOP always on)
 * ================================================================ */

#define DIGITS_PALETTE_SIZE  (1U)

/* LIFT palette: RED — digit primary color */
static const uint32_t g_digits_palette_lift[DIGITS_PALETTE_SIZE] = {
    WS2815_GRB888_RED,
};

/* RIGHT palette: GREEN — digit primary color */
static const uint32_t g_digits_palette_right[DIGITS_PALETTE_SIZE] = {
    WS2815_GRB888_GREEN,
};

/* STOP palette: RED — brake light */
static const uint32_t g_digits_palette_stop[DIGITS_PALETTE_SIZE] = {
    WS2815_GRB888_RED,
};

/* Legacy masks: digit "1" on LIFT, digit "2" on RIGHT (old definitions) */
static const uint8_t g_legacy_digit1_lift[] = {
    30, 31, 32, 40, 44, 43, 37, 25, 26, 36,
};
#define LEGACY_DIGIT1_LIFT_SIZE \
    (sizeof(g_legacy_digit1_lift) / sizeof(g_legacy_digit1_lift[0]))

static const uint8_t g_legacy_digit2_right[] = {
    28, 27, 26, 37, 36, 35, 42, 41, 31, 16, 32, 33, 20,
};
#define LEGACY_DIGIT2_RIGHT_SIZE \
    (sizeof(g_legacy_digit2_right) / sizeof(g_legacy_digit2_right[0]))

const ws2815_gesture_digits_t ws2815_gesture_digits = {
    .gesture = {
        .lift  = { g_digits_palette_lift,   DIGITS_PALETTE_SIZE },
        .right = { g_digits_palette_right,  DIGITS_PALETTE_SIZE },
        .stop  = { g_digits_palette_stop,   DIGITS_PALETTE_SIZE },
    },
    .lift_mask  = { g_legacy_digit1_lift,   (uint8_t)LEGACY_DIGIT1_LIFT_SIZE },
    .right_mask = { g_legacy_digit2_right,  (uint8_t)LEGACY_DIGIT2_RIGHT_SIZE },
};
