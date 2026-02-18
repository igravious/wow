#ifndef WOW_RUBIES_IMPL_H
#define WOW_RUBIES_IMPL_H

/*
 * Ruby implementation types.
 *
 * Every known Ruby implementation gets an entry — active, discontinued,
 * experimental, and our own.  The prefix column shows how each appears
 * in vendor/ruby-binary/share/ruby-binary/repos/ruby-builder/ definitions.
 * CRuby versions have no prefix (just "3.3.6"), all others are prefixed.
 * CosmoRuby lives in its own repo directory (repos/cosmoruby/).
 *
 *   Implementation        prefix                   Status
 *   ─────────────────     ──────────────────────   ─────────────
 *   CRuby (MRI/YARV)     ""                       Active
 *   JRuby                 "jruby-"                 Active
 *   mruby                 "mruby-"                 Active
 *   picoruby              "picoruby-"              Active
 *   TruffleRuby           "truffleruby-"           Active
 *   TruffleRuby+GraalVM   "truffleruby+graalvm-"   Active
 *   RubyMotion            NULL                     Active
 *   Artichoke             "artichoke-"             Experimental
 *   CosmoRuby             NULL (own repo dir)      Experimental
 *   Rubinius              "rbx-"                   Discontinued
 *   REE                   "ree-"                   Discontinued
 */

typedef enum {
    WOW_STATUS_ACTIVE,
    WOW_STATUS_EXPERIMENTAL,
    WOW_STATUS_DISCONTINUED,
} wow_impl_status_t;

typedef enum {
    /* Active */
    WOW_IMPL_CRUBY,                /* MRI/YARV — the canonical C implementation */
    WOW_IMPL_JRUBY,                /* JVM-based */
    WOW_IMPL_MRUBY,                /* Lightweight/embedded */
    WOW_IMPL_PICORUBY,             /* Microcontrollers */
    WOW_IMPL_TRUFFLERUBY,          /* GraalVM-based (standalone) */
    WOW_IMPL_TRUFFLERUBY_GRAALVM,  /* GraalVM-based (bundled with GraalVM) */
    WOW_IMPL_RUBYMOTION,           /* Proprietary, iOS/macOS/Android */

    /* Experimental */
    WOW_IMPL_ARTICHOKE,            /* Rust-based */
    WOW_IMPL_COSMORUBY,            /* Cosmopolitan libc — ours */

    /* Discontinued */
    WOW_IMPL_RUBINIUS,             /* rbx — ceased 2020 */
    WOW_IMPL_REE,                  /* Ruby Enterprise Edition — ceased 2012 */

    WOW_IMPL_UNKNOWN,
    WOW_IMPL_COUNT = WOW_IMPL_UNKNOWN
} wow_ruby_impl_t;

typedef struct {
    wow_ruby_impl_t    impl;
    const char        *prefix;  /* definition prefix: "" for CRuby, NULL if separate repo */
    const char        *name;    /* Human-readable: "CRuby", "JRuby", etc. */
    wow_impl_status_t  status;
} wow_impl_info_t;

/* Lookup table — indexed by wow_ruby_impl_t */
extern const wow_impl_info_t wow_impl_table[WOW_IMPL_COUNT];

/* Identify implementation from a ruby-build definition filename.
 * e.g. "jruby-9.4.14.0" → WOW_IMPL_JRUBY, "3.3.6" → WOW_IMPL_CRUBY */
wow_ruby_impl_t wow_impl_from_definition(const char *name);

/* Return human-readable name for an implementation */
const char *wow_impl_name(wow_ruby_impl_t impl);

#endif
