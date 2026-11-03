#!/usr/bin/env python3
"""Back up your Watcher's whole flash (32 MB) before you flash anything.

    python scripts/backup.py [--port PORT]

Writes backups/watcher_<mac>_full.bin (everything: SenseCraft firmware, its
settings and the factory identity) and backups/watcher_<mac>_nvsfactory.bin
(just the identity), plus SHA256SUMS. Takes about 9 minutes. Keep these files
safe: they are the only way back to the original firmware with your device's
own identity. scripts/restore.py puts them back.
"""
import argparse
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import watcher as w  # noqa: E402


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--port", help="serial port of the ESP32-S3 (found automatically if left out)")
    ap.add_argument("--out", default="backups", help="folder for the backup (default: backups)")
    a = ap.parse_args()

    w.need_esptool()
    port = w.find_port(a.port)
    mac = w.mac_of(port)
    os.makedirs(a.out, exist_ok=True)
    full = os.path.join(a.out, f"watcher_{mac}_full.bin")
    factory = os.path.join(a.out, f"watcher_{mac}_nvsfactory.bin")
    if os.path.exists(full):
        sys.exit(f"{full} already exists. Move it somewhere safe first; this script never overwrites a backup.")

    print("\nReading the whole 32 MB flash. This takes about 9 minutes, don't unplug the Watcher.\n")
    w.esptool("--baud", w.FAST_BAUD, "read-flash", "0", hex(w.FLASH_SIZE), full, port=port)
    print("\nReading the factory area again on its own, to check the first read.\n")
    w.esptool("--baud", w.FAST_BAUD, "read-flash", hex(w.FACTORY_START),
              hex(w.FACTORY_END - w.FACTORY_START), factory, port=port)

    with open(full, "rb") as f:
        f.seek(w.FACTORY_START)
        same = f.read(w.FACTORY_END - w.FACTORY_START) == open(factory, "rb").read()
    blank = all(b == 0xFF for b in open(factory, "rb").read())
    if not same:
        sys.exit("The two reads of the factory area differ. Don't flash anything; run the backup again.")
    if blank:
        print("WARNING: the factory area is empty. That's normal only if this Watcher never had SenseCraft.")

    with open(os.path.join(a.out, "SHA256SUMS"), "a") as f:
        for p in (full, factory):
            f.write(f"{w.sha256(p)}  {os.path.basename(p)}\n")
    print(f"\nBackup done and checked:\n  {full}\n  {factory}\nCopy the '{a.out}' folder somewhere safe "
          "(another drive or cloud) before you go on.")


if __name__ == "__main__":
    main()
