#ifndef WOW_RUBIES_DEFINITIONS_H
#define WOW_RUBIES_DEFINITIONS_H

#include "wow/rubies/impl.h"

/*
 * Scan ruby-binary definitions and list available Ruby versions.
 *
 * -l (list):     latest stable release per minor series + CosmoRuby
 * -L (list-all): every definition across all implementations
 */

/* Print latest stable versions (one per minor series / implementation) */
int wow_definitions_list(void);

/* Print all ruby-binary definitions */
int wow_definitions_list_all(void);

#endif
