"""
简单串口测试 — 直接发送字符到 MCU，检查是否收到 '#' ACK。
Simple UART test: send characters to MCU, check for '#' ACK.
"""
import serial
import time
import sys

PORT = sys.argv[1] if len(sys.argv) > 1 else 'COM4'
BAUD = 115200

print(f"Opening {PORT} at {BAUD} baud...")
try:
    ser = serial.Serial(port=PORT, baudrate=BAUD, timeout=1)
except serial.SerialException as e:
    print(f"Failed to open {PORT}: {e}")
    sys.exit(1)

print(f"Connected to {PORT}. Sending test characters...")

# Test characters: '0'-'9' and 'K'
test_chars = ['1', '2', '3', '0', 'K']

for ch in test_chars:
    # Clear any pending data
    if ser.in_waiting > 0:
        ser.read(ser.in_waiting)

    # Send character
    ser.write(ch.encode('ascii'))
    print(f"  Sent: '{ch}' (0x{ord(ch):02X})", end="", flush=True)

    # Wait for ACK
    time.sleep(0.05)  # 50 ms should be plenty
    if ser.in_waiting > 0:
        resp = ser.read(ser.in_waiting)
        print(f"  -> Received: {resp} ({'ACK OK' if b'#' in resp else 'NOT ACK'})")
    else:
        print(f"  -> No response (timeout)")

print("\nDone. Closing port.")
ser.close()
