#!/usr/bin/env python3
"""Flash Purple's face (XiaoZhi + Purple) onto a SenseCAP Watcher.

    python scripts/flash.py                  # the ready-made firmware in firmware/
    python scripts/flash.py --dir build/out  # your own build (scripts/build.sh)
    python scripts/flash.py --app-only       # just the program, after small changes

Only writes the parts listed in flash.json, and refuses anything that would
touch the factory area (0x9000-0x3B000), so your Watcher keeps its identity
and can always go back to the original firmware. Your Wi-Fi settings and
XiaoZhi activation are kept too.
"""
import argparse
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import watcher as w  # noqa: E402


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--port", help="serial port of the ESP32-S3 (found automatically if left out)")
    ap.add_argument("--dir", default="firmware", help="folder with flash.json and the .bin files (default: firmware)")
    ap.add_argument("--app-only", action="store_true", help="only write the program (app) part")
    a = ap.parse_args()

    m = w.load_manifest(a.dir)
    parts = [p for p in m["parts"] if not a.app_only or p["name"] == "app"]
    args = []
    for p in parts:
        path = os.path.join(a.dir, p["file"])
        off = int(p["offset"], 16)
        size = os.path.getsize(path)
        if w.overlaps_factory(off, size):
            sys.exit(f"Refusing to flash {p['file']}: it would overwrite the factory area.")
        if p.get("sha256") and w.sha256(path) != p["sha256"]:
            sys.exit(f"{p['file']} doesn't match its checksum: download it again.")
        args += [p["offset"], path]
        print(f"  {p['offset']:>9}  {p['file']}  ({size // 1024} KB)")

    if not os.path.isdir("backups") and not a.app_only:
        print("\nNo backups/ folder here. If you haven't backed up this Watcher yet, stop now (Ctrl+C) and run\n"
              "    python scripts/backup.py\nfirst. It's the only way back to the original firmware.")
        if input("Type 'go' to flash anyway: ").strip().lower() != "go":
            sys.exit("Stopped.")

    w.need_esptool()
    port = w.find_port(a.port)
    w.esptool("--baud", w.FAST_BAUD, "write-flash", "--flash-mode", "dio", "--flash-size", "32MB",
              "--flash-freq", "80m", *args, port=port)
    print("\nDone. The Watcher restarts by itself (if not, press the reset pinhole or unplug it for a moment).")


if __name__ == "__main__":
    main()
