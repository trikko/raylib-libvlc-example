#!/bin/bash
# Used by the CI: runs the example on the test videos and, while it's playing, captures the whole screen.
# Usage: tests/run.sh <name>  ->  window-<name>.png (from raylib) and screen-<name>.png (from the OS)
name=$1
rm -f "window-$name.png" "screen-$name.png"

# On Windows the runner's console would cover our window
case "$(uname -s)" in Linux|Darwin) ;; *) powershell -NoProfile -Command "(New-Object -ComObject Shell.Application).MinimizeAll()"; sleep 1 ;; esac

./raylib-libvlc-example --screenshot "window-$name.png" tests/bigbuckbunny1.mp4 tests/bigbuckbunny2.mp4 &
pid=$!

# The example saves its screenshot when the videos are playing, then stays open for 3 more seconds
while [ ! -f "window-$name.png" ] && kill -0 $pid 2>/dev/null; do sleep 0.2; done

if [ -f "window-$name.png" ]; then
    sleep 0.5
    case "$(uname -s)" in
        Linux)  import -window root "screen-$name.png" ;;
        Darwin) screencapture -x "screen-$name.png" ;;
        *)      powershell -NoProfile -Command "
                    Add-Type -AssemblyName System.Windows.Forms, System.Drawing
                    \$b = [System.Windows.Forms.Screen]::PrimaryScreen.Bounds
                    \$img = New-Object System.Drawing.Bitmap \$b.Width, \$b.Height
                    [System.Drawing.Graphics]::FromImage(\$img).CopyFromScreen(\$b.Location, [System.Drawing.Point]::Empty, \$b.Size)
                    \$img.Save('screen-$name.png')" ;;
    esac
fi

wait $pid
