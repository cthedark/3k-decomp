/* The settings file (3k.ini): `key=value` lines; `#` or `;` starts a comment line; `[section]` lines are allowed and ignored.
 * Keys: `language` (zh, en or ko; default zh) and `localization` (the localization directory; default "localization"). */
#ifndef TK_SETTINGS_H
#define TK_SETTINGS_H

typedef struct {
    char language[8];
    char localization[512];
} settings_t;

/* The defaults. */
void settings_default(settings_t *s);
/* Read `path` over `s`. Returns 0, 1 if the file does not exist (s unchanged), or -1 on a line it cannot use (reported on stderr,
 * the other lines still read). */
int settings_load(settings_t *s, const char *path);

#endif
