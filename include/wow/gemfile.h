#ifndef WOW_GEMFILE_H
#define WOW_GEMFILE_H

/*
 * Gemfile parsing -- re2c lexer + lemon parser.
 * This is a convenience header that includes all gemfile submodules.
 */

#include "wow/gemfile/types.h"
#include "wow/gemfile/lexer.h"
#include "wow/gemfile/eval.h"
#include "wow/gemfile/parse.h"

/* Forward declarations for CLI handlers */
int cmd_gemfile_parse(int argc, char *argv[]);
int cmd_gemfile_deps(int argc, char *argv[]);

#endif
