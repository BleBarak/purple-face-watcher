"""Shared helpers for backup.py, flash.py and restore.py.

Talks to the Watcher's ESP32-S3 with esptool (pip install esptool).
"""
import hashlib
import json
import os
import subprocess
import sys

# The Watcher's factory area: device identity and the keys it uses to reach
# Seeed's SenseCraft servers. Nothing in this repo ever writes here.
FACTORY_START, FACTORY_END = 0x9000, 0x3B000
FLASH_SIZE = 0x2000000  # 32 MB
FAST_BAUD = "2000000"


def esptool(*args, port=None, check=True, quiet=False):
    cmd = [sys.executable, "-m", "esptool", "--chip", "esp32s3"]
    if port:
        cmd += ["--port", port]
    cmd += list(args)
    if not quiet:
        print("$", " ".join(cmd))
    return subprocess.run(cmd, check=check, capture_output=quiet, text=True)


def need_esptool():
    try:
        out = subprocess.run([sys.executable, "-m", "esptool", "version"], capture_output=True, text=True)
    except OSError:
        out = None
    if out is None or out.returncode != 0:
        sys.exit("esptool is missing. Install it with:\n    python -m pip install --upgrade esptool")


def find_port(given=None):
    """The Watcher shows up as two serial ports: one is the ESP32-S3 (the one
    we want), the other the Himax AI chip. Ask each which chip it is."""
    if given:
        return given
    try:
        from serial.tools import list_ports  # comes with esptool
    except ImportError:
        sys.exit("pyserial is missing (it comes with esptool): python -m pip install esptool")
    ports = sorted(p.device for p in list_ports.comports() if p.vid is not None)
    if not ports:
        sys.exit("No USB serial ports found. Is the Watcher plugged in with a data cable (the USB-C on the back)?")
    print("Looking for the Watcher's ESP32-S3 on:", ", ".join(ports))
    for p in ports:
        r = esptool("chip-id", port=p, check=False, quiet=True)
        if r.returncode == 0 and "ESP32-S3" in r.stdout:
            print("Found it on", p)
            return p
    sys.exit("Couldn't find an ESP32-S3. Try again with --port, e.g. --port COM5 or --port /dev/ttyACM1")


def mac_of(port):
    r = esptool("read-mac", port=port, check=False, quiet=True)
    for line in r.stdout.splitlines():
        if line.strip().startswith("MAC:"):
            return line.split("MAC:")[1].strip().replace(":", "")
    return "unknown"


def sha256(path):
    h = hashlib.sha256()
    with open(path, "rb") as f:
        for block in iter(lambda: f.read(1 << 20), b""):
            h.update(block)
    return h.hexdigest()


def load_manifest(folder):
    path = os.path.join(folder, "flash.json")
    if not os.path.exists(path):
        sys.exit(f"{path} not found. Use --dir firmware (the ready-made build) or build/out (your own build).")
    with open(path) as f:
        return json.load(f)


def overlaps_factory(offset, size):
    return offset < FACTORY_END and offset + size > FACTORY_START
