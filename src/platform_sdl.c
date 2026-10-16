#include "platform.h"

#include <SDL.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static SDL_Window *g_win;
static SDL_Renderer *g_ren;
static SDL_Texture *g_tex;
static int g_space_hits;

static int g_audio_wanted = 1;
static SDL_AudioDeviceID g_adev;
static SDL_AudioSpec g_aspec;

void platform_audio_enable(int on) { g_audio_wanted = on; }

/* Clips come at 13,158 Hz or 6,060 Hz, mono. SDL's audio stream converts them to the device's
 * format and rate; the converted data is queued, and the queue running empty is what "the clip
 * has finished" means (the device holds at most one more buffer, about 23 ms). */
static void sdl_play(const int16_t *pcm, size_t frames, unsigned rate)
{
    if (!g_adev)
        return;
    SDL_ClearQueuedAudio(g_adev);
    SDL_AudioStream *st = SDL_NewAudioStream(AUDIO_S16SYS, 1, (int)rate, g_aspec.format, g_aspec.channels, g_aspec.freq);
    if (!st) {
        fprintf(stderr, "SDL_NewAudioStream: %s\n", SDL_GetError());
        return;
    }
    if (SDL_AudioStreamPut(st, pcm, (int)(frames * sizeof *pcm)) == 0 && SDL_AudioStreamFlush(st) == 0) {
        uint8_t buf[16384];
        int got;
        while ((got = SDL_AudioStreamGet(st, buf, sizeof buf)) > 0)
            SDL_QueueAudio(g_adev, buf, (Uint32)got);
    }
    SDL_FreeAudioStream(st);
}

static int sdl_playing(void) { return g_adev && SDL_GetQueuedAudioSize(g_adev) > 0; }

static void sdl_stop(void)
{
    if (g_adev)
        SDL_ClearQueuedAudio(g_adev);
}

/* Music has a device of its own, driven by a callback on SDL's audio thread, so it neither shares
 * nor disturbs the clip queue above. The device is opened at the music's own rate; SDL converts
 * to what the hardware wants. The render function and its data are set before the device runs and
 * are not changed while it does (stop closes the device first, which waits for a running
 * callback). */
static SDL_AudioDeviceID g_mdev;
static audio_render_fn g_mrender;
static void *g_muser;
static unsigned g_mrate = 49716;

static void SDLCALL music_callback(void *unused, Uint8 *stream, int len)
{
    (void)unused;
    if (g_mrender)
        g_mrender(g_muser, (int16_t *)stream, (size_t)len / sizeof(int16_t));
    else
        memset(stream, 0, (size_t)len);
    /* TK_AUDIO_LOG=1: report on stderr every 5 s of music handed to SDL, to check that the device
     * really pulls samples (e.g. with SDL_AUDIODRIVER=dummy, which plays nothing). */
    static unsigned long long total;
    static int logging = -1;
    if (logging < 0)
        logging = getenv("TK_AUDIO_LOG") != NULL;
    if (logging) {
        unsigned long long before = total / (g_mrate * 5ull);
        total += (unsigned long long)len / sizeof(int16_t);
        if (total / (g_mrate * 5ull) != before)
            fprintf(stderr, "music: %llu samples rendered (%.1f s)\n", total, (double)total / g_mrate);
    }
}

static void sdl_music_stop(void)
{
    if (g_mdev) {
        SDL_CloseAudioDevice(g_mdev);
        g_mdev = 0;
    }
    g_mrender = NULL;
    g_muser = NULL;
}

static void sdl_music_start(audio_render_fn render, void *user, unsigned rate)
{
    sdl_music_stop();
    if (!g_adev || !render)
        return;
    SDL_AudioSpec want, have;
    SDL_zero(want);
    want.freq = (int)rate;
    want.format = AUDIO_S16SYS;
    want.channels = 1;
    want.samples = 2048;
    want.callback = music_callback;
    g_mrender = render;
    g_muser = user;
    g_mrate = rate ? rate : 49716;
    g_mdev = SDL_OpenAudioDevice(NULL, 0, &want, &have, 0);
    if (!g_mdev) {
        fprintf(stderr, "no music output (%s): the intro plays without music\n", SDL_GetError());
        g_mrender = NULL;
        g_muser = NULL;
        return;
    }
    SDL_PauseAudioDevice(g_mdev, 0);
}

static const audio_sink g_sink = {sdl_play, sdl_playing, sdl_stop, sdl_music_start, sdl_music_stop};

const audio_sink *platform_audio(void) { return g_adev ? &g_sink : NULL; }

int platform_init(const char *title, int scale)
{
    if (SDL_Init(SDL_INIT_VIDEO) != 0) {
        fprintf(stderr, "SDL_Init: %s\n", SDL_GetError());
        return -1;
    }
    /* The window is scaled uniformly. A DOSBox-X capture of the original showed a window
     * of aspect about 1.6 for 320x200 pixels (docs/findings.md); whether to correct for
     * that is a later decision. */
    g_win = SDL_CreateWindow(title, SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
                             SCREEN_W * scale, SCREEN_H * scale, SDL_WINDOW_RESIZABLE);
    if (!g_win) {
        fprintf(stderr, "SDL_CreateWindow: %s\n", SDL_GetError());
        return -1;
    }
    g_ren = SDL_CreateRenderer(g_win, -1, SDL_RENDERER_PRESENTVSYNC);
    if (!g_ren)
        g_ren = SDL_CreateRenderer(g_win, -1, 0);
    if (!g_ren) {
        fprintf(stderr, "SDL_CreateRenderer: %s\n", SDL_GetError());
        return -1;
    }
    if (g_audio_wanted) {
        /* Sound is optional: without a device the scenes use the original's no-sound timing. */
        SDL_AudioSpec want;
        SDL_zero(want);
        want.freq = 44100;
        want.format = AUDIO_S16SYS;
        want.channels = 1;
        want.samples = 1024;
        if (SDL_InitSubSystem(SDL_INIT_AUDIO) == 0 &&
            (g_adev = SDL_OpenAudioDevice(NULL, 0, &want, &g_aspec, 0)) != 0)
            SDL_PauseAudioDevice(g_adev, 0);
        else
            fprintf(stderr, "no sound device (%s): the intro runs silent\n", SDL_GetError());
    }
    SDL_RenderSetLogicalSize(g_ren, SCREEN_W, SCREEN_H);
    SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "nearest");
    g_tex = SDL_CreateTexture(g_ren, SDL_PIXELFORMAT_RGB24, SDL_TEXTUREACCESS_STREAMING,
                              SCREEN_W, SCREEN_H);
    if (!g_tex) {
        fprintf(stderr, "SDL_CreateTexture: %s\n", SDL_GetError());
        return -1;
    }
    return 0;
}

void platform_shutdown(void)
{
    sdl_music_stop();
    if (g_adev) {
        SDL_CloseAudioDevice(g_adev);
        g_adev = 0;
    }
    if (g_tex)
        SDL_DestroyTexture(g_tex);
    if (g_ren)
        SDL_DestroyRenderer(g_ren);
    if (g_win)
        SDL_DestroyWindow(g_win);
    SDL_Quit();
    g_tex = NULL;
    g_ren = NULL;
    g_win = NULL;
}

void platform_present(const screen_t *s)
{
    static uint8_t rgb[SCREEN_PIXELS * 3];
    screen_to_rgb888(s, rgb);
    SDL_UpdateTexture(g_tex, NULL, rgb, SCREEN_W * 3);
    SDL_RenderClear(g_ren);
    SDL_RenderCopy(g_ren, g_tex, NULL, NULL);
    SDL_RenderPresent(g_ren);
}

int platform_poll(void)
{
    SDL_Event e;
    while (SDL_PollEvent(&e)) {
        if (e.type == SDL_QUIT)
            return 0;
        if (e.type == SDL_KEYDOWN && e.key.keysym.sym == SDLK_ESCAPE)
            return 0;
        if (e.type == SDL_KEYDOWN && !e.key.repeat && e.key.keysym.sym == SDLK_SPACE)
            g_space_hits++;
    }
    return 1;
}

int platform_space_hit(void)
{
    int hit = g_space_hits > 0;
    g_space_hits = 0;
    return hit;
}

uint32_t platform_ticks_ms(void)
{
    return SDL_GetTicks();
}

int platform_pressed(void)
{
    const Uint8 *k = SDL_GetKeyboardState(NULL);
    if (k[SDL_SCANCODE_SPACE] || k[SDL_SCANCODE_RETURN] || k[SDL_SCANCODE_KP_ENTER])
        return 1;
    return (SDL_GetMouseState(NULL, NULL) & (SDL_BUTTON_LMASK | SDL_BUTTON_RMASK | SDL_BUTTON_MMASK)) != 0;
}

void platform_sleep_ms(uint32_t ms)
{
    SDL_Delay(ms);
}
