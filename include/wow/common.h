#ifndef WOW_COMMON_H
#define WOW_COMMON_H

/*
 * Common definitions used throughout wow.
 *
 * This header is safe to include anywhere — it has no dependencies
 * on other wow headers and only uses standard C types.
 */

#include <limits.h>

/* Extended path buffer for composite paths (base + suffix) */
#define WOW_WPATH  (PATH_MAX + 256)

/* ANSI escape sequences for terminal output */
#define WOW_ANSI_BOLD   "\033[1m"
#define WOW_ANSI_DIM    "\033[2m"
#define WOW_ANSI_CYAN   "\033[36m"
#define WOW_ANSI_GREEN  "\033[32m"
#define WOW_ANSI_RED    "\033[31m"
#define WOW_ANSI_RESET  "\033[0m"

#endif
