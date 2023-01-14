/*************************************************************************\
To compile with gcc:  (tested on Ubuntu 14.04 64bit):
g++ cello_mech.cpp kiss_fft.c -I. -lSDL2 -lGL -o run_cello_mech


ej NOTE: In WSL2 if you run this and exit with ESC instead of clicking
         the window close box, it will NEVER run again, instead quitting
         after a single frame.
         To reset this, either restart, or else run sdl_repair_keys.

\*************************************************************************/

#include <stdio.h>
#include <math.h>
#include <stdint.h>
#include <assert.h>
#include <SDL2/SDL.h>
#include <SDL2/SDL_opengl.h>
#include <GL/gl.h>
#include <vector>
#include "kiss_fft.h"

// typedef int32_t i32;
// typedef uint32_t u32;
// typedef int32_t b32;

#define WinWidth 500
#define WinHeight 500

const uint64_t request_num_samples = 4 * 4096;
uint64_t actual_num_samples = 0;
SDL_AudioSpec desired_spec;
SDL_AudioSpec actual_spec;
SDL_AudioDeviceID mic_id = 0;
std::vector<int16_t> fake_wave;
//std::vector<float>   freq_spike_amp;
//std::vector<size_t>  freq_spike_index;
std::vector<int16_t> mic_buffer;
kiss_fft_cfg fft_cfg = NULL;
std::vector<kiss_fft_cpx> fft_in_buffer;
std::vector<kiss_fft_cpx> fft_out_buffer;
//kiss_fft_cpx fft_in_buffer[request_num_samples];
//kiss_fft_cpx fft_out_buffer[request_num_samples];
float current_amp = 0;
float current_freq = 0;

double freq_a_above_middle_c = 440;
double half_step_up = pow(2, 1.0/12.0);
double half_step_down = 1.0 / half_step_up;
float freq_a_string = freq_a_above_middle_c * pow(half_step_down, 12);
float freq_d_string = freq_a_above_middle_c * pow(half_step_down, 12+7);
float freq_g_string = freq_a_above_middle_c * pow(half_step_down, 12+14);
float freq_c_string = freq_a_above_middle_c * pow(half_step_down, 12+21);

uint64_t freq_to_index(float freq)
{
    return 0.5f + (float)freq * ((float)actual_num_samples / (float)actual_spec.freq);
}

float index_to_freq(uint64_t index)
{
    return (float)index * ((float)actual_spec.freq / (float)actual_num_samples);
}

bool start_mic()
{
    desired_spec.freq = 22050;
    desired_spec.format = AUDIO_S16SYS;
    desired_spec.channels = 1;
    desired_spec.samples = 2 * request_num_samples;
    desired_spec.padding = 0;
    desired_spec.callback = NULL;
    desired_spec.userdata = NULL;
    SDL_InitSubSystem(SDL_INIT_AUDIO);
    mic_id = SDL_OpenAudioDevice(NULL, 1, &desired_spec, &actual_spec, SDL_AUDIO_ALLOW_ANY_CHANGE);

    printf("Mic device id %d\n", (int)mic_id);
    printf("         freq %d\n", (int)actual_spec.freq);
    printf("       format 0x%x\n", (int)actual_spec.format);
    printf("     channels %d\n", (int)actual_spec.channels);
    printf("      samples %d\n", (int)actual_spec.samples);

    actual_num_samples = actual_spec.samples;
    mic_buffer.resize(actual_num_samples);
    fft_in_buffer.resize(actual_num_samples);
    fft_out_buffer.resize(actual_num_samples);
    SDL_PauseAudioDevice(mic_id, 0);

    for (uint64_t i = 0; i < 100; ++i)
        printf("    [%d] -> %f Hz\n", (int)i, index_to_freq(i));
    return mic_id > 0;
}

bool sample_mic()
{
    uint32_t bytes_received = SDL_DequeueAudio(mic_id, &mic_buffer[0], mic_buffer.size() * sizeof(mic_buffer[0]));
    if (1 && bytes_received)
        printf("  mic got %d samples\n", (int)bytes_received >> 1);
    bool do_fake_data = true;
    if (do_fake_data)
    {
        float freq = 120.0f;
        float amp = 4000.0f;
        int num_harmonics = 5;
        if (fake_wave.size() == 0)
        {
            fake_wave.resize(actual_spec.samples);
            double theta = 0;
            double theta_per_sample = 2 * M_PI * freq / (double)actual_spec.freq;
            for (size_t i = 0; i < fake_wave.size(); ++i)
            {
                float sample = 0;
                for (int h = 1; h <= num_harmonics; ++h)
                    sample += (1.0f / h) * amp * sin(h * theta);
                fake_wave[i] = sample;
                theta += theta_per_sample;
            }
            float num_waves = theta / (2 * M_PI);
            float num_seconds = fake_wave.size() / (double)actual_spec.freq;
            printf("There were %f waves in %f seconds = %f Hz\n", num_waves, num_seconds, num_waves / num_seconds);
        }
        // override tone
        for (size_t i = 0; i < actual_num_samples; ++i)
            mic_buffer[i] = fake_wave[i];
    }
    return bytes_received > 0;
}

void init_fft()
{
    fft_cfg = kiss_fft_alloc(request_num_samples, 0, NULL, NULL); 
}

void do_fft()
{
    float scale = 1.0f / 32768.0f;
    for (uint32_t i = 0; i < actual_num_samples; ++i)
    {
        fft_in_buffer[i].r = scale * mic_buffer[i] * sin(2*M_PI*(float)i/(float)actual_num_samples);
        fft_in_buffer[i].i = 0;
    }
    kiss_fft(fft_cfg, &fft_in_buffer[0], &fft_out_buffer[0]);
}

void find_spikes()
{
    float threshold = 0.0f;
    bool in_spike = false;

    uint32_t lowest_index = 5;
    uint32_t highest_index = 100000;
    int num_harmonics = 6;
    float max_amp = 0;
    uint32_t max_index = 0;
    float max_single_amp = 0;
    for (uint32_t i = lowest_index; i < (actual_num_samples / 2) && (i < highest_index); ++i)
    {
        float prod = 1.0;
        kiss_fft_cpx& c = fft_out_buffer[i];
        float amp = c.r * c.r + c.i * c.i;
        if (amp > max_single_amp)
            max_single_amp = amp;
        for (int h = 2; h <= num_harmonics && (i * h) < actual_num_samples; ++h)
        {
            kiss_fft_cpx& cc = fft_out_buffer[i * h];
            float aa = cc.r * cc.r + cc.i * cc.i;
            amp += aa;
            if (aa > max_single_amp)
                max_single_amp = aa;
        }
        if (amp > max_amp)
        {
            max_amp = amp;
            max_index = i;
        }
    }

    if (max_single_amp > threshold)
    {
        float amp = max_amp;
        size_t index = max_index;

        amp = amp * (1.0f);
        float freq = (float)actual_spec.freq * (float)index / (float)actual_num_samples;
        printf("  amp(%d): %g max_single: %f freq: %f  strings: %.1f %.1f %.1f %.1fHz\n", (int)max_index, amp, max_single_amp, freq, freq_c_string, freq_g_string, freq_d_string, freq_a_string);
    }
}

void do_mic_updates()
{
    if (sample_mic())
    {
        do_fft();
        find_spikes();
    }
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
    int draw_every_n_samples = 16;
    int samples_to_draw = actual_num_samples / draw_every_n_samples;
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

void draw_fft()
{
    int max_sample_to_draw = actual_num_samples / 16;
    int draw_every_n_samples = 16;
    int samples_to_draw = max_sample_to_draw / draw_every_n_samples;
    float x_scale = 1.0f / 200000.0f;
    float y_scale = 1.0f / samples_to_draw;
    glColor4f(0.0f, 0.5f, 0.0f, 1.0f);
    glBegin(GL_LINE_STRIP);
    for (int32_t i = 0; i < samples_to_draw; ++i)
    {
        float max_amp = 0;
        for (int j = 0; j < draw_every_n_samples; ++j)
        {
            kiss_fft_cpx& c = fft_out_buffer[i * draw_every_n_samples + j];
            float amp = c.r * c.r + c.i * c.i;
            if (max_amp < amp)
                max_amp = amp;
        }
        float x = x_scale * max_amp - 0.1f;
        float y = y_scale * (i - (samples_to_draw >> 1));
        glVertex3f(x, y, 0.0f);
    }
    glEnd();
}

void draw_all()
{
    draw_strings();
    draw_pitch();
    draw_fft();
    draw_wave();
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
    do_mic_updates();
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
    draw_all();
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
