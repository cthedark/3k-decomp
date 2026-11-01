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

static void test_input_load(void);

int platform_headless(void)
{
    return 0;
}

int platform_init(const char *title, int scale)
{
    if (SDL_Init(SDL_INIT_VIDEO) != 0) {
        fprintf(stderr, "SDL_Init: %s\n", SDL_GetError());
        return -1;
    }
    test_input_load();   /* TK_TEST_INPUT times count from here */
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

/* The keyboard port, as the original's menu reads it (platform.h). Only the keys the game uses are
 * translated; any other key leaves the value alone. */
static unsigned g_port60 = 0x9c;

static unsigned scancode_set1(SDL_Scancode sc)
{
    switch (sc) {
    case SDL_SCANCODE_UP: case SDL_SCANCODE_KP_8: return 0x48;
    case SDL_SCANCODE_DOWN: case SDL_SCANCODE_KP_2: return 0x50;
    case SDL_SCANCODE_LEFT: case SDL_SCANCODE_KP_4: return 0x4b;
    case SDL_SCANCODE_RIGHT: case SDL_SCANCODE_KP_6: return 0x4d;
    case SDL_SCANCODE_RETURN: case SDL_SCANCODE_KP_ENTER: return 0x1c;
    case SDL_SCANCODE_SPACE: return 0x39;
    case SDL_SCANCODE_ESCAPE: return 0x01;
    case SDL_SCANCODE_A: return 0x1e;
    /* the US top row Q..P stands in for F1..F10, for keyboards without F-keys; nothing reads these letters' own codes (0x10 .. 0x19) */
    case SDL_SCANCODE_Q: return 0x3b;
    case SDL_SCANCODE_W: return 0x3c;
    case SDL_SCANCODE_E: return 0x3d;
    case SDL_SCANCODE_R: return 0x3e;
    case SDL_SCANCODE_T: return 0x3f;
    case SDL_SCANCODE_Y: return 0x40;
    case SDL_SCANCODE_U: return 0x41;
    case SDL_SCANCODE_I: return 0x42;
    case SDL_SCANCODE_O: return 0x43;
    case SDL_SCANCODE_P: return 0x44;
    default:
        /* 1..9, 0 are set-1 codes 0x02..0x0b; F1..F10 are 0x3b..0x44 (WALK.SL's keys 1-4 and F-keys) */
        if (sc >= SDL_SCANCODE_1 && sc <= SDL_SCANCODE_0)
            return 0x02 + (unsigned)(sc - SDL_SCANCODE_1);
        if (sc >= SDL_SCANCODE_F1 && sc <= SDL_SCANCODE_F10)
            return 0x3b + (unsigned)(sc - SDL_SCANCODE_F1);
        return 0;
    }
}

/* TK_TEST_INPUT (testing only): a script of "MS:KIND:ARGS" items separated by commas, MS being the time
 * since the first call. KIND k = keyboard port: "500:k:0x50" sets port 0x60 to 0x50 at 500 ms; KIND m =
 * mouse: "700:m:150:80:1" puts the pointer at (150, 80) with the left button down. Replaces what the
 * keyboard and mouse deliver, so the real-time paths can run under SDL's dummy drivers. */
typedef struct { uint32_t ms; char kind; int a, b, c; } test_input;
static test_input g_ti[256];
static int g_ti_n = -1;
static uint32_t g_ti_t0;

static void test_input_load(void)
{
    g_ti_n = 0;
    g_ti_t0 = SDL_GetTicks();
    const char *v = getenv("TK_TEST_INPUT");
    while (v && *v && g_ti_n < 256) {
        test_input t = {0, 0, 0, 0, 0};
        unsigned ms;
        char kind;
        int used = 0;
        if (sscanf(v, "%u:%c%n", &ms, &kind, &used) < 2)
            break;
        v += used;
        t.ms = ms;
        t.kind = kind;
        if (kind == 'k')
            sscanf(v, ":%i%n", &t.a, &used);
        else
            sscanf(v, ":%i:%i:%i%n", &t.a, &t.b, &t.c, &used);
        v += used;
        g_ti[g_ti_n++] = t;
        if (*v == ',')
            v++;
    }
}

static int g_mouse_moved;   /* the pointer has moved over the window at least once */

unsigned platform_port60(void)
{
    if (g_ti_n < 0)
        test_input_load();
    if (g_ti_n > 0) {
        uint32_t now = SDL_GetTicks() - g_ti_t0;
        for (int i = 0; i < g_ti_n; i++)
            if (g_ti[i].kind == 'k' && g_ti[i].ms <= now)
                g_port60 = (unsigned)g_ti[i].a & 0xff;
    }
    return g_port60;
}

int platform_mouse(int *x, int *y, unsigned *buttons)
{
    if (!g_ren)
        return 0;
    if (g_ti_n < 0)
        test_input_load();
    if (g_ti_n > 0) {
        uint32_t now = SDL_GetTicks() - g_ti_t0;
        int found = 0;
        for (int i = 0; i < g_ti_n; i++)
            if (g_ti[i].kind == 'm' && g_ti[i].ms <= now) {
                *x = g_ti[i].a;
                *y = g_ti[i].b;
                *buttons = (unsigned)g_ti[i].c;
                found = 1;
            }
        if (found)
            return 1;
    }
    int wx, wy;
    Uint32 b = SDL_GetMouseState(&wx, &wy);
    float lx = 0, ly = 0;
    SDL_RenderWindowToLogical(g_ren, wx, wy, &lx, &ly);   /* undoes the window scale and letterboxing */
    *x = g_mouse_moved ? (int)lx : SCREEN_W / 2;          /* a driver puts the pointer mid-screen after a reset */
    *y = g_mouse_moved ? (int)ly : SCREEN_H / 2;
    *buttons = ((b & SDL_BUTTON_LMASK) ? 1u : 0u) | ((b & SDL_BUTTON_RMASK) ? 2u : 0u) |
               ((b & SDL_BUTTON_MMASK) ? 4u : 0u);
    return 1;
}

void platform_show_pointer(int on) { SDL_ShowCursor(on ? SDL_ENABLE : SDL_DISABLE); }

static int g_escape_quits = 1;
void platform_escape_quits(int on) { g_escape_quits = on; }

int platform_poll(void)
{
    /* Once the user has asked to quit it stays asked: scenes are chained, and every later loop
     * must end at once too (a one-shot answer would let the next scene start). */
    static int quit;
    /* TK_AUTO_QUIT_MS=N (testing only): post a genuine SDL_QUIT event N ms after the first poll,
     * the same event closing the window gives. */
    static int quit_ms = -1;
    static uint32_t quit_from;
    if (quit_ms < 0) {
        const char *v = getenv("TK_AUTO_QUIT_MS");
        quit_ms = v ? atoi(v) : 0;
        quit_from = SDL_GetTicks();
    } else if (quit_ms > 0 && (uint32_t)(SDL_GetTicks() - quit_from) >= (uint32_t)quit_ms) {
        SDL_Event q;
        SDL_zero(q);
        q.type = SDL_QUIT;
        SDL_PushEvent(&q);
        quit_ms = 0;   /* once */
    }
    SDL_Event e;
    while (SDL_PollEvent(&e)) {
        if (e.type == SDL_QUIT)
            quit = 1;
        if (e.type == SDL_MOUSEMOTION)
            g_mouse_moved = 1;
        if (e.type == SDL_KEYDOWN && e.key.keysym.sym == SDLK_ESCAPE && g_escape_quits)
            quit = 1;
        if (e.type == SDL_KEYDOWN && !e.key.repeat && e.key.keysym.sym == SDLK_SPACE)
            g_space_hits++;
        if ((e.type == SDL_KEYDOWN && !e.key.repeat) || e.type == SDL_KEYUP) {
            unsigned code = scancode_set1(e.key.keysym.scancode);
            if (code)
                g_port60 = e.type == SDL_KEYDOWN ? code : (code | 0x80);
        }
    }
    return !quit;
}

int platform_space_hit(void)
{
    /* TK_AUTO_SPACE_MS=N (testing only): behave as if Space were pressed every N ms, so a run
     * without a keyboard (SDL's dummy drivers) can go through the whole intro quickly. It does
     * not affect platform_pressed(), which the title uses, so the title still times out. */
    static int auto_ms = -1;
    static uint32_t last;
    if (auto_ms < 0) {
        const char *v = getenv("TK_AUTO_SPACE_MS");
        auto_ms = v ? atoi(v) : 0;
        last = SDL_GetTicks();
    }
    if (auto_ms > 0 && (uint32_t)(SDL_GetTicks() - last) >= (uint32_t)auto_ms) {
        last = SDL_GetTicks();
        g_space_hits++;
    }
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
    /* With TK_TEST_INPUT the scripted keyboard port decides: Space or Enter is held while the port
     * holds their make code (0x39, 0x1c), and the scripted mouse's buttons count. */
    if (g_ti_n < 0)
        test_input_load();
    if (g_ti_n > 0) {
        unsigned p = platform_port60();
        int x, y;
        unsigned b = 0;
        platform_mouse(&x, &y, &b);
        if (p == 0x39 || p == 0x1c || b)
            return 1;
        return 0;
    }
    const Uint8 *k = SDL_GetKeyboardState(NULL);
    if (k[SDL_SCANCODE_SPACE] || k[SDL_SCANCODE_RETURN] || k[SDL_SCANCODE_KP_ENTER])
        return 1;
    return (SDL_GetMouseState(NULL, NULL) & (SDL_BUTTON_LMASK | SDL_BUTTON_RMASK | SDL_BUTTON_MMASK)) != 0;
}

void platform_sleep_ms(uint32_t ms)
{
    SDL_Delay(ms);
}
