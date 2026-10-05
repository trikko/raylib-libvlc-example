// raylib + libvlc, the D version of ../main.c
import c; // raylib, rlgl and libvlc headers, through ImportC (see c.c)
import colors;
import video;

import std.algorithm : all, remove;
import std.getopt : getopt;
import std.random : uniform;
import std.stdio : stderr;
import std.string : toStringz;

enum windowWidth = 800;
enum windowHeight = 600;

// These are used by the main thread only (module variables in D are thread-local)
libvlc_instance_t* vlc;
Video[] videos;     // The videos on screen: the last one is on top
Video dragging;     // The video we're moving around

// With --screenshot <file> we save a screenshot as soon as every video is playing, and quit 3 seconds later
// (used by the CI, that meanwhile captures the whole screen).
string screenshot;
double quitAt = 0;
int exitCode = 0;

int main(string[] args)
{
    const(char)*[] vlcArgs = ["--verbose=-1", "--no-xlib", "--drop-late-frames", "--live-caching=0"];
    vlc = libvlc_new(cast(int) vlcArgs.length, vlcArgs.ptr);
    if (vlc is null)
    {
        stderr.writeln("Something went wrong with libvlc init.");
        return -1;
    }
    scope(exit) libvlc_release(vlc); // Only after all the players: scope(exit) runs in reverse order

    InitWindow(windowWidth, windowHeight, "raylib + vlc (D)");
    if (!IsWindowReady()) return -1;
    scope(exit) CloseWindow();
    SetTargetFPS(60);

    scope(exit) foreach (video; videos) video.close();

    // Videos can be passed on the command line too
    getopt(args, "screenshot", &screenshot);

    foreach (n, path; args[1 .. $])
    {
        // Two columns, slightly staggered, so that every video is visible
        const x = 30 + (n % 2) * 390 + (n / 2 % 5) * 20;
        const y = 40 + (n % 2) * 140 + (n / 2 % 5) * 20;
        open(path.toStringz, cast(int) x, cast(int) y);
    }

    while (!WindowShouldClose() && (quitAt == 0 || GetTime() < quitAt))
    {
        openDroppedFiles();
        handleKeys();
        handleMouse();

        foreach (video; videos) video.update();

        BeginDrawing();
        drawScene();
        if (screenshot !is null) checkScreenshot();
        EndDrawing();
    }

    return exitCode;
}

Video top() { return videos.length ? videos[$ - 1] : null; }

// By default at a random position, away from the edges and the info bar
Video open(const(char)* path,
           int x = uniform(20, windowWidth - maxSize - 20),
           int y = uniform(20, windowHeight - 40 - maxSize - 20))
{
    auto video = new Video(vlc, path, x, y);
    videos ~= video;
    video.play();
    return video;
}

void closeTop()
{
    if (dragging is top) dragging = null;
    top.close();
    videos = videos[0 .. $ - 1];
}

void openDroppedFiles()
{
    if (!IsFileDropped()) return;

    auto files = LoadDroppedFiles();
    foreach (path; files.paths[0 .. files.count]) open(path);
    UnloadDroppedFiles(files);
}

// The keys act on the video on top
void handleKeys()
{
    if (top is null) return;

    if (IsKeyPressed(KEY_SPACE)) top.togglePause();
    if (IsKeyPressed(KEY_R)) top.restart();
    if (IsKeyPressed(KEY_C)) closeTop();
}

void handleMouse()
{
    const mouse = GetMousePosition();

    if (IsMouseButtonUp(MOUSE_BUTTON_LEFT)) dragging = null;

    if (IsMouseButtonDown(MOUSE_BUTTON_LEFT))
    {
        if (dragging !is null)
        {
            // If mouse button was already pressed, we move the video around
            const delta = GetMouseDelta();
            dragging.x += cast(int) delta.x;
            dragging.y += cast(int) delta.y;
        }
        else
        {
            // User clicked: the topmost video under the mouse goes on top of the others
            foreach_reverse (i, video; videos)
            {
                if (!CheckCollisionPointRec(mouse, video.bounds)) continue;

                dragging = video;
                videos = videos.remove(i) ~ video;
                break;
            }
        }
    }

    // A click on the seek bar of the video on top
    if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT) && top !is null)
    {
        const bar = top.seekBar;
        if (CheckCollisionPointRec(mouse, bar)) top.seek((mouse.x - bar.x) / bar.width);
    }
}

void drawScene()
{
    ClearBackground(rayWhite);

    if (videos.length == 0)
    {
        enum message = "Drop here a video!";
        DrawText(message, (windowWidth - MeasureText(message, 20)) / 2, windowHeight / 2, 20, darkGray);
    }

    foreach (video; videos) video.draw(video is top);

    // Draw info
    DrawRectangle(0, windowHeight - 40, windowWidth, 40, lightGray);
    DrawText("SPACE : PLAY/PAUSE   R : RESTART   C : CLOSE", 150, windowHeight - 30, 20, black);

    // Not in the CI screenshots: there's no GPU, so the FPS would be misleading
    if (screenshot is null) DrawFPS(30, windowHeight - 30);
}

// Call it while drawing, after drawScene()
void checkScreenshot()
{
    if (quitAt != 0) return; // Already taken

    // About one second of video for each one. If it doesn't happen, the screenshot helps to understand why.
    const playing = videos.length > 0 && videos.all!(v => v.frames >= 30);
    if (!playing && GetTime() < 20) return;

    rlDrawRenderBatchActive(); // Flush what we've drawn so far, or the screenshot will be empty
    TakeScreenshot(screenshot.toStringz);

    if (!playing) stderr.writeln("Timeout: videos are not playing.");
    exitCode = playing ? 0 : 1;
    quitAt = GetTime() + 3;
}
