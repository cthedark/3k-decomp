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
            FILE *f = fopen(out, "wb");
            rc = f ? 0 : 2;
            for (int i = 0; f && i < n; i++) {
                if (battle_mode) {
                    uint32_t tk = (uint32_t)(base + (unsigned long)i / div);
                    int ev = bt ? battle_pass(bt, &scr, port_at(i)) : walk_pass(w, &scr, tk, port_at(i));
                    if (!bt && (ev & WALK_PASS_BATTLE)) {
                        bt = battle_begin(w, &scr);
                        if (!bt) {
                            rc = 1;
                            break;
                        }
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
                    st[0] = bt == NULL ? 0 : battle_over(bt) == BATTLE_LOST ? 2 : (uint8_t)battle_kind(bt);
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
    } else if (!strcmp(mode, "gameover") && argc >= 6) {
        /* build/walk_dump DATA gameover OUT N START: MARK.EXE's game-over screen from the screen START (64,000 pixels then the
         * palette); OUT gets the screen after each of N passes */
        static screen_t scr;
        gameover_t *g = NULL;
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
            }
            if (f)
                fclose(f);
        }
        gameover_free(g);
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
