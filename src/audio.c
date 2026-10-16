#include "audio.h"

static const audio_sink *g_sink;

void audio_set_sink(const audio_sink *sink) { g_sink = sink; }
int audio_enabled(void) { return g_sink != NULL; }

void audio_play(const int16_t *pcm, size_t frames, unsigned rate)
{
    if (g_sink)
        g_sink->play(pcm, frames, rate);
}

int audio_playing(void) { return g_sink && g_sink->playing(); }

void audio_stop(void)
{
    if (g_sink)
        g_sink->stop();
}

int audio_music_available(void) { return g_sink && g_sink->music_start && g_sink->music_stop; }

void audio_music_start(audio_render_fn render, void *user, unsigned rate)
{
    if (audio_music_available())
        g_sink->music_start(render, user, rate);
}

void audio_music_stop(void)
{
    if (audio_music_available())
        g_sink->music_stop();
}
