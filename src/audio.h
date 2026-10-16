/*
 * Audio output as the scenes see it.
 *
 * The original starts a clip through the Creative Voice driver and then polls a status word
 * (STORY2.SL DS:0x19a) that is non-zero while the clip sounds. The scenes do the same through
 * this small interface: start a clip, ask whether it is still playing, stop it. The platform layer
 * provides the sink (platform_sdl.c); with none installed (the stepped comparison runs, the
 * headless build, --no-audio, a machine without a sound device) the scenes behave as the original
 * does without a sound card.
 */
#ifndef TK_AUDIO_H
#define TK_AUDIO_H

#include <stddef.h>
#include <stdint.h>

typedef void (*audio_render_fn)(void *user, int16_t *out, size_t frames);

typedef struct {
    /* Start a mono 16-bit clip, replacing any clip that is playing. */
    void (*play)(const int16_t *pcm, size_t frames, unsigned rate);
    /* Non-zero while a clip started by play() has not finished. */
    int (*playing)(void);
    /* Cut the current clip short. */
    void (*stop)(void);
    /* Music, on its own output so it never touches the clip queue above (whose emptiness is what
     * the scenes' "the clip has finished" means). The sink calls render(user, out, frames) from
     * its own audio thread to get mono 16-bit samples at `rate`. Both may be NULL (no music). */
    void (*music_start)(audio_render_fn render, void *user, unsigned rate);
    void (*music_stop)(void);
} audio_sink;

void audio_set_sink(const audio_sink *sink);   /* NULL = no sound */
int audio_enabled(void);
void audio_play(const int16_t *pcm, size_t frames, unsigned rate);
int audio_playing(void);
void audio_stop(void);

/* Start music (replacing any that plays) / stop it. audio_stop() does not stop music. After
 * audio_music_stop returns, render is not called again. No-ops without a sink or music support. */
int audio_music_available(void);
void audio_music_start(audio_render_fn render, void *user, unsigned rate);
void audio_music_stop(void);

#endif
