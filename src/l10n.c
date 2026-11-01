#include "l10n.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "assets.h"

/* ---- a string map ------------------------------------------------------------------------------------------------ */

typedef struct entry {
    char *key, *val;
    struct entry *next;
} entry;

typedef struct {
    entry **slot;
    size_t n;
} map;

static size_t hash(const char *s)
{
    size_t h = 1469598103u;
    while (*s)
        h = (h ^ (unsigned char)*s++) * 16777619u;
    return h;
}

static void map_init(map *m, size_t n)
{
    m->n = n;
    m->slot = calloc(n, sizeof *m->slot);
}

static const char *map_get(const map *m, const char *key)
{
    if (!m->slot)
        return NULL;
    for (const entry *e = m->slot[hash(key) % m->n]; e; e = e->next)
        if (!strcmp(e->key, key))
            return e->val;
    return NULL;
}

/* keeps the first value given for a key */
static void map_put(map *m, const char *key, const char *val)
{
    if (!m->slot || map_get(m, key))
        return;
    entry *e = malloc(sizeof *e);
    if (!e)
        return;
    e->key = strdup(key);
    e->val = strdup(val);
    size_t h = hash(key) % m->n;
    e->next = m->slot[h];
    m->slot[h] = e;
}

static void map_free(map *m)
{
    if (!m->slot)
        return;
    for (size_t i = 0; i < m->n; i++)
        for (entry *e = m->slot[i], *nx; e; e = nx) {
            nx = e->next;
            free(e->key);
            free(e->val);
            free(e);
        }
    free(m->slot);
    m->slot = NULL;
}

/* ---- fonts ---------------------------------------------------------------------------------------------------------- */

typedef struct {
    unsigned w, h, bw, n;
    uint32_t *cp;
    uint8_t *bits;
} tkf;

static int tkf_load(tkf *f, const char *path)
{
    memset(f, 0, sizeof *f);
    FILE *fp = fopen(path, "rb");
    if (!fp)
        return -1;
    uint8_t head[12];
    int ok = fread(head, 1, 12, fp) == 12 && !memcmp(head, "TKF1", 4);
    if (ok) {
        f->w = head[4] | head[5] << 8;
        f->h = head[6] | head[7] << 8;
        f->n = (unsigned)head[8] | (unsigned)head[9] << 8 | (unsigned)head[10] << 16 | (unsigned)head[11] << 24;
        f->bw = (f->w + 7) / 8;
        ok = f->h == 14 && f->w <= 16 && f->n < 100000;
    }
    if (ok) {
        f->cp = malloc(sizeof *f->cp * f->n);
        f->bits = malloc((size_t)f->n * f->h * f->bw);
        ok = f->cp && f->bits;
        for (unsigned i = 0; ok && i < f->n; i++) {
            uint8_t b[4];
            ok = fread(b, 1, 4, fp) == 4;
            f->cp[i] = (uint32_t)b[0] | (uint32_t)b[1] << 8 | (uint32_t)b[2] << 16 | (uint32_t)b[3] << 24;
        }
        ok = ok && fread(f->bits, f->h * f->bw, f->n, fp) == f->n;
    }
    fclose(fp);
    if (!ok) {
        free(f->cp);
        free(f->bits);
        memset(f, 0, sizeof *f);
        return -1;
    }
    return 0;
}

static const uint8_t *tkf_find(const tkf *f, uint32_t cp)
{
    unsigned lo = 0, hi = f->n;
    while (lo < hi) {
        unsigned mid = (lo + hi) / 2;
        if (f->cp[mid] < cp)
            lo = mid + 1;
        else
            hi = mid;
    }
    return lo < f->n && f->cp[lo] == cp ? f->bits + (size_t)lo * f->h * f->bw : NULL;
}

/* ---- the state ------------------------------------------------------------------------------------------------------ */

#define FONT8_AT 0x1A6B7   /* RPG.LCP: 128 one-bit 8 x 15 characters (the message box's font) */

static struct {
    l10n_lang lang;
    map text;               /* id -> translation */
    map big5;               /* the original bytes in hex -> translation */
    uint8_t font8[128 * 15];
    tkf wide, narrow;
} S;

static const char *const TABLES[] = {"messages", "fields", "items", "spells", "code", "pictures"};

int l10n_lang_of(const char *name)
{
    if (!strcmp(name, "zh"))
        return L10N_ZH;
    if (!strcmp(name, "en"))
        return L10N_EN;
    if (!strcmp(name, "ko"))
        return L10N_KO;
    return -1;
}

l10n_lang l10n_current(void)
{
    return S.lang;
}

void l10n_free(void)
{
    map_free(&S.text);
    map_free(&S.big5);
    free(S.wide.cp);
    free(S.wide.bits);
    free(S.narrow.cp);
    free(S.narrow.bits);
    memset(&S, 0, sizeof S);
}

/* Split a line at tabs into at most `max` fields (the line is changed); returns the count. */
static int fields(char *line, char **f, int max)
{
    int n = 0;
    line[strcspn(line, "\r\n")] = 0;
    while (n < max) {
        f[n++] = line;
        char *t = strchr(line, '\t');
        if (!t)
            break;
        *t = 0;
        line = t + 1;
    }
    return n;
}

/* The hex of `s` without its trailing spaces and zero bytes, into `out` (2 n + 1 bytes). */
static void big5_key(const uint8_t *s, size_t n, char *out)
{
    while (n && (s[n - 1] == ' ' || s[n - 1] == 0))
        n--;
    static const char hex[] = "0123456789abcdef";
    for (size_t i = 0; i < n; i++)
        out[2 * i] = hex[s[i] >> 4], out[2 * i + 1] = hex[s[i] & 0xF];
    out[2 * n] = 0;
}

/* Read the translations of table `name` into S.text; then the source table's big5 column into S.big5 for the ids translated. */
static int load_table(const char *dir, const char *lang, const char *name)
{
    char path[1024], line[4096];
    snprintf(path, sizeof path, "%s/%s/%s.tsv", dir, lang, name);
    FILE *fp = fopen(path, "r");
    if (!fp)
        return 0;   /* a table not translated yet */
    int rows = 0;
    while (fgets(line, sizeof line, fp)) {
        char *f[3];
        if (line[0] == '#' || fields(line, f, 3) < 2 || !strcmp(f[0], "id") || !f[1][0])
            continue;
        map_put(&S.text, f[0], f[1]);
        rows++;
    }
    fclose(fp);
    snprintf(path, sizeof path, "%s/source/%s.tsv", dir, name);
    if (!(fp = fopen(path, "r")))
        return rows;
    while (fgets(line, sizeof line, fp)) {
        char *f[4];
        if (line[0] == '#' || fields(line, f, 4) < 4 || !strcmp(f[0], "id") || !f[3][0])
            continue;
        const char *t = map_get(&S.text, f[0]);
        if (!t)
            continue;
        /* trailing "20" / "00" pairs go, as in big5_key */
        size_t n = strlen(f[3]);
        while (n >= 2 && (!strncmp(f[3] + n - 2, "20", 2) || !strncmp(f[3] + n - 2, "00", 2)) && n % 2 == 0)
            n -= 2;
        f[3][n] = 0;
        map_put(&S.big5, f[3], t);
    }
    fclose(fp);
    return rows;
}

int l10n_init(l10n_lang lang, const char *dir)
{
    l10n_free();
    if (lang == L10N_ZH)
        return 0;
    static const char *const names[] = {"zh", "en", "ko"};
    char path[1024];
    if (assets_read_at("RPG.LCP", FONT8_AT, S.font8, sizeof S.font8) != 0) {
        fprintf(stderr, "l10n: RPG.LCP has no font at 0x%x\n", FONT8_AT);
        return -1;
    }
    snprintf(path, sizeof path, "%s/fonts/tk_hangul16.tkf", dir);
    if (tkf_load(&S.wide, path) != 0) {
        fprintf(stderr, "l10n: cannot read %s\n", path);
        l10n_free();
        return -1;
    }
    snprintf(path, sizeof path, "%s/fonts/tk_narrow6.tkf", dir);
    if (tkf_load(&S.narrow, path) != 0) {
        fprintf(stderr, "l10n: cannot read %s\n", path);
        l10n_free();
        return -1;
    }
    map_init(&S.text, 4099);
    map_init(&S.big5, 4099);
    int rows = 0;
    for (size_t i = 0; i < sizeof TABLES / sizeof *TABLES; i++)
        rows += load_table(dir, names[lang], TABLES[i]);
    if (rows == 0) {
        fprintf(stderr, "l10n: no translations in %s/%s\n", dir, names[lang]);
        l10n_free();
        return -1;
    }
    S.lang = lang;
    return 0;
}

const char *l10n_text(const char *id)
{
    return S.lang == L10N_ZH ? NULL : map_get(&S.text, id);
}

const char *l10n_message(unsigned msg)
{
    char id[16];
    snprintf(id, sizeof id, "msg.%04u", msg);
    return l10n_text(id);
}

const char *l10n_big5(const uint8_t *s, size_t n)
{
    if (S.lang == L10N_ZH || n == 0 || n > 0x100)
        return NULL;
    char key[2 * 0x100 + 1];
    big5_key(s, n, key);
    return key[0] ? map_get(&S.big5, key) : NULL;
}

/* ---- text ------------------------------------------------------------------------------------------------------------ */

uint32_t l10n_next(const char **s)
{
    const unsigned char *p = (const unsigned char *)*s;
    if (!*p)
        return 0;
    uint32_t c = *p++;
    int more = c < 0x80 ? 0 : (c & 0xE0) == 0xC0 ? 1 : (c & 0xF0) == 0xE0 ? 2 : (c & 0xF8) == 0xF0 ? 3 : -1;
    if (more < 0) {
        *s = (const char *)p;
        return 0xFFFD;
    }
    c &= more == 0 ? 0x7F : more == 1 ? 0x1F : more == 2 ? 0x0F : 0x07;
    for (int k = 0; k < more; k++, p++) {
        if ((*p & 0xC0) != 0x80) {
            *s = (const char *)p;
            return 0xFFFD;
        }
        c = c << 6 | (*p & 0x3F);
    }
    *s = (const char *)p;
    return c;
}

int l10n_glyph_of(uint32_t cp, int font, l10n_glyph *g)
{
    static const uint8_t blank[14 * 2];
    /* RPG.LCP's font draws ' as a low comma; its ` is a high quote: apostrophes and single quotes take that */
    if (font == L10N_FONT_NORMAL && (cp == '\'' || cp == 0x2018 || cp == 0x2019))
        cp = '`';
    if (cp >= 0x20 && cp < 0x7F) {
        if (font == L10N_FONT_NARROW) {
            const uint8_t *b = S.narrow.bits ? tkf_find(&S.narrow, cp) : NULL;
            if (b) {
                g->w = 6, g->bw = 1, g->bits = b;
                return 0;
            }
        }
        g->w = 8, g->bw = 1, g->bits = S.font8 + cp * 15;   /* rows 0 .. 13 of the 15 */
        return 0;
    }
    const uint8_t *b = S.wide.bits ? tkf_find(&S.wide, cp) : NULL;
    if (!b && font == L10N_FONT_NARROW && S.narrow.bits)
        b = tkf_find(&S.narrow, cp);
    if (b) {
        int narrow = !tkf_find(&S.wide, cp);
        g->w = narrow ? S.narrow.w : S.wide.w, g->bw = narrow ? S.narrow.bw : S.wide.bw, g->bits = b;
        return 0;
    }
    g->w = 8, g->bw = 2, g->bits = blank;
    return -1;
}

/* A "\n" (backslash, n) at `s`? */
static int is_break(const char *s, const char *end)
{
    return s + 1 < end && s[0] == '\\' && s[1] == 'n';
}

unsigned l10n_width(const char *text, size_t len, int font)
{
    const char *s = text, *end = text + len;
    unsigned w = 0;
    while (s < end && *s) {
        if (is_break(s, end)) {
            s += 2;
            continue;
        }
        l10n_glyph g;
        l10n_glyph_of(l10n_next(&s), font, &g);
        w += g.w;
    }
    return w;
}

unsigned l10n_wrap(const char *text, unsigned width, int font, l10n_line *lines, unsigned max)
{
    unsigned n = 0;
    const char *s = text;
    while (*s) {
        const char *start = s, *last_space = NULL, *p = s;
        unsigned w = 0;
        int forced = 0;
        while (*p) {
            if (is_break(p, p + 2)) {
                forced = 1;
                break;
            }
            const char *q = p;
            uint32_t c = l10n_next(&q);
            l10n_glyph g;
            l10n_glyph_of(c, font, &g);
            if (w + g.w > width && p > start)
                break;
            if (c == ' ')
                last_space = p;
            w += g.w;
            p = q;
        }
        const char *stop = p, *next = p;
        if (forced)
            next = p + 2;
        else if (*p && last_space && last_space > start)
            stop = last_space, next = last_space + 1;   /* break at the last space; the space itself goes */
        if (n < max) {
            lines[n].start = start;
            lines[n].len = (size_t)(stop - start);
        }
        n++;
        s = next;
        while (*s == ' ' && !forced)
            s++;   /* a line does not start with spaces */
    }
    return n;
}

int l10n_draw(const char *text, size_t len, int font, uint8_t *px, unsigned pw, unsigned x0,
              uint8_t (*cellfn)(void *ctx, unsigned gw, unsigned index), void *ctx, unsigned *end_x)
{
    const char *s = text, *end = text + len;
    unsigned x = x0;
    int missing = 0;
    while (s < end && *s) {
        if (is_break(s, end)) {
            s += 2;
            continue;
        }
        l10n_glyph g;
        if (l10n_glyph_of(l10n_next(&s), font, &g) != 0)
            missing = 1;
        for (unsigned r = 0; r < 14; r++)
            for (unsigned k = 0; k < g.w; k++)
                if ((g.bits[r * g.bw + k / 8] << (k % 8)) & 0x80 && x + k < pw)
                    px[r * pw + x + k] = cellfn(ctx, g.w, r * g.w + k);
        x += g.w;
    }
    if (end_x)
        *end_x = x;
    return missing ? -1 : 0;
}
