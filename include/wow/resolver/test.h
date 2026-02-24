/*
 * resolver/test.h — Resolver test harnesses
 *
 * Provides:
 *   wow debug version-test  — hardcoded version matching tests
 *   wow debug pubgrub-test  — hardcoded PubGrub solver tests
 */

#ifndef WOW_RESOLVER_TEST_H
#define WOW_RESOLVER_TEST_H

/* Version parsing and constraint matching tests */
int cmd_debug_version_test(int argc, char *argv[]);

/* PubGrub resolver algorithm tests */
int cmd_debug_pubgrub_test(int argc, char *argv[]);

#endif
