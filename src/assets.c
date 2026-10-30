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

static const char *g_write_dir = NULL;

void assets_set_write_dir(const char *dir)
{
    g_write_dir = dir;
}

static FILE *open_in(const char *dir, const char *name, const char *how)
{
    size_t dl = strlen(dir), nl = strlen(name);
    char *path = malloc(dl + nl + 2);
    if (!path)
        return NULL;
    FILE *f = NULL;
    for (int v = 0; v < 3 && !f; v++) {
        memcpy(path, dir, dl);
        path[dl] = '/';
        for (size_t i = 0; i <= nl; i++) {
            unsigned char c = (unsigned char)name[i];
            path[dl + 1 + i] = (char)(v == 1 ? toupper(c) : v == 2 ? tolower(c) : c);
        }
        f = fopen(path, how);
    }
    free(path);
    return f;
}

/* A file the game wrote is read from the write directory, when one is set and has it. */
static FILE *open_variant(const char *name, int mode)
{
    FILE *f = g_write_dir ? open_in(g_write_dir, name, mode ? "r+b" : "rb") : NULL;
    return f ? f : open_in(g_dir, name, mode ? "r+b" : "rb");
}

static const char *write_dir(void)
{
    return g_write_dir ? g_write_dir : g_dir;
}

/* The path of `name` upper-cased in the write directory (malloc'ed), as DOS stores the names the game creates. */
static char *write_path(const char *name)
{
    const char *dir = write_dir();
    size_t dl = strlen(dir), nl = strlen(name);
    char *path = malloc(dl + nl + 2);
    if (!path)
        return NULL;
    memcpy(path, dir, dl);
    path[dl] = '/';
    for (size_t i = 0; i <= nl; i++)
        path[dl + 1 + i] = (char)toupper((unsigned char)name[i]);
    return path;
}

/* The path of the first spelling of `name` (as given, upper, lower case) that exists in `dir` (malloc'ed), or NULL. */
static char *find_path(const char *dir, const char *name)
{
    size_t dl = strlen(dir), nl = strlen(name);
    char *path = malloc(dl + nl + 2);
    if (!path)
        return NULL;
    for (int v = 0; v < 3; v++) {
        memcpy(path, dir, dl);
        path[dl] = '/';
        for (size_t i = 0; i <= nl; i++) {
            unsigned char c = (unsigned char)name[i];
            path[dl + 1 + i] = (char)(v == 1 ? toupper(c) : v == 2 ? tolower(c) : c);
        }
        FILE *t = fopen(path, "rb");
        if (t) {
            fclose(t);
            return path;
        }
    }
    free(path);
    return NULL;
}

int assets_write(const char *name, const void *src, size_t len)
{
    char *path = find_path(write_dir(), name);   /* an existing file keeps its name's spelling */
    if (!path)
        path = write_path(name);
    FILE *f = path ? fopen(path, "wb") : NULL;
    free(path);
    if (!f)
        return -1;
    int ok = fwrite(src, 1, len, f) == len;
    return (fclose(f) == 0 && ok) ? 0 : -1;
}

int assets_write_at(const char *name, size_t offset, const void *src, size_t len)
{
    FILE *f = open_in(write_dir(), name, "r+b");
    if (!f) {   /* not in the write directory yet: a copy of the data directory's file first */
        size_t n = 0;
        uint8_t *all = assets_read(name, &n);
        if (!all)
            return -1;
        int rc = assets_write(name, all, n);
        free(all);
        if (rc != 0 || !(f = open_in(write_dir(), name, "r+b")))
            return -1;
    }
    int ok = fseek(f, (long)offset, SEEK_SET) == 0 && fwrite(src, 1, len, f) == len;
    return (fclose(f) == 0 && ok) ? 0 : -1;
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
