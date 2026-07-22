#!/usr/bin/env python
"""Live AS3935 strike logger — reads the as3935-bringup serial console and appends
every validated lightning strike to groundstation/logs/lightning-<date>.csv.

Standalone bench tool for the Zanshin-adjacent lightning bring-up (NOT the main
Shintai-OS CSV contract). Flushes per strike so a yanked USB cable loses nothing,
and reconnects on serial drop (the QT Py native-USB port drifts under load).

    python tools/lightning-logger.py [seconds]     # default 570s per segment
"""
import serial, time, sys, glob, os
from datetime import datetime

DUR = int(sys.argv[1]) if len(sys.argv) > 1 else 570
ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
CSV = os.path.join(ROOT, "groundstation", "logs",
                   "lightning-%s.csv" % datetime.now().strftime("%Y%m%d"))


def find_port():
    ports = glob.glob("/dev/cu.usbmodem*")
    return ports[0] if ports else None


def open_serial():
    for _ in range(8):
        port = find_port()
        if port:
            try:
                s = serial.Serial(port, 115200, timeout=1)
                time.sleep(1.5)
                s.reset_input_buffer()
                return s, port
            except Exception as e:
                print("  (open %s failed: %s)" % (port, e))
        time.sleep(1.5)
    return None, None


new_file = not os.path.exists(CSV)
csv = open(CSV, "a")
if new_file:
    csv.write("wall_time,board_ms,distance_km,energy\n")
    csv.flush()

print("logging strikes -> %s  (segment %ds)" % (CSV, DUR))
strikes = disturbers = noise = 0
ser, port = open_serial()
if not ser:
    print("!! no board on USB — is it plugged in?")
    sys.exit(1)
print("connected on %s" % port)

end = time.time() + DUR
while time.time() < end:
    try:
        raw = ser.readline()
    except Exception as e:
        print("  (serial drop: %s — reconnecting)" % e)
        try: ser.close()
        except Exception: pass
        ser, port = open_serial()
        if not ser:
            print("  (could not reconnect — waiting)"); time.sleep(2); continue
        print("  reconnected on %s" % port); continue
    ln = raw.decode("utf-8", "replace").rstrip()
    if not ln:
        continue
    if "STRIKE" in ln:
        strikes += 1
        # parse "[<ms> ms] ⚡ STRIKE #n — <desc> (energy=<e>)"
        ms = ln.split(" ms]")[0].lstrip("[").strip() if " ms]" in ln else ""
        eng = ln.split("energy=")[1].rstrip(")").strip() if "energy=" in ln else ""
        if "OVERHEAD" in ln:               dist = "overhead"
        elif "out of range" in ln:         dist = "out_of_range"
        else:
            seg = ln.split("~")[1] if "~" in ln else ""
            dist = seg.split(" km")[0].strip() if " km" in seg else ""
        wall = datetime.now().strftime("%Y-%m-%d %H:%M:%S")
        csv.write("%s,%s,%s,%s\n" % (wall, ms, dist, eng)); csv.flush()
        print("  %s  STRIKE #%d  dist=%s  energy=%s" % (wall, strikes, dist, eng))
    elif "disturber" in ln:
        disturbers += 1
    elif "noise floor too low" in ln:
        noise += 1

csv.close()
print("segment done: strikes=%d disturbers=%d noise=%d  (csv: %s)"
      % (strikes, disturbers, noise, CSV))
