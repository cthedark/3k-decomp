#include "assets.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const char *g_dir = "original_bin";

void assets_set_dir(const char *dir)
{
    g_dir = dir;
}

const char *assets_dir(void)
{
    return g_dir;
}

static FILE *open_variant(const char *name, int mode)
{
    size_t dl = strlen(g_dir), nl = strlen(name);
    char *path = malloc(dl + nl + 2);
    if (!path)
        return NULL;
    FILE *f = NULL;
    for (int v = 0; v < 3 && !f; v++) {
        memcpy(path, g_dir, dl);
        path[dl] = '/';
        for (size_t i = 0; i <= nl; i++) {
            unsigned char c = (unsigned char)name[i];
            path[dl + 1 + i] = (char)(v == 1 ? toupper(c) : v == 2 ? tolower(c) : c);
        }
        f = fopen(path, mode ? "r+b" : "rb");
    }
    free(path);
    return f;
}

uint8_t *assets_read(const char *name, size_t *size)
{
    FILE *f = open_variant(name, 0);
    if (!f)
        return NULL;
    if (fseek(f, 0, SEEK_END) != 0) {
        fclose(f);
        return NULL;
    }
    long n = ftell(f);
    if (n < 0 || fseek(f, 0, SEEK_SET) != 0) {
        fclose(f);
        return NULL;
    }
    uint8_t *buf = malloc(n ? (size_t)n : 1);
    if (!buf || fread(buf, 1, (size_t)n, f) != (size_t)n) {
        free(buf);
        fclose(f);
        return NULL;
    }
    fclose(f);
    *size = (size_t)n;
    return buf;
}

int assets_exists(const char *name)
{
    FILE *f = open_variant(name, 0);
    if (!f)
        return 0;
    fclose(f);
    return 1;
}

int assets_read_at(const char *name, size_t offset, void *dst, size_t len)
{
    FILE *f = open_variant(name, 0);
    if (!f)
        return -1;
    int rc = -1;
    if (fseek(f, (long)offset, SEEK_SET) == 0 && fread(dst, 1, len, f) == len)
        rc = 0;
    fclose(f);
    return rc;
}
