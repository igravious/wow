#ifndef WOW_SYNC_H
#define WOW_SYNC_H

/*
 * sync.h -- `wow sync` orchestrator
 *
 * Wires together Gemfile parser, PubGrub resolver, lockfile writer,
 * parallel downloader, and gem unpacker into a single command.
 */

int cmd_sync(int argc, char *argv[]);

#endif
