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
#include <time.h>

#include "assets.h"
#include "audio.h"
#include "credits.h"
#include "ending.h"
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
#include "battle.h"
#include "gameover.h"
#include "sfx.h"
#include "walk.h"

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
/* --write-dir DIR: where the files the game writes go (saves, MAN_INFO.NOW, __1.NOW; the data directory without it). With it, a pick
 * in the stepped menu also loads the slot there (menu_load_save), and confirming item 0 writes the new game's files there
 * (menu_new_game), as the original does before starting WALK.SL. */
static const char *g_write_dir = NULL;
static struct { int pass; unsigned code; } g_pick_keys[MAX_SCRIPT];
static int g_pick_nkeys;
static struct { int pass; int x, y; unsigned buttons; } g_pick_mice[MAX_SCRIPT];
static int g_pick_nmice;
static int g_pick_n[MAX_DUMPS];
static const char *g_pick_file[MAX_DUMPS];
static int g_pick_count;
static int g_pick_mask = -1;
/* The credits, stepped and headless (--scene credits): --credits-wipe-dump N:FILE writes the screen after N steps
 * of the wipe, --step-dump N:FILE after N completed passes of the loop, --credits-key N:CODE sets the keyboard
 * port 0x60 to CODE from the pass after N completed passes, and --screen-from FILE starts from the screen
 * (raw: 64,000 pixels then the 768-byte palette) that an earlier program left. */
static int g_cwipe_n[MAX_DUMPS];
static const char *g_cwipe_file[MAX_DUMPS];
static int g_cwipe_count;
static struct { int pass; unsigned code; } g_credits_keys[MAX_SCRIPT];
static int g_credits_nkeys;
static struct { int pass; unsigned code; } g_walk_keys[MAX_SCRIPT];   /* --walk-key N:CODE (headless runs) */
static int g_walk_nkeys;
static const char *g_screen_from;
static int g_stepped;          /* --stepped: the credits headless even with no dump and no key (only the end is reported) */
static int g_no_audio;        /* --no-audio */
static int g_no_music;        /* --no-music */
static unsigned g_music_gain = 100;   /* --music-gain PERCENT */
static int g_music_replay = 1; /* slide 0's clip plays again after it ends, as in the original; --slide0-once turns it off */
static long g_walk_clock = 0;  /* --walk-clock N|real: the BIOS tick count when WALK.SL starts; -1 = the time of day */

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
                if (r == MENU_NEW_GAME && g_write_dir)
                    printf(menu_new_game() == 0 ? "wrote the new game's files into %s\n" : "could not write the new game's files into %s\n",
                           g_write_dir);
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
                            if (g_write_dir)
                                printf(menu_load_save(slot) == 0 ? "loaded slot %d into %s\n" : "could not load slot %d into %s\n",
                                       slot, g_write_dir);
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

/* Read a raw dump (64,000 pixels, then the 768-byte palette) into the screen: what an earlier program left. */
static int read_raw(screen_t *s, const char *path)
{
    FILE *f = fopen(path, "rb");
    if (!f) {
        fprintf(stderr, "cannot read %s\n", path);
        return -1;
    }
    size_t a = fread(s->pix, 1, SCREEN_PIXELS, f);
    size_t b = fread(s->pal, 1, 768, f);
    fclose(f);
    if (a != SCREEN_PIXELS || b != 768) {
        fprintf(stderr, "%s is not a raw screen dump\n", path);
        return -1;
    }
    return 0;
}

/* The credits, stepped and headless. The screen at the start is --screen-from's, or the menu's frame (GMAIN with
 * the interface palette). Dumps: --credits-wipe-dump N:FILE after N wipe steps, --step-dump N:FILE after N completed
 * passes (N = 0: the wipe is over, no pass yet). The reference is the oracle's screen at its N-th arrival at the
 * loop top (tools/check_credits.sh). Prints how the loop ended, in passes started (the original's count of
 * completed passes plus one). */
static int run_credits_stepped(screen_t *scr, int ndump, const int *dump_pass, char **dump_file)
{
    if (g_screen_from) {
        if (read_raw(scr, g_screen_from) != 0)
            return 1;
    } else if (load_palette(scr, "rpg.lcp", 0) != 0 || load_screen(scr, "gmain.lcz", 0) != 0) {
        return 1;
    }
    credits_t *c = credits_create(scr);
    if (!c)
        return 1;
    int rc = 0;
    for (int i = 0; i < g_cwipe_count && rc == 0; i++)   /* wipe dumps may be asked for in any order: replay */
        if (g_cwipe_n[i] == 0 && write_raw(scr, g_cwipe_file[i]) != 0)
            rc = 1;
    for (int step = 1; step <= CREDITS_WIPE_STEPS && rc == 0; step++) {
        credits_wipe_step(c, scr);
        for (int i = 0; i < g_cwipe_count; i++)
            if (g_cwipe_n[i] == step && write_raw(scr, g_cwipe_file[i]) != 0) {
                fprintf(stderr, "cannot write %s\n", g_cwipe_file[i]);
                rc = 1;
            }
    }
    int last = -1;
    for (int i = 0; i < ndump; i++)
        if (dump_pass[i] > last)
            last = dump_pass[i];
    unsigned port60 = 0;   /* scripted runs start like the oracle: no scancode yet */
    for (int done = 0; rc == 0; done++) {
        for (int i = 0; i < ndump; i++)
            if (dump_pass[i] == done) {
                if (write_raw(scr, dump_file[i]) != 0) {
                    fprintf(stderr, "cannot write %s\n", dump_file[i]);
                    rc = 1;
                }
                printf("pass %d: wrote %s\n", done, dump_file[i]);
            }
        if (rc != 0 || (last >= 0 && done >= last))
            break;
        for (int i = 0; i < g_credits_nkeys; i++)
            if (g_credits_keys[i].pass == done)
                port60 = g_credits_keys[i].code;
        credits_status st = credits_pass(c, scr, port60);
        if (st != CREDITS_RUNNING) {
            printf("credits ended by %s after %d pass(es) started\n", st == CREDITS_END_KEY ? "key" : "time",
                   done + 1);
            break;
        }
    }
    credits_free(c);
    return rc;
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

/* The screen the credits start from when nothing was handed over: the menu's frame, GMAIN with the interface
 * palette (--screen-from FILE takes a screen dump instead). */
static int credits_start_screen(screen_t *scr)
{
    if (g_screen_from)
        return read_raw(scr, g_screen_from);
    if (load_palette(scr, "rpg.lcp", 0) != 0 || load_screen(scr, "gmain.lcz", 0) != 0)
        return -1;
    return 0;
}

/* The credits in real time (CAST.SL), on an already initialised platform, over what is on `scr` now (the menu's
 * last frame, whose palette they keep). Returns 0 when they ended (by time or by Space or Enter, after which the
 * original starts the menu again), 1 if the window was closed, -1 if they could not start. Song 0 of MUC.ALL plays
 * under them, as in the original (it uses the OPL rhythm mode). */
static int play_credits(screen_t *scr)
{
    credits_t *c = credits_create(scr);
    if (!c)
        return -1;
    int quit = 0;
    uint32_t t0 = platform_ticks_ms();
    unsigned done = 0;
    while (!quit && done < CREDITS_WIPE_STEPS) {
        if (!platform_poll()) {
            quit = 1;
            break;
        }
        unsigned due = (unsigned)((platform_ticks_ms() - t0) * TK_CREDITS_WIPE_HZ / 1000u);
        if (due > CREDITS_WIPE_STEPS)
            due = CREDITS_WIPE_STEPS;
        if (due > done) {   /* steps are sequential: catch up, then show */
            while (done < due) {
                credits_wipe_step(c, scr);
                done++;
            }
            platform_present(scr);
        } else {
            platform_sleep_ms(1);
        }
    }
    uint32_t next = platform_ticks_ms();
    /* The song starts when the loop does (CAST.SL starts it after reading the files, before the loop) and stops when
     * the credits end. It is silent where there is no sound device or with --no-music. */
    music_t *song = quit ? NULL : song_start_at("credits", MUSIC_CREDITS_OFFSET, MUSIC_CREDITS_LENGTH);
    credits_status st = CREDITS_RUNNING;
    while (!quit && st == CREDITS_RUNNING && platform_poll()) {
        uint32_t now = platform_ticks_ms();
        if ((int32_t)(now - next) < 0) {
            platform_sleep_ms(1);
            continue;
        }
        next = next_due(next, now, TK_CREDITS_PASS_HZ);
        st = credits_pass(c, scr, platform_port60());
        if (st != CREDITS_END_TIME)
            platform_present(scr);
    }
    if (st == CREDITS_RUNNING)   /* the loop above left because platform_poll said quit */
        quit = 1;
    song_stop(song);
    credits_free(c);
    return quit ? 1 : 0;
}

/* The screen WALK.SL starts over when nothing was handed over: the menu's first frame (GMAIN with the CPU label and
 * the interface palette); --screen-from FILE takes a screen dump instead. Only the window and the interface are
 * drawn over it, so this is enough to show the first screen. */
static int walk_start_screen(screen_t *scr)
{
    if (g_screen_from)
        return read_raw(scr, g_screen_from);
    menu_t *m = menu_create(scr);
    if (!m)
        return -1;
    menu_free(m);
    return 0;
}

/* The BIOS tick count WALK.SL starts with: --walk-clock N, or the time of day (ticks since midnight) with
 * --walk-clock real. The default is 0, which is what the oracle's runs start with: rain from the first moment
 * (the clock decides the weather, see walk_rain_stride). */
static uint32_t walk_clock0(void)
{
    if (g_walk_clock >= 0)
        return (uint32_t)g_walk_clock;
    time_t t = time(NULL);
    struct tm lt;
    if (!localtime_r(&t, &lt))
        return 0;
    unsigned long long secs = (unsigned long long)lt.tm_hour * 3600u + (unsigned)lt.tm_min * 60u + (unsigned)lt.tm_sec;
    return (uint32_t)(secs * TK_BIOS_TICKS_PER_SEC_NUM / TK_BIOS_TICKS_PER_SEC_DEN);
}

/* The window's pointer as WALK.SL's mouse driver: there when the platform reports one. */
static void feed_mouse(walk_t *w)
{
    int x = 0, y = 0;
    unsigned b = 0;
    int on = platform_mouse(&x, &y, &b);
    walk_set_mouse(w, on, x, y, b & 3);
}

/* One step of the world's start zoom on screen. The original waits for nothing between its 13 steps, so how long they take depends
 * on the machine; here one a pass of the world's pace (TK_WALK_PASS_HZ), about 0.37 s in all (a choice). A closed window ends the
 * waits; the world's loop then sees it. */
static void show_zoom_step(void *ctx, screen_t *scr)
{
    uint32_t *due = ctx;
    platform_present(scr);
    if (!*due || (int32_t)(platform_ticks_ms() - *due) > 100)   /* the first step of a zoom (the start's, or a door's later) */
        *due = platform_ticks_ms();
    *due += 1000u / TK_WALK_PASS_HZ;
    while ((int32_t)(platform_ticks_ms() - *due) < 0 && platform_poll())
        platform_sleep_ms(1);
}

/* One of the 48 steps of a helped member's face flash in the battle's picker (battle_set_show): about 10 ms each, half a second in
 * all (a choice; the original's 100 draws a step ran as fast as the machine). */
static void show_flash_step(void *ctx, screen_t *scr)
{
    uint32_t due = platform_ticks_ms() + 10;
    (void)ctx;
    platform_present(scr);
    while ((int32_t)(platform_ticks_ms() - due) < 0 && platform_poll())
        platform_sleep_ms(1);
}

/* The Sound Blaster's sounds (walk_set_sfx) through the sound sink: a sound replaces any that plays, and one is busy while the sink
 * still plays it, which stands for the card's busy word. */
static void sfx_play(void *ctx, unsigned file, unsigned off)
{
    const voc_clip *c = sfx_clip(ctx, file, off);
    if (c)
        audio_play(c->pcm, c->frames, c->rate);
}

static int sfx_busy(void *ctx)
{
    (void)ctx;
    return audio_playing();
}

static void sfx_cut(void *ctx)
{
    (void)ctx;
    audio_stop();
}

/* The original spins until the card's busy word is 0, so the game stands still while the sound plays; a closed window ends it */
static void sfx_wait(void *ctx)
{
    (void)ctx;
    while (audio_playing() && platform_poll())
        platform_sleep_ms(2);
}

/* WALK.SL's world (walk.h) over what is on `scr` now (the menu's last frame, whose palette it keeps): the first
 * screen, then one pass of the main loop per tick of TK_WALK_PASS_HZ, until the window is closed, with the keys, the
 * mouse (the pointer, walking and shooting in the world, the battle's buttons) and the music. Returns 1 when the window was closed, 2
 * after a lost battle, 3 after key 4's yes (the original then starts MARK.EXE), -1 if the screen could not be made. */
static int play_walk(screen_t *scr)
{
    walk_t *w = walk_create();
    if (!w)
        return -1;
    const uint32_t clock0 = walk_clock0();
    /* The scene's song (the loader found it in MUC.ALL: RPG.LCP 0x7add + 5 * the map header's byte 2) starts before the start's zoom
     * (the original starts it at the loader's end, BIOS tick 10) and plays on, starting again at its end, until the window is closed.
     * Its register writes are the original driver's, all 6,180 ticks of scene 0's song (tools/compare_opl.py). Silent without a sound
     * device or with --no-music. */
    size_t song_pos = walk_scene(w)->song_pos, song_len = walk_scene(w)->song_len;
    music_t *song = song_start_at("walk", song_pos, song_len);
    /* With music playing the game has an FM card: its flag is 0x81 and the music button starts pressed (walk_sound_buttons). With a
     * sound device it has a Sound Blaster too: SND_EF.1 and _1_.LOC are read (the original reads the second when a battle starts), the
     * sound flag is 0x81 and that button starts pressed; key 2 or the button turns the sounds off and on in a battle. */
    sfx_bank_t *bank = audio_enabled() ? sfx_bank_load() : NULL;
    walk_set_sound(w, song ? 0x81 : 0, bank ? 0x81 : 0);
    const walk_sfx_t sfx = {sfx_play, sfx_busy, sfx_cut, bank, sfx_wait};
    if (bank)
        walk_set_sfx(w, &sfx);
    walk_set_show(w, show_flash_step, NULL);   /* F4's picker: the face flash's steps, as in a battle */
    walk_set_live_port(w, platform_port60);    /* a door's picture ends on a key pressed while it runs */
    /* WALK.SL draws its own mouse pointer (a driver is taken to be there whenever the window reports a pointer): the world's and the
     * battle's (walk_set_mouse before each pass). In the world the left button walks towards the pointer and the right one shoots; a
     * press on an on-screen button is its key (the F-keys, 1 to 4, A). Key 3 saves into the data directory (SAVENN.00K, __1.NOW), as
     * the original does into its own. */
    platform_show_pointer(0);
    uint32_t zoom_due = 0;
    if (walk_begin_shown(w, scr, clock0 & 0xFFFF, clock0, show_zoom_step, &zoom_due) != 0) {
        song_stop(song);
        walk_free(w);
        sfx_bank_free(bank);
        return -1;
    }
    const walk_scene_t *sc = walk_scene(w);
    unsigned scene_changes = walk_scene_changes(w), inn_nights = walk_inn_nights(w);
    const uint32_t start = platform_ticks_ms();
    uint32_t next = start;
    int event_reported = 0;
    /* A touched enemy starts the battle (battle.h): its first screen at once, then its passes at the world's pace (the original
     * paces neither). The scene's song stops and the battle's starts: MUC.ALL 0x2bad9, the menu's song, which the loader had read
     * (the original's register writes match the player for 3,119 song ticks). The battle does not end yet. */
    battle_t *bt = NULL;
    unsigned battle_fm = 0;   /* the music flag the battle's song was last started or stopped for */
    int got = walk_begin_result(w), result = 1;
    platform_present(scr);
    while (platform_poll()) {
        if (!bt && (got & WALK_PASS_BATTLE)) {
            got = 0;
            bt = battle_begin(w, scr);
            if (!bt)
                break;
            battle_set_show(bt, show_flash_step, NULL);
            song_stop(song);   /* 0x12ed4: the scene's song stops whenever there is a card; the battle's plays if the music is on */
            song = *walk_fm(w) == 0x81 ? song_start_at("battle", MUSIC_MENU_OFFSET, MUSIC_MENU_LENGTH) : NULL;
            battle_fm = *walk_fm(w);
            platform_escape_quits(0);   /* Esc goes back from choosing a target; the window's close button still quits */
            platform_present(scr);
        }
        uint32_t now = platform_ticks_ms();
        if ((int32_t)(now - next) < 0) {
            platform_sleep_ms(1);
            continue;
        }
        next = next_due(next, now, TK_WALK_PASS_HZ);
        uint32_t ticks = clock0 + (uint32_t)((unsigned long long)(now - start) * TK_BIOS_TICKS_PER_SEC_NUM /
                                             TK_BIOS_TICKS_PER_SEC_DEN / 1000u);
        if (bt) {
            /* The rewards bar runs at the world's pace (the original paces neither). A message box's wait is a tight loop in the
             * original that steps the palette every 41st round; 41 rounds a pass here give one step a pass, like the world's cycle
             * (a choice: how fast the original's loop ran was not measured). The magic and item lists and the member picker are such loops too. */
            int kind = battle_kind(bt);
            int rounds = kind == BATTLE_KIND_DIALOG || kind == BATTLE_KIND_MAGIC || kind == BATTLE_KIND_PICK ||
                         kind == BATTLE_KIND_ITEMS ? 41 : 1;
            for (int i = 0; i < rounds && !battle_over(bt); i++) {
                feed_mouse(w);
                battle_pass(bt, scr, platform_port60());
            }   /* what is not built is said on stderr, once per new case */
            if (*walk_fm(w) != battle_fm) {   /* key 1 or the music button: 0x266f6 stops, 0x2660c plays the battle's song from its start */
                battle_fm = *walk_fm(w);
                song_stop(song);
                song = battle_fm == 0x81 ? song_start_at("battle", MUSIC_MENU_OFFSET, MUSIC_MENU_LENGTH) : NULL;
            }
            if (battle_over(bt) == BATTLE_LOST) {   /* the original writes 0x3e into ANI.LCP and starts MARK.EXE (play_gameover) */
                result = 2;
                break;
            }
            if (battle_over(bt) == BATTLE_BACK) {   /* 0x132cb: with the music on, the scene's song again from its start; the rest of the pass */
                printf("walk: the battle is won (%u killed); back in the world\n", battle_result(bt));
                song_stop(song);
                song = *walk_fm(w) == 0x81 ? song_start_at("walk", sc->song_pos, sc->song_len) : NULL;
                platform_escape_quits(1);
                got = walk_resume(w, ticks, platform_port60(), battle_result(bt));
                battle_free(bt);
                bt = NULL;
                if ((got & WALK_PASS_UNBUILT) && walk_unbuilt(w))
                    fprintf(stderr, "walk: not built: %s\n", walk_unbuilt(w));
            }
            platform_present(scr);
            continue;
        }
        feed_mouse(w);
        unsigned world_fm = *walk_fm(w);
        got = walk_pass(w, scr, ticks, platform_port60());
        if (walk_inn_nights(w) != inn_nights) {   /* an inn's night stops the music (0x24081); nothing read starts it again */
            inn_nights = walk_inn_nights(w);
            song_stop(song);
            song = NULL;
        }
        if (walk_scene_changes(w) != scene_changes) {   /* a door: the new scene's song from its start, with the music on (0x1132e) */
            scene_changes = walk_scene_changes(w);
            sc = walk_scene(w);
            printf("walk: scene %d\n", sc->scene);
            song_stop(song);
            song = *walk_fm(w) == 0x81 ? song_start_at("walk", sc->song_pos, sc->song_len) : NULL;
        }
        if (*walk_fm(w) != world_fm) {   /* key 1 or the music button: 0x165d:0x126 stops, 0x165d:0x3c plays the scene's song from its start */
            song_stop(song);
            song = *walk_fm(w) == 0x81 ? song_start_at("walk", sc->song_pos, sc->song_len) : NULL;
        }
        if (walk_left(w)) {   /* key 4's yes: WALK.SL ends (0x18b91) and MARK.EXE starts */
            printf("walk: left the game (the original starts MARK.EXE)\n");
            platform_present(scr);
            result = 3;
            break;
        }
        if (walk_ended(w)) {   /* a kind 0x13 record: the sounds and the music stopped, then ENDING.SL (not built) */
            printf("walk: the game's end (the original starts ENDING.SL)\n");
            platform_present(scr);
            result = 4;
            break;
        }
        /* Esc closes the window in the world, but F3's wait, F4's picker and list take it (they give up or end on it) */
        platform_escape_quits(walk_kind(w) == 0);
        if ((got & WALK_PASS_VILLAGER_EVENT) && !event_reported) {
            fprintf(stderr, "walk: a villager reached the case where the original starts an event (not built)\n");
            event_reported = 1;
        }
        if ((got & WALK_PASS_UNBUILT) && walk_unbuilt(w))
            fprintf(stderr, "walk: not built: %s\n", walk_unbuilt(w));
        platform_present(scr);
    }
    song_stop(song);
    battle_free(bt);
    platform_escape_quits(1);
    walk_free(w);
    if (bank)
        audio_stop();   /* the sounds are the bank's */
    sfx_bank_free(bank);
    return result;
}

static void ending_voice(void *ctx, unsigned off)
{
    audio_stop();   /* 0x11400: the sound cut first */
    sfx_play(ctx, SFX_LOC, off);
}

/* The period of ENDING.SL's next pass in microseconds, by the loop it is in (a scene's set-up step goes at the pace of the scene it
 * starts) */
static uint32_t ending_period_us(int loop)
{
    switch (loop) {
    case ENDING_SCENE1: return TK_ENDING_SCENE1_US;
    case ENDING_SCENE1_END: case ENDING_SCENE2: return TK_ENDING_SCENE2_US;
    case ENDING_SCENE2_END: case ENDING_SCENE3: return TK_ENDING_SCENE3_US;
    case ENDING_SCENE3_END: case ENDING_SCENE4_SHAKE: case ENDING_SCENE4_SHAKE_END: case ENDING_SCENE4_STROBE:
        return TK_ENDING_SCENE4_US;
    case ENDING_SCENE4_END: case ENDING_SCENE5_FACE: return TK_ENDING_SCENE5_FACE_US;
    case ENDING_SCENE5_FACE_END: case ENDING_SCENE5_EYES: return TK_ENDING_SCENE5_EYES_US;
    case ENDING_SCENE5_END: case ENDING_SCENE6_WIPE: return TK_ENDING_SCENE6_WIPE_US;
    case ENDING_SCENE6_FADE: return TK_ENDING_SCENE6_FADE_US;
    default: return TK_ENDING_CREDITS_US;
    }
}

/* ENDING.SL (ending.h) in real time, with the intro's song (ENDING.SL 0x102d5 starts the same block when there is an FM card). Its
 * loops wait for nothing; each runs at its own period (TK_ENDING_SCENE1_US ..), several passes between two frames shown when the period
 * is below a millisecond. With a sound device the voice lines play (a Sound Blaster). Only the first
 * scenes are built: after them the run stops and says so.
 * Returns 0, or 1 when the window was closed. */
static int play_ending(screen_t *scr)
{
    ending_t *e = ending_new(scr);
    if (!e) {
        fprintf(stderr, "ending: the data files cannot be read\n");
        return 0;
    }
    music_t *song = song_start("ending");
    sfx_bank_t *bank = audio_enabled() ? sfx_bank_load() : NULL;   /* the voice lines are blocks of _1_.LOC */
    if (bank)
        ending_set_voice(e, ending_voice, sfx_cut, sfx_busy, bank);
    platform_present(scr);
    uint64_t next = (uint64_t)platform_ticks_ms() * 1000u;
    int loop = ENDING_SCENE1, quit = 0;
    while (loop) {
        if (!platform_poll()) {
            quit = 1;
            break;
        }
        uint64_t now = (uint64_t)platform_ticks_ms() * 1000u;
        if (now < next) {
            platform_sleep_ms(1);
            continue;
        }
        if (now - next > 100000u)   /* more than 0.1 s behind (a stall): the backlog is dropped */
            next = now;
        for (int k = 0; k < 400 && loop && next <= now; k++) {
            next += ending_period_us(loop);
            loop = ending_step(e, scr);
        }
        platform_present(scr);
    }
    song_stop(song);   /* 0x11397: the music stopped and its driver shut down */
    if (!quit) {   /* the text screen it leaves on exit, until a key, a button or 20 s */
        ending_text_end(scr);
        platform_present(scr);
        uint32_t until = platform_ticks_ms() + 20000u;
        unsigned held = platform_port60();   /* a key still down from the game does not count: a new press does */
        while ((int32_t)(platform_ticks_ms() - until) < 0) {
            if (!platform_poll()) {
                quit = 1;
                break;
            }
            int mx, my;
            unsigned b = 0;
            platform_mouse(&mx, &my, &b);
            unsigned k = platform_port60();
            if ((k != held && (k & 0x80) == 0) || b)
                break;
            platform_sleep_ms(10);
        }
        printf("ending: THE END\n");
    }
    if (bank)
        audio_stop();
    sfx_bank_free(bank);
    ending_free(e);
    return quit;
}

/* MARK.EXE's game-over screen (gameover.h) over what WALK.SL left on `scr`, with its song, until Space or Enter, or a mouse button. The original's loop
 * is unpaced; it runs at the world's pace here (a choice, not measured). Returns 0 after the key, 1 when the window was closed. */
static int play_gameover(screen_t *scr)
{
    gameover_t *g = gameover_begin(scr);
    if (!g)
        return 1;
    music_t *song = song_start_at("gameover", GAMEOVER_SONG_OFFSET, GAMEOVER_SONG_LENGTH);
    uint32_t next = platform_ticks_ms();
    int rc = 1;
    while (platform_poll()) {
        uint32_t now = platform_ticks_ms();
        if ((int32_t)(now - next) < 0) {
            platform_sleep_ms(1);
            continue;
        }
        next = next_due(next, now, TK_WALK_PASS_HZ);
        gameover_pass(g, scr);
        platform_present(scr);
        int mx = 0, my = 0;
        unsigned mb = 0;
        int mouse = platform_mouse(&mx, &my, &mb);
        if (gameover_key(platform_port60()) || (mouse && gameover_mouse(mb & 3))) {
            rc = 0;
            break;
        }
    }
    song_stop(song);
    gameover_free(g);
    return rc;
}

/* The world headless: --step-dump N:FILE writes the screen after pass N (raw: 64,000 pixels then the 768-byte
 * palette); pass 0 is the first screen. The clock is --walk-clock + pass / 3, which is how tools/walk_idle_trace.py
 * drives the original in the oracle. The reference is the original's screens (tools/check_walk.sh). */
static int run_walk_stepped(screen_t *scr, int ndump, const int *dump_pass, const char *ppm, char **dump_file)
{
    if (walk_start_screen(scr) != 0)
        return 1;
    walk_t *w = walk_create();
    const uint32_t clock0 = walk_clock0();
    if (!w || walk_begin(w, scr, 0, clock0) != 0) {
        walk_free(w);
        return 1;
    }
    int last = 0;
    for (int i = 0; i < ndump; i++)
        if (dump_pass[i] > last)
            last = dump_pass[i];
    int rc = 0, lost_said = 0;
    battle_t *bt = NULL;
    int got = walk_begin_result(w);
    for (int n = 0; n <= last && rc == 0; n++) {
        if (n > 0) {
            unsigned port60 = 0;   /* the value set at the latest pass number <= n holds */
            int best = -1;
            for (int i = 0; i < g_walk_nkeys; i++)
                if (g_walk_keys[i].pass <= n && g_walk_keys[i].pass >= best) {
                    best = g_walk_keys[i].pass;
                    port60 = g_walk_keys[i].code;
                }
            if (bt) {
                battle_pass(bt, scr, port60);
                if (battle_over(bt) == BATTLE_BACK) {
                    got = walk_resume(w, clock0 + (uint32_t)n / 3, port60, battle_result(bt));
                    battle_free(bt);
                    bt = NULL;
                    got &= ~WALK_PASS_BATTLE;
                } else if (battle_over(bt) == BATTLE_LOST && !lost_said) {
                    fprintf(stderr, "walk: pass %d: the party has lost (the original goes to MARK.EXE's game-over screen)\n", n);
                    lost_said = 1;
                }
            } else {
                got = walk_pass(w, scr, clock0 + (uint32_t)n / 3, port60);
                if ((got & WALK_PASS_UNBUILT) && walk_unbuilt(w))
                    fprintf(stderr, "walk: pass %d: not built: %s\n", n, walk_unbuilt(w));
            }
        }
        if (!bt && (got & WALK_PASS_BATTLE) && !(bt = battle_begin(w, scr)))
            rc = 1;
        for (int i = 0; i < ndump; i++)
            if (dump_pass[i] == n && write_raw(scr, dump_file[i]) != 0) {
                fprintf(stderr, "cannot write %s\n", dump_file[i]);
                rc = 1;
            }
    }
    battle_free(bt);
    walk_free(w);
    if (rc == 0 && ppm && screen_write_ppm(scr, ppm) != 0)
        rc = 1;
    return rc;
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
    int rc = 0, rounds = 0, again = 0;
    for (;;) {
        printf("game: title (round %d)\n", rounds);
        title_result r = play_title(scr);
        if (r == TITLE_RUNNING) {   /* window closed (or the title could not start) */
            rc = platform_poll() ? 1 : 0;
            break;
        }
        if (r == TITLE_TO_MENU) {
            int stop = 0;
            while (!stop) {
                printf("game: menu\n");
                int slot = MENU_NONE;
                menu_result c = play_menu(scr, &slot);
                if (c == MENU_NONE) {
                    rc = platform_poll() ? 1 : 0;   /* window closed (or the menu could not start) */
                    break;
                }
                if (c == MENU_CREDITS) {   /* CAST.SL, which ends by starting the menu again */
                    printf("game: credits\n");
                    int q = play_credits(scr);
                    if (q != 0) {
                        rc = q < 0 ? 1 : 0;
                        break;
                    }
                    continue;
                }
                if (c == MENU_LOAD && slot != MENU_NONE) {   /* the slot's files into MAN_INFO.NOW and __1.NOW, then WALK.SL */
                    if (menu_load_save(slot) != 0) {
                        printf("game: slot %d could not be loaded, stopping\n", slot);
                        rc = 1;
                        break;
                    }
                    printf("game: slot %d loaded\n", slot);
                }
                if (c == MENU_NEW_GAME) {   /* __1.AL1, __1.AL2 and MAN_INFO.ALL into __1.NOW and MAN_INFO.NOW, then WALK.SL */
                    if (menu_new_game() != 0) {
                        printf("game: the new game's files could not be written, stopping\n");
                        rc = 1;
                        break;
                    }
                }
                if (c == MENU_NEW_GAME || (c == MENU_LOAD && slot != MENU_NONE)) {   /* WALK.SL */
                    printf(c == MENU_NEW_GAME ? "game: new game (WALK.SL)\n" : "game: WALK.SL\n");
                    int q = play_walk(scr);
                    if (q == 2) {   /* the party fell: MARK.EXE's game-over screen, then its title as after any start */
                        printf("game: the party has lost: game over\n");
                        if (play_gameover(scr) == 0)
                            again = 1;
                        break;
                    }
                    if (q == 3) {   /* key 4's yes: MARK.EXE, its title */
                        again = 1;
                        break;
                    }
                    if (q == 4) {   /* the game's end: ENDING.SL, then (when it is all built) DOS */
                        printf("game: the end (ENDING.SL)\n");
                        rc = play_ending(scr);
                        break;
                    }
                    rc = q < 0 ? 1 : 0;
                    break;
                }
                if (slot != MENU_NONE)
                    printf("game: slot %d picked\n", slot);
                printf("game: the menu chose %s; that part is not built yet, stopping\n", menu_item_name(c));
                stop = 1;
            }
            if (again) {
                again = 0;
                continue;
            }
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
        else if (!strcmp(argv[i], "--write-dir") && i + 1 < argc) {
            g_write_dir = argv[++i];
            assets_set_write_dir(g_write_dir);
        }
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
        else if (!strcmp(argv[i], "--credits-wipe-dump") && i + 1 < argc && g_cwipe_count < MAX_DUMPS) {
            char *arg = argv[++i];
            char *colon = strchr(arg, ':');
            if (!colon) {
                fprintf(stderr, "--credits-wipe-dump wants N:FILE\n");
                return 2;
            }
            *colon = 0;
            g_cwipe_n[g_cwipe_count] = atoi(arg);
            g_cwipe_file[g_cwipe_count++] = colon + 1;
        } else if (!strcmp(argv[i], "--credits-key") && i + 1 < argc && g_credits_nkeys < MAX_SCRIPT) {
            int n;
            unsigned code;
            if (sscanf(argv[++i], "%d:%i", &n, &code) != 2) {
                fprintf(stderr, "--credits-key wants N:CODE\n");
                return 2;
            }
            g_credits_keys[g_credits_nkeys].pass = n;
            g_credits_keys[g_credits_nkeys++].code = code;
        } else if (!strcmp(argv[i], "--walk-key") && i + 1 < argc && g_walk_nkeys < MAX_SCRIPT) {
            int n;
            unsigned code;
            if (sscanf(argv[++i], "%d:%i", &n, &code) != 2) {
                fprintf(stderr, "--walk-key wants N:CODE\n");
                return 2;
            }
            g_walk_keys[g_walk_nkeys].pass = n;
            g_walk_keys[g_walk_nkeys++].code = code;
        } else if (!strcmp(argv[i], "--screen-from") && i + 1 < argc)
            g_screen_from = argv[++i];
        else if (!strcmp(argv[i], "--walk-clock") && i + 1 < argc) {
            ++i;
            g_walk_clock = !strcmp(argv[i], "real") ? -1 : strtol(argv[i], NULL, 0);
            if (g_walk_clock < -1) {
                fprintf(stderr, "--walk-clock wants a BIOS tick count or 'real'\n");
                return 2;
            }
        }
        else if (!strcmp(argv[i], "--stepped"))
            g_stepped = 1;
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
                    "usage: %s [--data DIR] [--scale N] [--scene title|gmain|intro0|intro1|intro2|intro3|intro4|intro5|intro6|intro|game|menu|credits|walk|ending] [--ppm FILE]\n"
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
    if (!strcmp(scene, "credits")) {
        if (g_cwipe_count > 0 || ndump > 0 || g_credits_nkeys > 0 || g_stepped)
            return run_credits_stepped(&screen, ndump, dump_pass, dump_file);
        if (init_platform(scale) != 0)
            return 1;
        int rc = credits_start_screen(&screen);
        if (rc == 0)
            rc = play_credits(&screen);
        printf("credits: %s\n", rc == 0 ? "ended" : rc > 0 ? "closed" : "could not start");
        shutdown_platform();
        return rc < 0 ? 1 : 0;
    }
    if (!strcmp(scene, "ending")) {
        if (init_platform(scale) != 0)
            return 1;
        int rc = play_ending(&screen);
        shutdown_platform();
        (void)rc;
        return 0;
    }
    if (!strcmp(scene, "walk")) {
        if (ndump > 0 || ppm)
            return run_walk_stepped(&screen, ndump, dump_pass, ppm, dump_file);
        if (init_platform(scale) != 0)
            return 1;
        int rc = walk_start_screen(&screen);
        if (rc == 0)
            rc = play_walk(&screen);
        if (rc == 2) {   /* lost: MARK.EXE's game-over screen; its title is not started from here */
            printf("walk: the party has lost\n");
            play_gameover(&screen);
            rc = 1;
        }
        if (rc == 3) {   /* key 4's yes: the original starts MARK.EXE's title, which is not started from here */
            printf("walk: left\n");
            rc = 1;
        }
        if (rc == 4) {   /* the game's end: ENDING.SL */
            printf("walk: ended\n");
            play_ending(&screen);
            rc = 1;
        }
        printf("walk: %s\n", rc == 1 ? "closed" : "could not start");
        shutdown_platform();
        return rc < 0 ? 1 : 0;
    }
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
