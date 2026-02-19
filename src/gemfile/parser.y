/*
 * parser.y -- lemon grammar for Gemfile parsing
 *
 * Parses a restricted subset of Gemfile syntax (no dynamic Ruby).
 * Uses %extra_argument to build a wow_gemfile struct directly.
 *
 * Generate: lemon src/gemfile/parser.y
 * Produces: parser.c (parser) + parser.h (token IDs)
 */

%include {
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include "wow/gemfile/lexer.h"
#include "wow/gemfile/types.h"

/*
 * Helper: duplicate a token's text, stripping leading/trailing chars.
 * strip_left/strip_right indicate how many chars to remove from each end.
 * For STRING tokens: strip 1 from each side (the quotes).
 * For SYMBOL tokens: strip 1 from left (the colon).
 * For KEY tokens: strip 1 from right (the colon).
 */
static char *tok_strdup(struct wow_token t, int strip_left, int strip_right)
{
    int len = t.length - strip_left - strip_right;
    if (len <= 0) return strdup("");
    return strndup(t.start + strip_left, (size_t)len);
}

/*
 * Accumulator for gem_opts: collects version constraints + keyword
 * options as the parser reduces gem_opts rules left-to-right.
 */
struct gem_opts_acc {
    char **constraints;
    int    n_constraints;
    int    constraints_cap;
    char **groups;
    int    n_groups;
    int    groups_cap;
    char **autorequire;
    int    n_autorequire;
    int    autorequire_cap;
    bool   autorequire_specified;
    char **platforms;
    int    n_platforms;
    int    platforms_cap;
};

static void gem_opts_acc_init(struct gem_opts_acc *a)
{
    memset(a, 0, sizeof(*a));
}

static void gem_opts_acc_add_constraint(struct gem_opts_acc *a, char *s)
{
    if (a->n_constraints >= a->constraints_cap) {
        a->constraints_cap = a->constraints_cap ? a->constraints_cap * 2 : 4;
        a->constraints = realloc(a->constraints,
                                 sizeof(char *) * (size_t)a->constraints_cap);
    }
    a->constraints[a->n_constraints++] = s;
}

static void gem_opts_acc_add_string(char ***arr, int *n, int *cap, char *s)
{
    if (*n >= *cap) {
        *cap = *cap ? *cap * 2 : 4;
        *arr = realloc(*arr, sizeof(char *) * (size_t)*cap);
    }
    (*arr)[(*n)++] = s;
}

/*
 * Parse a %w[...] or %i[...] token into individual whitespace-separated items.
 * Token format: "%w[foo bar baz]" or "%i[mri windows]" or with () delimiters.
 * Items are stored in acc->groups[] (generic string array).
 */
static void parse_percent_array(struct gem_opts_acc *acc, struct wow_token t)
{
    gem_opts_acc_init(acc);
    /* Skip "%w[" or "%i[" or "%w(" or "%i(" prefix (3 chars)
     * and trailing "]" or ")" (1 char) */
    if (t.length < 4) return;
    const char *p = t.start + 3;
    const char *end = t.start + t.length - 1;
    while (p < end) {
        /* Skip whitespace */
        while (p < end && (*p == ' ' || *p == '\t' || *p == '\n')) p++;
        if (p >= end) break;
        /* Find end of item */
        const char *item_start = p;
        while (p < end && *p != ' ' && *p != '\t' && *p != '\n') p++;
        int len = (int)(p - item_start);
        if (len > 0) {
            gem_opts_acc_add_string(&acc->groups, &acc->n_groups,
                                    &acc->groups_cap,
                                    strndup(item_start, (size_t)len));
        }
    }
}

static void gem_opts_acc_free(struct gem_opts_acc *a)
{
    int i;
    for (i = 0; i < a->n_constraints; i++)
        free(a->constraints[i]);
    free(a->constraints);
    for (i = 0; i < a->n_groups; i++)
        free(a->groups[i]);
    free(a->groups);
    for (i = 0; i < a->n_autorequire; i++)
        free(a->autorequire[i]);
    free(a->autorequire);
    for (i = 0; i < a->n_platforms; i++)
        free(a->platforms[i]);
    free(a->platforms);
}

} /* end %include */

/* The token value type is our wow_token struct */
%token_type { struct wow_token }

/* Default destructor for tokens (no-op, tokens point into source buffer) */
%default_destructor { (void)$$; (void)gf; }

/* The extra argument passed to Parse() -- our output struct */
%extra_argument { struct wow_gemfile *gf }

/* Non-terminal types */
%type gem_opts { struct gem_opts_acc }
%type ruby_opts { struct gem_opts_acc }
%type string_array { struct gem_opts_acc }
%type string_array_items { struct gem_opts_acc }

/* Destructors to prevent leaks on parse error */
%destructor gem_opts { gem_opts_acc_free(&$$); }
%destructor ruby_opts { gem_opts_acc_free(&$$); }
%destructor string_array { gem_opts_acc_free(&$$); }
%destructor string_array_items { gem_opts_acc_free(&$$); }

/* Token declarations -- these define the IDs exported in parser.h */
%token SOURCE GEM GROUP DO END RUBY GEMSPEC .
%token LIT_TRUE LIT_FALSE LIT_NIL .
%token STRING SYMBOL KEY .
%token COMMA HASHROCKET NEWLINE .
%token IDENT UNSUPPORTED ERROR .
%token GIT_SOURCE PLUGIN PLATFORMS .
%token PATH GIT GITHUB INSTALL_IF .
%token LBRACKET RBRACKET LPAREN RPAREN .
%token PERCENT_ARRAY .

/* Evaluator-only tokens — the parser never sees these; they exist
 * solely so parser.h assigns them numeric IDs for the lexer/evaluator. */
%token IF UNLESS ELSE ELSIF EVAL_GEMFILE .
%token EQ NEQ GTE LTE GT LT MATCH .
%token AND OR BANG ASSIGN DOT COLON_COLON PIPE QUESTION .
%token INTEGER FLOAT_LIT .

%syntax_error {
    (void)yymajor;
    (void)yyminor;
    fprintf(stderr, "wow: Gemfile syntax error at line %d\n",
            TOKEN.line);
    gf->_deps_cap = (size_t)-1;  /* signal error */
}

%parse_failure {
    fprintf(stderr, "wow: Gemfile parsing failed\n");
}

/* ------------------------------------------------------------------ */
/* Grammar rules                                                       */
/* ------------------------------------------------------------------ */

file ::= stmts .

stmts ::= stmts stmt .
stmts ::= .

stmt ::= source_stmt .
stmt ::= gem_stmt .
stmt ::= group_stmt .
stmt ::= ruby_stmt .
stmt ::= gemspec_stmt .
stmt ::= platforms_stmt .
stmt ::= plugin_stmt .
stmt ::= path_stmt .
stmt ::= git_stmt .
stmt ::= github_stmt .
stmt ::= install_if_stmt .
stmt ::= GIT_SOURCE .
stmt ::= NEWLINE .

/* ------------------------------------------------------------------ */
/* source "https://rubygems.org"                                       */
/* source :rubygems / :gemcutter                                       */
/* source "url" do ... end                                             */
/* source("url")                                                       */
/* ------------------------------------------------------------------ */

source_stmt ::= SOURCE STRING(S) . {
    free(gf->source);
    gf->source = tok_strdup(S, 1, 1);
}

/* Legacy symbol form: source :rubygems, source :gemcutter */
source_stmt ::= SOURCE SYMBOL(S) . {
    free(gf->source);
    char *sym = tok_strdup(S, 1, 0);  /* strip leading colon */
    if (strcmp(sym, "rubygems") == 0 || strcmp(sym, "gemcutter") == 0) {
        gf->source = strdup("https://rubygems.org");
        free(sym);
    } else {
        gf->source = sym;  /* unknown symbol -- store as-is */
    }
}

/* Scoped source block: source "url" do ... end
 * Note: creates 1 expected shift-reduce conflict with SOURCE STRING.
 * Lemon resolves by shifting DO, which is correct. */
source_stmt ::= SOURCE STRING(S) DO stmts END . {
    free(gf->source);
    gf->source = tok_strdup(S, 1, 1);
}

/* Parenthesised: source("url") */
source_stmt ::= SOURCE LPAREN STRING(S) RPAREN . {
    free(gf->source);
    gf->source = tok_strdup(S, 1, 1);
}

/* ------------------------------------------------------------------ */
/* gem "name", "~> 4.0", require: false, group: :development          */
/* ------------------------------------------------------------------ */

gem_stmt ::= GEM STRING(N) gem_opts(O) . {
    struct wow_gemfile_dep dep;
    memset(&dep, 0, sizeof(dep));
    dep.name          = tok_strdup(N, 1, 1);
    dep.constraints   = O.constraints;
    dep.n_constraints = O.n_constraints;
    dep.autorequire   = O.autorequire;
    dep.n_autorequire = O.n_autorequire;
    dep.autorequire_specified = O.autorequire_specified;
    dep.platforms     = O.platforms;
    dep.n_platforms   = O.n_platforms;

    /* groups: keyword option takes priority over block context */
    if (O.n_groups > 0) {
        dep.groups = O.groups;
        dep.n_groups = O.n_groups;
    } else if (gf->_n_current_groups > 0) {
        dep.groups = malloc(sizeof(char *) * (size_t)gf->_n_current_groups);
        for (int i = 0; i < gf->_n_current_groups; i++)
            dep.groups[i] = strdup(gf->_current_groups[i]);
        dep.n_groups = gf->_n_current_groups;
    } else {
        dep.groups = malloc(sizeof(char *));
        dep.groups[0] = strdup("default");
        dep.n_groups = 1;
    }

    /* platforms from block context */
    if (gf->_n_current_platforms > 0 && dep.n_platforms == 0) {
        dep.platforms = malloc(sizeof(char *) * (size_t)gf->_n_current_platforms);
        for (int i = 0; i < gf->_n_current_platforms; i++)
            dep.platforms[i] = strdup(gf->_current_platforms[i]);
        dep.n_platforms = gf->_n_current_platforms;
    }

    /* Prevent gem_opts destructor from freeing transferred pointers */
    O.constraints = NULL;
    O.n_constraints = 0;
    O.groups = NULL;
    O.n_groups = 0;
    O.autorequire = NULL;
    O.n_autorequire = 0;
    O.platforms = NULL;
    O.n_platforms = 0;

    wow_gemfile_add_dep(gf, &dep);
}

/* Parenthesised: gem("name", "~> 1.0", require: false) */
gem_stmt ::= GEM LPAREN STRING(N) gem_opts(O) RPAREN . {
    struct wow_gemfile_dep dep;
    memset(&dep, 0, sizeof(dep));
    dep.name          = tok_strdup(N, 1, 1);
    dep.constraints   = O.constraints;
    dep.n_constraints = O.n_constraints;
    dep.autorequire   = O.autorequire;
    dep.n_autorequire = O.n_autorequire;
    dep.autorequire_specified = O.autorequire_specified;
    dep.platforms     = O.platforms;
    dep.n_platforms   = O.n_platforms;

    if (O.n_groups > 0) {
        dep.groups = O.groups;
        dep.n_groups = O.n_groups;
    } else if (gf->_n_current_groups > 0) {
        dep.groups = malloc(sizeof(char *) * (size_t)gf->_n_current_groups);
        for (int i = 0; i < gf->_n_current_groups; i++)
            dep.groups[i] = strdup(gf->_current_groups[i]);
        dep.n_groups = gf->_n_current_groups;
    } else {
        dep.groups = malloc(sizeof(char *));
        dep.groups[0] = strdup("default");
        dep.n_groups = 1;
    }

    if (gf->_n_current_platforms > 0 && dep.n_platforms == 0) {
        dep.platforms = malloc(sizeof(char *) * (size_t)gf->_n_current_platforms);
        for (int i = 0; i < gf->_n_current_platforms; i++)
            dep.platforms[i] = strdup(gf->_current_platforms[i]);
        dep.n_platforms = gf->_n_current_platforms;
    }

    O.constraints = NULL;
    O.n_constraints = 0;
    O.groups = NULL;
    O.n_groups = 0;
    O.autorequire = NULL;
    O.n_autorequire = 0;
    O.platforms = NULL;
    O.n_platforms = 0;

    wow_gemfile_add_dep(gf, &dep);
}

/* gem_opts accumulator */
gem_opts(R) ::= . {
    gem_opts_acc_init(&R);
}

gem_opts(R) ::= gem_opts(L) COMMA STRING(S) . {
    R = L;
    memset(&L, 0, sizeof(L));  /* prevent double-free */
    gem_opts_acc_add_constraint(&R, tok_strdup(S, 1, 1));
}

gem_opts(R) ::= gem_opts(L) COMMA KEY(K) LIT_FALSE . {
    R = L;
    memset(&L, 0, sizeof(L));
    char *key = tok_strdup(K, 0, 1);  /* strip trailing colon */
    if (strcmp(key, "require") == 0) {
        R.autorequire_specified = true;
        /* require: false → empty autorequire array */
        R.autorequire = NULL;
        R.n_autorequire = 0;
    }
    free(key);
}

gem_opts(R) ::= gem_opts(L) COMMA KEY(K) LIT_TRUE . {
    R = L;
    memset(&L, 0, sizeof(L));
    char *key = tok_strdup(K, 0, 1);
    if (strcmp(key, "require") == 0) {
        /* require: true → not specified (use default) */
        R.autorequire_specified = false;
    }
    free(key);
}

gem_opts(R) ::= gem_opts(L) COMMA KEY(K) LIT_NIL . {
    R = L;
    memset(&L, 0, sizeof(L));
    char *key = tok_strdup(K, 0, 1);
    if (strcmp(key, "require") == 0) {
        /* require: nil == require: false */
        R.autorequire_specified = true;
        R.autorequire = NULL;
        R.n_autorequire = 0;
    }
    free(key);
}

gem_opts(R) ::= gem_opts(L) COMMA KEY(K) SYMBOL(S) . {
    R = L;
    memset(&L, 0, sizeof(L));
    char *key = tok_strdup(K, 0, 1);  /* strip trailing colon */
    if (strcmp(key, "group") == 0 || strcmp(key, "groups") == 0) {
        gem_opts_acc_add_string(&R.groups, &R.n_groups, &R.groups_cap,
                                tok_strdup(S, 1, 0));
    } else if (strcmp(key, "platform") == 0 || strcmp(key, "platforms") == 0) {
        gem_opts_acc_add_string(&R.platforms, &R.n_platforms, &R.platforms_cap,
                                tok_strdup(S, 1, 0));
    }
    free(key);
}

gem_opts(R) ::= gem_opts(L) COMMA KEY(K) STRING(S) . {
    R = L;
    memset(&L, 0, sizeof(L));
    char *key = tok_strdup(K, 0, 1);
    if (strcmp(key, "require") == 0) {
        R.autorequire_specified = true;
        gem_opts_acc_add_string(&R.autorequire, &R.n_autorequire, &R.autorequire_cap,
                                tok_strdup(S, 1, 1));
    }
    free(key);
}

gem_opts(R) ::= gem_opts(L) COMMA KEY(K) typed_array(A) . {
    R = L;
    memset(&L, 0, sizeof(L));
    char *key = tok_strdup(K, 0, 1);
    if (strcmp(key, "groups") == 0 || strcmp(key, "group") == 0) {
        /* Transfer groups from array */
        for (int i = 0; i < A.n_groups; i++)
            gem_opts_acc_add_string(&R.groups, &R.n_groups, &R.groups_cap, A.groups[i]);
        free(A.groups);
        A.groups = NULL;
        A.n_groups = 0;
    } else if (strcmp(key, "platforms") == 0 || strcmp(key, "platform") == 0) {
        /* Transfer platforms from array */
        for (int i = 0; i < A.n_groups; i++)  /* A.groups holds platforms here */
            gem_opts_acc_add_string(&R.platforms, &R.n_platforms, &R.platforms_cap, A.groups[i]);
        free(A.groups);
        A.groups = NULL;
    } else if (strcmp(key, "require") == 0) {
        /* Transfer autorequire paths from array */
        R.autorequire_specified = true;
        for (int i = 0; i < A.n_groups; i++)
            gem_opts_acc_add_string(&R.autorequire, &R.n_autorequire, &R.autorequire_cap, A.groups[i]);
        free(A.groups);
        A.groups = NULL;
    }
    free(key);
}

/* Hashrocket syntax: :require => false, :group => :development */
gem_opts(R) ::= gem_opts(L) COMMA SYMBOL(S) HASHROCKET LIT_FALSE . {
    R = L;
    memset(&L, 0, sizeof(L));
    char *key = tok_strdup(S, 1, 0);  /* strip leading colon */
    if (strcmp(key, "require") == 0) {
        R.autorequire_specified = true;
        R.autorequire = NULL;
        R.n_autorequire = 0;
    }
    free(key);
}

gem_opts(R) ::= gem_opts(L) COMMA SYMBOL(S) HASHROCKET LIT_TRUE . {
    R = L;
    memset(&L, 0, sizeof(L));
    char *key = tok_strdup(S, 1, 0);
    if (strcmp(key, "require") == 0) {
        R.autorequire_specified = false;  /* require: true is default */
    }
    free(key);
}

gem_opts(R) ::= gem_opts(L) COMMA SYMBOL(S) HASHROCKET LIT_NIL . {
    R = L;
    memset(&L, 0, sizeof(L));
    char *key = tok_strdup(S, 1, 0);
    if (strcmp(key, "require") == 0) {
        R.autorequire_specified = true;
        R.autorequire = NULL;
        R.n_autorequire = 0;
    }
    free(key);
}

gem_opts(R) ::= gem_opts(L) COMMA SYMBOL(S) HASHROCKET SYMBOL(V) . {
    R = L;
    memset(&L, 0, sizeof(L));
    char *key = tok_strdup(S, 1, 0);
    if (strcmp(key, "group") == 0 || strcmp(key, "groups") == 0) {
        gem_opts_acc_add_string(&R.groups, &R.n_groups, &R.groups_cap,
                                tok_strdup(V, 1, 0));
    } else if (strcmp(key, "platform") == 0 || strcmp(key, "platforms") == 0) {
        gem_opts_acc_add_string(&R.platforms, &R.n_platforms, &R.platforms_cap,
                                tok_strdup(V, 1, 0));
    }
    free(key);
}

gem_opts(R) ::= gem_opts(L) COMMA SYMBOL HASHROCKET STRING . {
    R = L;
    memset(&L, 0, sizeof(L));
    /* Unknown hashrocket key with string value -- ignore */
}

gem_opts(R) ::= gem_opts(L) COMMA SYMBOL(S) HASHROCKET typed_array(A) . {
    R = L;
    memset(&L, 0, sizeof(L));
    char *key = tok_strdup(S, 1, 0);  /* strip leading colon */
    if (strcmp(key, "groups") == 0 || strcmp(key, "group") == 0) {
        for (int i = 0; i < A.n_groups; i++)
            gem_opts_acc_add_string(&R.groups, &R.n_groups, &R.groups_cap, A.groups[i]);
        free(A.groups);
        A.groups = NULL;
    } else if (strcmp(key, "platforms") == 0 || strcmp(key, "platform") == 0) {
        for (int i = 0; i < A.n_groups; i++)
            gem_opts_acc_add_string(&R.platforms, &R.n_platforms, &R.platforms_cap, A.groups[i]);
        free(A.groups);
        A.groups = NULL;
    } else if (strcmp(key, "require") == 0) {
        R.autorequire_specified = true;
        for (int i = 0; i < A.n_groups; i++)
            gem_opts_acc_add_string(&R.autorequire, &R.n_autorequire, &R.autorequire_cap, A.groups[i]);
        free(A.groups);
        A.groups = NULL;
    }
    free(key);
}

/* Variable reference as version arg: gem "x", some_var (unresolvable) */
gem_opts(R) ::= gem_opts(L) COMMA IDENT . {
    R = L;
    memset(&L, 0, sizeof(L));
    /* Accept but ignore — variable couldn't be resolved by evaluator */
}

/* Bare array as positional constraint: gem 'x', ['>= 1', '< 2'] */
gem_opts(R) ::= gem_opts(L) COMMA string_array(A) . {
    R = L;
    memset(&L, 0, sizeof(L));
    for (int i = 0; i < A.n_constraints; i++)
        gem_opts_acc_add_constraint(&R, A.constraints[i]);
    free(A.constraints);  /* strings transferred, free container only */
    A.constraints = NULL;
    A.n_constraints = 0;
}

/* ------------------------------------------------------------------ */
/* String array literal: ['str', 'str']                                */
/* ------------------------------------------------------------------ */

string_array(R) ::= LBRACKET string_array_items(A) RBRACKET . {
    R = A;
    memset(&A, 0, sizeof(A));
}

string_array_items(R) ::= STRING(S) . {
    gem_opts_acc_init(&R);
    gem_opts_acc_add_constraint(&R, tok_strdup(S, 1, 1));
}

string_array_items(R) ::= string_array_items(L) COMMA STRING(S) . {
    R = L;
    memset(&L, 0, sizeof(L));
    gem_opts_acc_add_constraint(&R, tok_strdup(S, 1, 1));
}

/* ------------------------------------------------------------------ */
/* Typed array: captures STRING or SYMBOL values for keyword args      */
/* ------------------------------------------------------------------ */

%type typed_array { struct gem_opts_acc }
%destructor typed_array { gem_opts_acc_free(&$$); }

%type typed_array_items { struct gem_opts_acc }
%destructor typed_array_items { gem_opts_acc_free(&$$); }

typed_array(R) ::= LBRACKET typed_array_items(A) RBRACKET . {
    R = A;
    memset(&A, 0, sizeof(A));
}

typed_array(R) ::= LBRACKET RBRACKET . {
    gem_opts_acc_init(&R);
}

typed_array(R) ::= PERCENT_ARRAY(P) . {
    parse_percent_array(&R, P);
}

typed_array_items(R) ::= SYMBOL(S) . {
    gem_opts_acc_init(&R);
    gem_opts_acc_add_string(&R.groups, &R.n_groups, &R.groups_cap,
                            tok_strdup(S, 1, 0));  /* strip leading colon */
}

typed_array_items(R) ::= STRING(S) . {
    gem_opts_acc_init(&R);
    gem_opts_acc_add_string(&R.groups, &R.n_groups, &R.groups_cap,
                            tok_strdup(S, 1, 1));  /* strip quotes */
}

typed_array_items(R) ::= typed_array_items(L) COMMA SYMBOL(S) . {
    R = L;
    memset(&L, 0, sizeof(L));
    gem_opts_acc_add_string(&R.groups, &R.n_groups, &R.groups_cap,
                            tok_strdup(S, 1, 0));
}

typed_array_items(R) ::= typed_array_items(L) COMMA STRING(S) . {
    R = L;
    memset(&L, 0, sizeof(L));
    gem_opts_acc_add_string(&R.groups, &R.n_groups, &R.groups_cap,
                            tok_strdup(S, 1, 1));
}

/* ------------------------------------------------------------------ */
/* group :development do ... end                                       */
/* group 'test' do ... end                                             */
/* group(:dev, :test) do ... end                                       */
/* ------------------------------------------------------------------ */

group_open ::= GROUP name_list DO . {
    /* _current_groups was set by name_list reduction */
}

group_open ::= GROUP LPAREN name_list RPAREN DO . {
    /* _current_groups was set by name_list reduction */
}

group_stmt ::= group_open stmts END . {
    for (int i = 0; i < gf->_n_current_groups; i++)
        free(gf->_current_groups[i]);
    free(gf->_current_groups);
    gf->_current_groups = NULL;
    gf->_n_current_groups = 0;
}

name_list ::= SYMBOL(S) . {
    for (int i = 0; i < gf->_n_current_groups; i++)
        free(gf->_current_groups[i]);
    free(gf->_current_groups);
    gf->_current_groups = malloc(sizeof(char *));
    gf->_current_groups[0] = tok_strdup(S, 1, 0);
    gf->_n_current_groups = 1;
}

name_list ::= STRING(S) . {
    for (int i = 0; i < gf->_n_current_groups; i++)
        free(gf->_current_groups[i]);
    free(gf->_current_groups);
    gf->_current_groups = malloc(sizeof(char *));
    gf->_current_groups[0] = tok_strdup(S, 1, 1);
    gf->_n_current_groups = 1;
}

name_list ::= name_list COMMA SYMBOL(S) . {
    gf->_current_groups = realloc(gf->_current_groups,
        sizeof(char *) * (size_t)(gf->_n_current_groups + 1));
    gf->_current_groups[gf->_n_current_groups] = tok_strdup(S, 1, 0);
    gf->_n_current_groups++;
}

name_list ::= name_list COMMA STRING(S) . {
    gf->_current_groups = realloc(gf->_current_groups,
        sizeof(char *) * (size_t)(gf->_n_current_groups + 1));
    gf->_current_groups[gf->_n_current_groups] = tok_strdup(S, 1, 1);
    gf->_n_current_groups++;
}

/* Keyword args in group call: group :dev, optional: true */
name_list ::= name_list COMMA KEY LIT_TRUE .
name_list ::= name_list COMMA KEY LIT_FALSE .
name_list ::= name_list COMMA KEY STRING .
name_list ::= name_list COMMA KEY SYMBOL .

/* ------------------------------------------------------------------ */
/* platforms :ruby do ... end                                          */
/* platform :jruby do ... end                                          */
/* ------------------------------------------------------------------ */

platforms_open ::= PLATFORMS platform_names DO . {
    /* platform_names already populated gf->_current_platforms */
}

platforms_stmt ::= platforms_open stmts END . {
    /* Clear current platforms */
    for (int i = 0; i < gf->_n_current_platforms; i++)
        free(gf->_current_platforms[i]);
    free(gf->_current_platforms);
    gf->_current_platforms = NULL;
    gf->_n_current_platforms = 0;
}

platform_names ::= SYMBOL(S) . {
    gf->_current_platforms = malloc(sizeof(char *));
    gf->_current_platforms[0] = tok_strdup(S, 1, 0);  /* strip leading colon */
    gf->_n_current_platforms = 1;
}

platform_names ::= platform_names COMMA SYMBOL(S) . {
    gf->_current_platforms = realloc(gf->_current_platforms,
        sizeof(char *) * (size_t)(gf->_n_current_platforms + 1));
    gf->_current_platforms[gf->_n_current_platforms] = tok_strdup(S, 1, 0);
    gf->_n_current_platforms++;
}

/* ------------------------------------------------------------------ */
/* path "." do ... end                                                 */
/* git "url" do ... end                                                */
/* github "org/repo" do ... end                                        */
/* ------------------------------------------------------------------ */

/* Shared keyword opts consumed and ignored for block constructs */
block_kw_opts ::= .
block_kw_opts ::= block_kw_opts COMMA KEY STRING .
block_kw_opts ::= block_kw_opts COMMA KEY SYMBOL .
block_kw_opts ::= block_kw_opts COMMA KEY LIT_TRUE .
block_kw_opts ::= block_kw_opts COMMA KEY LIT_FALSE .

path_stmt ::= PATH STRING block_kw_opts DO stmts END .
git_stmt ::= GIT STRING block_kw_opts DO stmts END .
github_stmt ::= GITHUB STRING block_kw_opts DO stmts END .

/* ------------------------------------------------------------------ */
/* install_if -> { ... } do ... end                                    */
/* ------------------------------------------------------------------ */

install_if_stmt ::= INSTALL_IF DO stmts END .

/* ------------------------------------------------------------------ */
/* ruby "3.3.0", engine: "jruby" -- version stored, opts ignored       */
/* ruby file: ".ruby-version" -- accept, don't store version           */
/* ------------------------------------------------------------------ */

ruby_stmt ::= RUBY STRING(S) ruby_opts(O) . {
    free(gf->ruby_version);
    gf->ruby_version = tok_strdup(S, 1, 1);
    gem_opts_acc_free(&O);
}

ruby_stmt ::= RUBY KEY STRING . {
    /* ruby file: ".ruby-version" -- accept but don't store version */
}

/* Reuse same accumulator shape for ruby keyword opts */
ruby_opts(R) ::= . {
    gem_opts_acc_init(&R);
}

ruby_opts(R) ::= ruby_opts(L) COMMA KEY STRING . {
    R = L;
    memset(&L, 0, sizeof(L));
}

ruby_opts(R) ::= ruby_opts(L) COMMA KEY SYMBOL . {
    R = L;
    memset(&L, 0, sizeof(L));
}

ruby_opts(R) ::= ruby_opts(L) COMMA STRING . {
    R = L;
    memset(&L, 0, sizeof(L));
    /* ruby "~> 3.2", ">= 3.2.1" -- version constraints, ignored */
}

/* ------------------------------------------------------------------ */
/* gemspec, gemspec path: ".", gemspec :name => "x"                    */
/* ------------------------------------------------------------------ */

gemspec_stmt ::= GEMSPEC gemspec_opts . {
    gf->has_gemspec = true;
}

gemspec_opts ::= .

/* Comma-preceded keyword opts (existing) */
gemspec_opts ::= gemspec_opts COMMA KEY STRING .
gemspec_opts ::= gemspec_opts COMMA KEY SYMBOL .

/* Direct keyword opts -- no leading comma (gemspec path: ".") */
gemspec_opts ::= KEY STRING .
gemspec_opts ::= KEY SYMBOL .

/* Hashrocket opts (gemspec :path => ".", gemspec :name => "x") */
gemspec_opts ::= SYMBOL HASHROCKET STRING .
gemspec_opts ::= SYMBOL HASHROCKET SYMBOL .
gemspec_opts ::= gemspec_opts COMMA SYMBOL HASHROCKET STRING .
gemspec_opts ::= gemspec_opts COMMA SYMBOL HASHROCKET SYMBOL .

/* ------------------------------------------------------------------ */
/* plugin "name", "version"                                            */
/* ------------------------------------------------------------------ */

plugin_stmt ::= PLUGIN STRING .
plugin_stmt ::= PLUGIN STRING COMMA STRING .
