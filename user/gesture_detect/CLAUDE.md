# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project Overview

PC-side hand gesture detection application that sends recognized gestures over UART to an MSPM0 MCU driving WS2815B LED taillights. Uses MediaPipe for hand landmark detection, a TensorFlow/Keras MLP for classification, and PySimpleGUI for the desktop UI. The MCU side (firmware in the parent `taillight/` repo) receives gesture codes and renders digit/OK patterns on the LED chains.

## Run & Build

```bash
# Install dependencies (Python 3.9+)
pip install -r requirements.txt

# Launch the GUI application (main entry point)
python CVideo.py

# Manage gesture classes, collect training data, train model
python manage_gestures.py            # interactive menu
python manage_gestures.py list       # show all classes + data status
python manage_gestures.py add heart  # add a new gesture class
python manage_gestures.py remove heart
python manage_gestures.py collect --all      # collect data for all classes
python manage_gestures.py collect --missing  # collect only classes lacking .npy
python manage_gestures.py train      # train model and save to Model/
python manage_gestures.py eval       # live camera evaluation

# Test UART communication with MCU
python test_uart.py [COM_PORT]       # default COM4

# Package as Windows exe (PyInstaller)
pyinstaller HandAction.spec
```

No test framework, linter, or CI is configured.

## Architecture

```
CVideo.py              ← main entry: PySimpleGUI app, camera loop, prediction + UART send
hand.py                ← MediaPipe hand detector wrapper (landmarks, bounding boxes, finger states)
landmarks.py           ← landmark math: wrist-normalization, exponential smoothing, primary-hand selection
gesture.py             ← data collection, model definition (Keras MLP), training, evaluation
gestures_config.py     ← gestures.json load/save, path helpers, default config
manage_gestures.py     ← CLI/interactive tool for add/remove/collect/train/eval workflow
test_uart.py           ← standalone UART loopback test (send chars, check '#' ACK)

gestures.json          ← gesture class list, training params, model path
Data/static/*.npy      ← per-class landmark sample arrays (shape: N×63)
Model/action_13.h5     ← trained Keras model (HDF5)
```

### Data Flow (Live Detection)

```
Camera frame
  → MediaPipe Hands (hand.py: runDetec)
  → select_primary_hand (landmarks.py): prefer right hand, else largest
  → LandmarkSmoother.to_features (landmarks.py): EMA smooth → wrist-center → scale-normalize → 63-d vector
  → Keras model.predict → class id + confidence
  → Sliding vote window (15 frames, need 10): stable gesture confirmation
  → act_id_to_uart_char → uart_process: ACK-based ASCII char send to MCU
```

### Gesture ↔ UART Mapping (MCU Firmware Protocol)

The MCU firmware (`taillight_app.c: taillight_process_command`) accepts **ASCII characters**:

| Class Index | Gesture | UART Char | MCU Action |
|---|---|---|---|
| 0–8 | one–nine | `'1'`–`'9'` (0x31–0x39) | Digit display on LIFT+RIGHT chains (green), STOP red |
| 9 | ten (fist) | `'0'` (0x30) | Digit 0 display |
| 10, 11 | good, not good | — | Ignored by MCU |
| 12 | ok | `'K'` (0x4B) | OK gesture (cyan), STOP yellow |

Additional classes (e.g. "125", "235") are defined in `gestures.json` but have no MCU display logic.

### UART Protocol

- **Port:** COM4 (hardcoded in `CVideo.py`), **baud:** 115200, 8N1
- **PC → MCU:** ASCII character per the table above
- **MCU → PC:** `'#'` (0x23) ACK after receiving
- Hardware flow control (RTS/CTS, DSR/DTR) is explicitly disabled
- `test_uart.py` uses the correct ASCII protocol; it sends `['1','2','3','0','K']`

### Landmark Feature Pipeline (`landmarks.py`)

- **63-dimensional feature vector**: 21 landmarks × 3 coordinates (x, y, z)
- **Wrist-centering**: subtract wrist position from all landmarks
- **Scale normalization**: divide by distance from wrist to middle-finger MCP (index 9)
- **Exponential moving average** (`LandmarkSmoother`, α=0.3): smooths jitter across frames

### Stability Logic (`CVideo.py: _predict_action`)

1. Confidence threshold: 0.70 (below → "Unsure", no vote)
2. Sliding vote window: 15 frames, need 10 matching votes to confirm
3. Only sends UART code when a **new** gesture is confirmed (differs from `last_confirmed_id`)
4. Resets smoother + vote window when hand disappears or detection is toggled off

### Model Architecture (`gesture.py: getModel`)

Keras Sequential MLP: `63 → Dense(128,relu) → Dropout(0.3) → Dense(256,relu) → Dropout(0.3) → Dense(128,relu) → Dropout(0.3) → Dense(32,relu) → Dense(N,softmax)`

Trained with Adam optimizer, sparse categorical crossentropy, configurable epochs (default 80). Training adds Gaussian noise (σ=0.01) to training data for augmentation.

## Configuration (`gestures.json`)

All gesture classes and training hyperparameters live in `gestures.json`:

```json
{
  "actions": ["one", "two", ...],
  "frames_per_action": 500,
  "epochs": 80,
  "model_path": "Model/action.h5"
}
```

- `actions`: ordered list — the index determines the UART code (index + 1). **Changing the order invalidates the model** and requires retraining.
- `frames_per_action`: samples to collect per class
- `model_path`: relative to project root; the actual file on disk is `Model/action_13.h5`

## Known Issues

1. **Class count mismatch** — `gestures.json` lists 15 actions (including "125", "235") but the trained model `action_13.h5` has 13 output classes. When the model loads, `CVideo.py` requires `num_classes == len(self.actions)` and would show "Model/label mismatch, retrain" permanently. Fix: either remove "125"/"235" from config or retrain with all 15 classes (after collecting data for them).

2. **`pyserial` missing from `requirements.txt`** — both `CVideo.py` and `test_uart.py` import `serial`, but `pyserial` is not listed. Add `pyserial>=3.5` to `requirements.txt`.

## Conventions

- **Chinese comments**: existing code has Chinese headers and log messages — maintain this convention.
- **Serial port**: hardcoded as `COM4` in `CVideo.py`; to debug without MCU, comment out the `ser = serial.Serial(...)` block and uncomment `ser = None`.
- **Error logging**: unhandled exceptions are written to `error.log` via `sys.excepthook`.
- **PyInstaller**: `HandAction.spec` bundles mediapipe modules and the model file. Paths in the spec are absolute and need updating for other machines.
- **Unused dependencies**: `pycaw`, `comtypes`, `PyAudio` are in `requirements.txt` but never imported — leftovers from the upstream volume-control feature.
- **`actions` order matters**: the index in `gestures.json["actions"]` determines the UART code. Changing the order invalidates the trained model and requires retraining.
