/*
 * Platform layer: window, presenting the screen, input, time. This is the only
 * part of the program that depends on SDL.
 */
#ifndef TK_PLATFORM_H
#define TK_PLATFORM_H

#include "audio.h"
#include "screen.h"

/* Whether platform_init opens a sound device (default yes). Call before platform_init. */
void platform_audio_enable(int on);

/* The sound output, or NULL if there is none (disabled, no device, or a headless build). Valid
 * between platform_init and platform_shutdown; install it with audio_set_sink. */
const audio_sink *platform_audio(void);

int platform_init(const char *title, int scale);
void platform_shutdown(void);

/* Show the screen (palette applied). */
void platform_present(const screen_t *s);

/* Process window and input events. Returns 0 when the user asked to quit. */
int platform_poll(void);

/* Milliseconds since start. */
uint32_t platform_ticks_ms(void);
void platform_sleep_ms(uint32_t ms);

/* Non-zero while Space, Enter or a mouse button is held. The original reads the last
 * keyboard scan code from the hardware port and the mouse driver's button state. */
int platform_pressed(void);

/* True once for each time Space was pressed since the last call. The slideshow's loops react to
 * the Space key only (scan code 0x39), not to Enter or the mouse. */
int platform_space_hit(void);

#endif
