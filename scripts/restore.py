#!/usr/bin/env python3
"""Put your Watcher back exactly as it was: writes the full backup made by
backup.py over the whole flash (original SenseCraft firmware, settings and
identity).

    python scripts/restore.py backups/watcher_<mac>_full.bin [--port PORT]
"""
import argparse
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import watcher as w  # noqa: E402


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("backup", help="the *_full.bin file from backup.py")
    ap.add_argument("--port", help="serial port of the ESP32-S3 (found automatically if left out)")
    a = ap.parse_args()

    if os.path.getsize(a.backup) != w.FLASH_SIZE:
        sys.exit(f"{a.backup} isn't a full 32 MB backup.")
    w.need_esptool()
    port = w.find_port(a.port)
    mac = w.mac_of(port)
    if mac != "unknown" and mac not in os.path.basename(a.backup):
        print(f"Careful: this Watcher's MAC is {mac}, but the file name says it came from another one.\n"
              "Restoring another Watcher's backup copies its identity too.")
    if input("This replaces everything on the Watcher with the backup. Type 'restore' to go on: ").strip() != "restore":
        sys.exit("Stopped.")
    w.esptool("--baud", w.FAST_BAUD, "write-flash", "--flash-mode", "dio", "--flash-size", "32MB",
              "--flash-freq", "80m", "0x0", a.backup, port=port)
    print("\nRestored. The Watcher restarts with its original firmware.")


if __name__ == "__main__":
    main()
