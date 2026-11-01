/* The translated modes (localization/README.md): the language, its text tables and its fonts. In Chinese (the default) nothing here
 * is loaded and every lookup returns NULL, so the game draws exactly what the original draws. */
#ifndef TK_L10N_H
#define TK_L10N_H

#include <stddef.h>
#include <stdint.h>

typedef enum { L10N_ZH = 0, L10N_EN = 1, L10N_KO = 2 } l10n_lang;

/* The language from its name ("zh", "en" or "ko"); -1 for any other. */
int l10n_lang_of(const char *name);

/* Load language `lang` from directory `dir` (its `source/` tables, `<lang>/` tables and `fonts/`). The data directory must be set
 * first (assets_set_dir): the half-width font is RPG.LCP's. Returns 0, or -1 with a message on stderr (the language then stays
 * Chinese). Loading Chinese unloads. */
int l10n_init(l10n_lang lang, const char *dir);
void l10n_free(void);
l10n_lang l10n_current(void);

/* The translation of a table id ("msg.0001", "label.03", "story1.2" ...); NULL in Chinese or when the id has none. */
const char *l10n_text(const char *id);
/* The translation of MES_TXT.LCC's message `msg` (1 ..), "msg.NNNN". */
const char *l10n_message(unsigned msg);
/* The translation of the string the original draws from bytes `s` (`n` bytes; trailing spaces and zero bytes are not compared):
 * matched against the `big5` column of the source tables. NULL in Chinese or when there is none. */
const char *l10n_big5(const uint8_t *s, size_t n);

/* One character of a translated text: 14 rows of `w` pixels, `bw` bytes a row, the most significant bit the leftmost pixel. */
typedef struct {
    unsigned w, bw;
    const uint8_t *bits;
} l10n_glyph;

/* Fonts: L10N_FONT_NORMAL draws ASCII with RPG.LCP's 8 x 15 characters (the original's half-width font) and Hangul and the other
 * marks 16 wide; L10N_FONT_NARROW draws ASCII 6 wide (for names that do not fit otherwise). */
enum { L10N_FONT_NORMAL = 0, L10N_FONT_NARROW = 1 };

/* The next code point of UTF-8 text `*s` (moved past it); 0 at the end. A malformed byte gives U+FFFD. */
uint32_t l10n_next(const char **s);
/* The glyph of code point `cp` in `font`: 0, or -1 when the fonts have none (`g` is then an empty 8-wide glyph). A space is
 * always there (8 wide, 6 narrow). */
int l10n_glyph_of(uint32_t cp, int font, l10n_glyph *g);
/* The width in pixels of `text` up to its end or `len` bytes (a "\n" counts as nothing). */
unsigned l10n_width(const char *text, size_t len, int font);

/* Lay `text` out in lines at most `width` pixels wide: breaks at spaces where it can, inside a word only when the word alone is
 * too wide; "\n" (the two characters) breaks a line. Fills up to `max` (start, length) pairs; returns the number of lines. */
typedef struct {
    const char *start;
    size_t len;
} l10n_line;
unsigned l10n_wrap(const char *text, unsigned width, int font, l10n_line *lines, unsigned max);

/* Draw `len` bytes of `text` into the `pw` x 14 one-byte-a-pixel picture `px` from x = `x0` (the picture is not cleared): each
 * set pixel becomes the value cellfn(ctx, glyph width, index in the glyph's 14-row cell) gives, so the caller colours the way its
 * original code does. Pixels past the picture are dropped. `*end_x` (when not NULL) gets the x after the text. Returns 0, or -1 if a
 * character was missing (drawn blank). */
int l10n_draw(const char *text, size_t len, int font, uint8_t *px, unsigned pw, unsigned x0,
              uint8_t (*cellfn)(void *ctx, unsigned gw, unsigned index), void *ctx, unsigned *end_x);

#endif
