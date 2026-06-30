"""Pull onboard flash logs off the Shintai-OS board over USB.

The board logs autonomously to its internal LittleFS while untethered. Plug it
back in and run this: it sends 'P', the board dumps every flash file wrapped in
<<<BEGIN>>>/<<<END>>> markers, and we reconstruct each into logs/ with a name
that matches the shintai_log_*.csv glob (so hud.py / analyze.py pick them up).
Then it offers to erase the board's flash.

Run:  conda activate shintai && python ~/shintai-os/groundstation/shintai-pull.py
"""
import serial
import serial.tools.list_ports
import os
import re
import sys
import time
import datetime

BAUD = 115200
LOG_DIR = os.path.join(os.path.dirname(os.path.abspath(__file__)), "logs")


def find_port():
    ports = serial.tools.list_ports.comports()
    for p in ports:
        if any(x in p.description for x in ["QT Py", "ESP32", "USB Serial", "CP210", "CH340"]):
            return p.device
    print("Available ports:")
    for i, p in enumerate(ports):
        print(f"  {i}: {p.device} — {p.description}")
    return ports[int(input("Enter port number: "))].device


def main():
    os.makedirs(LOG_DIR, exist_ok=True)
    port = find_port()
    print(f"Connecting to {port} at {BAUD} baud...")

    pull_ts = datetime.datetime.now().strftime("%Y%m%d_%H%M%S")
    files, current, name, saved = {}, None, None, []

    with serial.Serial(port, BAUD, timeout=2) as ser:
        time.sleep(0.4)              # let the port settle (board keeps running)
        ser.reset_input_buffer()
        ser.write(b"\nP\n")          # request the flash dump
        print("Requested dump, reading...\n")

        idle = 0
        while True:
            raw = ser.readline()
            if not raw:
                idle += 1
                if idle > 6:          # ~12s of silence -> give up
                    print("Timed out waiting for board.")
                    break
                continue
            idle = 0
            line = raw.decode("utf-8", errors="replace").rstrip("\r\n")

            if line == "<<<NOFS>>>":
                print("Board reports no filesystem / onboard logging disabled.")
                return
            if line == "<<<DONE>>>":
                break

            m = re.match(r"<<<BEGIN (\S+) (\d+)>>>", line)
            if m:
                name, current = m.group(1), []
                continue
            if line.startswith("<<<END"):
                if name is not None:
                    files[name] = "\n".join(current)
                name, current = None, []
                continue
            if current is not None:
                current.append(line)

    # Reconstruct each flash file into logs/ under the shared glob name.
    for fname, body in files.items():
        body = body.strip("\n")
        rows = body.count("\n")               # minus header ≈ data rows
        seq = re.search(r"(\d+)", fname)
        seq = seq.group(1) if seq else "x"
        out = os.path.join(LOG_DIR, f"shintai_log_{pull_ts}_flash{seq}.csv")
        with open(out, "w", newline="") as fh:
            fh.write(body + "\n")
        saved.append((out, max(rows, 0)))
        print(f"  pulled {fname}  ->  {os.path.basename(out)}  ({max(rows,0)} rows)")

    if not saved:
        print("No flash files found on the board.")
        return

    print(f"\n{len(saved)} file(s) saved to logs/.")
    ans = input("Erase these files from the board flash now? [y/N] ").strip().lower()
    if ans == "y":
        with serial.Serial(port, BAUD, timeout=2) as ser:
            time.sleep(0.4)
            ser.reset_input_buffer()
            ser.write(b"\nE\n")
            t0 = time.time()
            while time.time() - t0 < 8:
                line = ser.readline().decode("utf-8", errors="replace").strip()
                if line.startswith("<<<ERASED"):
                    print(f"Board: {line}")
                    break
    else:
        print("Left board flash intact (run again any time, or send 'E' to erase).")


if __name__ == "__main__":
    main()
