/*
 * lexer.re -- re2c input for Gemfile lexer
 *
 * Tokenises a restricted subset of Gemfile syntax.
 * Token IDs come from the lemon-generated parser.h.
 *
 * Generate: re2c -o src/gemfile/lexer.c src/gemfile/lexer.re --no-debug-info
 */

#include <string.h>
#include "wow/gemfile/lexer.h"
#include "parser.h"

void wow_lexer_init(struct wow_lexer *lex, const char *buf, int len)
{
    lex->cursor = buf;
    lex->limit  = buf + len;
    lex->marker = buf;
    lex->tok    = buf;
    lex->start  = buf;
    lex->line   = 1;
}

/* ------------------------------------------------------------------ */
/* Helpers for multi-line skip patterns                                */
/* ------------------------------------------------------------------ */

/*
 * skip_git_source_block -- consume a git_source(...) { ... } declaration.
 *
 * Called after the lexer has matched "git_source". Advances the cursor
 * past the entire declaration including its block body.
 *
 * Handles:
 *   git_source(:github) { |repo| "https://github.com/#{repo}.git" }
 *   git_source(:github) do |repo|
 *     "https://github.com/#{repo}"
 *   end
 *   git_source :github do |repo| ... end
 */
static int skip_git_source_block(struct wow_lexer *lex, struct wow_token *token)
{
    const char *p = lex->cursor;
    const char *lim = lex->limit;

    /* Phase 1: skip optional (args) or bare :symbol */
    while (p < lim && (*p == ' ' || *p == '\t'))
        p++;
    if (p < lim && *p == '(') {
        p++;
        while (p < lim && *p != ')')
            p++;
        if (p < lim) p++;  /* skip ')' */
    } else if (p < lim && *p == ':') {
        p++;
        while (p < lim && ((*p >= 'a' && *p <= 'z') || (*p >= 'A' && *p <= 'Z') ||
               (*p >= '0' && *p <= '9') || *p == '_'))
            p++;
    }

    /* Phase 2: skip whitespace to find block opener */
    while (p < lim && (*p == ' ' || *p == '\t'))
        p++;

    if (p < lim && *p == '{') {
        /* Brace block — track depth, skip strings and comments */
        int depth = 1;
        p++;
        while (p < lim && depth > 0) {
            if (*p == '"' || *p == '\'') {
                /* Skip quoted string */
                char q = *p++;
                while (p < lim && *p != q && *p != '\n')
                    p++;
                if (p < lim && *p == q) p++;
                continue;
            }
            if (*p == '#') {
                /* Skip comment to end of line */
                while (p < lim && *p != '\n' && *p != '\0')
                    p++;
                continue;
            }
            if (*p == '{') depth++;
            else if (*p == '}') depth--;
            else if (*p == '\n') lex->line++;
            p++;
        }
    } else if (p + 1 < lim && *p == 'd' && *(p + 1) == 'o' &&
               (p + 2 >= lim || *(p + 2) == ' ' || *(p + 2) == '\n' ||
                *(p + 2) == '\t' || *(p + 2) == '\r' || *(p + 2) == '|')) {
        /* do...end block — scan for matching "end" */
        int depth = 1;
        p += 2;
        while (p < lim && depth > 0) {
            if (*p == '"' || *p == '\'') {
                /* Skip quoted string */
                char q = *p++;
                while (p < lim && *p != q && *p != '\n')
                    p++;
                if (p < lim && *p == q) p++;
                continue;
            }
            if (*p == '#') {
                /* Skip comment to end of line */
                while (p < lim && *p != '\n' && *p != '\0')
                    p++;
                continue;
            }
            if (*p == '\n') lex->line++;
            /* Check for 'end' keyword (word boundary check) */
            if (*p == 'e' && p + 2 < lim && p[1] == 'n' && p[2] == 'd' &&
                (p + 3 >= lim || p[3] == '\n' || p[3] == ' ' || p[3] == '\t' ||
                 p[3] == '\r' || p[3] == '\0')) {
                /* Verify word boundary before 'end' */
                if (p == lex->start || *(p - 1) == '\n' || *(p - 1) == ' ' ||
                    *(p - 1) == '\t') {
                    depth--;
                    if (depth == 0) { p += 3; break; }
                }
            }
            p++;
        }
    } else {
        /* No block found — consume to end of line */
        while (p < lim && *p != '\n' && *p != '\0')
            p++;
    }

    lex->cursor = p;
    token->length = (int)(lex->cursor - lex->tok);
    return GIT_SOURCE;
}

/*
 * skip_block_comment -- skip =begin ... =end block.
 *
 * Called after the lexer has matched "=begin" at column 0.
 * Advances cursor past the matching "=end" line.
 */
static void skip_block_comment(struct wow_lexer *lex)
{
    const char *p = lex->cursor;
    const char *lim = lex->limit;

    /* Skip rest of =begin line */
    while (p < lim && *p != '\n' && *p != '\0')
        p++;
    if (p < lim && *p == '\n') {
        lex->line++;
        p++;
    }

    /* Scan for =end at start of line */
    while (p < lim) {
        if (p + 4 <= lim && p[0] == '=' && p[1] == 'e' &&
            p[2] == 'n' && p[3] == 'd' &&
            (p + 4 >= lim || p[4] == '\n' || p[4] == ' ' ||
             p[4] == '\t' || p[4] == '\r' || p[4] == '\0')) {
            p += 4;
            /* Skip rest of =end line */
            while (p < lim && *p != '\n' && *p != '\0')
                p++;
            if (p < lim && *p == '\n') {
                lex->line++;
                p++;
            }
            break;
        }
        if (*p == '\n') lex->line++;
        p++;
    }

    lex->cursor = p;
}

/* ------------------------------------------------------------------ */
/* Scanner                                                             */
/* ------------------------------------------------------------------ */

int wow_lexer_scan(struct wow_lexer *lex, struct wow_token *token)
{
    for (;;) {
        lex->tok = lex->cursor;
        token->start  = lex->cursor;
        token->line   = lex->line;

        /*!re2c
            re2c:define:YYCTYPE  = "unsigned char";
            re2c:define:YYCURSOR = lex->cursor;
            re2c:define:YYLIMIT  = lex->limit;
            re2c:define:YYMARKER = lex->marker;
            re2c:yyfill:enable   = 0;

            /* Whitespace (not newlines) -- skip */
            [ \t\r]+  { continue; }

            /* Newlines -- track line, return token */
            "\n"  {
                lex->line++;
                token->length = 1;
                return NEWLINE;
            }

            /* Comments -- skip to end of line */
            "#" [^\n\x00]*  { continue; }

            /* =begin...=end block comments (only at column 0) */
            "=begin"  {
                if (lex->tok == lex->start ||
                    (lex->tok > lex->start && *(lex->tok - 1) == '\n')) {
                    skip_block_comment(lex);
                    continue;
                }
                /* Not at column 0 — treat as identifier */
                token->length = (int)(lex->cursor - lex->tok);
                return IDENT;
            }

            /* Keywords (must come before identifier rules for re2c priority) */
            "source"    { token->length = 6;  return SOURCE; }
            "gem"       { token->length = 3;  return GEM; }
            "group"     { token->length = 5;  return GROUP; }
            "do"        { token->length = 2;  return DO; }
            "end"       { token->length = 3;  return END; }
            "ruby"      { token->length = 4;  return RUBY; }
            "gemspec"   { token->length = 7;  return GEMSPEC; }
            "true"      { token->length = 4;  return LIT_TRUE; }
            "false"     { token->length = 5;  return LIT_FALSE; }
            "nil"       { token->length = 3;  return LIT_NIL; }
            "plugin"    { token->length = 6;  return PLUGIN; }
            "platforms" { token->length = 9;  return PLATFORMS; }
            "platform"  { token->length = 8;  return PLATFORMS; }

            /* git_source -- consume entire declaration including block body */
            "git_source"  {
                return skip_git_source_block(lex, token);
            }

            /* Unsupported Ruby constructs -- detected early for clean errors */
            "if"            { token->length = (int)(lex->cursor - lex->tok); return UNSUPPORTED; }
            "unless"        { token->length = (int)(lex->cursor - lex->tok); return UNSUPPORTED; }
            "else"          { token->length = (int)(lex->cursor - lex->tok); return UNSUPPORTED; }
            "elsif"         { token->length = (int)(lex->cursor - lex->tok); return UNSUPPORTED; }
            "case"          { token->length = (int)(lex->cursor - lex->tok); return UNSUPPORTED; }
            "when"          { token->length = (int)(lex->cursor - lex->tok); return UNSUPPORTED; }
            "eval"          { token->length = (int)(lex->cursor - lex->tok); return UNSUPPORTED; }
            "eval_gemfile"  { token->length = (int)(lex->cursor - lex->tok); return UNSUPPORTED; }

            /* Percent-literal arrays: %i[...] or %w[...] */
            "%i[" [^\]\x00]* "]"  {
                token->length = (int)(lex->cursor - lex->tok);
                return PERCENT_ARRAY;
            }
            "%w[" [^\]\x00]* "]"  {
                token->length = (int)(lex->cursor - lex->tok);
                return PERCENT_ARRAY;
            }

            /* Double-quoted string */
            ["] [^"\n\x00]* ["]  {
                token->length = (int)(lex->cursor - lex->tok);
                return STRING;
            }

            /* Single-quoted string */
            ['] [^'\n\x00]* [']  {
                token->length = (int)(lex->cursor - lex->tok);
                return STRING;
            }

            /* Symbol :identifier */
            ":" [a-zA-Z_][a-zA-Z0-9_]*  {
                token->length = (int)(lex->cursor - lex->tok);
                return SYMBOL;
            }

            /* Keyword argument key: identifier followed by colon */
            [a-zA-Z_][a-zA-Z0-9_]* ":"  {
                token->length = (int)(lex->cursor - lex->tok);
                return KEY;
            }

            /* Bare identifier (catch-all for unknown words) */
            [a-zA-Z_][a-zA-Z0-9_]*  {
                token->length = (int)(lex->cursor - lex->tok);
                return IDENT;
            }

            /* Punctuation */
            ","   { token->length = 1; return COMMA; }
            "=>"  { token->length = 2; return HASHROCKET; }
            "["   { token->length = 1; return LBRACKET; }
            "]"   { token->length = 1; return RBRACKET; }
            "("   { token->length = 1; return LPAREN; }
            ")"   { token->length = 1; return RPAREN; }

            /* End of input (null sentinel) */
            "\x00"  { token->length = 0; return 0; }

            /* Default: any unrecognised byte */
            *  { token->length = 1; return ERROR; }
        */
    }
}
