#ifndef WOW_GEMFILE_PARSE_H
#define WOW_GEMFILE_PARSE_H

/*
 * parse.h -- Gemfile parsing public API
 *
 * Two entry points: file path or in-memory buffer.
 * Both produce a wow_gemfile struct.
 */

#include "wow/gemfile/types.h"

/*
 * Parse a Gemfile from a file path.
 * Returns 0 on success, -1 on error (diagnostic on stderr).
 */
int wow_gemfile_parse_file(const char *path, struct wow_gemfile *gf);

/*
 * Parse a Gemfile from an in-memory buffer.
 * The buffer must be null-terminated (buf[len] == '\0').
 * Returns 0 on success, -1 on error (diagnostic on stderr).
 */
int wow_gemfile_parse_buf(const char *buf, int len, struct wow_gemfile *gf);

/*
 * Lex a Gemfile and print the token stream to stdout (debug).
 * Returns 0 on success, -1 on error.
 */
int wow_gemfile_lex_file(const char *path);

#endif
