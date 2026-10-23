/* Dump what the WALK.SL port builds, for comparing with the original and with the Python model:
 *
 *   build/walk_dump DATA_DIR scene OUT          the loaded scene (walk_scene_dump's layout)
 *   build/walk_dump DATA_DIR first OUT START    the first screen as a raw dump (64,000 pixels then the 768-byte palette),
 *                                               drawn over START, a raw dump of the picture the menu left
 *   build/walk_dump DATA_DIR idle OUT N TICKBASE TICKDIV START [PASS:CODE ...]
 *                                               N idle passes (walk.h) at the clock TICKBASE + pass / TICKDIV, drawn over START;
 *                                               OUT gets, after each pass, 64,000 pixels, the 768-byte palette and
 *                                               walk_state_dump's WALK_STATE_BYTES (the state the screen does not show)
 *
 * Exit status 0 on success, 1 if the scene cannot be loaded (the message is on stderr), 2 for a usage error.
 * A debugging and checking aid (tools/check_walk.sh); not part of the game. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "assets.h"
#include "battle.h"
#include "gameover.h"
#include "sfx.h"
#include "walk.h"

enum { DUMP_STATE = 4 + 1000 + 0x124 };   /* "battle" mode: the state after each pass (see below); at least BATTLE_STATE_BYTES */
static int read_raw(screen_t *scr, const char *path)
{
    uint8_t *b = malloc(64768);
    FILE *f = fopen(path, "rb");
    if (!b || !f || fread(b, 1, 64768, f) != 64768) {
        if (f)
            fclose(f);
        free(b);
        return -1;
    }
    fclose(f);
    memcpy(scr->pix, b, 64000);
    for (int i = 0; i < 256; i++)
        memcpy(scr->pal[i], b + 64000 + 3 * i, 3);
    free(b);
    return 0;
}

/* The keyboard port's value over the passes: --key style PASS:CODE arguments, the value set at pass P holds from P on. */
static struct { int pass; unsigned code; } g_keys[4096];
static int g_nkeys;

static unsigned port_at(int pass)
{
    unsigned v = 0;
    int best = -1;
    for (int i = 0; i < g_nkeys; i++)
        if (g_keys[i].pass <= pass && g_keys[i].pass >= best) {
            best = g_keys[i].pass;
            v = g_keys[i].code;
        }
    return v;
}

/* The mouse over the passes (m:PASS:X:Y:BUTTONS arguments): none given means no driver; else the earliest holds until the next. */
static struct { int pass, x, y; unsigned b; } g_mice[1024];
static int g_nmice;

static void mouse_at(walk_t *w, int pass)
{
    int k = -1;
    for (int i = 0; i < g_nmice; i++) {   /* the latest at or before `pass`; before the first one, the first one */
        int ok = g_mice[i].pass <= pass;
        if (k < 0 || (ok && (g_mice[k].pass > pass || g_mice[i].pass >= g_mice[k].pass)) ||
            (!ok && g_mice[k].pass > pass && g_mice[i].pass < g_mice[k].pass))
            k = i;
    }
    if (k >= 0)
        walk_set_mouse(w, 1, g_mice[k].x, g_mice[k].y, g_mice[k].b);
}

/* f:PATH: each step of the picker face's flash (battle_set_show) as the pass number (int32, little-endian) and the 64,000 pixels */
static FILE *g_flash;
static int g_pass;

static void flash_step(void *ctx, screen_t *scr)
{
    (void)ctx;
    uint8_t p[4] = {(uint8_t)g_pass, (uint8_t)(g_pass >> 8), (uint8_t)(g_pass >> 16), (uint8_t)(g_pass >> 24)};
    if (g_flash && (fwrite(p, 1, 4, g_flash) != 4 || fwrite(scr->pix, 1, sizeof scr->pix, g_flash) != sizeof scr->pix)) {
        fclose(g_flash);
        g_flash = NULL;
    }
}

/* sb, s:PATH: a Sound Blaster (tools/battle_check.py --sb). Its busy word follows tools/sb_stub.py's model: a sound started in pass P
 * plays until pass P + sfx_passes; each sound started or cut while one plays is written to PATH as "PASS play FILE OFFSET" or
 * "PASS cut". */
static struct {
    FILE *log;
    sfx_bank_t *bank;
    int playing, until;
} g_sb;

static int sb_busy(void *ctx)
{
    (void)ctx;
    return g_sb.playing && g_pass < g_sb.until;
}

static void sb_play(void *ctx, unsigned file, unsigned off)
{
    size_t avail = 0;
    const uint8_t *blk = sfx_block(g_sb.bank, file, off, &avail);
    if (g_sb.log)
        fprintf(g_sb.log, "%d play %s %#x\n", g_pass, sfx_file_name(file), off);
    g_sb.playing = 1;
    g_sb.until = g_pass + (int)(blk ? sfx_passes(blk, avail) : 1);
    (void)ctx;
}

static void sb_stop(void *ctx)
{
    if (sb_busy(ctx) && g_sb.log)
        fprintf(g_sb.log, "%d cut\n", g_pass);
    g_sb.playing = 0;
}

static int write_bytes(const char *path, const void *a, size_t na, const void *b, size_t nb)
{
    FILE *f = fopen(path, "wb");
    if (!f)
        return -1;
    int ok = fwrite(a, 1, na, f) == na && (nb == 0 || fwrite(b, 1, nb, f) == nb);
    return (fclose(f) == 0 && ok) ? 0 : -1;
}

int main(int argc, char **argv)
{
    if (argc < 4)
        return 2;
    assets_set_dir(argv[1]);
    const char *mode = argv[2], *out = argv[3];
    if (!strcmp(mode, "scene")) {
        walk_scene_t *s = walk_scene_load();
        if (!s)
            return 1;
        int rc = walk_scene_dump(s, out) == 0 ? 0 : 2;
        walk_scene_free(s);
        return rc;
    }
    walk_t *w = walk_create();
    if (!w)
        return 1;
    int rc = 2;
    if ((!strcmp(mode, "idle") || !strcmp(mode, "battle")) && argc >= 8) {
        /* build/walk_dump DATA idle OUT N TICKBASE TICKDIV START: N idle passes at clock TICKBASE + pass / TICKDIV, drawn
         * over START (after the interface); OUT gets, after each pass, 64,000 pixels then the 768-byte palette.
         * "battle" instead of "idle": a pass that touches an enemy starts the battle (battle.h) and the later passes are the battle's;
         * after each pass OUT gets the pixels, the palette, a byte (0 world, 1 battle) and BATTLE_STATE_BYTES (a world pass: the rand
         * state, then zeros). */
        int battle_mode = !strcmp(mode, "battle");
        int fm_shown = 0;
        battle_t *bt = NULL;
        static screen_t scr;
        if (argc - 8 > (int)(sizeof g_keys / sizeof g_keys[0])) {
            fprintf(stderr, "idle: at most %d key arguments\n", (int)(sizeof g_keys / sizeof g_keys[0]));
            walk_free(w);
            return 2;
        }
        for (int a = 8; a < argc; a++) {
            int p;
            unsigned c;
            if (!strcmp(argv[a], "fm") || !strcmp(argv[a], "fm-shown")) {   /* an FM card, music on (tools/battle_check.py --opl);
                                                                             * "fm-shown": START already shows its button pressed */
                walk_set_sound(w, 0x81, *walk_sb(w));
                fm_shown |= argv[a][2] != 0;
                continue;
            }
            if (!strcmp(argv[a], "sb") || !strcmp(argv[a], "sb-shown")) {   /* a Sound Blaster, sounds on; "sb-shown": START shows
                                                                             * its button pressed already */
                static const walk_sfx_t ops = {sb_play, sb_busy, sb_stop, NULL};
                if (!g_sb.bank && !(g_sb.bank = sfx_bank_load())) {
                    walk_free(w);
                    return 1;
                }
                walk_set_sound(w, *walk_fm(w), 0x81);
                walk_set_sfx(w, &ops);
                fm_shown |= argv[a][2] != 0;
                continue;
            }
            if (!strncmp(argv[a], "s:", 2)) {
                if (!(g_sb.log = fopen(argv[a] + 2, "w"))) {
                    fprintf(stderr, "idle: cannot write %s\n", argv[a] + 2);
                    walk_free(w);
                    return 2;
                }
                continue;
            }
            if (!strncmp(argv[a], "f:", 2)) {
                if (!(g_flash = fopen(argv[a] + 2, "wb"))) {
                    fprintf(stderr, "idle: cannot write %s\n", argv[a] + 2);
                    walk_free(w);
                    return 2;
                }
                continue;
            }
            if (argv[a][0] == 'm') {   /* m:PASS:X:Y:BUTTONS, a mouse driver is there (tools/battle_check.py --mouse) */
                int x, y;
                if (sscanf(argv[a], "m:%d:%i:%i:%i", &p, &x, &y, &c) != 4 || g_nmice >= (int)(sizeof g_mice / sizeof g_mice[0])) {
                    fprintf(stderr, "idle: mouse arguments are m:PASS:X:Y:BUTTONS\n");
                    walk_free(w);
                    return 2;
                }
                g_mice[g_nmice].pass = p;
                g_mice[g_nmice].x = x;
                g_mice[g_nmice].y = y;
                g_mice[g_nmice++].b = c;
                continue;
            }
            if (sscanf(argv[a], "%d:%i", &p, &c) != 2) {
                fprintf(stderr, "idle: key arguments are PASS:CODE\n");
                walk_free(w);
                return 2;
            }
            g_keys[g_nkeys].pass = p;
            g_keys[g_nkeys++].code = c;
        }
        int n = atoi(argv[4]);
        unsigned long base = strtoul(argv[5], NULL, 0), div = strtoul(argv[6], NULL, 0);
        if (read_raw(&scr, argv[7]) != 0 || div == 0 || walk_draw_interface(&scr) != 0 || walk_reset(w, 0) != 0) {
            fprintf(stderr, "idle: cannot start\n");
            rc = 1;
        } else {
            if (!fm_shown)
                walk_sound_buttons(w, &scr);
            FILE *f = fopen(out, "wb");
            rc = f ? 0 : 2;
            for (int i = 0; f && i < n; i++) {
                if (battle_mode) {
                    uint32_t tk = (uint32_t)(base + (unsigned long)i / div);
                    g_pass = i;
                    mouse_at(w, i);
                    int ev = bt ? battle_pass(bt, &scr, port_at(i)) : walk_pass(w, &scr, tk, port_at(i));
                    if (!bt && (ev & WALK_PASS_BATTLE)) {
                        bt = battle_begin(w, &scr);
                        if (!bt) {
                            rc = 1;
                            break;
                        }
                        battle_set_show(bt, flash_step, NULL);
                    }
                    if (bt && battle_over(bt) == BATTLE_BACK) {   /* the same pass goes on in the world */
                        ev = walk_resume(w, tk, port_at(i), battle_result(bt));
                        battle_free(bt);
                        bt = NULL;
                    }
                    if (ev & WALK_PASS_UNBUILT)
                        fprintf(stderr, "battle: pass %d: %s\n", i,
                                bt ? (battle_unbuilt(bt) ? battle_unbuilt(bt) : "?") : (walk_unbuilt(w) ? walk_unbuilt(w) : "?"));
                    /* the kind of the next arrival: 0 world, 1 battle, 2 lost (the end), 3 the rewards bar, 4 a message box; then the rand
                     * state and, in the battle, BATTLE_STATE_BYTES - 4, in the world the villager table (1000) and the party table to
                     * the coins (0x124); zeros after */
                    uint8_t st[1 + DUMP_STATE] = {0};
                    st[0] = bt == NULL ? (uint8_t)walk_kind(w) : battle_over(bt) == BATTLE_LOST ? 2 : (uint8_t)battle_kind(bt);
                    if (bt) {
                        battle_state_dump(bt, st + 1);
                    } else {
                        uint32_t r = *walk_rng(w);
                        for (int j = 0; j < 4; j++)
                            st[1 + j] = (uint8_t)(r >> (8 * j));
                        memcpy(st + 5, walk_scene(w)->tab_a, 1000);
                        memcpy(st + 1005, walk_scene(w)->tab_b, 0x124);
                    }
                    if (fwrite(scr.pix, 1, sizeof scr.pix, f) != sizeof scr.pix || fwrite(scr.pal, 1, sizeof scr.pal, f) != sizeof scr.pal ||
                        fwrite(st, 1, sizeof st, f) != sizeof st)
                        rc = 2;
                    continue;
                }
                int ev = walk_pass(w, &scr, (uint32_t)(base + (unsigned long)i / div), port_at(i));
                if (ev & WALK_PASS_BATTLE)
                    fprintf(stderr, "idle: pass %d: a battle: the party touched an enemy\n", i);
                else if (ev)
                    fprintf(stderr, "idle: pass %d: %s\n", i, walk_unbuilt(w) ? walk_unbuilt(w) : "the unbuilt villager event");
                if (fwrite(scr.pix, 1, sizeof scr.pix, f) != sizeof scr.pix || fwrite(scr.pal, 1, sizeof scr.pal, f) != sizeof scr.pal)
                    rc = 2;
                uint8_t st[WALK_STATE_BYTES];
                walk_state_dump(w, st);
                if (fwrite(st, 1, sizeof st, f) != sizeof st)
                    rc = 2;
            }
            if (f)
                fclose(f);
        }
        battle_free(bt);
        if (g_flash && fclose(g_flash) != 0)
            rc = 2;
        g_flash = NULL;
        if (g_sb.log && fclose(g_sb.log) != 0)
            rc = 2;
        g_sb.log = NULL;
        sfx_bank_free(g_sb.bank);
        g_sb.bank = NULL;
    } else if (!strcmp(mode, "gameover") && argc >= 6) {
        /* build/walk_dump DATA gameover OUT N START [PASS:CODE | m:PASS:X:Y:BUTTONS ...]: MARK.EXE's game-over screen from the screen
         * START (64,000 pixels then the palette); OUT gets the screen after each pass, up to N or the pass a key or a button ends it */
        static screen_t scr;
        gameover_t *g = NULL;
        for (int a = 6; a < argc; a++) {
            int p, x, y;
            unsigned c;
            if (sscanf(argv[a], "m:%d:%i:%i:%i", &p, &x, &y, &c) == 4 && g_nmice < (int)(sizeof g_mice / sizeof g_mice[0])) {
                g_mice[g_nmice].pass = p;
                g_mice[g_nmice].x = x;
                g_mice[g_nmice].y = y;
                g_mice[g_nmice++].b = c;
            } else if (sscanf(argv[a], "%d:%i", &p, &c) == 2 && g_nkeys < (int)(sizeof g_keys / sizeof g_keys[0])) {
                g_keys[g_nkeys].pass = p;
                g_keys[g_nkeys++].code = c;
            } else {
                fprintf(stderr, "gameover: arguments are PASS:CODE or m:PASS:X:Y:BUTTONS\n");
                walk_free(w);
                return 2;
            }
        }
        if (read_raw(&scr, argv[5]) != 0 || !(g = gameover_begin(&scr))) {
            fprintf(stderr, "gameover: cannot start\n");
            rc = 1;
        } else {
            FILE *f = fopen(out, "wb");
            rc = f ? 0 : 2;
            for (int i = 0; f && i < atoi(argv[4]); i++) {
                gameover_pass(g, &scr);
                if (fwrite(scr.pix, 1, sizeof scr.pix, f) != sizeof scr.pix || fwrite(scr.pal, 1, sizeof scr.pal, f) != sizeof scr.pal)
                    rc = 2;
                unsigned mb = 0;
                int mouse = g_nmice > 0;
                for (int k = 0, best = -1; k < g_nmice; k++)
                    if (g_mice[k].pass <= i && g_mice[k].pass >= best) {
                        best = g_mice[k].pass;
                        mb = g_mice[k].b;
                    }
                if (gameover_key(port_at(i)) || (mouse && gameover_mouse(mb)))
                    break;
            }
            if (f)
                fclose(f);
        }
        gameover_free(g);
    } else if (!strcmp(mode, "zoom") && argc >= 5) {
        /* build/walk_dump DATA zoom OUT START: the start's zoom over START (after the interface); OUT gets the screen after each of
         * its WALK_ZOOM_STEPS steps (tools/zoom_check.py) */
        static screen_t scr;
        FILE *f = NULL;
        if (read_raw(&scr, argv[4]) != 0 || walk_draw_interface(&scr) != 0 || walk_reset(w, 0) != 0 || !(f = fopen(out, "wb"))) {
            fprintf(stderr, "zoom: cannot start\n");
            rc = 1;
        } else {
            rc = 0;
            for (int k = 0; k < WALK_ZOOM_STEPS; k++) {
                walk_zoom_step(w, &scr, k);
                if (fwrite(scr.pix, 1, sizeof scr.pix, f) != sizeof scr.pix || fwrite(scr.pal, 1, sizeof scr.pal, f) != sizeof scr.pal)
                    rc = 2;
            }
        }
        if (f && fclose(f) != 0)
            rc = 2;
    } else if (!strcmp(mode, "first") && argc >= 5) {
        static screen_t scr;
        if (read_raw(&scr, argv[4]) != 0) {
            fprintf(stderr, "cannot read %s (want 64,768 bytes)\n", argv[4]);
        } else if (walk_first_screen(w, &scr) == 0) {
            rc = write_bytes(out, scr.pix, sizeof scr.pix, scr.pal, sizeof scr.pal) == 0 ? 0 : 2;
        } else {
            rc = 1;
        }
    }
    walk_free(w);
    return rc;
}
