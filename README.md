# Hey!

I bet you have been trying to render and control a video with raylib for a long long time.

Don't you think you should at least buy me a [beer](https://paypal.me/andreafontana/5)?

See also: [raylib-ffmpeg-video](https://github.com/trikko/raylib-ffmpeg-video)

## What's this?
![](https://github.com/trikko/raylib-libvlc-example/blob/main/raylib-libvlc-example.gif?raw=true)


## How to build
 - Install [raylib](https://github.com/raysan5/raylib) 4.2 or newer. Build instructions [here](https://github.com/raysan5/raylib#build-and-installation).
 - Install libvlc 3.x and glib. On debian/ubuntu/etc.: ```sudo apt-get install libvlc-dev libglib2.0-dev```
 - Run ```make```

### Windows
Use [MSYS2](https://www.msys2.org/). From the UCRT64 shell:
```
pacman -S make mingw-w64-ucrt-x86_64-{gcc,pkgconf,raylib,glib2,vlc}
make
```

## How to use
 - Drop one or more videos on the window.
 - Drag a video to move it, click on its bar to seek.
 - `SPACE` play/pause, `R` restart, `C` close the video on top.
