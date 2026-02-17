/*
 * util/colour.c — Terminal colour detection
 */

#include <stdio.h>
#include <unistd.h>

#include "wow/util/colour.h"

int wow_use_colour(void)
{
    return isatty(STDERR_FILENO);
}
