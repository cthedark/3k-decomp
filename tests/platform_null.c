/*
 * Platform layer with no window and no SDL, for headless builds.
 *
 * `build/3k_headless` uses this instead of src/platform_sdl.c. The stepped modes
 * (--step-dump) never call the platform layer, so they run unchanged; the real-time
 * modes fail at platform_init, with a message. Its purpose is to let the program be built with
 * the address and undefined-behaviour sanitizers and run without SDL (SDL's initializer can
 * raise a modal dialog under sanitizers on macOS and block a script indefinitely).
 */
#include <stdio.h>

#include "platform.h"

void platform_audio_enable(int on) { (void)on; }
const audio_sink *platform_audio(void) { return NULL; }

int platform_init(const char *title, int scale)
{
    (void)title;
    (void)scale;
    fprintf(stderr, "this is the headless build: no window is available\n");
    return -1;
}

void platform_shutdown(void) {}
int platform_headless(void) { return 1; }
void platform_present(const screen_t *s) { (void)s; }
int platform_poll(void) { return 0; }
uint32_t platform_ticks_ms(void) { return 0; }
void platform_sleep_ms(uint32_t ms) { (void)ms; }
int platform_pressed(void) { return 0; }
int platform_space_hit(void) { return 0; }
unsigned platform_port60(void) { return 0x9c; }
void platform_show_pointer(int on) { (void)on; }
void platform_escape_quits(int on) { (void)on; }
int platform_mouse(int *x, int *y, unsigned *buttons) { (void)x; (void)y; (void)buttons; return 0; }
