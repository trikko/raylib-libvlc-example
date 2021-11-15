#include <raylib.h>
#include <rlgl.h>

#include "vlc/vlc.h"

// Used for lists and thread
#include <glib.h>

#define WINDOW_WIDTH 800
#define WINDOW_HEIGHT 600


// A video we show.
typedef struct {
    int         x,y;        // Position
    uint32_t    texW,texH;  // Frame width & height
    uint32_t    w,h;        // Render width & height
    float       scale;      // Render scale;

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

    video->needUpdate = false;
    video->x = rand()%WINDOW_WIDTH/2;
    video->y = rand()%WINDOW_HEIGHT/2;

    video->texW = 0;
    video->texH = 0;
    video->buffer = 0;
    video->texture.id = 0;

    // Set callback for frame drawing
    libvlc_video_set_callbacks(video->player, begin_vlc_rendering, end_vlc_rendering, NULL, video);
    
    return video;
}


int main(int argc, char *argv[]) 
{

    // The list of video we're currently displaying.
    GList* video_list = NULL;

    // The video we're moving around
    Video* dragging = NULL;

    libvlc_instance_t *libvlc = libvlc_new(3, (const char*[]){"--no-xlib", "--verbose", "-1"});
    
    if(libvlc == NULL) {
        g_print("Something went wrong with libvlc init.\n");
        return -1;
    }

    // Create raylib windows
    InitWindow(WINDOW_WIDTH, WINDOW_HEIGHT, "raylib + vlc");
    SetTargetFPS(60);

    while (!WindowShouldClose()) {

        // Drop a file to load it.
        if (IsFileDropped())
        {
            int count;
            char** files = GetDroppedFiles(&count);

            for(int i = 0; i < count; ++i)
            {
                Video* new_video = add_new_video(libvlc, files[i]);
                video_list = g_list_append(video_list, new_video);
                libvlc_media_player_play(new_video->player);
            }

            ClearDroppedFiles();
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

                // First time this video is rendered? Checking size.
                if (video->buffer == 0 && libvlc_media_player_get_state(video->player) == libvlc_Playing)
                {
                    libvlc_video_get_size(video->player, 0, &video->texW, &video->texH);

                    // If we can't get width/height, we don't allocate anything
                    if (video->texW > 0 && video->texH > 0)
                    {
                        // Video will be rendered in a 350x350px max
                        if (video->texW > video->texH) video->scale = 350.0f/video->texW;
                        else video->scale = 350.0f/video->texH;

                        video->w = (int)(video->texW * video->scale);
                        video->h = (int)(video->texH * video->scale);
                        
                        libvlc_video_set_format(video->player, "RV24", video->texW,video->texH, video->texW*3);

                        // Create a texture for raylibc
                        g_mutex_lock(&video->mutex);
                        video->texture.id = rlLoadTexture(NULL,  video->texW, video->texH, PIXELFORMAT_UNCOMPRESSED_R8G8B8, 1);
                        video->texture.width =  video->texW;
                        video->texture.height = video->texH;
                        video->texture.format = PIXELFORMAT_UNCOMPRESSED_R8G8B8;
                        video->texture.mipmaps = 1;

                        // Create a buffer to store pixels
                        video->buffer = MemAlloc(video->texW*video->texH*3); // Every pixel has 3 bytes (RGB)
                        video->needUpdate = false;
                        g_mutex_unlock(&video->mutex);  
                    }

                    continue; 
                }

                // The video on top has a blue border.
                if (element->next == NULL) DrawRectangle(video->x-4, video->y-4, video->w+8, video->h+8, DARKBLUE);
                else DrawRectangle(video->x-4, video->y-4, video->w+8, video->h+8, DARKGRAY);

                // We have new data from vlc, let's update the texture!
                if (video->needUpdate)
                {
                    g_mutex_lock(&video->mutex);
                    UpdateTexture(video->texture, video->buffer);
                    video->needUpdate = false;
                    g_mutex_unlock(&video->mutex);
                }

                // Draw the current frame
                DrawTextureEx(video->texture, (Vector2){video->x, video->y}, 0, video->scale, WHITE);

                // Draw the seek bar
                double p = libvlc_media_player_get_position(video->player);
                DrawRectangle(video->x + 10, video->y + video->h - 20, video->w-20, 10, LIGHTGRAY);
                DrawRectangle(video->x + 12, video->y + video->h - 18, (int)((video->w-24)*p), 6, BLUE);
                
                element = element->next;
            }
            
            // Draw info
            DrawRectangle(0,600-40,800,40, LIGHTGRAY);
            DrawText("SPACE : PLAY/PAUSE   R : RESTART", 200, 600-30, 20, BLACK);
            DrawFPS(30,600-30);

        EndDrawing();
    }

    // Clean it up!
    GList* element = g_list_first(video_list);
    while(element != NULL)
    {
        Video *video = element->data;
        element = element->next;

        libvlc_media_player_stop(video->player);
        libvlc_media_player_release(video->player);

        UnloadTexture(video->texture);
        g_mutex_clear(&video->mutex);
        MemFree(video->buffer);
    }

    g_list_free(video_list);
    libvlc_release(libvlc);
    
    return 0;
}
