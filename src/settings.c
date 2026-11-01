#include "settings.h"

#include <ctype.h>
#include <stdio.h>
#include <string.h>

void settings_default(settings_t *s)
{
    memset(s, 0, sizeof *s);
    strcpy(s->language, "zh");
    strcpy(s->localization, "localization");
}

/* `p` without its leading and trailing white space (changes the string) */
static char *trim(char *p)
{
    while (isspace((unsigned char)*p))
        p++;
    size_t n = strlen(p);
    while (n && isspace((unsigned char)p[n - 1]))
        p[--n] = 0;
    return p;
}

int settings_load(settings_t *s, const char *path)
{
    FILE *fp = fopen(path, "r");
    if (!fp)
        return 1;
    char line[1024];
    int rc = 0, no = 0;
    while (fgets(line, sizeof line, fp)) {
        no++;
        char *p = trim(line);
        if (!*p || *p == '#' || *p == ';' || *p == '[')
            continue;
        char *eq = strchr(p, '=');
        if (!eq) {
            fprintf(stderr, "%s:%d: not a key=value line\n", path, no);
            rc = -1;
            continue;
        }
        *eq = 0;
        char *key = trim(p), *val = trim(eq + 1);
        if (!strcmp(key, "language") && strlen(val) < sizeof s->language)
            strcpy(s->language, val);
        else if (!strcmp(key, "localization") && strlen(val) < sizeof s->localization)
            strcpy(s->localization, val);
        else {
            fprintf(stderr, "%s:%d: unknown key or value too long: %s\n", path, no, key);
            rc = -1;
        }
    }
    fclose(fp);
    return rc;
}
