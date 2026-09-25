# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project Overview

Bare-metal embedded C firmware for **TI MSPM0G3507** (ARM Cortex-M0+, 128 KB flash / 32 KB SRAM) driving three WS2815B addressable RGB LED chains for a vehicle taillight (LIFT / RIGHT-turn / STOP lights). Uses timer PWM + DMA for bit-banging the WS2815 protocol — no bit-bang CPU loop, no SPI, no RTOS.

## Build & Run

This is a **TI Code Composer Studio (Theia-based)** project — there is no standalone top-level Makefile or CMakeLists.txt. Build happens inside CCS (Project → Build), which generates `Debug/makefile` and invokes `tiarmclang.exe` from the TI clang-based ARM toolchain.

- **Build (CLI):** `cd Debug && gmake all` — requires CCS toolchain at `E:/exe/ccs/ide/ccs/tools/compiler/ti-cgt-armllvm_4.0.4.LTS/bin/`
- **Build (IDE):** CCS Theia → Project → Build All
- **Flash/Debug:** CCS Debug perspective, using `targetConfigs/MSPM0G3507.ccxml` (XDS110 SWD probe)
- **Output:** `Debug/taillight.out` (ELF); map at `Debug/taillight.map`
- **No test framework, no CI, no linter** beyond `-Wall`

## Toolchain & SDK

- **Compiler:** `ti-cgt-armllvm_4.0.4.LTS` (TI's clang-based ARM compiler, `tiarmclang`)
- **Flags:** `-march=thumbv6m -mcpu=cortex-m0plus -O2 -g -Wall`
- **Preprocessor define:** `__MSPM0G3507__`
- **SDK:** MSPM0-SDK 2.09.00.01 at `E:/exe/ccs/sdk/mspm0_sdk_2_09_00_01` — linked against `driverlib.a`
- **Linker script:** `mspm0g3507.cmd` — FLASH at 0x00000000 (128K), SRAM at 0x20200000 (32K), 512-byte stack

## Architecture

Source is organized under `user/` with three layers. CCS includes are configured so that `user/driver/inc/`, `user/gesture/inc/`, and `user/app/inc/` are on the include path.

```
main.c                          entry point (UART-driven main loop, __WFI idle)
user/driver/                    WS2815B hardware driver (DMA/PWM + color buffers) + UART comm
  inc/ws2815_types.h            pure types, color macros, chain enum — no hw deps
  inc/ws2815.h                  driver API (includes ws2815_types.h + ti_msp_dl_config.h)
  inc/ws2815_color.h            internal: color arrays shared between ws2815_color.c and ws2815.c
  inc/uart_comm.h               UART0 communication driver API (init, has_data, get_char)
  src/ws2815_color.c            color buffer management (set/fill/clear per chain)
  src/ws2815.c                  DMA/PWM engine (frame build + DMA_IRQ handling)
  src/uart_comm.c               UART0 RX ISR + ACK TX + driver implementation
user/gesture/                   gesture & digit mask data (pure data, no hw deps)
  inc/ws2815_gesture.h          palette, digit-mask, and gesture structs
  src/ws2815_gesture_default.c  default 12-color palette per chain
  src/ws2815_gesture_digits.c   10 digit mask pairs (0–9) + legacy gesture_digits instance
  src/ws2815_gesture_misc.c     OK gesture mask pair
  index.txt                     global LED numbering reference
user/app/                       taillight effect functions
  inc/taillight_app.h           effect API declarations
  src/taillight_app.c           palette fill, scan, breathe, digit display, process_command
user/gesture_detect/            PC-side gesture detection (Python — see its own CLAUDE.md)
```

Plus CCS boilerplate at the repo root:
- `ti_msp_dl_config.h/.c` — hand-written device config (clocks, GPIO, timers, DMA, timer tick constants)
- `startup_mspm0g350x_ticlang.c` — vector table + reset handler
- `mspm0g3507.cmd` — linker command file

### Include Dependency Graph

```
ti_msp_dl_config.h
ws2815_types.h                 (standalone — <stdint.h> only)
ws2815_color.h  →  ws2815_types.h
ws2815.h  →  ws2815_types.h + ti_msp_dl_config.h
uart_comm.h  →  ti_msp_dl_config.h
ws2815_gesture.h  →  ws2815_types.h
taillight_app.h  →  ws2815_gesture.h
main.c  →  ti_msp_dl_config.h + ws2815.h + taillight_app.h + uart_comm.h
```

Gesture and app layers depend only on `ws2815_types.h` (no hardware headers), making them portable for test/tooling.

### Hardware Mapping

Three independent WS2815 chains, each on its own timer capture channel + DMA channel:

| Chain | Pin | Timer | DMA CH | LED Count |
|---|---|---|---|---|
| LIFT | PA12 | TIMG0_CCP0 | CH2 | 45 |
| RIGHT | PA28 | TIMA0_CCP3 | CH1 | 45 |
| STOP | PB4 | TIMA0_CCP2 | CH0 | 10 |

### UART Serial Communication

| Function | Pin | IOMUX | Peripheral |
|---|---|---|---|
| TX | PA10 | PINCM21 | UART0_TX |
| RX | PA11 | PINCM22 | UART0_RX |

- **Baud rate:** 115200, 8N1
- **UART0 clock:** 40 MHz (not 80 MHz BUSCLK — UART0 has an implicit ÷2 on this device)
- **XDS110 backchannel:** PA10/PA11 are the LaunchPad backchannel UART pins (same USB cable as debug)

### Serial Protocol

PC gesture detection → MCU LED display, ACK-based flow control:

1. PC detects gesture → maps to character → sends repeatedly over UART
2. MCU UART0 RX ISR fires → stores char → sends `'#'` (0x23) ACK immediately
3. MCU main loop consumes char → `taillight_process_command()` → single-frame LED update
4. PC reads `'#'` → stops re-sending until gesture changes

Character mapping:

| Gesture class | Name | UART char | MCU LED action |
|---|---|---|---|
| 0–8 | one–nine | `'1'`–`'9'` | Digit on both sides (green), STOP red |
| 9 | ten (fist) | `'0'` | Digit 0 on both sides |
| 10, 11 | good, not good | — | Ignored |
| 12 | ok | `'K'` | OK gesture on both sides (cyan), STOP yellow |
| no hand | — | — | Keep last display |

### Clock Configuration

- 80 MHz MCLK from SYSPLL (with `[SYSPLL_ERR_01]` FCC ratio-lock workaround in `SYSCFG_DL_SYSCTL_SYSPLL_init`)
- TIMA0 at BUSCLK=80 MHz (period 100 ticks); TIMG0 at ULPCLK=40 MHz (period 50 ticks)
- Both produce 800 kHz / 1.25 µs WS2815 bit period; per-timer T1H/T0H tick constants differ

### DMA Protocol

Each timer ZERO event triggers a single DMA byte transfer into the timer's CC register — the byte IS the PWM compare value encoding one WS2815 bit. Three trailing dummy zero bytes hold the line low for reset. `ws2815_wait_idle()` ensures ≥300 µs RESET latch (constant `WS2815_RESET_US`, exceeding the WS2815B minimum of 280 µs). `DMA_IRQHandler` stops both timers only after all three channels complete.

**Per-chain DMA buffer size:** `LED_COUNT × 24 + 3` bytes (24 bits per GRB888 LED + 3 dummy zero bytes for reset). Total SRAM for all three buffers: 1083 + 1083 + 243 = 2409 bytes.

### Driver API (`ws2815.h`)

- Init/update: `ws2815_init`, `ws2815_update`, `ws2815_is_busy`, `ws2815_wait_idle`
- Color: `ws2815_set_color`, `ws2815_fill_color`, `ws2815_clear_all`
- Chain enum: `ws2815_chain_t` (LIFT, RIGHT, STOP)
- 13 GRB888 color macros; colors packed GRB888 (bits [23:16]=G, [15:8]=R, [7:0]=B)
- LED counts: `WS2815_LIFT_LED_NUM=45`, `RIGHT=45`, `STOP=10`, `TOTAL=100`

### Main Loop

The `main()` loop uses `__WFI()` (Wait For Interrupt) between UART polls — the CPU sleeps in low-power mode and wakes on UART0 RX or DMA interrupts. There is no RTOS; all scheduling is interrupt-driven.

**Delay macro:** `LED_DELAY_MS(ms)` expands to `DL_Common_delayCycles(ms × 80000)` (busy-wait at 80 MHz MCLK). Defined identically in both `main.c` and `taillight_app.c` — if you change one, change both.

### PC-Side Gesture Detection (`user/gesture_detect/`)

A Python application (MediaPipe + PySimpleGUI) that detects hand gestures via webcam and sends the corresponding UART character to the MCU. Uses **binary finger counting** for digits (no ML model required) and **geometric detection** for OK gesture. It has its own `CLAUDE.md` with full details. Key points:

- **Entry point:** `python CVideo.py` (requires Python 3.9+, venv at `taillight/`)
- **Data flow:** Camera → MediaPipe landmarks → EMA smoothing → finger state detection → per-finger debounce → OK geometric check / binary counting → value stabilization → UART char send
- **Binary counting:** Thumb=1, Index=2, Middle=4, Ring=8, Pinky=16; digit = sum of raised fingers
- **UART:** COM port selectable in GUI, 115200 baud, ACK-based (`'#'` from MCU stops re-send)
- **Config:** `config.json` for detection thresholds (OK distance, debounce params, smoother alpha)
- **No TensorFlow dependency** — removed in binary counting refactoring
- **No training required** — binary counting and geometric OK detection are deterministic

### Gesture & Digit System

The gesture layer defines **per-chain color palettes** (`ws2815_gesture_t`) and **LED index masks** (`ws2815_digit_mask_t`, `ws2815_digit_mask_pair_t`). Effects receive gesture pointers so palettes are data-driven, not hardcoded.

- **Digit display**: 10 digit mask pairs (0–9) in `ws2815_digit_masks[]`, each with independent left (LIFT) and right (RIGHT) masks. `taillight_show_digit(digit, side, color)` paints a digit on one side only.
- **OK gesture**: `ws2815_gesture_ok` mask pair, displayed via `taillight_show_ok(side, color)`.
- **Rainbow-breathe**: `taillight_digit_rainbow_breathe()` cycles digits 0–9 then OK, with flowing rainbow colors and breathing brightness. STOP chain breathes blue (digits) or yellow-red warm (OK). **Warning:** this function contains an infinite `while(1)` loop — it must never be called from the UART command loop.
- **Legacy compat**: `ws2815_gesture_digits` combines gesture + single-digit masks for `taillight_display_digits()`.

## LED Physical Layout

### PCB Regions

The PCB has a natural **X=350 vertical boundary** separating left and right LED regions, and a **Y≈90 horizontal boundary** within the left region separating STOP from LIFT:

| Region | X range | Y range | Chain | LED Count |
|---|---|---|---|---|
| Upper-left | X < 350 | Y < 90 | STOP | 10 |
| Left-center | X < 350 | Y ≥ 90 | LIFT | 45 |
| Right | X ≥ 350 | all | RIGHT | 45 |

### Taillight Housing Mapping

When assembled into the housing, the three PCB regions map to functional zones separated by horizontal dividers:

| PCB Region | Housing Zone | Lens/Reflector |
|---|---|---|
| STOP (upper-left) | Upper narrow section (Y < 280) | Red lens |
| LIFT (left-center) | Middle wide section (Y = 300–730) | Chrome reflector bowl |
| RIGHT (right) | Lower wide section (Y = 740–1040) | Amber lens |

### LED Numbering — Spiral Rule (Global #1–#90)

The 90 LIFT+RIGHT LEDs follow a **global spiral numbering** (#1 through #90) that reflects the physical data-line daisy-chain order on the PCB. This is distinct from the per-chain WS2815 index (0–44).

#### LIFT Chain — Clockwise Spiral Outward (#1 → #45)

- **#1** = left center LED (closest to X=350 boundary on left side, WS2815 index 0)
- **#2** = the LED directly above the center
- Direction: **clockwise** spiral expanding outward (UP → RIGHT → DOWN → LEFT → UP …)
- **#45** = upper-right corner of left side (WS2815 index 44)

#### RIGHT Chain — Counterclockwise Spiral Inward (#46 → #90)

- **#46** = upper-left corner of right side (WS2815 index 0)
- Direction: **counterclockwise** spiral contracting inward (DOWN → RIGHT → UP → LEFT → DOWN …)
- **#90** = right center LED (closest to X=350 boundary on right side, WS2815 index 44)

#### The Two "Center" LEDs

- Left center (**#1**) and right center (**#90**) are **two different LEDs**
- They sit on opposite sides of the X=350 boundary, each the innermost LED of its spiral

#### STOP Chain (10 LEDs, not in #1–#90 scheme)

- Upper-left corner of PCB, small fan/trapezoid shape
- Row 0: 3 LEDs, Row 1: 7 LEDs
- Numbering follows WS2815 daisy-chain order (index 0–9)

### Global ↔ Chain Index Conversion

**IMPORTANT**: The conversion differs depending on context.

**WS2815 data-line order** (used by `ws2815_set_color` and the mapping table in driver API):
- Global #G (1–45): chain = LIFT, chain_index = G − 1
- Global #G (46–90): chain = RIGHT, chain_index = G − 46

**Gesture/digit mask order** (used in `ws2815_gesture_digits.c` and `ws2815_gesture_misc.c`):
- LIFT: `chain_index = 45 − global_number` (reversed — #1 → index 44, #45 → index 0)
- RIGHT: `chain_index = global_number − 46` (same as data-line order)

The reversal on the LIFT side exists because the clockwise-outward spiral means global #1 (center) is at the *end* of the WS2815 data line but the *start* of the spiral, so mask definitions that reference by global number must invert the LIFT index.

### Spiral Direction Diagram

```
        LEFT SIDE (LIFT)                  RIGHT SIDE (RIGHT)
        #1 → #45                               #46 → #90

    UR(#45) ← ← ← ← ←                  UL(#46)
     ↓                  ↑               ↓
     ↓   clockwise      ↑               ↓  counterclockwise
     ↓   outward        ↑               ↓  inward
     ↓                  ↑               ↓
     ↓   #2 (above)    ↑                ↓ → → → → center(#90)
     ↓       ↑         ↑
   center(#1) → → → → ↑

   UR = upper-right corner           UL = upper-left corner
   center = LED nearest X=350        center = LED nearest X=350
```

## Conventions

- **No SysConfig:** Device configuration is hand-maintained in `ti_msp_dl_config.h/.c`. Do not introduce a `.syscfg` file.
- **Documentation style:** Extensive Chinese/English explanatory header comments at the top of each source file — this is the primary documentation. Maintain this convention when adding new files.
- **Color format:** GRB888, not RGB — the WS2815B data wire order is G→R→B.
- **clangd:** `.clangd` points at `Debug/.clangd/compile_commands.json` (auto-generated by CCS, not for source control).
- **Git:** The repo root is `D:/data/code/ccs/`; this project is `taillight/` within it.
- **Layer separation:** Gesture and app layers include only `ws2815_types.h` (no hardware headers). New gesture data files should stay hw-free; only `ws2815.c` and `ti_msp_dl_config.c` touch registers.
- **Gitignored paths:** `Debug/`, `.settings/`, `.project`, `.cproject`, `targetConfigs/`, `user/gesture_detect/taillight/` — these are not in source control.
- **Linker memory:** FLASH 0x00000000 (128K), SRAM 0x20200000 (32K), 512-byte stack. Additional BCR_CONFIG (0x41C00000) and BSL_CONFIG (0x41C00100) regions exist for boot configuration but are unused in normal application code.
- **Error handling:** Silent bounds-checking throughout — out-of-range indices are dropped, invalid UART chars are ignored (display unchanged). No asserts, no error codes, no logging on the MCU. Unhandled interrupts hit `Default_Handler` (infinite loop) — this is the only "crash" mechanism.
- **ISR safety:** UART ACK (`'#'`) is sent from inside the RX ISR. DMA IRQ clears busy flag and stops timers. Neither ISR calls into the driver API — they only set flags / write registers.
