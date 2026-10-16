/*
 * Timing constants of the port.
 *
 * Policy (docs/port.md, "Timing policy"): the port never depends on the speed of the
 * machine it runs on. Waits that the original paces with the BIOS clock keep their exact
 * length and are expressed in BIOS ticks. Parts that the original runs as unpaced loops get
 * a fixed rate chosen here. Every value marked GUESS has no measurement behind it and is meant
 * to be retuned by looking at the result.
 */
#ifndef TK_TIMING_H
#define TK_TIMING_H

/* One BIOS clock tick is 65536 / 1193182 s = 54.925 ms. Milliseconds * 91 / 5000 converts
 * to ticks (18.2 per second), the same conversion the title uses. */
#define TK_TICK_MS_NUM 5000
#define TK_TICK_MS_DEN 91

/* Slide 0 of the intro (STORY2.SL 0x10590 loop): how many loop passes per second. The
 * original loop has no pacing at all (each pass copies 64,000 bytes to video memory), so the
 * speed of the star field's drift depended on the machine. GUESS. */
#define TK_INTRO0_PASS_HZ 35

/* Palette fades (STORY2.SL 0x121ce): 64 steps. The original waits for a vertical retrace
 * before every palette upload, so one step per frame of a 70 Hz display is the quickest it
 * could go; it also burns a busy loop per step. GUESS. */
#define TK_FADE_STEP_HZ 70

/* Slide 1 (STORY2.SL 0x10c80 loop): how many animation passes per second. Like slide 0's loop it
 * has no pacing in the original (a pass copies about 25 KB to the screen). Taken from slide 0,
 * whose rate was judged right by eye. GUESS for this slide. */
#define TK_INTRO1_PASS_HZ 35

/* Slide 2's animation loop (STORY2.SL 0x10f9d), 500 passes. The original pass copies about 20 KB
 * (a 75x78 and a 155x125 block) with no pacing. At the rate of slide 1 the hair counter's 91-pass
 * cycle takes 2.6 s and a figure frame 0.6 s. GUESS, taken from slide 1. */
#define TK_INTRO2_PASS_HZ 35

/* Slide 3's animation loop (STORY2.SL 0x1132e), 142 passes: each copies the whole picture twice
 * (64,000 bytes to the work buffer, 64,000 to the screen). Slide 0's loop did the same amount of
 * copying and ran well at 35 per second, but the operator found slide 3 too fast at that rate and
 * asked for half the speed (2026-10-15), hence 17 per second (58 ms per pass; 35/2 = 17.5 is not an
 * integer rate here). The loop is 142 passes, 8.4 s at this rate: the figure sinks one row per two
 * passes and the picture scrolls one row per twelve. Set by eye. */
#define TK_INTRO3_PASS_HZ 17

/* Slide 4's animation loop (STORY2.SL 0x1172c). It is long and its pace in the original is whatever
 * the machine gives: 11,401 passes, of which 5,005 are two pauses (2 x 1,001 and 3 x 1,001 passes)
 * during which the eyes blink on a 61-pass cycle. Each pass copies a 79x99 block, so the original
 * ran at some hundreds of passes per second on the hardware it was made for. The scene runs
 * several passes per displayed frame: this many passes per second, in steps of 60 (the frame
 * rate). At 300 the loop takes 38 s, the blink cycle 0.2 s and a pause 6.7 s or 10 s. GUESS. */
#define TK_INTRO4_PASS_HZ 300

/* Slide 5's animation loop (STORY2.SL 0x11d5b), 2,000 passes, each drawing one 97x84 block; the face
 * changes every 151 passes. Unpaced in the original. 120 passes per second (two per displayed
 * frame, see slide 4) gives 16.7 s and a new expression every 1.3 s. GUESS. */
#define TK_INTRO5_PASS_HZ 120

/* Slide 6's animation loop (STORY2.SL 0x11f93), 2,000 passes, each drawing one 189x160 block; the hair
 * switches frames every 25 passes. Unpaced in the original. Same rate as slide 5, which the
 * operator found right: 16.7 s, the hair changing every 0.2 s. GUESS. */
#define TK_INTRO6_PASS_HZ 120

/* Slide 1's subtitle cross-fade and fade-out passes (STORY2.SL 0x1250e, 0x12870). Each original
 * pass redraws a 320x14 strip 20 or 35 times in a row; the port draws it once per pass. The
 * rate is taken from slide 0's subtitle passes. GUESS. */
#define TK_SUBTITLE_PASS_HZ 35

/* The menu's loop (WALK0.SL 0x10b91): how many passes per second. Each original pass copies about 46 KB
 * inside the machine and 32 KB to the video memory and has no pacing, so the backdrop's drift and
 * the response to the keys depended on the machine (about 2.3 passes per BIOS tick in the oracle at an
 * arbitrary 500,000 instructions per tick, which says nothing about real hardware). The backdrop moves one
 * pixel per pass (one row, later): 35 per second takes 5.5 s for a full turn of the picture. Taken from
 * the intro's rate, which was judged right by eye. GUESS. */
#define TK_MENU_PASS_HZ 35

/* The menu's opening (WALK0.SL 0x10357 .. 0x10537): a zoom of 20 steps, each stretching a rectangle of the
 * picture to the whole screen (about 128 KB of memory traffic per step), then the lettering "Ready"
 * shrinking away in 160 steps, each of them a few KB. Neither is paced in the original and it checks no
 * retrace, so how long they took depended on the machine: in the oracle at 500,000 instructions per
 * tick the zoom took about 12 BIOS ticks and the shrink about 4, which says nothing about real hardware.
 * Chosen by eye: the zoom 0.7 s, the shrink 1.3 s. GUESS. */
#define TK_MENU_WIPE_HZ 30
#define TK_MENU_SHRINK_HZ 120

/* The transition after a choice (WALK0.SL 0x10e9d .. 0x10fe1): 33 steps, each about 160 KB of memory
 * traffic with no wait and no sound, so its length in the original was the speed of the machine. A
 * check mark is drawn in the chosen label by an arrow; 33 steps per second makes it take a second,
 * long enough to see it being drawn. GUESS. */
#define TK_MENU_CONFIRM_HZ 33

#endif
