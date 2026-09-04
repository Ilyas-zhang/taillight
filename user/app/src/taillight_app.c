/*
 *  ============ taillight_app.c =============
 *  Taillight effect implementations — palette fill, scan, and breathe.
 *
 *  Migrated from main.c.  All effects receive a gesture pointer which
 *  provides per-chain color palettes.  Internal helpers (scale_brightness,
 *  chain_led_num, fill_all, scan_chain) are kept static.
 */
#include "taillight_app.h"
#include "ws2815.h"

/* Busy-wait delay helper (MCLK = 80 MHz, DL_Common_delayCycles is a loop). */
#define LED_DELAY_MS(ms)  DL_Common_delayCycles((uint32_t)(ms) * (CPUCLK_FREQ / 1000U))

/* ---------------- internal helpers ---------------- */

/* Scale the brightness of a GRB888 color: k = 0..255 (0 = off, 255 = full). */
static uint32_t scale_brightness(uint32_t grb, uint8_t k)
{
    uint32_t g = ((grb >> 16) & 0xFFU) * k / 255U;
    uint32_t r = ((grb >> 8)  & 0xFFU) * k / 255U;
    uint32_t b = ( grb        & 0xFFU) * k / 255U;
    return (g << 16) | (r << 8) | b;
}

/* Return the number of LEDs in a given chain. */
static uint8_t chain_led_num(ws2815_chain_t chain)
{
    switch (chain) {
    case WS2815_CHAIN_LIFT:  return WS2815_LIFT_LED_NUM;
    case WS2815_CHAIN_RIGHT: return WS2815_RIGHT_LED_NUM;
    case WS2815_CHAIN_STOP:  return WS2815_STOP_LED_NUM;
    default:                 return 0U;
    }
}

/* Fill every LED of every chain with the same color. */
static void fill_all(uint32_t grb)
{
    ws2815_fill_color(WS2815_CHAIN_LIFT,  grb);
    ws2815_fill_color(WS2815_CHAIN_RIGHT, grb);
    ws2815_fill_color(WS2815_CHAIN_STOP,  grb);
    ws2815_update();
}

/* Move a single lit LED up and down one chain. */
static void scan_chain(ws2815_chain_t chain, uint32_t color)
{
    uint8_t n = chain_led_num(chain);
    uint8_t i;

    for (i = 0U; i < n; i++) {
        ws2815_clear_all();
        ws2815_set_color(chain, i, color);
        ws2815_update();
        LED_DELAY_MS(35);
    }
    for (i = n - 1U; i > 0U; i--) {
        ws2815_clear_all();
        ws2815_set_color(chain, i, color);
        ws2815_update();
        LED_DELAY_MS(35);
    }
}

/* ---------------- public effect functions ---------------- */

void taillight_palette_fill(const ws2815_gesture_t *gesture)
{
    /* Step through the palettes; each chain shows its own color at
     * each index.  The number of steps is the maximum palette count
     * across the three chains. */
    uint8_t max_count = gesture->lift.count;
    uint8_t i;

    if (gesture->right.count > max_count) {
        max_count = gesture->right.count;
    }
    if (gesture->stop.count > max_count) {
        max_count = gesture->stop.count;
    }

    for (i = 0U; i < max_count; i++) {
        ws2815_fill_color(WS2815_CHAIN_LIFT,
            gesture->lift.colors[i % gesture->lift.count]);
        ws2815_fill_color(WS2815_CHAIN_RIGHT,
            gesture->right.colors[i % gesture->right.count]);
        ws2815_fill_color(WS2815_CHAIN_STOP,
            gesture->stop.colors[i % gesture->stop.count]);
        ws2815_update();
        LED_DELAY_MS(280);
    }
}

void taillight_scan_chains(const ws2815_gesture_t *gesture)
{
    scan_chain(WS2815_CHAIN_LIFT,  gesture->lift.colors[0]);
    scan_chain(WS2815_CHAIN_RIGHT, gesture->right.colors[0]);
    scan_chain(WS2815_CHAIN_STOP,  gesture->stop.colors[0]);
}

void taillight_breathe(const ws2815_gesture_t *gesture)
{
    /* Each chain breathes its own primary color (palette[0]).
     * 40 steps, ~18 ms per step. */
    const uint8_t steps = 40U;
    uint8_t i;
    int16_t k;

    for (i = 0U; i < steps; i++) {
        /* sine-ish approximation using two linear ramps */
        if (i < steps / 2U) {
            k = (int16_t)(2 * i * 255 / steps);
        } else {
            k = (int16_t)(2 * (steps - i) * 255 / steps);
        }

        ws2815_fill_color(WS2815_CHAIN_LIFT,
            scale_brightness(gesture->lift.colors[0], (uint8_t)k));
        ws2815_fill_color(WS2815_CHAIN_RIGHT,
            scale_brightness(gesture->right.colors[0], (uint8_t)k));
        ws2815_fill_color(WS2815_CHAIN_STOP,
            scale_brightness(gesture->stop.colors[0], (uint8_t)k));
        ws2815_update();
        LED_DELAY_MS(18);
    }
}

/*
 *  数字显示效果 — LIFT 显示 "1"，RIGHT 显示 "2"，STOP 常亮。
 *  Digit display effect: LIFT shows "1", RIGHT shows "2", STOP always on.
 *
 *  All three chains are painted once and held.  No animation loop —
 *  the digits stay lit at full brightness indefinitely.
 */
void taillight_display_digits(const ws2815_gesture_digits_t *gd)
{
    const ws2815_gesture_t *gesture = &gd->gesture;
    const uint32_t lift_color  = gesture->lift.colors[0];
    const uint32_t right_color = gesture->right.colors[0];
    const uint32_t stop_color  = gesture->stop.colors[0];
    uint8_t j;

    /* --- LIFT chain: paint digit "1" mask --- */
    ws2815_fill_color(WS2815_CHAIN_LIFT, WS2815_GRB888_BLACK);
    for (j = 0U; j < gd->lift_mask.count; j++) {
        ws2815_set_color(WS2815_CHAIN_LIFT,
                         gd->lift_mask.indices[j], lift_color);
    }

    /* --- RIGHT chain: paint digit "2" mask --- */
    ws2815_fill_color(WS2815_CHAIN_RIGHT, WS2815_GRB888_BLACK);
    for (j = 0U; j < gd->right_mask.count; j++) {
        ws2815_set_color(WS2815_CHAIN_RIGHT,
                         gd->right_mask.indices[j], right_color);
    }

    /* --- STOP chain: all LEDs on at full brightness --- */
    ws2815_fill_color(WS2815_CHAIN_STOP, stop_color);

    ws2815_update();
    ws2815_wait_idle();
}

/*
 *  显示单个数字 (0–9) 在指定侧，使用给定颜色。
 *  Display a single digit (0–9) on the specified side with the given color.
 *
 *  仅修改目标链（清除后点亮掩码 LED），其余链不变。
 *  Only the target chain is cleared and repainted; other chains are unchanged.
 */
void taillight_show_digit(uint8_t digit, ws2815_digit_side_t side, uint32_t color)
{
    ws2815_chain_t chain;
    const ws2815_digit_mask_t *mask;
    uint8_t j;

    if (digit > 9U) return;

    if (side == WS2815_DIGIT_SIDE_LEFT) {
        chain = WS2815_CHAIN_LIFT;
        mask  = &ws2815_digit_masks[digit].left;
    } else {
        chain = WS2815_CHAIN_RIGHT;
        mask  = &ws2815_digit_masks[digit].right;
    }

    /* 清除目标链，然后点亮数字掩码 LED */
    ws2815_fill_color(chain, WS2815_GRB888_BLACK);
    for (j = 0U; j < mask->count; j++) {
        ws2815_set_color(chain, mask->indices[j], color);
    }

    ws2815_update();
    ws2815_wait_idle();
}

/*
 *  显示 OK 手势在指定侧，使用给定颜色。
 *  Display the OK gesture on the specified side with the given color.
 *
 *  仅修改目标链（清除后点亮掩码 LED），其余链不变。
 *  Only the target chain is cleared and repainted; other chains are unchanged.
 */
void taillight_show_ok(ws2815_digit_side_t side, uint32_t color)
{
    ws2815_chain_t chain;
    const ws2815_digit_mask_t *mask;
    uint8_t j;

    if (side == WS2815_DIGIT_SIDE_LEFT) {
        chain = WS2815_CHAIN_LIFT;
        mask  = &ws2815_gesture_ok.left;
    } else {
        chain = WS2815_CHAIN_RIGHT;
        mask  = &ws2815_gesture_ok.right;
    }

    /* 清除目标链，然后点亮 OK 手势 LED */
    ws2815_fill_color(chain, WS2815_GRB888_BLACK);
    for (j = 0U; j < mask->count; j++) {
        ws2815_set_color(chain, mask->indices[j], color);
    }

    ws2815_update();
    ws2815_wait_idle();
}

/*
 *  数字流彩呼吸效果 — 左右 LED 块循环显示数字 0–9 + OK 手势，
 *  颜色流彩呼吸；STOP 长条蓝色呼吸（数字阶段）/ 黄红混合流彩呼吸（OK 阶段）。
 *
 *  Digit rainbow-breathing test effect:
 *    - LIFT (left) and RIGHT chains cycle: digits 0–9, then OK gesture.
 *    - Digit/OK color flows through a 10-color rainbow palette, shifting
 *      every ~72 ms to produce a continuous "流彩" (flowing-color) effect.
 *    - Brightness follows a linear breathe curve (ramp up → ramp down).
 *    - STOP chain: blue breathing during digits; yellow-red warm flowing
 *      breathing during OK.
 *
 *  The function runs indefinitely (internal while-1 loop).
 */
void taillight_digit_rainbow_breathe(void)
{
    /* 10-color rainbow palette flowing through the visible spectrum.
     * Each color is displayed for ~72 ms (4 frames × 18 ms) before
     * advancing to the next, creating a smooth flow. */
    static const uint32_t rainbow[10] = {
        WS2815_GRB888_RED,
        WS2815_GRB888_ORANGE,
        WS2815_GRB888_YELLOW,
        WS2815_GRB888_GREEN,
        WS2815_GRB888_CYAN,
        WS2815_GRB888_BLUE,
        WS2815_GRB888_VIOLET,
        WS2815_GRB888_PURPLE,
        WS2815_GRB888_PINK,
        WS2815_GRB888_IRED,
    };

    /* STOP warm palette: yellow ↔ orange ↔ red ↔ orange flowing mix
     * for the OK gesture phase. */
    static const uint32_t stop_warm[4] = {
        WS2815_GRB888_YELLOW,
        WS2815_GRB888_ORANGE,
        WS2815_GRB888_RED,
        WS2815_GRB888_ORANGE,
    };

    const uint8_t breath_steps = 60U;  /* steps per breathe cycle (~1080 ms) */
    uint8_t digit, step, j;
    uint16_t flow = 0U;                 /* monotonically increasing color offset */

    while (1) {
        /* ---- Phase 1: digits 0–9 ---- */
        for (digit = 0U; digit < 10U; digit++) {
            for (step = 0U; step < breath_steps; step++) {
                int16_t k;
                uint8_t color_idx;
                uint32_t digit_color, stop_color;
                const ws2815_digit_mask_t *mask;

                /* Breathing brightness: linear ramp up then down */
                if (step < breath_steps / 2U) {
                    k = (int16_t)(2 * step * 255 / breath_steps);
                } else {
                    k = (int16_t)(2 * (breath_steps - step) * 255 / breath_steps);
                }

                /* Flowing rainbow: color index shifts every 4 frames (~72 ms) */
                color_idx   = (uint8_t)((flow >> 2U) % 10U);
                digit_color = scale_brightness(rainbow[color_idx], (uint8_t)k);
                stop_color  = scale_brightness(WS2815_GRB888_BLUE, (uint8_t)k);

                /* LIFT chain: clear then paint digit mask */
                ws2815_fill_color(WS2815_CHAIN_LIFT, WS2815_GRB888_BLACK);
                mask = &ws2815_digit_masks[digit].left;
                for (j = 0U; j < mask->count; j++) {
                    ws2815_set_color(WS2815_CHAIN_LIFT,
                                     mask->indices[j], digit_color);
                }

                /* RIGHT chain: clear then paint digit mask */
                ws2815_fill_color(WS2815_CHAIN_RIGHT, WS2815_GRB888_BLACK);
                mask = &ws2815_digit_masks[digit].right;
                for (j = 0U; j < mask->count; j++) {
                    ws2815_set_color(WS2815_CHAIN_RIGHT,
                                     mask->indices[j], digit_color);
                }

                /* STOP chain: blue breathing */
                ws2815_fill_color(WS2815_CHAIN_STOP, stop_color);

                ws2815_update();
                LED_DELAY_MS(18);
                flow++;
            }
        }

        /* ---- Phase 2: OK gesture (3 breathe cycles, ~2.2 s) ----
         *  LIFT & RIGHT show OK with flowing rainbow colors + breathing.
         *  STOP shows yellow-red warm flowing breathing. */
        {
            uint8_t cycle;
            for (cycle = 0U; cycle < 3U; cycle++) {
                for (step = 0U; step < breath_steps; step++) {
                    int16_t k;
                    uint8_t color_idx, warm_idx;
                    uint32_t ok_color, stop_color;
                    const ws2815_digit_mask_t *mask;

                    /* Breathing brightness */
                    if (step < breath_steps / 2U) {
                        k = (int16_t)(2 * step * 255 / breath_steps);
                    } else {
                        k = (int16_t)(2 * (breath_steps - step) * 255 / breath_steps);
                    }

                    /* Flowing rainbow for OK gesture */
                    color_idx = (uint8_t)((flow >> 2U) % 10U);
                    ok_color  = scale_brightness(rainbow[color_idx], (uint8_t)k);

                    /* STOP: yellow-red warm flowing breathing */
                    warm_idx   = (uint8_t)((flow >> 2U) % 4U);
                    stop_color = scale_brightness(stop_warm[warm_idx], (uint8_t)k);

                    /* LIFT chain: OK gesture */
                    ws2815_fill_color(WS2815_CHAIN_LIFT, WS2815_GRB888_BLACK);
                    mask = &ws2815_gesture_ok.left;
                    for (j = 0U; j < mask->count; j++) {
                        ws2815_set_color(WS2815_CHAIN_LIFT,
                                         mask->indices[j], ok_color);
                    }

                    /* RIGHT chain: OK gesture */
                    ws2815_fill_color(WS2815_CHAIN_RIGHT, WS2815_GRB888_BLACK);
                    mask = &ws2815_gesture_ok.right;
                    for (j = 0U; j < mask->count; j++) {
                        ws2815_set_color(WS2815_CHAIN_RIGHT,
                                         mask->indices[j], ok_color);
                    }

                    /* STOP chain: warm flowing breathing */
                    ws2815_fill_color(WS2815_CHAIN_STOP, stop_color);

                    ws2815_update();
                    LED_DELAY_MS(18);
                    flow++;
                }
            }
        }
    }
}

/*
 *  处理 UART 命令 — 在左右两侧同时显示数字或 OK 手势（单帧更新）。
 *  Process UART command: display digit or OK gesture on both sides
 *  simultaneously in a single frame update.
 *
 *  数字默认颜色: 绿色 (WS2815_GRB888_GREEN)
 *  OK 手势默认颜色: 青色 (WS2815_GRB888_CYAN)
 *  STOP 链: 数字时红色常亮, OK 时黄色常亮
 *
 *  无效字符被忽略，不影响当前显示。
 *  Invalid characters are ignored; current display is unchanged.
 */
void taillight_process_command(uint8_t cmd)
{
    uint8_t j;

    if (cmd >= '0' && cmd <= '9') {
        /* 数字显示 / Digit display */
        uint8_t digit = (uint8_t)(cmd - '0');
        const ws2815_digit_mask_pair_t *mask = &ws2815_digit_masks[digit];
        const uint32_t digit_color = WS2815_GRB888_GREEN;

        /* 左侧 (LIFT): 清除后点亮数字掩码 / Left (LIFT): clear then paint */
        ws2815_fill_color(WS2815_CHAIN_LIFT, WS2815_GRB888_BLACK);
        for (j = 0U; j < mask->left.count; j++) {
            ws2815_set_color(WS2815_CHAIN_LIFT,
                             mask->left.indices[j], digit_color);
        }

        /* 右侧 (RIGHT): 清除后点亮数字掩码 / Right (RIGHT): clear then paint */
        ws2815_fill_color(WS2815_CHAIN_RIGHT, WS2815_GRB888_BLACK);
        for (j = 0U; j < mask->right.count; j++) {
            ws2815_set_color(WS2815_CHAIN_RIGHT,
                             mask->right.indices[j], digit_color);
        }

        /* STOP: 红色常亮 / STOP: solid red */
        ws2815_fill_color(WS2815_CHAIN_STOP, WS2815_GRB888_RED);

    } else if (cmd == 'K') {
        /* OK 手势显示 / OK gesture display */
        const ws2815_digit_mask_pair_t *mask = &ws2815_gesture_ok;
        const uint32_t ok_color = WS2815_GRB888_CYAN;

        /* 左侧 (LIFT) / Left (LIFT) */
        ws2815_fill_color(WS2815_CHAIN_LIFT, WS2815_GRB888_BLACK);
        for (j = 0U; j < mask->left.count; j++) {
            ws2815_set_color(WS2815_CHAIN_LIFT,
                             mask->left.indices[j], ok_color);
        }

        /* 右侧 (RIGHT) / Right (RIGHT) */
        ws2815_fill_color(WS2815_CHAIN_RIGHT, WS2815_GRB888_BLACK);
        for (j = 0U; j < mask->right.count; j++) {
            ws2815_set_color(WS2815_CHAIN_RIGHT,
                             mask->right.indices[j], ok_color);
        }

        /* STOP: 黄色常亮 / STOP: solid yellow */
        ws2815_fill_color(WS2815_CHAIN_STOP, WS2815_GRB888_YELLOW);

    } else {
        /* 其他字符: 忽略，保持当前显示 / Other chars: ignored */
        return;
    }

    /* 单帧刷新所有三条链 / Single-frame refresh of all three chains */
    ws2815_update();
    ws2815_wait_idle();
}
