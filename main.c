#include <raylib.h>
#include "vlc/vlc.h"

// Used for lists and thread
#include <glib.h>

#define WINDOW_WIDTH 800
#define WINDOW_HEIGHT 600

#define VIDEO_WIDTH 1280
#define VIDEO_HEIGHT 720

#define VIDEO_RENDER_WIDTH  384
#define VIDEO_RENDER_HEIGHT 216

// A video we show.
typedef struct {
    int         x,y;        // Position
    GMutex      mutex;      // Mutex to begin_vlc_rendering texture on drawing
    Texture2D   texture;    // Here we draw the pixel from vlc
    uint8_t*    buffer;     // Pixel received from vlc
    bool        needUpdate; // Texture is changed, we need to reload 

    libvlc_media_player_t *player;  // The mediaplayer
} Video;


static void *begin_vlc_rendering(void *data, void **p_pixels) 
{
    // Lock pixels. Wait for vlc to draw a frame inside.
    Video* video = (Video*)data;
    g_mutex_lock(&video->mutex);
    *p_pixels = video->buffer;

    return NULL; // Not used
}

static void end_vlc_rendering(void *data, void *id, void *const *p_pixels) 
{
    // Frame drawn. Unlock pixels.
    Video* video = (Video*)data;
    video->needUpdate = true;
    g_mutex_unlock(&video->mutex);
}

Video* add_new_video(libvlc_instance_t *libvlc, const char* src)
{
    // Init struct
    Video* video = malloc(sizeof(Video));

    g_mutex_init(&video->mutex); 

    libvlc_media_t* media = libvlc_media_new_path(libvlc, src); 
    video->player = libvlc_media_player_new_from_media(media);
    libvlc_media_release(media);

    video->buffer = malloc(VIDEO_WIDTH*VIDEO_HEIGHT*4); // Every pixel has 4 bytes (RGBA)
    video->needUpdate = true;
    video->x = rand()%WINDOW_WIDTH/2;
    video->y = rand()%WINDOW_HEIGHT/2;
    
    // Create a texture for raylib
    Image image = GenImageColor(VIDEO_WIDTH, VIDEO_HEIGHT, WHITE);
    ImageFormat(&image, PIXELFORMAT_UNCOMPRESSED_R5G6B5);
    video->texture = LoadTextureFromImage(image);
    UnloadImage(image);

    // Set callback for frame drawing
    libvlc_video_set_callbacks(video->player, begin_vlc_rendering, end_vlc_rendering, NULL, video);
    libvlc_video_set_format(video->player, "RV16", 1280, 720, 1280*2);

    return video;
}


int main(int argc, char *argv[]) 
{

    // The list of video we're currently displaying.
    GList* video_list = NULL;

    // The video we're moving around
    Video* dragging = NULL;

    libvlc_instance_t *libvlc = libvlc_new(0, NULL);
    
    if(libvlc == NULL) {
        g_print("Something went wrong with libvlc init.\n");
        return -1;
    }

    // Create raylib windows
    InitWindow(WINDOW_WIDTH, WINDOW_HEIGHT, "raylib + vlc");
    SetTargetFPS(60);

    while (!WindowShouldClose()) {

        if (IsKeyPressed(KEY_A))
        {
            Video* new_video = add_new_video(libvlc, "/home/andrea/src/raylib-gstreamer/BigBuckBunny.mp4");
            video_list = g_list_append(video_list, new_video);
            libvlc_media_player_play(new_video->player);
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
                GList* first = g_list_first(video_list);
                
                while(element != NULL)
                {
                    Video *video = element->data;
                    
                    if (
                        video->x < mouse_position.x && video->x+VIDEO_RENDER_WIDTH > mouse_position.x &&
                        video->y < mouse_position.y && video->y+VIDEO_RENDER_HEIGHT > mouse_position.y
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
                    video_on_top->x +12 <= mouse_position.x && video_on_top->x+VIDEO_RENDER_WIDTH-12 >= mouse_position.x &&
                    video_on_top->y +VIDEO_RENDER_HEIGHT-18 < mouse_position.y && video_on_top->y+VIDEO_RENDER_HEIGHT-18+6 > mouse_position.y
                )
                    libvlc_media_player_set_position(video_on_top->player, 1.0f * (mouse_position.x - video_on_top->x - 12) / (VIDEO_RENDER_WIDTH-24) );
                
            }
            
        }

        BeginDrawing();

            ClearBackground(RAYWHITE);

            // Draw all videos!
            GList* element = g_list_first(video_list);

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

                // The video on top has a blue border.
                if (element->next == NULL) DrawRectangle(video->x-4, video->y-4, VIDEO_RENDER_WIDTH+8, VIDEO_RENDER_HEIGHT+8, DARKBLUE);
                else DrawRectangle(video->x-4, video->y-4, VIDEO_RENDER_WIDTH+8, VIDEO_RENDER_HEIGHT+8, DARKGRAY);

                // We have new data from vlc, let's update the texture!
                if (video->needUpdate)
                {
                    g_mutex_lock(&video->mutex);
                    UpdateTexture(video->texture, video->buffer);
                    video->needUpdate = false;
                    g_mutex_unlock(&video->mutex);
                }

                // Draw the current frame
                DrawTextureEx(video->texture, (Vector2){video->x, video->y}, 0, 0.30f, WHITE);

                // Draw the seek bar
                double p = libvlc_media_player_get_position(video->player);
                DrawRectangle(video->x + 10, video->y + VIDEO_RENDER_HEIGHT - 20, VIDEO_RENDER_WIDTH-20, 10, LIGHTGRAY);
                DrawRectangle(video->x + 12, video->y + VIDEO_RENDER_HEIGHT - 18, (int)((VIDEO_RENDER_WIDTH-24)*p), 6, BLUE);
                
                element = element->next;
            }
            
            // Draw info
            DrawRectangle(0,600-40,800,40, LIGHTGRAY);
            DrawText("A : NEW VIDEO   SPACE : PLAY/PAUSE   R : RESTART", 200, 600-30, 20, BLACK);
            DrawFPS(30,600-30);

        EndDrawing();
    }

    // Cleanup!
    GList* element = g_list_first(video_list);
    while(element != NULL)
    {
        Video *video = element->data;
        element = element->next;

        libvlc_media_player_stop(video->player);
        libvlc_media_player_release(video->player);

        UnloadTexture(video->texture);
        g_mutex_clear(&video->mutex);
        free(video->buffer);
    }

    g_list_free(video_list);
    libvlc_release(libvlc);
    
    return 0;
}
