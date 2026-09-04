# Taillight — MSPM0G3507 WS2815B LED Controller

Bare-metal embedded C firmware for **TI MSPM0G3507** (ARM Cortex-M0+, 128 KB flash / 32 KB SRAM) driving three WS2815B addressable RGB LED chains for a vehicle taillight (LIFT / RIGHT-turn / STOP lights). Uses timer PWM + DMA for bit-banging the WS2815 protocol.

A PC-side gesture detection app communicates with the MCU via UART, sending detected hand gestures (digits 0–9 and OK) to drive LED display patterns.

## Hardware

| Chain | Pin | Timer | DMA CH | LEDs | Zone |
|---|---|---|---|---|---|
| LIFT | PA12 | TIMG0_CCP0 | CH2 | 45 | Left side (chrome reflector) |
| RIGHT | PA28 | TIMA0_CCP3 | CH1 | 45 | Right side (amber lens) |
| STOP | PB4 | TIMA0_CCP2 | CH0 | 10 | Upper section (red lens) |

| UART | Pin | Function |
|---|---|---|
| UART0 | PA10 | TX (to PC) |
| UART0 | PA11 | RX (from PC) |

- **Clock:** 80 MHz MCLK from SYSPLL
- **UART:** 115200 baud, 8N1 (XDS110 backchannel on LaunchPad)

## Serial Protocol

```
PC gesture detection  ──'1'-'9','0','K'──▶  MCU UART0 RX ISR
                                                     │
                                              stores char, sends '#'
                                                     │
                                              main loop: LED display
```

| Gesture | UART char | MCU LED action |
|---|---|---|
| Digits 1–9 | `'1'`–`'9'` | Digit on both left+right (green), STOP red |
| Fist (zero) | `'0'` | Digit 0 on both sides |
| OK | `'K'` | OK gesture on both sides (cyan), STOP yellow |

## Build & Flash

This is a **TI Code Composer Studio (Theia-based)** project.

- **IDE:** CCS Theia → Project → Build All
- **CLI:** `cd Debug && gmake all` (requires CCS toolchain)
- **Flash:** CCS Debug perspective, XDS110 SWD probe
- **Output:** `Debug/taillight.out` + `Debug/taillight.hex`

## Project Structure

```
main.c                          UART-driven main loop
user/driver/                    WS2815B driver + UART communication
  inc/ws2815_types.h            Pure types, color macros, chain enum
  inc/ws2815.h                  WS2815 driver API
  inc/uart_comm.h               UART0 communication driver API
  src/ws2815.c                  DMA/PWM engine + DMA_IRQ handler
  src/uart_comm.c               UART0 RX ISR + ACK TX
user/gesture/                   Gesture & digit mask data
  inc/ws2815_gesture.h          Palette, digit-mask, gesture structs
  src/ws2815_gesture_digits.c   10 digit mask pairs (0–9)
  src/ws2815_gesture_misc.c     OK gesture mask
user/app/                       Taillight effect functions
  src/taillight_app.c           process_command, palette fill, scan, breathe
user/gesture_detect/            PC-side gesture detection (Python)
  CVideo.py                     PySimpleGUI app + UART integration
  hand.py                       MediaPipe hand detector
  requirements.txt              Python dependencies
ti_msp_dl_config.h/.c           Hand-written device config (no SysConfig)
```

## PC Gesture Detection Setup

```bash
cd user/gesture_detect
python -m venv .venv           # requires Python 3.10
.venv/Scripts/activate         # Windows
pip install -r requirements.txt
python CVideo.py
```

In the GUI: select COM port → Connect → enable 手势 checkbox → show gestures to camera.

## Conventions

- **No SysConfig** — device config is hand-maintained in `ti_msp_dl_config.h/.c`
- **Color format:** GRB888 (WS2815B data wire order is G→R→B)
- **Documentation:** Chinese/English dual header comments in each source file
