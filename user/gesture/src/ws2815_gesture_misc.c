/*
 *  ============ ws2815_gesture_misc.c =============
 *  杂项手势 LED 掩码 — OK 等，左/右独立定义。
 *
 *  Miscellaneous gesture LED masks (OK, etc.): independent left (LIFT) and
 *  right (RIGHT) definitions.  New gestures can be added here as needed.
 *
 *  掩码索引为 WS2815 链内索引（0–44），由用户提供的全局编号转换而来：
 *    左侧 (LIFT):  chain_index = 45 - global_number   (global 1–45)
 *    右侧 (RIGHT): chain_index = global_number - 46    (global 46–90)
 *
 *  全局编号来源: user/gesture/index.txt
 */
#include "ws2815_gesture.h"

/* ================================================================
 *  OK 手势 — 左侧 (LIFT)
 *  ok(left) = 19/15/6/7/34/30/31/33/32/8/2/1/5/14/22/3/4/26
 *  转换: chain_index = 45 - global_number
 *    45-19=26  45-15=30  45-6=39   45-7=38   45-34=11  45-30=15
 *    45-31=14  45-33=12  45-32=13  45-8=37   45-2=43   45-1=44
 *    45-5=40   45-14=31  45-22=23  45-3=42   45-4=41   45-26=19
 * ================================================================ */
static const uint8_t g_ok_left[] = {
    26, 30, 39, 38, 11, 15, 14, 12, 13, 37, 43, 44, 40, 31, 23, 42, 41, 19,
};
#define OK_LEFT_SIZE  (sizeof(g_ok_left) / sizeof(g_ok_left[0]))

/* ================================================================
 *  OK 手势 — 右侧 (RIGHT)
 *  ok(right) = 73/77/85/86/62/58/59/61/60/72/84/90/87/78/89/70/88/66
 *  转换: chain_index = global_number - 46
 *    73-46=27  77-46=31  85-46=39  86-46=40  62-46=16  58-46=12
 *    59-46=13  61-46=15  60-46=14  72-46=26  84-46=38  90-46=44
 *    87-46=41  78-46=32  89-46=43  70-46=24  88-46=42  66-46=20
 * ================================================================ */
static const uint8_t g_ok_right[] = {
    27, 31, 39, 40, 16, 12, 13, 15, 14, 26, 38, 44, 41, 32, 43, 24, 42, 20,
};
#define OK_RIGHT_SIZE  (sizeof(g_ok_right) / sizeof(g_ok_right[0]))

/* ================================================================
 *  OK 手势掩码对实例
 * ================================================================ */
const ws2815_digit_mask_pair_t ws2815_gesture_ok = {
    .left  = { g_ok_left,  (uint8_t)OK_LEFT_SIZE },
    .right = { g_ok_right, (uint8_t)OK_RIGHT_SIZE },
};
