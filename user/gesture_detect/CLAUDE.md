# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project Overview

PC-side hand gesture detection application that sends recognized gestures over UART to an MSPM0 MCU driving WS2815B LED taillights. Uses **binary finger counting** for digit gestures (0-10) and **geometric detection** for the OK gesture. No ML model or training required — MediaPipe hand landmarks are used directly to detect finger states and compute values.

## Run & Build

```bash
# Setup (Python 3.9+)
python -m venv taillight
taillight/Scripts/activate     # Windows
pip install -r requirements.txt

# Launch the GUI application
python CVideo.py

# Test UART communication with MCU
python test_uart.py [COM_PORT]       # default COM4
```

No test framework, linter, or CI is configured.

## Architecture

```
CVideo.py              ← main entry: PySimpleGUI app, camera loop, gesture detection + UART send
hand.py                ← MediaPipe hand detector wrapper (landmarks, bounding boxes, drawing)
landmarks.py           ← landmark math, finger state detection, OK gesture detection, per-finger debounce
config.py              ← detection parameter configuration (thresholds, debounce sizes)
config.json            ← parameter values (editable without code changes)
test_uart.py           ← standalone UART loopback test (send chars, check '#' ACK)
```

### Data Flow (Live Detection)

```
Camera frame
  → MediaPipe Hands (hand.py: runDetec)
  → select_primary_hand (landmarks.py): prefer right hand, else largest; also returns handedness
  → LandmarkSmoother.update (landmarks.py): EMA smooth → 21×3 array
  → get_finger_states (landmarks.py): smoothed landmarks → [thumb, index, middle, ring, pinky]
  → FingerDebouncer.update (landmarks.py): per-finger majority vote → debounced states
  → detect_ok_gesture? → 'K' : _binary_to_uart_char(binary_value)
  → _stabilize_value: 3 consecutive identical chars → confirmed UART char
  → uart_process: ACK-based ASCII char send to MCU
```

### Binary Finger Counting

Each finger represents a bit in a binary number:

| Finger | Bit | Value |
|---|---|---|
| Thumb | 0 | 1 |
| Index | 1 | 2 |
| Middle | 2 | 4 |
| Ring | 3 | 8 |
| Pinky | 4 | 16 |

Digit = sum of raised finger values. Example: thumb + index = 1 + 2 = **3**.

### Gesture ↔ UART Mapping (MCU Firmware Protocol)

| Binary Value | Fingers | UART Char | MCU Action |
|---|---|---|---|
| 0 | fist (all closed) | `'0'` (0x30) | Digit 0 display |
| 1 | thumb | `'1'` (0x31) | Digit 1 on LIFT+RIGHT (green), STOP red |
| 2 | index | `'2'` | Digit 2 |
| 3 | thumb+index | `'3'` | Digit 3 |
| 4 | middle | `'4'` | Digit 4 |
| 5 | thumb+middle | `'5'` | Digit 5 |
| 6 | index+middle | `'6'` | Digit 6 |
| 7 | thumb+index+middle | `'7'` | Digit 7 |
| 8 | ring | `'8'` | Digit 8 |
| 9 | thumb+ring | `'9'` | Digit 9 |
| 10 | index+ring | `'0'` | Digit 0 (ten) |
| 11-31 | various | — | No send, MCU keeps last display |
| OK | geometric | `'K'` (0x4B) | OK gesture (cyan), STOP yellow |

### Finger State Detection (`landmarks.get_finger_states`)

- **Thumb**: x-comparison of tip (landmark 4) vs IP joint (landmark 3), handedness-dependent. For right hand: tip.x < IP.x → up. For left hand: tip.x > IP.x → up.
- **Other 4 fingers**: tip.y < PIP.y → extended (image y-axis points downward).
- Works on normalized (0-1) landmark coordinates — no pixel conversion needed.
- Operates on EMA-smoothed landmarks for stability.

### OK Gesture Detection (`landmarks.detect_ok_gesture`)

Geometric detection — no ML model required:

1. **Thumb-index proximity**: 2D Euclidean distance between thumb tip and index tip, normalized by hand size (wrist-to-middle-MCP distance). Threshold: 0.25 (configurable via `config.json`).
2. **Other fingers extended**: middle, ring, and pinky must all be up (from debounced finger states).
3. **Priority**: OK is checked BEFORE binary counting. OK's raw finger state would give binary value 28 (unmapped), so checking OK first correctly routes it to `'K'`.

### Three-Layer Stability

| Layer | Stabilizes | Mechanism | Params | Latency |
|---|---|---|---|---|
| LandmarkSmoother | Raw landmark positions | EMA (α=0.3) | ~3 frames to converge | ~120ms |
| FingerDebouncer | Per-finger up/down state | 5-frame sliding window per finger, need 3 votes | ~2-3 frames | ~100ms |
| Value stabilizer | Final UART character | 3 consecutive identical chars | 3 frames | ~120ms |

Total worst-case ~8 frames (320ms at 25fps). Per-finger debouncing is better than whole-value voting: a single finger misdetection flips only one bit, avoiding large value jumps.

### UART Protocol

- **Port:** Selectable in GUI, **baud:** 115200, 8N1
- **PC → MCU:** ASCII character per the table above
- **MCU → PC:** `'#'` (0x23) ACK after receiving
- `test_uart.py` sends `['1','2','3','0','K']` and checks for `'#'` ACK
- **Startup:** Available COM ports are printed to console on launch
- **Not-connected warning:** If a gesture is confirmed but UART is not connected, a one-time warning is printed

## Configuration (`config.json`)

Detection parameters — no training config needed:

```json
{
  "ok_dist_threshold": 0.25,
  "finger_debounce_window": 5,
  "finger_debounce_threshold": 3,
  "value_stable_need": 3,
  "smoother_alpha": 0.3
}
```

- `ok_dist_threshold`: relative distance ratio for OK gesture detection
- `finger_debounce_window` / `finger_debounce_threshold`: per-finger debounce sliding window size and vote threshold
- `value_stable_need`: consecutive identical chars needed for value confirmation
- `smoother_alpha`: EMA smoothing coefficient for landmark positions

## Conventions

- **Chinese comments**: existing code has Chinese headers and log messages — maintain this convention.
- **Serial port**: selectable in GUI; to debug without MCU, the ACK-based flow control simply retries on timeout.
- **Error logging**: unhandled exceptions are written to `error.log` via `sys.excepthook`.
- **No ML model**: binary finger counting and geometric OK detection are deterministic — no training data or model file required.
