#!/usr/bin/env bash
# Build XiaoZhi with Purple's face for the SenseCAP Watcher.
#
# Needs ESP-IDF 6.0.1 or newer, loaded in this shell:
#     . ~/esp/esp-idf/export.sh
#
# Fetches XiaoZhi (the version this repo was made for) into build/xiaozhi,
# copies Purple's files into its sensecap-watcher board, builds, and puts the
# results with a flash.json in build/out. Flash them with:
#     python scripts/flash.py --dir build/out
#
# Settings (environment variables):
#     LANGUAGE=en-US             screen and prompt language (zh-CN, de-DE, ...)
#     WAKE_WORD=wn9_hijoy_tts    "Hi Joy"; see README for others
set -euo pipefail

XIAOZHI_TAG="v2.5.2"
HERE="$(cd "$(dirname "$0")/.." && pwd)"
WORK="$HERE/build/xiaozhi"
OUT="$HERE/build/out"
LANGUAGE="${LANGUAGE:-en-US}"
WAKE_WORD="${WAKE_WORD:-wn9_hijoy_tts}"

if [ -z "${IDF_PATH:-}" ]; then
  echo "ESP-IDF isn't loaded. Run its export script first, for example:"
  echo "    . ~/esp/esp-idf/export.sh"
  exit 1
fi

if [ ! -d "$WORK/.git" ]; then
  echo "== Fetching XiaoZhi $XIAOZHI_TAG"
  git clone --depth 1 --branch "$XIAOZHI_TAG" https://github.com/78/xiaozhi-esp32.git "$WORK"
fi

echo "== Adding Purple's face to the sensecap-watcher board"
BOARD="$WORK/main/boards/sensecap-watcher"
git -C "$WORK" checkout -- main/boards/sensecap-watcher/sensecap_watcher.cc
cp "$HERE/board/sensecap-watcher/sensecap_watcher.cc" "$BOARD/"
rm -rf "$BOARD/purple_face"
cp -r "$HERE/board/sensecap-watcher/purple_face" "$BOARD/"

echo "== Building (language $LANGUAGE, wake word $WAKE_WORD). The first build takes 10-20 minutes."
(cd "$WORK" && python scripts/build.py sensecap-watcher --language "$LANGUAGE" --wake-word "$WAKE_WORD")

echo "== Collecting the results in build/out"
mkdir -p "$OUT"
B="$WORK/build"
cp "$B/bootloader/bootloader.bin" "$OUT/bootloader.bin"
cp "$B/partition_table/partition-table.bin" "$OUT/partition-table.bin"
cp "$B/ota_data_initial.bin" "$OUT/ota_data_initial.bin"
cp "$B/xiaozhi.bin" "$OUT/xiaozhi.bin"
cp "$B/generated_assets.bin" "$OUT/assets.bin"
python - "$OUT" "$XIAOZHI_TAG" "$LANGUAGE" "$WAKE_WORD" <<'EOF'
import hashlib, json, os, sys
out, tag, lang, wake = sys.argv[1:5]
parts = [("bootloader", "0x0", "bootloader.bin"), ("partition-table", "0x8000", "partition-table.bin"),
         ("ota-data", "0x10d000", "ota_data_initial.bin"), ("app", "0x200000", "xiaozhi.bin"),
         ("assets", "0xa00000", "assets.bin")]
sha = lambda f: hashlib.sha256(open(os.path.join(out, f), "rb").read()).hexdigest()
json.dump({"xiaozhi": tag, "language": lang, "wake_word": wake,
           "parts": [{"name": n, "offset": o, "file": f, "sha256": sha(f)} for n, o, f in parts]},
          open(os.path.join(out, "flash.json"), "w"), indent=2)
EOF
echo
echo "Built. Flash it with:"
echo "    python scripts/flash.py --dir build/out"
