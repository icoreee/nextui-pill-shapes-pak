#!/bin/sh

PAK_DIR="$(cd "$(dirname "$0")" && pwd)"
PAK_NAME="$(basename "$PAK_DIR")"
PAK_NAME="${PAK_NAME%.*}"

export SDCARD_PATH="${SDCARD_PATH:-/mnt/SDCARD}"
export PLATFORM="${PLATFORM:-tg5040}"
export USERDATA_PATH="${USERDATA_PATH:-$SDCARD_PATH/.userdata/$PLATFORM}"
export LOGS_PATH="${LOGS_PATH:-$USERDATA_PATH/logs}"
export PILL_SHAPES_PAK_DIR="$PAK_DIR"
export PILL_SHAPES_LOG="${PILL_SHAPES_LOG:-$LOGS_PATH/$PAK_NAME.txt}"
export HOME="${HOME:-$USERDATA_PATH/shaper}"

mkdir -p "$USERDATA_PATH" "$LOGS_PATH" "$HOME"

rm -f "$PILL_SHAPES_LOG"
exec >>"$PILL_SHAPES_LOG" 2>&1

if [ "$PILL_SHAPES_DEBUG" = "1" ]; then
	set -x
fi

echo "$0" "$*"
date

cd "$PAK_DIR" || exit 1
./pill-shapes.elf
