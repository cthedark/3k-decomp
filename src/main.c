/*
 * 3K native port, vertical slice.
 *
 *   3k [--data DIR] [--scale N] [--scene title|gmain]
 *      [--ppm FILE]                       gmain only: write the screen as PPM, no window
 *      [--step-dump N:FILE ...]           title only, headless: run passes deterministically
 *      [--press-pass N ...]               and write screen+palette (raw) after pass N;
 *                                         press-pass N holds the key down from pass N+1
 *
 * Raw dump format = 64000 pixel bytes followed by the 768-byte 6-bit palette. It is
 * the format tools/dosemu.py writes, so tools/compare_frames.py can compare the two.
 *
 * Deterministic stepping uses the same model as the reference run: pass k (1-based) sees
 * the clock value (k-1) / 3, so the BIOS clock "ticks" once every 3 passes.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "assets.h"
#include "audio.h"
#include "intro0.h"
#include "intro1.h"
#include "intro2.h"
#include "intro3.h"
#include "intro4.h"
#include "intro5.h"
#include "intro6.h"
#include "tkrand.h"
#include "slide.h"
#include "music.h"
#include "menu.h"
#include "subs.h"
#include "lcz.h"
#include "platform.h"
#include "screen.h"
#include "timing.h"
#include "title.h"

#define MAX_DUMPS 512

/* --open-dump N:FILE (menu, headless): the screen after N steps of the opening animation. */
static int g_open_n[MAX_DUMPS];
static const char *g_open_file[MAX_DUMPS];
static int g_open_count;
/* --confirm-dump N:FILE (menu, headless): the screen after N steps of the transition that follows a choice. */
static int g_conf_n[MAX_DUMPS];
static const char *g_conf_file[MAX_DUMPS];
static int g_conf_count;
static int g_highlight;       /* --highlight K: the menu's selected item for stepped runs */
/* Scripted input for the stepped menu, applied before pass N+1 (the same convention as the oracle's
 * --key-pass / --mouse-pass: "after N completed passes"). --menu-key N:CODE sets the keyboard port to
 * CODE (and it stays until changed); --menu-mouse N:X:Y:BUTTONS puts a mouse there. */
enum { MAX_SCRIPT = 256 };
static struct { int pass; unsigned code; } g_menu_keys[MAX_SCRIPT];
static int g_menu_nkeys;
static struct { int pass; int x, y; unsigned buttons; } g_menu_mice[MAX_SCRIPT];
static int g_menu_nmice;
/* The slot picker in the stepped menu, after the transition of item 1: --picker-key N:CODE and
 * --picker-mouse N:X:Y:BUTTONS are applied before picker pass N + 1 (N counted from the picker's first pass),
 * --picker-dump N:FILE writes the screen after N picker passes, --picker-slots MASK says which saves
 * exist (default: look for SAVE00.000 .. SAVE10.000 in the data directory). */
static struct { int pass; unsigned code; } g_pick_keys[MAX_SCRIPT];
static int g_pick_nkeys;
static struct { int pass; int x, y; unsigned buttons; } g_pick_mice[MAX_SCRIPT];
static int g_pick_nmice;
static int g_pick_n[MAX_DUMPS];
static const char *g_pick_file[MAX_DUMPS];
static int g_pick_count;
static int g_pick_mask = -1;
static int g_no_audio;        /* --no-audio */
static int g_no_music;        /* --no-music */
static unsigned g_music_gain = 100;   /* --music-gain PERCENT */
static int g_music_replay = 1; /* slide 0's clip plays again after it ends, as in the original; --slide0-once turns it off */

/* The window, and the sound output if there is one. Scenes load their sound when they are created,
 * so this must come first. */
static int init_platform(int scale)
{
    platform_audio_enable(!g_no_audio);
    if (platform_init("3K (work in progress)", scale) != 0)
        return -1;
    audio_set_sink(platform_audio());
    return 0;
}

static void shutdown_platform(void)
{
    audio_stop();
    audio_set_sink(NULL);
    platform_shutdown();
}

static int load_palette(screen_t *s, const char *file, size_t offset)
{
    uint8_t pal[768];
    if (assets_read_at(file, offset, pal, sizeof pal) != 0) {
        fprintf(stderr, "cannot read palette from %s\n", file);
        return -1;
    }
    for (int i = 0; i < 768; i++)
        if (pal[i] > 63) {
            fprintf(stderr, "%s at %zu is not a 6-bit palette\n", file, offset);
            return -1;
        }
    screen_set_palette(s, pal, 0, 256);
    return 0;
}

/* Decode chunk `index` of an LCZ file into a full screen. */
static int load_screen(screen_t *s, const char *file, int index)
{
    size_t len = 0;
    uint8_t *data = assets_read(file, &len);
    if (!data) {
        fprintf(stderr, "cannot read %s (data dir: %s)\n", file, assets_dir());
        return -1;
    }
    size_t pos = 0;
    int rc = -1;
    for (int i = 0;; i++) {
        lcz_chunk c;
        if (lcz_next_chunk(data, len, &pos, &c) != LCZ_OK) {
            fprintf(stderr, "%s: chunk %d not found\n", file, index);
            break;
        }
        if (i == index) {
            if (c.len >= SCREEN_PIXELS) {
                memcpy(s->pix, c.pixels, SCREEN_PIXELS);
                rc = 0;
            } else {
                fprintf(stderr, "%s: chunk %d is %zu bytes, not a full screen\n", file, index, c.len);
            }
            lcz_chunk_free(&c);
            break;
        }
        lcz_chunk_free(&c);
    }
    free(data);
    return rc;
}

static int write_raw(const screen_t *s, const char *path)
{
    FILE *f = fopen(path, "wb");
    if (!f)
        return -1;
    size_t a = fwrite(s->pix, 1, SCREEN_PIXELS, f);
    size_t b = fwrite(s->pal, 1, 768, f); /* pal is [256][3] of 6-bit values: 768 contiguous bytes */
    return (fclose(f) == 0 && a == SCREEN_PIXELS && b == 768) ? 0 : -1;
}

/* BIOS clock (18.2 Hz) from milliseconds. */
static unsigned bios_ticks(uint32_t ms)
{
    return (unsigned)((uint64_t)ms * 91 / 5000);
}

static music_t *song_start(const char *who);
static void song_stop(music_t *m);

/* The title in real time, with its music, on an already initialised platform. Returns the
 * decision (TITLE_TO_INTRO after the timeout, TITLE_TO_MENU after a key or click), or
 * TITLE_RUNNING if the window was closed or the scene could not be created.
 *
 * The original's title loop is not paced by the clock after the fade-in (docs/code.md, "Title
 * screen"), so its speed depended on the CPU. Here it runs at a fixed 70 passes per second, the
 * rate of a 70 Hz VGA display, which is the one rate the fade-in (one palette step per vertical
 * retrace) is tied to. The song starts as soon as the title's data is loaded (MARK 0x105d0) and
 * stops when the loop ends (0x10b1c), so it plays through the fade-out. */
static title_result play_title(screen_t *scr)
{
    title_t *t = title_create(scr, 0);
    if (!t)
        return TITLE_RUNNING;
    music_t *song = song_start("title");
    const uint32_t start = platform_ticks_ms();
    uint32_t next = start;
    title_result r = TITLE_RUNNING;
    while (r == TITLE_RUNNING && platform_poll()) {
        uint32_t now = platform_ticks_ms();
        if ((int32_t)(now - next) < 0) {
            platform_sleep_ms(1);
            continue;
        }
        next += 1000 / 70 + ((next - start) % 7 < 3 ? 1 : 0); /* about 70 Hz without drift to speak of */
        r = title_pass(t, scr, bios_ticks(now - start), platform_pressed());
        platform_present(scr);
    }
    song_stop(song);
    title_free(t);
    return r;
}

/* The menu, stepped and headless: --step-dump N:FILE writes the screen (raw: 64,000 pixels then the
 * 768-byte palette) after N passes of the menu loop, N = 0 being before the first. The reference is
 * the oracle's screen at its N-th arrival at the end of the loop (tools/check_menu.sh). */
/* Bit i set if SAVEii.000 exists in the data directory: the original probes these files to draw the icons. */
static unsigned saves_present(void)
{
    unsigned mask = 0;
    for (int i = 0; i < MENU_SLOTS; i++) {
        char name[16];
        snprintf(name, sizeof name, "save%02d.000", i);
        if (assets_exists(name))
            mask |= 1u << i;
    }
    return mask;
}

static int run_menu_opening_stepped(screen_t *scr)
{
    menu_t *m = menu_create(scr);
    if (!m)
        return 1;
    for (int i = 0; i < g_open_count; i++) {
        menu_opening_frame(m, scr, g_open_n[i]);
        if (write_raw(scr, g_open_file[i]) != 0) {
            fprintf(stderr, "cannot write %s\n", g_open_file[i]);
            menu_free(m);
            return 1;
        }
    }
    printf("opening: %d steps in all; wrote %d frame(s)\n", menu_opening_steps(), g_open_count);
    menu_free(m);
    return 0;
}

static int run_menu_stepped(screen_t *scr, int ndump, const int *dump_pass, char **dump_file)
{
    menu_t *m = menu_create(scr);
    if (!m)
        return 1;
    menu_set_highlight(m, g_highlight);
    int last = 0;
    for (int i = 0; i < ndump; i++)
        if (dump_pass[i] > last)
            last = dump_pass[i];
    unsigned port60 = 0x80;   /* a break code: the latch is open unless the script says otherwise */
    if (g_menu_nkeys > 0)
        port60 = 0x00;        /* scripted runs start like the oracle: no scancode yet */
    menu_mouse mouse = {0, 0, 0};
    int have_mouse = 0;
    for (int done = 0; done <= last; done++) {
        for (int i = 0; i < ndump; i++)
            if (dump_pass[i] == done) {
                if (write_raw(scr, dump_file[i]) != 0) {
                    fprintf(stderr, "cannot write %s\n", dump_file[i]);
                    menu_free(m);
                    return 1;
                }
                printf("pass %d: wrote %s\n", done, dump_file[i]);
            }
        if (done < last) {
            for (int i = 0; i < g_menu_nkeys; i++)
                if (g_menu_keys[i].pass == done)
                    port60 = g_menu_keys[i].code;
            for (int i = 0; i < g_menu_nmice; i++)
                if (g_menu_mice[i].pass == done) {
                    mouse.x = g_menu_mice[i].x;
                    mouse.y = g_menu_mice[i].y;
                    mouse.buttons = g_menu_mice[i].buttons;
                    have_mouse = 1;
                }
            menu_result r = menu_pass_input(m, scr, port60, have_mouse ? &mouse : NULL);
            if (r != MENU_NONE) {
                printf("confirmed item %d after %d completed pass(es)\n", r, done);
                if (g_conf_count > 0 || (r == MENU_LOAD && g_pick_count > 0)) {
                    menu_confirm_begin(m);
                    for (int step = 1; step <= MENU_CONFIRM_STEPS; step++) {
                        menu_confirm_step(m, scr);
                        for (int i = 0; i < g_conf_count; i++)
                            if (g_conf_n[i] == step && write_raw(scr, g_conf_file[i]) != 0) {
                                fprintf(stderr, "cannot write %s\n", g_conf_file[i]);
                                menu_free(m);
                                return 1;
                            }
                    }
                    printf("transition: %d steps\n", MENU_CONFIRM_STEPS);
                }
                if (r == MENU_LOAD && g_pick_count > 0) {
                    unsigned mask = g_pick_mask >= 0 ? (unsigned)g_pick_mask : saves_present();
                    menu_picker_begin(m, mask);
                    int plast = 0;
                    for (int i = 0; i < g_pick_count; i++)
                        if (g_pick_n[i] > plast)
                            plast = g_pick_n[i];
                    for (int pdone = 0; pdone <= plast; pdone++) {
                        for (int i = 0; i < g_pick_count; i++)
                            if (g_pick_n[i] == pdone && write_raw(scr, g_pick_file[i]) != 0) {
                                fprintf(stderr, "cannot write %s\n", g_pick_file[i]);
                                menu_free(m);
                                return 1;
                            }
                        if (pdone == plast)
                            break;
                        for (int i = 0; i < g_pick_nkeys; i++)
                            if (g_pick_keys[i].pass == pdone)
                                port60 = g_pick_keys[i].code;
                        for (int i = 0; i < g_pick_nmice; i++)
                            if (g_pick_mice[i].pass == pdone) {
                                mouse.x = g_pick_mice[i].x;
                                mouse.y = g_pick_mice[i].y;
                                mouse.buttons = g_pick_mice[i].buttons;
                                have_mouse = 1;
                            }
                        int slot = menu_picker_pass(m, scr, port60, have_mouse ? &mouse : NULL);
                        if (slot != MENU_NONE) {
                            printf("picked slot %d after %d completed picker pass(es)\n", slot, pdone);
                            break;
                        }
                    }
                }
                break;
            }
        }
    }
    menu_free(m);
    return 0;
}

static int run_title_stepped(screen_t *scr, int ndump, const int *dump_pass, char **dump_file,
                             int npress, const int *press_pass)
{
    title_t *t = title_create(scr, 0);
    if (!t)
        return 1;
    int last = 0, press_from = -1;
    for (int i = 0; i < ndump; i++)
        if (dump_pass[i] > last)
            last = dump_pass[i];
    for (int i = 0; i < npress; i++)
        if (press_from < 0 || press_pass[i] < press_from)
            press_from = press_pass[i];

    for (int done = 0; done <= last; done++) {
        for (int i = 0; i < ndump; i++)
            if (dump_pass[i] == done) {
                if (write_raw(scr, dump_file[i]) != 0) {
                    fprintf(stderr, "cannot write %s\n", dump_file[i]);
                    title_free(t);
                    return 1;
                }
                printf("pass %d: wrote %s\n", done, dump_file[i]);
            }
        if (done == last)
            break;
        int pressed = press_from >= 0 && done >= press_from;
        title_result r = title_pass(t, scr, (unsigned)(done / 3), pressed);
        if (r != TITLE_RUNNING) {
            printf("title ended after pass %d: %s\n", done + 1, r == TITLE_TO_MENU ? "menu" : "intro");
            break;
        }
    }
    title_free(t);
    return 0;
}

/* Real-time loops. Fade steps and loop passes run at the fixed rates in timing.h; the holds that
 * the original paces with the BIOS clock keep their length.
 *
 * A step that runs late (the display's vertical sync can make a step take longer than its slot,
 * e.g. a 60 Hz display against 70 fade steps per second) must not be paid back later by running
 * the following steps early: that played the first passes of the next phase several times too
 * fast (seen on slide 3, 2026-10-15, with TK_PACE_LOG). So the schedule only carries up to one
 * step of lateness; beyond that the backlog is dropped. */
static uint32_t next_due(uint32_t due, uint32_t now, unsigned hz)
{
    uint32_t period = 1000u / hz;
    due += period;
    if ((int32_t)(now - due) > (int32_t)period)
        due = now;
    return due;
}

static void rt_intro0(intro0_t *t, screen_t *scr)
{
    const uint32_t start = platform_ticks_ms();
    uint32_t next = start;
    while (intro0_phase_now(t) != INTRO0_DONE && platform_poll()) {
        uint32_t now = platform_ticks_ms();
        if ((int32_t)(now - next) < 0) {
            platform_sleep_ms(1);
            continue;
        }
        intro0_phase ph = intro0_phase_now(t);
        next = next_due(next, now, ph == INTRO0_RUN ? TK_INTRO0_PASS_HZ : TK_FADE_STEP_HZ);
        intro0_step(t, scr, bios_ticks(now - start));
        platform_present(scr);
    }
}

static void rt_slide(const slide_ops *ops, void *t, screen_t *scr)
{
    const uint32_t start = platform_ticks_ms();
    uint32_t next = start;
    int space = 0;
    slide_probe p;
    ops->probe(t, &p);
    /* TK_PACE_LOG=FILE: one line per step with its time and how late it ran, to see whether the
     * real-time loop keeps its rate (a step that runs late is followed by early ones). */
    const char *pace_path = getenv("TK_PACE_LOG");
    FILE *pace = pace_path ? fopen(pace_path, "w") : NULL;
    while (!p.done && platform_poll()) {
        space |= platform_space_hit();
        uint32_t now = platform_ticks_ms();
        if ((int32_t)(now - next) < 0) {
            platform_sleep_ms(1);
            continue;
        }
        unsigned hz = TK_FADE_STEP_HZ;
        unsigned reps = 1;   /* passes run for each displayed frame */
        if (p.anim) {
            hz = p.anim_hz;
            if (hz > 60) {   /* a loop faster than the display: several passes per frame */
                reps = hz / 60;
                hz = 60;
            }
        } else if (p.subs)
            hz = subs_state_now(p.subs) == SUBS_WAIT ? 70 /* only the polling rate of the wait */ : TK_SUBTITLE_PASS_HZ;
        if (pace)
            fprintf(pace, "%u %s late=%d\n", (unsigned)(now - start), p.anim ? "anim" : p.subs ? "subs" : "fade",
                    (int)(int32_t)(now - next));
        next = next_due(next, now, hz);
        /* The original looks at the keyboard only inside the animation loops and the waits. */
        int use = p.looks ? space : 0;
        for (unsigned r = 0; r < reps; r++) {
            ops->step(t, scr, bios_ticks(now - start), r == 0 ? use : 0);
            ops->probe(t, &p);
            if (!p.anim)
                break;
        }
        if (use || !p.looks)
            space = 0;
        platform_present(scr);
    }
    if (pace)
        fclose(pace);
}

/* Slide 0 of the intro. With --step-dump the run is deterministic: the fade-in is done first
 * without counting, then pass k (1-based) of the main loop sees the clock value (k-1)/tick_div,
 * the same model as the reference run (tools/dosemu.py --loop-top 0x10590). A dump "after N
 * passes" is written when N loop passes have completed. */
static int run_intro0(screen_t *scr, int scale, int ndump, const int *dump_pass, char **dump_file, int tick_div)
{
    if (ndump == 0 && init_platform(scale) != 0)
        return 1;
    intro0_t *t = intro0_create(scr);
    if (!t) {
        if (ndump == 0)
            shutdown_platform();
        return 1;
    }
    intro0_set_music_replay(t, g_music_replay);

    if (ndump > 0) {
        int last = 0;
        for (int i = 0; i < ndump; i++)
            if (dump_pass[i] > last)
                last = dump_pass[i];
        while (intro0_phase_now(t) == INTRO0_FADE_IN)
            intro0_step(t, scr, 0);
        for (int done = 0; done <= last; done++) {
            for (int i = 0; i < ndump; i++)
                if (dump_pass[i] == done) {
                    if (write_raw(scr, dump_file[i]) != 0) {
                        fprintf(stderr, "cannot write %s\n", dump_file[i]);
                        intro0_free(t);
                        return 1;
                    }
                    printf("pass %d: wrote %s\n", done, dump_file[i]);
                }
            if (done == last)
                break;
            if (intro0_step(t, scr, (unsigned)(done / tick_div)) != INTRO0_RUN) {
                printf("slide 0 loop ended after pass %d\n", done + 1);
                break;
            }
        }
        intro0_free(t);
        return 0;
    }

    /* Real time. Fade steps and loop passes run at the fixed rates in timing.h; the 56-tick
     * hold of a subtitle line is paced by the real clock, as in the original. */
    rt_intro0(t, scr);
    printf("slide 0 finished: the next slide is not built yet\n");
    intro0_free(t);
    shutdown_platform();
    return 0;
}

/* The slides after slide 0 (intro1.c, intro2.c, ...) through the uniform interface of slide.h.
 * Stepped mode (any dump given): the fades and the passes run as fast as possible; dumps are
 * written "after N passes" of the loop they name, which is the state the reference run has at its
 * N-th arrival (0-based) at that loop's first instruction. For slide 1 (STORY2.SL):
 *   anim  0x10c80   passes of the animation loop
 *   sub   0x1250e   cross-fade passes, all lines of this slide counted together
 *   out   0x12870   passes of the last line's fade-out; N = 14 is the state at 0x12928
 * The reference run counts arrivals over the whole program, so for later slides its N for sub and
 * out starts at the number of passes the earlier slides made; the C side counts per slide. */
typedef enum { DUMP_ANIM, DUMP_SUB, DUMP_OUT, DUMP_END, DUMP_FINAL } dump_kind;

static const slide_ops *const slides[] = {&intro1_ops, &intro2_ops, &intro3_ops, &intro4_ops, &intro5_ops, &intro6_ops};
enum { NSLIDES = sizeof slides / sizeof slides[0] };

static int run_slide(const slide_ops *ops, screen_t *scr, int scale, int ndump, const dump_kind *kind,
                     const int *dump_pass, char **dump_file, int faithful)
{
    if (ndump == 0 && init_platform(scale) != 0)
        return 1;
    void *t = ops->create(scr, faithful);
    if (!t) {
        if (ndump == 0)
            shutdown_platform();
        return 1;
    }
    slide_probe p;
    ops->probe(t, &p);

    if (ndump > 0) {
        unsigned step = 0;
        int written = 0;
        int wrote[MAX_DUMPS] = {0};
        while (!p.done && written < ndump) {
            for (int i = 0; i < ndump; i++) {
                if (wrote[i])
                    continue;
                int hit = (kind[i] == DUMP_ANIM && p.anim && (int)p.anim_done == dump_pass[i]) ||
                          /* the first state after the animation loop, before any fade step */
                          (kind[i] == DUMP_END && !p.anim && !p.subs && p.anim_done > 0 &&
                           (int)p.anim_done == dump_pass[i]) ||
                          (kind[i] == DUMP_SUB && p.subs && subs_state_now(p.subs) == SUBS_PASS &&
                           (int)subs_pass_done(p.subs) == dump_pass[i]) ||
                          (kind[i] == DUMP_OUT && p.subs && subs_state_now(p.subs) == SUBS_OUT &&
                           (int)subs_out_done(p.subs) == dump_pass[i]);
                if (hit) {
                    if (write_raw(scr, dump_file[i]) != 0) {
                        fprintf(stderr, "cannot write %s\n", dump_file[i]);
                        ops->destroy(t);
                        return 1;
                    }
                    printf("%s %d: wrote %s\n",
                           kind[i] == DUMP_ANIM ? "anim" : kind[i] == DUMP_SUB ? "sub" : kind[i] == DUMP_END ? "end" : "out",
                           dump_pass[i], dump_file[i]);
                    wrote[i] = 1;
                    written++;
                }
            }
            /* A wait ends at once: the clock jumps 100 ticks per step. */
            ops->step(t, scr, 100 * ++step, 0);
            ops->probe(t, &p);
        }
        /* "final": the state once the scene has finished (after its last fade to black). */
        for (int i = 0; i < ndump; i++)
            if (!wrote[i] && kind[i] == DUMP_FINAL && p.done) {
                if (write_raw(scr, dump_file[i]) != 0) {
                    fprintf(stderr, "cannot write %s\n", dump_file[i]);
                    ops->destroy(t);
                    return 1;
                }
                printf("final: wrote %s\n", dump_file[i]);
            }
        ops->destroy(t);
        return 0;
    }

    rt_slide(ops, t, scr);
    printf("%s finished\n", ops->name);
    ops->destroy(t);
    shutdown_platform();
    return 0;
}

/* Slide 0, then the slides after it in order, in real time. `last` limits how many of the later
 * slides are played (all of them if it is NSLIDES). */
/* The song (MUC.ALL offset 0x27137) is played by both MARK.EXE's title (started right after the
 * data is loaded, MARK 0x105d0) and STORY2.SL (started at the beginning of slide 1's set-up,
 * 0x10ab5). It has its own output, so the voice clips and their timing are unaffected. Returns
 * NULL when there is no music (no sound device, --no-music, file missing). */
static music_t *song_start_at(const char *who, size_t offset, size_t length)
{
    if (g_no_music || !audio_music_available())
        return NULL;
    music_t *m = music_load_at(offset, length);
    if (!m) {
        fprintf(stderr, "%s: no music, continuing without it\n", who);
        return NULL;
    }
    music_set_gain(m, g_music_gain);
    audio_music_start(music_render_cb, m, music_rate());
    return m;
}

static music_t *song_start(const char *who)
{
    return song_start_at(who, MUSIC_INTRO_OFFSET, MUSIC_INTRO_LENGTH);
}

static void song_stop(music_t *m)
{
    audio_music_stop();   /* before the song is freed: the audio thread must be done with it */
    music_free(m);
}

/* The menu in real time, on an already initialised platform (WALK0.SL). Returns the item the player
 * confirmed (0..3, see menu.h), or MENU_NONE if the window was closed or the menu could not start.
 * The original now runs a closing transition of 33 steps and then the action; neither is built, so
 * this returns at the confirmation. The original's mouse driver reports positions in a virtual
 * range it may have doubled; here the pointer maps 1:1 to screen pixels (docs/port.md). The song is
 * the menu's own (WALK0.SL reads MUC.ALL at 0x2bad9). */
static menu_result play_menu(screen_t *scr, int *slot)
{
    menu_t *m = menu_create(scr);
    if (!m)
        return MENU_NONE;
    platform_show_pointer(0);
    uint32_t next = platform_ticks_ms();
    menu_result r = MENU_NONE;

    /* The opening animation (menu.h): the zoom, then the lettering shrinking. The original reads no input
     * while it runs (the keyboard port keeps its value, so a key still held when the loop starts is
     * seen then). Its song starts when the opening is over (WALK0.SL 0x10b3b). */
    const int opening = menu_opening_steps();
    const uint32_t t0 = next;
    const uint32_t zoom_ms = (uint32_t)(MENU_WIPE_STEPS + 1) * 1000u / TK_MENU_WIPE_HZ;   /* steps 0..21 */
    int quit = 0, shown = -1;
    while (!quit && shown < opening) {
        if (!platform_poll()) {
            quit = 1;
            break;
        }
        /* The step due now: by elapsed time, so that a rate above the display's does not slow it down;
         * steps that fall between two displayed frames are skipped (each frame depends only on its step). */
        uint32_t el = platform_ticks_ms() - t0;
        int n = el < zoom_ms ? (int)(el * TK_MENU_WIPE_HZ / 1000u)
                             : MENU_WIPE_STEPS + 1 + (int)((el - zoom_ms) * TK_MENU_SHRINK_HZ / 1000u);
        if (n > opening)
            n = opening;
        if (n != shown) {
            menu_opening_frame(m, scr, n);
            platform_present(scr);
            shown = n;
        } else {
            platform_sleep_ms(1);
        }
    }
    next = platform_ticks_ms();
    music_t *song = quit ? NULL : song_start_at("menu", MUSIC_MENU_OFFSET, MUSIC_MENU_LENGTH);
    while (!quit && r == MENU_NONE && platform_poll()) {
        uint32_t now = platform_ticks_ms();
        if ((int32_t)(now - next) < 0) {
            platform_sleep_ms(1);
            continue;
        }
        next = next_due(next, now, TK_MENU_PASS_HZ);
        menu_mouse mouse;
        int have_mouse = platform_mouse(&mouse.x, &mouse.y, &mouse.buttons);
        r = menu_pass_input(m, scr, platform_port60(), have_mouse ? &mouse : NULL);
        if (r == MENU_NONE)
            platform_present(scr);
    }
    /* The transition after a choice: the check mark. The song plays on through it (the original stops it
     * when the next program is about to start). */
    if (!quit && r != MENU_NONE) {
        menu_confirm_begin(m);
        int more = 1;
        next = platform_ticks_ms();
        while (more) {
            if (!platform_poll()) {
                quit = 1;
                break;
            }
            uint32_t now = platform_ticks_ms();
            if ((int32_t)(now - next) < 0) {
                platform_sleep_ms(1);
                continue;
            }
            next = next_due(next, now, TK_MENU_CONFIRM_HZ);
            more = menu_confirm_step(m, scr);
            platform_present(scr);
        }
        /* "Load a saved game": the slot picker, until a slot with a save is picked (it has no way out). */
        if (!quit && r == MENU_LOAD) {
            menu_picker_begin(m, saves_present());
            int picked = MENU_NONE;
            next = platform_ticks_ms();
            while (picked == MENU_NONE) {
                if (!platform_poll()) {
                    quit = 1;
                    break;
                }
                uint32_t now = platform_ticks_ms();
                if ((int32_t)(now - next) < 0) {
                    platform_sleep_ms(1);
                    continue;
                }
                next = next_due(next, now, TK_MENU_PASS_HZ);
                menu_mouse mouse;
                int have_mouse = platform_mouse(&mouse.x, &mouse.y, &mouse.buttons);
                picked = menu_picker_pass(m, scr, platform_port60(), have_mouse ? &mouse : NULL);
                if (picked == MENU_NONE)
                    platform_present(scr);
            }
            if (slot)
                *slot = picked;
        }
        /* let the last frame show for a moment before the next program takes over */
        for (uint32_t t0 = platform_ticks_ms(); !quit && platform_ticks_ms() - t0 < 300 && platform_poll();)
            platform_sleep_ms(5);
    }
    song_stop(song);
    platform_show_pointer(1);
    menu_free(m);
    return quit ? MENU_NONE : r;
}

static const char *menu_item_name(menu_result r)
{
    switch (r) {
    case MENU_NEW_GAME: return "new game (the original reads the new-game files and starts WALK.SL, the world)";
    case MENU_LOAD: return "load a saved game (the slot picker)";
    case MENU_CREDITS: return "credits (the original starts CAST.SL)";
    case MENU_QUIT: return "quit to DOS";
    default: return "nothing";
    }
}

/* Slide 0, then the slides after it, in real time, on an already initialised platform. Returns
 * 0, or 1 if a scene could not be created. It also returns early (0) when the window is closed. */
static int play_intro(screen_t *scr, int faithful, int last)
{
    intro0_t *t0 = intro0_create(scr);
    if (!t0)
        return 1;
    intro0_set_music_replay(t0, g_music_replay);
    rt_intro0(t0, scr);
    intro0_free(t0);
    if (!platform_poll())
        return 0;
    music_t *song = song_start("intro");
    int rc = 0;
    for (int i = 0; i < last && rc == 0 && platform_poll(); i++) {
        void *t = slides[i]->create(scr, faithful);
        if (!t) {
            rc = 1;
            break;
        }
        rt_slide(slides[i], t, scr);
        slides[i]->destroy(t);
    }
    song_stop(song);
    return rc;
}

static int run_intro(screen_t *scr, int scale, int faithful, int last)
{
    if (init_platform(scale) != 0)
        return 1;
    int rc = play_intro(scr, faithful, last);
    if (rc == 0 && last == NSLIDES)
        printf("the intro is over: the original now starts the title again (not chained here yet)\n");
    else
        printf("%s: the next slide is not built yet\n", last > 0 ? slides[last - 1]->name : "intro0");
    shutdown_platform();
    return rc;
}

/* The program chain as the original runs it, in one process: MARK.EXE's title; after its timeout
 * STORY2.SL's intro, then MARK.EXE again; after a key or click WALK0.SL's menu (not built, so the
 * game ends there). Each original program is started fresh by `exec`, so each scene here starts
 * from its own initial state. The one-byte message that MARK leaves in ANI.LCP is 0x3f in every
 * hand-over that STORY2.SL takes part in, and STORY2.SL does not change it, so it is not modelled. */
static int run_game(screen_t *scr, int scale, int faithful)
{
    if (init_platform(scale) != 0)
        return 1;
    int rc = 0, rounds = 0;
    for (;;) {
        printf("game: title (round %d)\n", rounds);
        title_result r = play_title(scr);
        if (r == TITLE_RUNNING) {   /* window closed (or the title could not start) */
            rc = platform_poll() ? 1 : 0;
            break;
        }
        if (r == TITLE_TO_MENU) {
            printf("game: menu\n");
            int slot = MENU_NONE;
            menu_result c = play_menu(scr, &slot);
            if (c == MENU_NONE) {
                rc = platform_poll() ? 1 : 0;   /* window closed (or the menu could not start) */
                break;
            }
            if (slot != MENU_NONE)
                printf("game: slot %d picked\n", slot);
            printf("game: the menu chose %s; that part is not built yet, stopping\n", menu_item_name(c));
            break;
        }
        printf("game: intro\n");
        if (play_intro(scr, faithful, NSLIDES) != 0) {
            rc = 1;
            break;
        }
        if (!platform_poll())
            break;
        printf("game: the intro is over (round %d), back to the title\n", ++rounds);
    }
    shutdown_platform();
    return rc;
}

int main(int argc, char **argv)
{
    setvbuf(stdout, NULL, _IOLBF, 0);   /* progress lines must survive a kill */
    const char *data_dir = getenv("TK_DATA");
    const char *ppm = NULL, *scene = "title";
    int scale = 3, tick_div = 3;
    int ndump = 0, npress = 0;
    int dump_pass[MAX_DUMPS], press_pass[MAX_DUMPS];
    char *dump_file[MAX_DUMPS];
    int ndump1 = 0, faithful = 0;
    dump_kind dump1_kind[MAX_DUMPS];
    int dump1_pass[MAX_DUMPS];
    char *dump1_file[MAX_DUMPS];

    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--data") && i + 1 < argc)
            data_dir = argv[++i];
        else if (!strcmp(argv[i], "--scale") && i + 1 < argc)
            scale = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--ppm") && i + 1 < argc)
            ppm = argv[++i];
        else if (!strcmp(argv[i], "--scene") && i + 1 < argc)
            scene = argv[++i];
        else if (!strcmp(argv[i], "--tick-div") && i + 1 < argc)
            tick_div = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--step-dump") && i + 1 < argc && ndump < MAX_DUMPS) {
            char *arg = argv[++i];
            char *colon = strchr(arg, ':');
            if (!colon) {
                fprintf(stderr, "--step-dump wants N:FILE\n");
                return 2;
            }
            *colon = 0;
            dump_pass[ndump] = atoi(arg);
            dump_file[ndump++] = colon + 1;
        } else if (!strcmp(argv[i], "--picker-dump") && i + 1 < argc && g_pick_count < MAX_DUMPS) {
            char *arg = argv[++i];
            char *colon = strchr(arg, ':');
            if (!colon) {
                fprintf(stderr, "--picker-dump wants N:FILE\n");
                return 2;
            }
            *colon = 0;
            g_pick_n[g_pick_count] = atoi(arg);
            g_pick_file[g_pick_count++] = colon + 1;
        } else if (!strcmp(argv[i], "--picker-slots") && i + 1 < argc)
            g_pick_mask = (int)strtol(argv[++i], NULL, 0);
        else if (!strcmp(argv[i], "--picker-key") && i + 1 < argc && g_pick_nkeys < MAX_SCRIPT) {
            int n;
            unsigned code;
            if (sscanf(argv[++i], "%d:%i", &n, &code) != 2) {
                fprintf(stderr, "--picker-key wants N:CODE\n");
                return 2;
            }
            g_pick_keys[g_pick_nkeys].pass = n;
            g_pick_keys[g_pick_nkeys++].code = code;
        } else if (!strcmp(argv[i], "--picker-mouse") && i + 1 < argc && g_pick_nmice < MAX_SCRIPT) {
            int n, x, y;
            unsigned b;
            if (sscanf(argv[++i], "%d:%i:%i:%i", &n, &x, &y, &b) != 4) {
                fprintf(stderr, "--picker-mouse wants N:X:Y:BUTTONS\n");
                return 2;
            }
            g_pick_mice[g_pick_nmice].pass = n;
            g_pick_mice[g_pick_nmice].x = x;
            g_pick_mice[g_pick_nmice].y = y;
            g_pick_mice[g_pick_nmice++].buttons = b;
        } else if (!strcmp(argv[i], "--confirm-dump") && i + 1 < argc && g_conf_count < MAX_DUMPS) {
            char *arg = argv[++i];
            char *colon = strchr(arg, ':');
            if (!colon) {
                fprintf(stderr, "--confirm-dump wants N:FILE\n");
                return 2;
            }
            *colon = 0;
            g_conf_n[g_conf_count] = atoi(arg);
            g_conf_file[g_conf_count++] = colon + 1;
        } else if (!strcmp(argv[i], "--open-dump") && i + 1 < argc && g_open_count < MAX_DUMPS) {
            char *arg = argv[++i];
            char *colon = strchr(arg, ':');
            if (!colon) {
                fprintf(stderr, "--open-dump wants N:FILE\n");
                return 2;
            }
            *colon = 0;
            g_open_n[g_open_count] = atoi(arg);
            g_open_file[g_open_count++] = colon + 1;
        } else if (!strcmp(argv[i], "--press-pass") && i + 1 < argc && npress < MAX_DUMPS)
            press_pass[npress++] = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--faithful-keys"))
            faithful = 1;
        else if (!strcmp(argv[i], "--no-audio"))
            g_no_audio = 1;
        else if (!strcmp(argv[i], "--no-music"))
            g_no_music = 1;
        else if (!strcmp(argv[i], "--highlight") && i + 1 < argc)
            g_highlight = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--menu-key") && i + 1 < argc && g_menu_nkeys < MAX_SCRIPT) {
            int n;
            unsigned code;
            if (sscanf(argv[++i], "%d:%i", &n, &code) != 2) {
                fprintf(stderr, "--menu-key wants N:CODE\n");
                return 2;
            }
            g_menu_keys[g_menu_nkeys].pass = n;
            g_menu_keys[g_menu_nkeys++].code = code;
        } else if (!strcmp(argv[i], "--menu-mouse") && i + 1 < argc && g_menu_nmice < MAX_SCRIPT) {
            int n, x, y;
            unsigned b;
            if (sscanf(argv[++i], "%d:%i:%i:%i", &n, &x, &y, &b) != 4) {
                fprintf(stderr, "--menu-mouse wants N:X:Y:BUTTONS\n");
                return 2;
            }
            g_menu_mice[g_menu_nmice].pass = n;
            g_menu_mice[g_menu_nmice].x = x;
            g_menu_mice[g_menu_nmice].y = y;
            g_menu_mice[g_menu_nmice++].buttons = b;
        }
        else if (!strcmp(argv[i], "--music-gain") && i + 1 < argc) {
            long gain = strtol(argv[++i], NULL, 10);
            if (gain < 0 || gain > 400) {
                fprintf(stderr, "--music-gain wants a percentage from 0 to 400\n");
                return 2;
            }
            g_music_gain = (unsigned)gain;
        }
        else if (!strcmp(argv[i], "--slide0-replay"))
            g_music_replay = 1;   /* the default now; kept so old command lines still work */
        else if (!strcmp(argv[i], "--slide0-once"))
            g_music_replay = 0;
        else if (!strcmp(argv[i], "--rand-state") && i + 1 < argc)
            tk_set_state((uint32_t)strtoul(argv[++i], NULL, 16));
        else if (!strcmp(argv[i], "--dump") && i + 1 < argc && ndump1 < MAX_DUMPS) {
            /* KIND:N:FILE, KIND = anim | sub | out | end (slides 1+, see run_intro1) */
            char *arg = argv[++i];
            char *c1 = strchr(arg, ':');
            char *c2 = c1 ? strchr(c1 + 1, ':') : NULL;
            if (!c2) {
                fprintf(stderr, "--dump wants KIND:N:FILE\n");
                return 2;
            }
            *c1 = 0;
            *c2 = 0;
            if (!strcmp(arg, "anim"))
                dump1_kind[ndump1] = DUMP_ANIM;
            else if (!strcmp(arg, "sub"))
                dump1_kind[ndump1] = DUMP_SUB;
            else if (!strcmp(arg, "out"))
                dump1_kind[ndump1] = DUMP_OUT;
            else if (!strcmp(arg, "final"))
                dump1_kind[ndump1] = DUMP_FINAL;
            else if (!strcmp(arg, "end"))
                dump1_kind[ndump1] = DUMP_END;
            else {
                fprintf(stderr, "--dump kind must be anim, sub, out, end or final\n");
                return 2;
            }
            dump1_pass[ndump1] = atoi(c1 + 1);
            dump1_file[ndump1++] = c2 + 1;
        } else {
            fprintf(stderr,
                    "usage: %s [--data DIR] [--scale N] [--scene title|gmain|intro0|intro1|intro2|intro3|intro4|intro5|intro6|intro|game|menu] [--ppm FILE]\n"
                    "          [--step-dump N:FILE]... [--press-pass N]... [--tick-div N]\n"
                    "          [--dump anim|sub|out|end|final:N:FILE]... [--faithful-keys] [--rand-state HEX]\n"
                    "          [--no-audio] [--no-music] [--music-gain PERCENT] [--slide0-once]\n",
                    argv[0]);
            return 2;
        }
    }
    if (data_dir)
        assets_set_dir(data_dir);
    if (scale < 1)
        scale = 1;

    static screen_t screen;

    if (!strcmp(scene, "gmain")) {
        /* The interface palette is the first 768 bytes of RPG.LCP. */
        if (load_palette(&screen, "rpg.lcp", 0) != 0 || load_screen(&screen, "gmain.lcz", 0) != 0)
            return 1;
        if (ppm)
            return screen_write_ppm(&screen, ppm) == 0 ? 0 : 1;
        if (platform_init("3K (work in progress)", scale) != 0)
            return 1;
        while (platform_poll()) {
            platform_present(&screen);
            platform_sleep_ms(16);
        }
        platform_shutdown();
        return 0;
    }

    if (!strcmp(scene, "intro0"))
        return run_intro0(&screen, scale, ndump, dump_pass, dump_file, tick_div < 1 ? 1 : tick_div);
    if (!strcmp(scene, "intro1") || !strcmp(scene, "intro2") || !strcmp(scene, "intro3") ||
        !strcmp(scene, "intro4") || !strcmp(scene, "intro5") ||
        !strcmp(scene, "intro6")) {
        int k = scene[5] - '1';   /* intro1 -> slides[0], intro2 -> slides[1], ... */
        return run_slide(slides[k], &screen, scale, ndump1, dump1_kind, dump1_pass, dump1_file, faithful);
    }
    if (!strcmp(scene, "intro"))
        return run_intro(&screen, scale, faithful, NSLIDES);
    if (!strcmp(scene, "game"))
        return run_game(&screen, scale, faithful);
    if (!strcmp(scene, "menu")) {
        if (g_open_count > 0)
            return run_menu_opening_stepped(&screen);
        if (ndump > 0)
            return run_menu_stepped(&screen, ndump, dump_pass, dump_file);
        if (init_platform(scale) != 0)
            return 1;
        int slot = MENU_NONE;
        menu_result r = play_menu(&screen, &slot);
        printf("menu: %s\n", r == MENU_NONE ? "closed without a choice" : menu_item_name(r));
        if (slot != MENU_NONE)
            printf("menu: slot %d picked\n", slot);
        shutdown_platform();
        return 0;
    }

    if (strcmp(scene, "title") != 0) {
        fprintf(stderr, "unknown scene '%s'\n", scene);
        return 2;
    }

    if (ndump > 0)
        return run_title_stepped(&screen, ndump, dump_pass, dump_file, npress, press_pass);

    if (init_platform(scale) != 0)
        return 1;
    title_result r = play_title(&screen);
    if (r == TITLE_RUNNING)
        printf("title: stopped before it finished\n");
    else
        printf("title finished: next scene would be the %s (not built yet)\n",
               r == TITLE_TO_MENU ? "menu" : "intro slideshow");
    shutdown_platform();
    return 0;
}
