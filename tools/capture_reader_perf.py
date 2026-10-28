#!/usr/bin/env python3
"""中文：有时限的只读串口采集，断线后重连，结束关闭端口。
English: Bounded read-only serial capture, reconnecting on disconnect and closing on exit.
"""
import argparse
from pathlib import Path
import time


def main():
    import serial
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--port", required=True)
    parser.add_argument("--seconds", type=int, default=60)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    if args.seconds <= 0:
        parser.error("--seconds must be positive")
    args.output.parent.mkdir(parents=True, exist_ok=True)
    deadline = time.monotonic() + args.seconds
    samples = 0
    try:
        with args.output.open("xb", buffering=0) as output:
            while time.monotonic() < deadline:
                try:
                    # 打开前禁用控制线，避免主动复位。/ Disable control lines before opening to avoid requesting reset.
                    with serial.Serial(port=None, baudrate=115200, timeout=.25, exclusive=True) as port:
                        port.dtr = False
                        port.rts = False
                        port.port = args.port
                        port.open()
                        while time.monotonic() < deadline:
                            data = port.read(8192)
                            output.write(data)
                            samples += data.count(b"READER_PERF ")
                except (serial.SerialException, OSError) as error:
                    print(f"Waiting for {args.port}: {error}", flush=True)
                    time.sleep(min(1, max(0, deadline - time.monotonic())))
    except KeyboardInterrupt:
        pass
    print(f"Capture closed; approximately {samples} records saved to {args.output}")


if __name__ == "__main__":
    main()
