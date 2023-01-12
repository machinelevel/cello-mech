/*************************************************************************\
To compile with gcc:  (tested on Ubuntu 14.04 64bit):
g++ cello_mech.cpp kiss_fft.c -I. -lSDL2 -lGL -o run_cello_mech


ej NOTE: In WSL2 if you run this and exit with ESC instead of clicking
         the window close box, it will NEVER run again, instead quitting
         after a single frame.
         To reset this, either restart, or else run sdl_repair_keys.

\*************************************************************************/

#include <stdio.h>
#include <stdint.h>
#include <assert.h>
#include <SDL2/SDL.h>
#include <SDL2/SDL_opengl.h>
#include <GL/gl.h>
#include "kiss_fft.h"

// typedef int32_t i32;
// typedef uint32_t u32;
// typedef int32_t b32;

#define WinWidth 1000
#define WinHeight 1000

SDL_AudioDeviceID mic_id = 0;
int16_t mic_buffer[1024 * 1024];
uint32_t mic_samples = 0;
kiss_fft_cfg fft_cfg = NULL;
kiss_fft_cpx fft_in_buffer[8192];
kiss_fft_cpx fft_out_buffer[8192];
float current_freq = 0;

bool start_mic()
{
    SDL_AudioSpec desired_spec;
    SDL_AudioSpec actual_spec;
    desired_spec.freq = 44100;
    desired_spec.format = AUDIO_S16SYS;
    desired_spec.channels = 1;
    desired_spec.samples = 8192;
    desired_spec.padding = 0;
    desired_spec.callback = NULL;
    desired_spec.userdata = NULL;
    SDL_InitSubSystem(SDL_INIT_AUDIO);
    mic_id = SDL_OpenAudioDevice(NULL, 1, &desired_spec, &actual_spec, SDL_AUDIO_ALLOW_ANY_CHANGE);

    printf("Mic device id %d\n", (int)mic_id);
    printf("         freq %d\n", (int)actual_spec.freq);
    printf("       format 0x%x\n", (int)actual_spec.format);
    printf("      samples %d\n", (int)actual_spec.samples);

    SDL_PauseAudioDevice(mic_id, 0);
    return mic_id > 0;
}

void sample_mic()
{
    uint32_t bytes_received = SDL_DequeueAudio(mic_id, mic_buffer, sizeof(mic_buffer));
    if (0 && bytes_received)
        printf("  mic got %d bytes\n", (int)bytes_received);
    mic_samples = bytes_received >> 1;
}

void init_fft()
{
    fft_cfg = kiss_fft_alloc(4096, 0, NULL, NULL); 
}

void do_fft()
{
    if (mic_samples == 0)
        return;
    for (uint32_t i = 0; i < mic_samples; ++i)
    {
        fft_in_buffer[i].r = mic_buffer[i];
        fft_in_buffer[i].i = 0;
    }
    kiss_fft(fft_cfg, fft_in_buffer, fft_out_buffer);
    float max = 0;
    uint32_t max_index = 0;
    for (uint32_t i = 0; i < mic_samples; ++i)
    {
        kiss_fft_cpx& c = fft_out_buffer[i];
        float val = c.r * c.r + c.i * c.i;
        if (max < val)
        {
            max = val;
            max_index = i;
        }
    }
    current_freq = 0.5 * current_freq + (20.0f * (float)max_index / (float)mic_samples);
}

void draw_strings()
{
    glColor4f(0.3f, 0.3f, 0.3f, 1.0f);
    glBegin(GL_LINES);
    glVertex3f(-0.2f, -1.0f, 0.0f);
    glVertex3f(-0.2f,  1.0f, 0.0f);
    glVertex3f(-0.3f, -1.0f, 0.0f);
    glVertex3f(-0.3f,  1.0f, 0.0f);
    glVertex3f(-0.4f, -1.0f, 0.0f);
    glVertex3f(-0.4f,  1.0f, 0.0f);
    glVertex3f(-0.5f, -1.0f, 0.0f);
    glVertex3f(-0.5f,  1.0f, 0.0f);
    glEnd();
}

void draw_pitch()
{
    glColor4f(0.0f, 1.0f, 0.0f, 1.0f);
    glBegin(GL_LINES);
    glVertex3f(-0.2f, current_freq, 0.0f);
    glVertex3f(-0.5f, current_freq, 0.0f);
    glEnd();
}

void draw_wave()
{
    draw_strings();
    draw_pitch();
    int draw_every_n_samples = 16;
    int samples_to_draw = mic_samples / draw_every_n_samples;
    samples_to_draw = 256;
    float x_scale = 1.0f / 65536;
    float y_scale = 1.0f / samples_to_draw;
    glColor4f(0.0f, 0.5f, 0.0f, 1.0f);
    glBegin(GL_LINE_STRIP);
    for (int32_t i = 0; i < samples_to_draw; ++i)
    {
        float x = x_scale * mic_buffer[i * draw_every_n_samples] - 0.3f;
//        x = 0;
        float y = y_scale * (i - (samples_to_draw >> 1));
//        y = i;
        glVertex3f(x, y, 0.0f);
    }
    glEnd();
}

int main (int ArgCount, char **Args)
{

  uint32_t WindowFlags = SDL_WINDOW_OPENGL;
  SDL_Window *Window = SDL_CreateWindow("OpenGL Test", 0, 0, WinWidth, WinHeight, WindowFlags);
  assert(Window);
  SDL_GLContext Context = SDL_GL_CreateContext(Window);
  
  start_mic();
  init_fft();

  int32_t Running = 1;
  int32_t FullScreen = 0;
  while (Running)
  {
    sample_mic();
    do_fft();
    SDL_Event Event;
    while (SDL_PollEvent(&Event))
    {
      if (Event.type == SDL_KEYDOWN)
      {
        switch (Event.key.keysym.sym)
        {
          case SDLK_ESCAPE:
            Running = 0;
            break;
          case 'f':
            FullScreen = !FullScreen;
            if (FullScreen)
            {
              SDL_SetWindowFullscreen(Window, WindowFlags | SDL_WINDOW_FULLSCREEN_DESKTOP);
            }
            else
            {
              SDL_SetWindowFullscreen(Window, WindowFlags);
            }
            break;
          default:
            break;
        }
      }
      else if (Event.type == SDL_QUIT)
      {
        Running = 0;
      }
    }

    glViewport(0, 0, WinWidth, WinHeight);
    glClearColor(0.1f, 0.0f, 0.1f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);
    glDisable(GL_DEPTH_TEST);
    if (0)
    {
        glColor4f(0.1f, 0.1f, 0.0f, 1.0f);
        glBegin(GL_TRIANGLES);
        glVertex3f(-1.0f, -1.0f, 0.0f);
        glVertex3f(-1.0f,  1.0f, 0.0f);
        glVertex3f( 1.0f,  1.0f, 0.0f);
        glEnd();
    }
    draw_wave();
    glFlush();
    glFinish();

    SDL_GL_SwapWindow(Window);
  }
  glFlush();
  glFinish();
  glFlush();
  glFinish();
  SDL_DestroyWindow(Window);
  SDL_Quit();
  exit(0);
  return 0;
}
