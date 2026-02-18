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
    char  *group;
    bool   require;
};

static void gem_opts_acc_init(struct gem_opts_acc *a)
{
    memset(a, 0, sizeof(*a));
    a->require = true;
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

static void gem_opts_acc_free(struct gem_opts_acc *a)
{
    for (int i = 0; i < a->n_constraints; i++)
        free(a->constraints[i]);
    free(a->constraints);
    free(a->group);
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

/* Destructors to prevent leaks on parse error */
%destructor gem_opts { gem_opts_acc_free(&$$); }
%destructor ruby_opts { gem_opts_acc_free(&$$); }

/* Token declarations -- these define the IDs exported in parser.h */
%token SOURCE GEM GROUP DO END RUBY GEMSPEC .
%token LIT_TRUE LIT_FALSE LIT_NIL .
%token STRING SYMBOL KEY .
%token COMMA HASHROCKET NEWLINE .
%token IDENT UNSUPPORTED ERROR .
%token GIT_SOURCE PLUGIN PLATFORMS .
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
    dep.require       = O.require;

    /* group: keyword option takes priority over block group */
    if (O.group) {
        dep.group = O.group;
    } else if (gf->_current_group) {
        dep.group = strdup(gf->_current_group);
    }

    /* Prevent gem_opts destructor from freeing transferred pointers */
    O.constraints = NULL;
    O.n_constraints = 0;
    O.group = NULL;

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
    if (strcmp(key, "require") == 0)
        R.require = false;
    free(key);
}

gem_opts(R) ::= gem_opts(L) COMMA KEY(K) LIT_TRUE . {
    R = L;
    memset(&L, 0, sizeof(L));
    (void)K;  /* require: true is the default */
}

gem_opts(R) ::= gem_opts(L) COMMA KEY(K) LIT_NIL . {
    R = L;
    memset(&L, 0, sizeof(L));
    char *key = tok_strdup(K, 0, 1);
    if (strcmp(key, "require") == 0)
        R.require = false;  /* require: nil == require: false */
    free(key);
}

gem_opts(R) ::= gem_opts(L) COMMA KEY(K) SYMBOL(S) . {
    R = L;
    memset(&L, 0, sizeof(L));
    char *key = tok_strdup(K, 0, 1);  /* strip trailing colon */
    if (strcmp(key, "group") == 0 || strcmp(key, "groups") == 0) {
        free(R.group);
        R.group = tok_strdup(S, 1, 0);  /* strip leading colon */
    }
    free(key);
}

gem_opts(R) ::= gem_opts(L) COMMA KEY STRING . {
    R = L;
    memset(&L, 0, sizeof(L));
    /* Unknown key with string value -- accept but ignore (path:, git:, etc.) */
}

gem_opts(R) ::= gem_opts(L) COMMA KEY array . {
    R = L;
    memset(&L, 0, sizeof(L));
    /* Key with array value -- accept but ignore (platforms:, groups:, etc.) */
}

/* Hashrocket syntax: :require => false, :group => :development */
gem_opts(R) ::= gem_opts(L) COMMA SYMBOL(S) HASHROCKET LIT_FALSE . {
    R = L;
    memset(&L, 0, sizeof(L));
    char *key = tok_strdup(S, 1, 0);  /* strip leading colon */
    if (strcmp(key, "require") == 0)
        R.require = false;
    free(key);
}

gem_opts(R) ::= gem_opts(L) COMMA SYMBOL(S) HASHROCKET LIT_TRUE . {
    R = L;
    memset(&L, 0, sizeof(L));
    (void)S;
}

gem_opts(R) ::= gem_opts(L) COMMA SYMBOL(S) HASHROCKET LIT_NIL . {
    R = L;
    memset(&L, 0, sizeof(L));
    char *key = tok_strdup(S, 1, 0);
    if (strcmp(key, "require") == 0)
        R.require = false;
    free(key);
}

gem_opts(R) ::= gem_opts(L) COMMA SYMBOL(S) HASHROCKET SYMBOL(V) . {
    R = L;
    memset(&L, 0, sizeof(L));
    char *key = tok_strdup(S, 1, 0);
    if (strcmp(key, "group") == 0) {
        free(R.group);
        R.group = tok_strdup(V, 1, 0);
    }
    free(key);
}

gem_opts(R) ::= gem_opts(L) COMMA SYMBOL HASHROCKET STRING . {
    R = L;
    memset(&L, 0, sizeof(L));
    /* Unknown hashrocket key with string value -- ignore */
}

gem_opts(R) ::= gem_opts(L) COMMA SYMBOL HASHROCKET array . {
    R = L;
    memset(&L, 0, sizeof(L));
    /* Hashrocket key with array value -- accept but ignore */
}

/* ------------------------------------------------------------------ */
/* Array literals: [:sym, :sym] and %i[sym sym]                        */
/* ------------------------------------------------------------------ */

array ::= LBRACKET array_items RBRACKET .
array ::= LBRACKET RBRACKET .
array ::= PERCENT_ARRAY .

array_items ::= SYMBOL .
array_items ::= STRING .
array_items ::= array_items COMMA SYMBOL .
array_items ::= array_items COMMA STRING .

/* ------------------------------------------------------------------ */
/* group :development do ... end                                       */
/* group 'test' do ... end                                             */
/* group(:dev, :test) do ... end                                       */
/* ------------------------------------------------------------------ */

group_open ::= GROUP name_list DO . {
    /* _current_group was set by name_list reduction */
}

group_open ::= GROUP LPAREN name_list RPAREN DO . {
    /* _current_group was set by name_list reduction */
}

group_stmt ::= group_open stmts END . {
    free(gf->_current_group);
    gf->_current_group = NULL;
}

name_list ::= SYMBOL(S) . {
    free(gf->_current_group);
    gf->_current_group = tok_strdup(S, 1, 0);  /* strip leading colon */
}

name_list ::= STRING(S) . {
    free(gf->_current_group);
    gf->_current_group = tok_strdup(S, 1, 1);  /* strip quotes */
}

name_list ::= name_list COMMA SYMBOL . {
    /* Keep the first name, ignore subsequent ones */
}

name_list ::= name_list COMMA STRING . {
    /* Keep the first name, ignore subsequent ones */
}

/* ------------------------------------------------------------------ */
/* platforms :ruby do ... end                                          */
/* platform :jruby do ... end                                          */
/* ------------------------------------------------------------------ */

platforms_open ::= PLATFORMS platform_names DO .

platforms_stmt ::= platforms_open stmts END . {
    /* Gems inside are collected normally; platform filter is ignored */
}

platform_names ::= SYMBOL .
platform_names ::= platform_names COMMA SYMBOL .

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
