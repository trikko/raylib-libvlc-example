#include <raylib.h>
#include <rlgl.h>

#include "vlc/vlc.h"

// Used for lists and thread
#include <glib.h>

#include <stdlib.h>
#include <string.h>

#define WINDOW_WIDTH 800
#define WINDOW_HEIGHT 600


// A video we show.
// Frames are triple buffered: vlc writes into "back", a finished frame is swapped into "ready",
// and the main thread swaps "ready" with "front" to upload it. The mutex is held only for the swaps.
typedef struct {
    int         x,y;        // Position
    uint32_t    w,h;        // Frame size: vlc scales the video for us

    GMutex      mutex;      // Protects the buffer swaps and the flags below
    Texture2D   texture;    // Here we draw the pixel from vlc
    uint8_t*    back;       // vlc is drawing here (vlc thread only)
    uint8_t*    ready;      // Last complete frame
    uint8_t*    front;      // Uploaded to the texture (main thread only)
    bool        needUpdate; // A new frame is ready
    bool        needTexture;// Frame size is known (or changed), we need a new texture
    int         frames;     // Frames shown so far

    libvlc_media_player_t *player;  // The mediaplayer
} Video;

static void *begin_vlc_rendering(void *data, void **p_pixels) 
{
    // The back buffer belongs to vlc: no need to lock.
    Video* video = (Video*)data;
    *p_pixels = video->back;

    return NULL; // Not used
}

static void end_vlc_rendering(void *data, void *id, void *const *p_pixels) 
{
    // Frame drawn. It becomes the ready one.
    Video* video = (Video*)data;
    g_mutex_lock(&video->mutex);
    uint8_t* tmp = video->ready;
    video->ready = video->back;
    video->back = tmp;
    video->needUpdate = true;
    g_mutex_unlock(&video->mutex);
}

static unsigned setup_vlc_format(void **opaque, char *chroma, unsigned *width, unsigned *height, unsigned *pitches, unsigned *lines)
{
    // Called by vlc (on its own thread) before the first frame, and again after every stop/play.
    // We ask for RGB 24 bit, already scaled to fit 350x350px: much less data to move than the original size.
    Video* video = (Video*)*opaque;
    memcpy(chroma, "RV24", 4);

    float scale = (*width > *height) ? 350.0f / *width : 350.0f / *height;
    uint32_t w = (uint32_t)(*width * scale);
    uint32_t h = (uint32_t)(*height * scale);

    g_mutex_lock(&video->mutex);

    if (video->back == NULL || video->w != w || video->h != h)
    {
        video->w = w;
        video->h = h;

        // Every pixel has 3 bytes (RGB). The front buffer and the texture are (re)created on the main thread.
        MemFree(video->back);
        MemFree(video->ready);
        video->back = MemAlloc(w*h*3);
        video->ready = MemAlloc(w*h*3);
        video->needTexture = true;
        video->needUpdate = false;
    }

    g_mutex_unlock(&video->mutex);

    *width = w;
    *height = h;
    pitches[0] = w * 3;
    lines[0] = h;

    return 1; // Number of picture buffers
}

static void cleanup_vlc_format(void *opaque)
{
    // Nothing to do: the buffer is reused if the video is restarted, and freed by release_video()
}

Video* add_new_video(libvlc_instance_t *libvlc, const char* src, const char* protocol)
{
    // Init struct
    Video* video = malloc(sizeof(Video));

    g_mutex_init(&video->mutex); 

    // Local files are opened by path: vlc builds a valid uri on every OS
    libvlc_media_t* media;
    if (strcmp(protocol, "file") == 0) media = libvlc_media_new_path(libvlc, src);
    else
    {
        char *location = g_strdup_printf("%s://%s", protocol, src);
        media = libvlc_media_new_location(libvlc, location);
        g_free(location);
    }

    video->player = libvlc_media_player_new_from_media(media);
    libvlc_media_release(media);

    video->needUpdate = false;
    video->x = rand()%WINDOW_WIDTH/2;
    video->y = rand()%WINDOW_HEIGHT/2;

    video->w = 0;
    video->h = 0;
    video->back = NULL;
    video->ready = NULL;
    video->front = NULL;
    video->texture.id = 0;
    video->needTexture = false;
    video->frames = 0;

    // Set callbacks for frame format and drawing
    libvlc_video_set_callbacks(video->player, begin_vlc_rendering, end_vlc_rendering, NULL, video);
    libvlc_video_set_format_callbacks(video->player, setup_vlc_format, cleanup_vlc_format);
    
    return video;
}

void release_video(Video* video)
{
    // Stop the player first: after that vlc won't call our callbacks anymore,
    // so it's safe to free what they use.
    libvlc_media_player_stop(video->player);
    libvlc_media_player_release(video->player);

    if (video->texture.id != 0) UnloadTexture(video->texture);
    MemFree(video->back);
    MemFree(video->ready);
    MemFree(video->front);
    g_mutex_clear(&video->mutex);
    free(video);
}


int main(int argc, char *argv[]) 
{

    // The list of video we're currently displaying.
    GList* video_list = NULL;

    // The video we're moving around
    Video* dragging = NULL;

    libvlc_instance_t *libvlc = libvlc_new(4, (const char*[]){"--verbose=-1", "--no-xlib", "--drop-late-frames", "--live-caching=0"});
    
    if(libvlc == NULL) {
        g_print("Something went wrong with libvlc init.\n");
        return -1;
    }

/*  
    
    STARTING A WEBCAM STREAM: 

    Video* new_video = add_new_video(libvlc, "/dev/video0:chroma=mjpg:width=1280:height:720:fps=30:live-caching=0", "v4l2");
    video_list = g_list_append(video_list, new_video);
    libvlc_media_player_play(new_video->player);

*/

    // Create raylib windows
    InitWindow(WINDOW_WIDTH, WINDOW_HEIGHT, "raylib + vlc");
    SetTargetFPS(60);

    // Videos can be passed on the command line too.
    // With --screenshot <file> we save a screenshot and quit as soon as every video is playing (used by the CI).
    const char* screenshot = NULL;
    int exit_code = 0;
    bool quit = false;

    for (int i = 1; i < argc; ++i)
    {
        if (strcmp(argv[i], "--screenshot") == 0 && i + 1 < argc) screenshot = argv[++i];
        else
        {
            Video* new_video = add_new_video(libvlc, argv[i], "file");
            video_list = g_list_append(video_list, new_video);
            libvlc_media_player_play(new_video->player);
        }
    }

    while (!WindowShouldClose() && !quit) {

        // Drop a file to load it.
        if (IsFileDropped())
        {
            FilePathList files = LoadDroppedFiles();

            for(unsigned int i = 0; i < files.count; ++i)
            {
                Video* new_video = add_new_video(libvlc, files.paths[i], "file");
                video_list = g_list_append(video_list, new_video);
                libvlc_media_player_play(new_video->player);
            }

            UnloadDroppedFiles(files);
        }

        if (IsKeyPressed(KEY_SPACE))
        {
            GList* element = g_list_last(video_list);
            if (element != NULL)
            {
                Video* video = element->data;
                if (libvlc_media_player_is_playing(video->player)) libvlc_media_player_pause(video->player);
                else libvlc_media_player_play(video->player);
            }
        }

        if (IsKeyPressed(KEY_R))
        {
            GList* element = g_list_last(video_list);
            if (element != NULL) 
            {
                Video* video = element->data;
                libvlc_media_player_set_position(video->player, 0.0f);
                libvlc_media_player_play(video->player);
            }
        }

        if (IsKeyPressed(KEY_C))
        {
            // Close the video on top
            GList* element = g_list_last(video_list);
            if (element != NULL)
            {
                Video* video = element->data;
                if (dragging == video) dragging = NULL;
                video_list = g_list_delete_link(video_list, element);
                release_video(video);
            }
        }


        if (IsMouseButtonUp(MOUSE_BUTTON_LEFT)) dragging = NULL;
        if (IsMouseButtonDown(MOUSE_BUTTON_LEFT))
        {

            // If mouse button was already pressed, we move the video around
            if (dragging != NULL)
            {
                Vector2 delta = GetMouseDelta();
                dragging->x += delta.x;
                dragging->y += delta.y;
            } 
            else 
            {
                // User clicked mouse, checking if we're inside a video widget.
                Vector2 mouse_position = GetMousePosition();

                GList* element = g_list_last(video_list);
                
                while(element != NULL)
                {
                    Video *video = element->data;
                    
                    if (
                        video->x < mouse_position.x && video->x+video->w > mouse_position.x &&
                        video->y < mouse_position.y && video->y+video->h > mouse_position.y
                    )
                    {
                        dragging = element->data;

                        // We are over a video, move it on the top!
                        if (element != g_list_last(video_list))
                        {
                            video_list = g_list_remove_link(video_list, element);
                            video_list = g_list_append(video_list, video);
                        }
                    
                        break;
                    }

                    element = element->prev;
                }
            }
        }

        // If we click on the seek bar of current video...
        if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT))
        {
            GList* last = g_list_last(video_list);

            if (last != NULL)
            {
                Video *video_on_top = last->data;
                Vector2 mouse_position = GetMousePosition();

                if (
                    video_on_top->x +12 <= mouse_position.x && video_on_top->x+video_on_top->w-12 >= mouse_position.x &&
                    video_on_top->y +video_on_top->h-18 < mouse_position.y && video_on_top->y+video_on_top->h-18+6 > mouse_position.y
                )
                    libvlc_media_player_set_position(video_on_top->player, 1.0f * (mouse_position.x - video_on_top->x - 12) / (video_on_top->w-24) );
                
            }
            
        }

        BeginDrawing();

            ClearBackground(RAYWHITE);

            // Draw all videos!
            GList* element = g_list_first(video_list);

            if (element == NULL)
            {
                const char* message = "Drop here a video!";
                DrawText(message, (WINDOW_WIDTH-MeasureText(message,20))/2, (WINDOW_HEIGHT/2), 20, DARKGRAY);
            }

            while(element != NULL)
            {
                Video *video = element->data;
                
                // If video is ended, restart it!
                if (libvlc_media_player_get_state(video->player) == libvlc_Ended) 
                {
                    libvlc_media_player_stop(video->player);
                    libvlc_media_player_set_position(video->player, 0.0f);
                    libvlc_media_player_play(video->player);
                }

                g_mutex_lock(&video->mutex);

                // Frame size is known (or changed)? Create the front buffer and the texture for raylib.
                if (video->needTexture)
                {
                    if (video->texture.id != 0) UnloadTexture(video->texture);

                    MemFree(video->front);
                    video->front = MemAlloc(video->w*video->h*3);

                    Image image = { NULL, video->w, video->h, 1, PIXELFORMAT_UNCOMPRESSED_R8G8B8 };
                    video->texture = LoadTextureFromImage(image);
                    video->needTexture = false;
                }

                // Take the last complete frame, if any.
                bool newFrame = video->needUpdate;
                if (newFrame)
                {
                    uint8_t* tmp = video->front;
                    video->front = video->ready;
                    video->ready = tmp;
                    video->needUpdate = false;
                }

                g_mutex_unlock(&video->mutex);

                // Nothing to show until the first frame size is known.
                if (video->texture.id != 0)
                {
                    // The video on top has a blue border.
                    if (element->next == NULL) DrawRectangle(video->x-4, video->y-4, video->w+8, video->h+8, DARKBLUE);
                    else DrawRectangle(video->x-4, video->y-4, video->w+8, video->h+8, DARKGRAY);

                    // We have new data from vlc, let's update the texture! No lock: vlc never touches the front buffer.
                    if (newFrame)
                    {
                        UpdateTexture(video->texture, video->front);
                        video->frames++;
                    }

                    // Draw the current frame
                    DrawTexture(video->texture, video->x, video->y, WHITE);

                    // Draw the seek bar
                    double p = libvlc_media_player_get_position(video->player);
                    DrawRectangle(video->x + 10, video->y + video->h - 20, video->w-20, 10, LIGHTGRAY);
                    DrawRectangle(video->x + 12, video->y + video->h - 18, (int)((video->w-24)*p), 6, BLUE);
                }

                element = element->next;
            }
            
            // Draw info
            DrawRectangle(0,600-40,800,40, LIGHTGRAY);
            DrawText("SPACE : PLAY/PAUSE   R : RESTART   C : CLOSE", 150, 600-30, 20, BLACK);
            DrawFPS(30,600-30);

            if (screenshot != NULL)
            {
                // About one second of video for each one. If it doesn't happen, the screenshot helps to understand why.
                bool playing = video_list != NULL;
                for (GList* e = g_list_first(video_list); e != NULL; e = e->next)
                    if (((Video*)e->data)->frames < 30) playing = false;

                if (playing || GetTime() > 20)
                {
                    rlDrawRenderBatchActive(); // Flush what we've drawn so far, or the screenshot will be empty
                    TakeScreenshot(screenshot);
                    if (!playing) g_print("Timeout: videos are not playing.\n");
                    exit_code = playing ? 0 : 1;
                    quit = true;
                }
            }

        EndDrawing();
    }

    // Clean it up!
    GList* element = g_list_first(video_list);
    while(element != NULL)
    {
        release_video(element->data);
        element = element->next;
    }

    g_list_free(video_list);

    // Release libvlc only after all the players.
    libvlc_release(libvlc);

    CloseWindow();
    
    return exit_code;
}
