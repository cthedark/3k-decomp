/*
 * Locating and reading the game's data files.
 *
 * The original files are not part of this repository. The program is pointed at
 * a directory that holds the files from the user's own copy of the game
 * (--data DIR, or the TK_DATA environment variable, default "original_bin").
 *
 * The original programs ask for lower-case names ("title1.lcz") while the
 * files on disk are upper case ("TITLE1.LCZ"), because DOS ignores case. Lookup
 * therefore tries the name as given, then upper case, then lower case.
 */
#ifndef TK_ASSETS_H
#define TK_ASSETS_H

#include <stddef.h>
#include <stdint.h>

void assets_set_dir(const char *dir);
const char *assets_dir(void);

/* Read a whole data file. Returns a malloc'ed buffer (free it) or NULL. */
uint8_t *assets_read(const char *name, size_t *size);

/* Whether a data file can be opened (the lookup of assets_read). */
int assets_exists(const char *name);

/* Read `len` bytes at `offset` of a data file into dst. Returns 0 on success. */
int assets_read_at(const char *name, size_t offset, void *dst, size_t len);

/* Where the files the game writes (its saves, __1.NOW) go: the data directory unless set. A file written there is read from there
 * afterwards. The comparisons point it at a scratch directory so the shared test data stays as it is. */
void assets_set_write_dir(const char *dir);
/* Create (or replace) a file in the write directory, its name upper-cased as DOS keeps it. Returns 0 on success. */
int assets_write(const char *name, const void *src, size_t len);
/* Write `len` bytes at `offset` of a file, first copying it into the write directory if it is not there. Returns 0 on success. */
int assets_write_at(const char *name, size_t offset, const void *src, size_t len);

#endif
