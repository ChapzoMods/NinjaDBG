#!/usr/bin/env bash
# NinjaDBG screenshot helper
# Starts Xvfb, runs the app for a moment, captures the window
set -e

cd /home/z/my-project/NinjaDBG

# Start Xvfb on display :99 with 1920x1200x24
export DISPLAY=:99
if ! pgrep -x Xvfb > /dev/null; then
    Xvfb :99 -screen 0 1920x1200x24 -ac +extension RANDR +render -noreset &
    XPID=$!
    echo "[shot] Xvfb started (pid $XPID)"
    sleep 1
fi

# Start a window manager (none available — we'll just rely on the app window itself)

# Launch target test program in background so we can attach
./build/target_test &
TPID=$!
echo "[shot] target_test pid=$TPID"
sleep 1

# Launch the debugger in background
./build/ninjadb &
DPID=$!
echo "[shot] ninjadb pid=$DPID"
sleep 2

# Auto-attach to the target by simulating user input via xdotool if available,
# otherwise just take the initial screen
if command -v xdotool &> /dev/null; then
    # Click "Attach" button (x ~ 250, y ~ 28)
    xdotool mousemove 250 28 click 1
    sleep 0.5
    # Now in modal: click first row of process list then click "Attach" button
    # Process picker modal: x ~ 460 (first row), y ~ 100
    xdotool mousemove 460 120 click 1
    sleep 0.3
    # Attach button at bottom right of modal
    xdotool mousemove 1380 940 click 1
    sleep 0.8
else
    echo "[shot] xdotool not available - taking initial screen"
fi

# Take screenshot using ImageMagick's import
OUT=/home/z/my-project/download/ninjadb_screenshot.png
mkdir -p /home/z/my-project/download
import -window root "$OUT"
echo "[shot] saved -> $OUT"

# Also save a window-only shot if we can find the window id
if command -v xdotool &> /dev/null; then
    WID=$(xdotool search --name "NinjaDBG" | head -1 || true)
    if [ -n "$WID" ]; then
        import -window "$WID" /home/z/my-project/download/ninjadb_window.png
        echo "[shot] window shot saved"
    fi
fi

# Cleanup
kill $DPID 2>/dev/null || true
kill $TPID 2>/dev/null || true

echo "[shot] done"
