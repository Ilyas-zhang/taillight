/*
 *  ============ taillight_app.h =============
 *  Taillight effect functions — palette fill, scan, and breathe.
 *
 *  Each effect takes a const pointer to a ws2815_gesture_t, which
 *  provides per-chain color palettes.  The gesture's palette[0] is
 *  used as the chain's primary color for scan and breathe effects.
 */
#ifndef TAILLIGHT_APP_H
#define TAILLIGHT_APP_H

#include "ws2815_gesture.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Iterate through each chain's palette, showing all colors in turn. */
void taillight_palette_fill(const ws2815_gesture_t *gesture);

/* Scan a single lit LED up and down each chain, using each chain's
 * primary color (palette[0]). */
void taillight_scan_chains(const ws2815_gesture_t *gesture);

/* Breathing effect: all LEDs fade in and out, each chain using its
 * primary color (palette[0]). */
void taillight_breathe(const ws2815_gesture_t *gesture);

/* Digit display: LIFT shows "1", RIGHT shows "2", STOP always on.
 * Digit masks and palettes come from ws2815_gesture_digits_t. */
void taillight_display_digits(const ws2815_gesture_digits_t *gd);

/*
 *  显示单个数字 (0–9) 在指定侧，使用给定颜色。
 *  Display a single digit (0–9) on the specified side with the given color.
 *
 *  仅修改目标链，其余链不变。调用者可自行管理其他链的显示。
 *  Only the target chain is modified; other chains are unchanged.
 *  The caller may manage other chains independently.
 */
void taillight_show_digit(uint8_t digit, ws2815_digit_side_t side, uint32_t color);

/*
 *  显示 OK 手势在指定侧，使用给定颜色。
 *  Display the OK gesture on the specified side with the given color.
 *
 *  仅修改目标链（清除后点亮掩码 LED），其余链不变。
 *  Only the target chain is cleared and repainted; other chains are unchanged.
 */
void taillight_show_ok(ws2815_digit_side_t side, uint32_t color);

/*
 *  数字流彩呼吸效果 — 左右 LED 块循环显示数字 0–9 + OK 手势，
 *  颜色流彩呼吸；STOP 长条蓝色呼吸（数字）/ 黄红流彩呼吸（OK）。
 *  函数内部无限循环。
 *
 *  Digit rainbow-breathing test: LIFT & RIGHT cycle digits 0–9 then OK
 *  with flowing rainbow colors + breathing; STOP breathes blue (digits)
 *  or yellow-red warm flowing (OK). Runs indefinitely (while-1 loop).
 */
void taillight_digit_rainbow_breathe(void);

/*
 *  处理 UART 接收的命令字符，在左右 LED 块上执行对应的显示动作。
 *  Process a UART-received command character, performing the corresponding
 *  LED display action on both left and right LED blocks.
 *
 *  支持的命令 / Supported commands:
 *    '0'-'9' → 数字 0-9 在左右两侧同时显示（绿色），STOP 红色常亮
 *             digit 0-9 on both sides (green), STOP solid red
 *    'K'     → OK 手势在左右两侧同时显示（青色），STOP 黄色常亮
 *             OK gesture on both sides (cyan), STOP solid yellow
 *    其余    → 忽略，不影响当前显示 / ignored, display unchanged
 *
 *  单帧更新：三条链同时刷新，无闪烁。
 *  Single-frame update: all three chains refreshed together, no flicker.
 */
void taillight_process_command(uint8_t cmd);

#ifdef __cplusplus
}
#endif

#endif /* TAILLIGHT_APP_H */
