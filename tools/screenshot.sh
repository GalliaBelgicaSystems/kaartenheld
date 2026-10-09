#!/usr/bin/env bash
set -euo pipefail

# Output file
OUTPUT_IMAGE="${1:-build/screenshot.png}"
ROM_PATH="${2:-build/kaartenheld.gb}"

if [ ! -f "$ROM_PATH" ]; then
    echo "Error: ROM file $ROM_PATH does not exist." >&2
    exit 1
fi

mkdir -p build

# Find emulator binary
EMULATOR=""
if command -v sameboy >/dev/null 2>&1; then
    EMULATOR="sameboy"
elif command -v mgba-sdl >/dev/null 2>&1; then
    EMULATOR="mgba-sdl"
elif command -v mgba-qt >/dev/null 2>&1; then
    EMULATOR="mgba-qt"
elif command -v mgba >/dev/null 2>&1; then
    EMULATOR="mgba"
fi

if [ -z "$EMULATOR" ]; then
    echo "Error: No supported emulator binary found for screenshot capture." >&2
    exit 1
fi

echo "Capturing screenshot of $ROM_PATH using $EMULATOR..."

# If headless screenshot capture is possible with Xvfb + ImageMagick
if command -v xvfb-run >/dev/null 2>&1 && command -v import >/dev/null 2>&1; then
    xvfb-run --auto-servernum --server-args="-screen 0 640x480x24" bash -c "
        $EMULATOR \"$ROM_PATH\" &
        EMU_PID=\$!
        sleep 4
        import -window root \"$OUTPUT_IMAGE\" || true
        kill -9 \$EMU_PID 2>/dev/null || true
    "
else
    echo "Warning: xvfb-run or import (ImageMagick) not found. Creating placeholder screenshot indicator."
    # Fail or produce valid image if image generator available
    if command -v convert >/dev/null 2>&1; then
        convert -size 160x144 xc:black -fill white -draw "text 20,70 'Game Boy ROM'" "$OUTPUT_IMAGE"
    else
        echo "Could not generate screenshot automatically." >&2
        exit 1
    fi
fi

if [ -f "$OUTPUT_IMAGE" ]; then
    echo "Screenshot saved to $OUTPUT_IMAGE"
else
    echo "Error: Failed to create $OUTPUT_IMAGE" >&2
    exit 1
fi
