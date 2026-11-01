/* Check a translation (localization/README.md): every id is one of the source tables', every character is in the fonts, and every
 * text fits the room the game gives it. Usage: build/l10n_check DATA LANG [LOCALIZATION_DIR]
 * Prints each problem and a summary; exit status 1 when there is a problem. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "assets.h"
#include "l10n.h"

static const char *const TABLES[] = {"messages", "fields", "items", "spells", "code", "pictures"};
static int problems = 0;

static void problem(const char *id, const char *what, const char *text)
{
    printf("%s: %s: %s\n", id, what, text);
    problems++;
}

/* Is `id` in source table `name`? Also gives its `bytes` column. */
static int source_has(const char *dir, const char *name, const char *id, char *bytes, size_t cap)
{
    char path[1024], line[4096];
    snprintf(path, sizeof path, "%s/source/%s.tsv", dir, name);
    FILE *fp = fopen(path, "r");
    if (!fp)
        return 0;
    int found = 0;
    size_t n = strlen(id);
    while (!found && fgets(line, sizeof line, fp))
        if (!strncmp(line, id, n) && line[n] == '\t') {
            char *b = line + n + 1, *t = strchr(b, '\t');
            if (t)
                *t = 0;
            snprintf(bytes, cap, "%s", b);
            found = 1;
        }
    fclose(fp);
    return found;
}

static int starts(const char *s, const char *p)
{
    return !strncmp(s, p, strlen(p));
}

/* The width a text may take, in pixels, and whether the narrow font may be used; 0 for no limit (messages). */
static unsigned room(const char *id, const char *bytes, int *narrow_ok)
{
    *narrow_ok = 1;
    if (starts(id, "msg."))
        return 0;
    if (starts(id, "label."))
        return 32;
    if (!strcmp(id, "code.yes") || !strcmp(id, "code.no"))
        return 16;
    if (starts(id, "menu."))
        return 120;
    if (starts(id, "story"))
        return 320;
    if (starts(id, "ending1t."))
        return 236;
    if (starts(id, "ending4t."))
        return 220;
    if (starts(id, "shout."))
        return 136;
    if (!strcmp(id, "title.line"))
        return 112;
    if (!strcmp(id, "gameover"))
        return 93;
    *narrow_ok = starts(id, "item.") || starts(id, "spell.") || starts(id, "member.");
    return 8u * (unsigned)atoi(bytes);
}

int main(int argc, char **argv)
{
    if (argc < 3) {
        fprintf(stderr, "usage: %s DATA LANG [LOCALIZATION_DIR]\n", argv[0]);
        return 2;
    }
    const char *dir = argc > 3 ? argv[3] : "localization";
    int lang = l10n_lang_of(argv[2]);
    if (lang <= 0) {
        fprintf(stderr, "LANG must be en or ko\n");
        return 2;
    }
    assets_set_dir(argv[1]);
    if (l10n_init((l10n_lang)lang, dir) != 0)
        return 2;
    unsigned rows = 0, boxes = 0, longest = 0;
    char longest_id[32] = "";
    for (size_t t = 0; t < sizeof TABLES / sizeof *TABLES; t++) {
        char path[1024], line[4096];
        snprintf(path, sizeof path, "%s/%s/%s.tsv", dir, argv[2], TABLES[t]);
        FILE *fp = fopen(path, "r");
        if (!fp)
            continue;
        while (fgets(line, sizeof line, fp)) {
            line[strcspn(line, "\r\n")] = 0;
            char *tab = strchr(line, '\t');
            if (line[0] == '#' || !tab || starts(line, "id\t"))
                continue;
            *tab = 0;
            const char *id = line, *text = tab + 1;
            if (!*text)
                continue;
            rows++;
            char bytes[32];
            if (!source_has(dir, TABLES[t], id, bytes, sizeof bytes)) {
                problem(id, "not an id of the source table", TABLES[t]);
                continue;
            }
            size_t len = strlen(text);
            for (const char *s = text; *s;) {
                if (s[0] == '\\' && s[1] == 'n') {
                    s += 2;
                    continue;
                }
                const char *at = s;
                uint32_t c = l10n_next(&s);
                l10n_glyph g;
                if (c < 0x80 && strchr("@{|}~", (int)c)) {   /* RPG.LCP's font has other marks there (a circle, corner brackets) */
                    char what[64];
                    snprintf(what, sizeof what, "'%c' is drawn as another mark in the game's font", (int)c);
                    problem(id, what, text);
                }
                if (l10n_glyph_of(c, L10N_FONT_NORMAL, &g) != 0 && l10n_glyph_of(c, L10N_FONT_NARROW, &g) != 0) {
                    char what[64];
                    snprintf(what, sizeof what, "no glyph for U+%04X at byte %d", (unsigned)c, (int)(at - text));
                    problem(id, what, text);
                }
            }
            int narrow_ok;
            unsigned limit = room(id, bytes, &narrow_ok);
            if (limit) {
                unsigned w = l10n_width(text, len, L10N_FONT_NORMAL), wn = l10n_width(text, len, L10N_FONT_NARROW);
                if (w > limit && (!narrow_ok || wn > limit)) {
                    char what[96];
                    snprintf(what, sizeof what, "%u pixels (%u narrow) in %u", w, wn, limit);
                    problem(id, what, text);
                }
                if ((starts(id, "item.") || starts(id, "spell.")) && l10n_width(text, len, L10N_FONT_NARROW) / 6 > 16)
                    problem(id, "more than 16 characters", text);
            } else {   /* a message: how many boxes of three 320-pixel rows */
                unsigned n = l10n_wrap(text, 320, L10N_FONT_NORMAL, NULL, 0), b = (n + 2) / 3;
                boxes += b;
                if (b > longest) {
                    longest = b;
                    snprintf(longest_id, sizeof longest_id, "%s", id);
                }
            }
        }
        fclose(fp);
    }
    printf("%s: %u texts, %u message boxes (the longest %s, %u boxes), %d problems\n", argv[2], rows, boxes, longest_id, longest,
           problems);
    l10n_free();
    return problems ? 1 : 0;
}
