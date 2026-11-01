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
/* 1 in the headless build (tests/platform_null.c), which the checks run: it reads no settings file unless told to (main.c). */
int platform_headless(void);
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

/* The keyboard as the original's menu sees it: the value of port 0x60, which is the LAST scancode the
 * keyboard sent and keeps that value until the next one (a make code when a key goes down, make | 0x80
 * when it comes up). Auto-repeat is not reported (a held key sends nothing more). Only the keys the
 * game uses are known: Up 0x48, Down 0x50, Left 0x4b, Right 0x4d, Enter 0x1c, Space 0x39, Esc 0x01.
 * The value starts as the break code of Enter (0x9c), as if the game had been started from the DOS
 * prompt with Enter, which is how the keyboard port is left. */
unsigned platform_port60(void);

/* The mouse in screen pixels (0..319, 0..199, the window scaled back), and the buttons held (bit 0
 * left, bit 1 right, bit 2 middle). Returns 0 and leaves the arguments alone when there is no
 * pointer to report (headless). */
int platform_mouse(int *x, int *y, unsigned *buttons);

/* Show or hide the system's mouse pointer over the window. The menu draws its own arrow. */
void platform_show_pointer(int on);

/* Whether Esc closes the program (the default) or only reaches platform_port60 as scan code 0x01 (the battle uses it to go back
 * from choosing a target). The window's close button always closes. */
void platform_escape_quits(int on);

/* True once for each time Space was pressed since the last call. The slideshow's loops react to
 * the Space key only (scan code 0x39), not to Enter or the mouse. */
int platform_space_hit(void);

#endif
