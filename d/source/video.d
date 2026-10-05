// A video played by libvlc and drawn with raylib.
module video;

import c;
import colors;

import core.stdc.stdlib : malloc, free;
import core.sync.mutex : Mutex;
import std.algorithm : swap;

enum maxSize = 350; // Videos are scaled to fit maxSize x maxSize px

// A video we show.
// Frames are triple buffered: vlc writes into "back", a finished frame is swapped into "ready",
// and the main thread swaps "ready" with "front" to upload it. The mutex is held only for the swaps.
final class Video
{
    int x, y;           // Position
    uint w, h;          // Frame size: vlc scales the video for us
    int frames;         // Frames shown so far

    private
    {
        libvlc_media_player_t* player;
        Texture2D texture;

        Mutex mutex;        // Protects the buffer swaps and the flags below
        ubyte[] back;       // vlc is drawing here (vlc thread only)
        ubyte[] ready;      // Last complete frame
        ubyte[] front;      // Uploaded to the texture (main thread only)
        bool newFrame;      // A new frame is ready
        bool newSize;       // Frame size is known (or changed), we need a new texture
    }

    this(libvlc_instance_t* vlc, const(char)* path, int x, int y)
    {
        this.x = x;
        this.y = y;
        mutex = new Mutex;

        // Local files are opened by path: vlc builds a valid uri on every OS
        auto media = libvlc_media_new_path(vlc, path);
        scope(exit) libvlc_media_release(media);

        player = libvlc_media_player_new_from_media(media);
        libvlc_video_set_callbacks(player, &beginRendering, &endRendering, null, cast(void*) this);
        libvlc_video_set_format_callbacks(player, &setupFormat, null);
    }

    void close()
    {
        // Stop the player first: after that vlc won't call our callbacks anymore
        libvlc_media_player_stop(player);
        libvlc_media_player_release(player);

        if (texture.id != 0) UnloadTexture(texture);
        freeFrame(back);
        freeFrame(ready);
        freeFrame(front);
    }

    void play()     { libvlc_media_player_play(player); }
    void restart()  { libvlc_media_player_set_position(player, 0); play(); }
    void seek(float position) { libvlc_media_player_set_position(player, position); }

    void togglePause()
    {
        if (libvlc_media_player_is_playing(player)) libvlc_media_player_pause(player);
        else play();
    }

    Rectangle bounds() const { return Rectangle(x, y, w, h); }

    // The seek bar, inside the bottom of the video
    Rectangle seekBar() const { return Rectangle(x + 12, y + h - 18, w - 24, 6); }

    // Called once per frame, before drawing
    void update()
    {
        // If video is ended, restart it!
        if (libvlc_media_player_get_state(player) == libvlc_Ended)
        {
            libvlc_media_player_stop(player);
            restart();
        }

        bool upload;
        {
            mutex.lock_nothrow();
            scope(exit) mutex.unlock_nothrow();

            // Frame size is known (or changed)? Create the front buffer and the texture.
            if (newSize)
            {
                if (texture.id != 0) UnloadTexture(texture);
                freeFrame(front);
                front = allocFrame(w, h);
                texture = LoadTextureFromImage(Image(null, w, h, 1, PIXELFORMAT_UNCOMPRESSED_R8G8B8));
                newSize = false;
            }

            // Take the last complete frame, if any.
            if (newFrame)
            {
                swap(front, ready);
                newFrame = false;
                upload = true;
            }
        }

        // No lock: vlc never touches the front buffer.
        if (upload)
        {
            UpdateTexture(texture, front.ptr);
            frames++;
        }
    }

    void draw(bool onTop)
    {
        if (texture.id == 0) return; // Nothing to show until the first frame size is known

        DrawRectangle(x - 4, y - 4, w + 8, h + 8, onTop ? darkBlue : darkGray);
        DrawTexture(texture, x, y, white);

        auto bar = seekBar;
        DrawRectangle(x + 10, y + h - 20, w - 20, 10, lightGray);
        DrawRectangleRec(Rectangle(bar.x, bar.y, bar.width * libvlc_media_player_get_position(player), bar.height), blue);
    }

    // vlc callbacks: they run on vlc threads, unknown to the D runtime, so no GC and no exceptions.
    private extern(C) static nothrow @nogc
    {
        void* beginRendering(void* opaque, void** pixels)
        {
            // The back buffer belongs to vlc: no need to lock.
            *pixels = (cast(Video) opaque).back.ptr;
            return null;
        }

        void endRendering(void* opaque, void* picture, const(void*)* pixels)
        {
            // Frame drawn. It becomes the ready one.
            auto video = cast(Video) opaque;
            video.mutex.lock_nothrow();
            scope(exit) video.mutex.unlock_nothrow();

            swap(video.ready, video.back);
            video.newFrame = true;
        }

        // Called before the first frame, and again after every stop/play.
        // We ask for RGB 24 bit, already scaled to fit maxSize x maxSize.
        uint setupFormat(void** opaque, char* chroma, uint* width, uint* height, uint* pitches, uint* lines)
        {
            auto video = cast(Video) *opaque;
            chroma[0 .. 4] = "RV24";

            const scale = float(maxSize) / (*width > *height ? *width : *height);
            const w = cast(uint)(*width * scale);
            const h = cast(uint)(*height * scale);

            {
                video.mutex.lock_nothrow();
                scope(exit) video.mutex.unlock_nothrow();

                if (video.back is null || video.w != w || video.h != h)
                {
                    video.w = w;
                    video.h = h;

                    // The front buffer and the texture are (re)created on the main thread.
                    freeFrame(video.back);
                    freeFrame(video.ready);
                    video.back = allocFrame(w, h);
                    video.ready = allocFrame(w, h);
                    video.newSize = true;
                    video.newFrame = false;
                }
            }

            *width = w;
            *height = h;
            pitches[0] = w * 3;
            lines[0] = h;

            return 1; // Number of picture buffers
        }
    }
}

private:

// Frame buffers are shared with vlc threads, so they can't live in the GC heap: they're malloc'ed.
// Pixels are RGB, 3 bytes each.

ubyte[] allocFrame(uint w, uint h) nothrow @nogc
{
    const size = w * h * 3;
    return (cast(ubyte*) malloc(size))[0 .. size];
}

void freeFrame(ref ubyte[] frame) nothrow @nogc
{
    free(frame.ptr);
    frame = null;
}
