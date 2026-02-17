#ifndef WOW_RUBIES_INSTALL_H
#define WOW_RUBIES_INSTALL_H

/*
 * Ruby installation, uninstallation, and listing.
 */

/* Ruby subcommand handler (used by main.c) */
int cmd_ruby(int argc, char *argv[]);

/* Install a single Ruby version */
int wow_ruby_install(const char *version);

/* Install multiple Ruby versions in parallel */
int wow_ruby_install_many(const char **versions, int n);

/* Uninstall a Ruby version */
int wow_ruby_uninstall(const char *version);

/* List installed Ruby versions */
int wow_ruby_list(const char *active_version);

/* Install if not already present */
int wow_ruby_ensure(const char *version);

#endif
