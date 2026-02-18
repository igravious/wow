/*
 * rubies/impl.c — Ruby implementation registry
 */

#include <string.h>

#include "wow/rubies/impl.h"

const wow_impl_info_t wow_impl_table[WOW_IMPL_COUNT] = {
    [WOW_IMPL_CRUBY]               = { WOW_IMPL_CRUBY,               "",                     "CRuby",               WOW_STATUS_ACTIVE },
    [WOW_IMPL_JRUBY]               = { WOW_IMPL_JRUBY,               "jruby-",               "JRuby",               WOW_STATUS_ACTIVE },
    [WOW_IMPL_MRUBY]               = { WOW_IMPL_MRUBY,               "mruby-",               "mruby",               WOW_STATUS_ACTIVE },
    [WOW_IMPL_PICORUBY]            = { WOW_IMPL_PICORUBY,            "picoruby-",            "picoruby",            WOW_STATUS_ACTIVE },
    [WOW_IMPL_TRUFFLERUBY]         = { WOW_IMPL_TRUFFLERUBY,         "truffleruby-",         "TruffleRuby",         WOW_STATUS_ACTIVE },
    [WOW_IMPL_TRUFFLERUBY_GRAALVM] = { WOW_IMPL_TRUFFLERUBY_GRAALVM, "truffleruby+graalvm-", "TruffleRuby+GraalVM", WOW_STATUS_ACTIVE },
    [WOW_IMPL_RUBYMOTION]          = { WOW_IMPL_RUBYMOTION,          NULL,                   "RubyMotion",          WOW_STATUS_ACTIVE },
    [WOW_IMPL_ARTICHOKE]           = { WOW_IMPL_ARTICHOKE,           "artichoke-",           "Artichoke",           WOW_STATUS_EXPERIMENTAL },
    [WOW_IMPL_COSMORUBY]           = { WOW_IMPL_COSMORUBY,           NULL,                   "CosmoRuby",           WOW_STATUS_EXPERIMENTAL },
    [WOW_IMPL_RUBINIUS]            = { WOW_IMPL_RUBINIUS,            "rbx-",                 "Rubinius",            WOW_STATUS_DISCONTINUED },
    [WOW_IMPL_REE]                 = { WOW_IMPL_REE,                 "ree-",                 "REE",                 WOW_STATUS_DISCONTINUED },
};

wow_ruby_impl_t wow_impl_from_definition(const char *name)
{
    /* Try each prefixed implementation (longest prefix first isn't needed
     * since no prefix is a proper prefix of another, but we check
     * truffleruby+graalvm before truffleruby to avoid a false match) */
    for (int i = 0; i < WOW_IMPL_COUNT; i++) {
        const char *pfx = wow_impl_table[i].prefix;
        if (!pfx || pfx[0] == '\0')
            continue;
        size_t len = strlen(pfx);
        if (strncmp(name, pfx, len) == 0)
            return (wow_ruby_impl_t)i;
    }

    /* No prefix matched — if it starts with a digit, it's CRuby */
    if (name[0] >= '0' && name[0] <= '9')
        return WOW_IMPL_CRUBY;

    return WOW_IMPL_UNKNOWN;
}

const char *wow_impl_name(wow_ruby_impl_t impl)
{
    if (impl >= 0 && impl < WOW_IMPL_COUNT)
        return wow_impl_table[impl].name;
    return "Unknown";
}
