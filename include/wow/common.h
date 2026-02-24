#ifndef WOW_COMMON_H
#define WOW_COMMON_H

/*
 * Common definitions used throughout wow.
 *
 * This header is safe to include anywhere — it has no dependencies
 * on other wow headers and only uses standard C types.
 */

#include <limits.h>

/* Path buffer sizing:
 * - WOW_OS_PATH_MAX:  full path (what the OS accepts)
 * - WOW_DIR_PATH_MAX: directory-only (leaves room for NAME_MAX + '/')
 *
 * With correct sizing, GCC can prove snprintf(full, WOW_OS_PATH_MAX,
 * "%s/%s", dir, name) won't truncate — no pragma needed.
 */
#define WOW_OS_PATH_MAX   PATH_MAX
#define WOW_DIR_PATH_MAX  (PATH_MAX - (NAME_MAX + 1))

/* ANSI escape sequences for terminal output */
#define WOW_ANSI_BOLD   "\033[1m"
#define WOW_ANSI_DIM    "\033[2m"
#define WOW_ANSI_CYAN   "\033[36m"
#define WOW_ANSI_GREEN  "\033[32m"
#define WOW_ANSI_RED    "\033[31m"
#define WOW_ANSI_RESET  "\033[0m"

#endif
