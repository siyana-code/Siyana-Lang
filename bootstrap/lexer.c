/* lexer.c -- tokenizer for Siyana Lang.
 *
 * Deliberately small and linear. At this phase the goal is a correct pipeline,
 * not a fast one. Comments are skipped in the lexer rather than the parser (see
 * decision 11), which is what keeps block-comment handling trivial.
 */

#include "lexer.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    Lexer *lexer;
    Token *tokens;
    size_t count;
    size_t capacity;
} TokenList;

/* Must stay in the same order as TokenType. The names are the full keywords,
 * so a diagnostic can quote exactly what the programmer wrote. */
static const char *const kTypeNames[] = {
    "end of file", "error", "identifier", "number", "character", "string",
    "Number", "Number32", "Word", "Character",
    "Variable", "Function",
    "If", "Else", "Loop", "Return",
    "True", "False",
    "Terminal", "Write", "ReadLine",
    "Struct", "Union", "Error", "Match",
    "'('", "')'", "'{'", "'}'", "','", "';'", "':'", "'.'",
    "'+'", "'-'", "'*'", "'/'", "'='", "'=='", "'!'", "'!='",
    "'<'", "'<='", "'>'", "'>='"
};

const char *token_type_name(TokenType type) {
    if ((size_t)type >= sizeof(kTypeNames) / sizeof(kTypeNames[0])) {
        return "unknown";
    }
    return kTypeNames[type];
}

const char *token_type_description(TokenType type) {
    return token_type_name(type);
}

static bool is_at_end(const Lexer *lexer) {
    return lexer->offset >= lexer->source_length;
}

static char peek(const Lexer *lexer) {
    if (is_at_end(lexer)) return '\0';
    return lexer->source[lexer->offset];
}

static char peek_next(const Lexer *lexer) {
    if (lexer->offset + 1 >= lexer->source_length) return '\0';
    return lexer->source[lexer->offset + 1];
}

static char advance(Lexer *lexer) {
    if (is_at_end(lexer)) return '\0';
    char c = lexer->source[lexer->offset++];
    if (c == '\n') {
        lexer->position.line++;
        lexer->position.column = 1;
    } else {
        lexer->position.column++;
    }
    return c;
}

static bool match(Lexer *lexer, char expected) {
    if (is_at_end(lexer)) return false;
    if (lexer->source[lexer->offset] != expected) return false;
    advance(lexer);
    return true;
}

static void error_at(Lexer *lexer, Position position, const char *fmt, ...) {
    /* Keep the first error. Later ones are usually consequences of it. */
    if (lexer->had_error) return;
    lexer->had_error = true;
    lexer->error_position = position;

    va_list args;
    va_start(args, fmt);
    vsnprintf(lexer->error_message, sizeof(lexer->error_message), fmt, args);
    va_end(args);
}

static bool is_ident_start(char c) {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_';
}

static bool is_ident_continue(char c) {
    return is_ident_start(c) || (c >= '0' && c <= '9');
}

static bool is_digit(char c) {
    return c >= '0' && c <= '9';
}

static Token *list_push(TokenList *list) {
    if (list->count + 1 >= list->capacity) {
        size_t new_capacity = list->capacity ? list->capacity * 2 : 64;
        Token *grown = realloc(list->tokens, new_capacity * sizeof(Token));
        if (grown == NULL) {
            free(list->tokens);
            list->tokens = NULL;
            return NULL;
        }
        list->tokens = grown;
        list->capacity = new_capacity;
    }
    Token *token = &list->tokens[list->count++];
    memset(token, 0, sizeof(*token));
    return token;
}

static TokenType keyword_type(const char *text, size_t length) {
    static const struct { const char *word; TokenType type; } table[] = {
        {"Number", TOKEN_NUMBER_TYPE},   {"Character", TOKEN_CHARACTER_TYPE},
        {"Number32", TOKEN_NUMBER32_TYPE}, {"Word", TOKEN_WORD_TYPE},
        {"Variable", TOKEN_VARIABLE},    {"Function", TOKEN_FUNCTION},
        {"If", TOKEN_IF},                {"Else", TOKEN_ELSE},
        {"Loop", TOKEN_LOOP},            {"Return", TOKEN_RETURN},
        {"True", TOKEN_TRUE},            {"False", TOKEN_FALSE},
        {"Terminal", TOKEN_TERMINAL},    {"Write", TOKEN_WRITE},
        {"ReadLine", TOKEN_READ_LINE},
        {"Struct", TOKEN_STRUCT}, {"Union", TOKEN_UNION},
        {"Error", TOKEN_ERROR}, {"Match", TOKEN_MATCH},
    };
    for (size_t i = 0; i < sizeof(table) / sizeof(table[0]); i++) {
        if (strlen(table[i].word) == length &&
            memcmp(table[i].word, text, length) == 0) {
            return table[i].type;
        }
    }
    return TOKEN_IDENT;
}

static void scan_identifier(Lexer *lexer, TokenList *list) {
    Position start = lexer->position;
    size_t start_offset = lexer->offset;

    while (!is_at_end(lexer) && is_ident_continue(peek(lexer))) {
        advance(lexer);
    }

    size_t length = lexer->offset - start_offset;
    TokenType type = keyword_type(lexer->source + start_offset, length);

    Token *token = list_push(list);
    if (token == NULL) return;
    token->type = type;
    token->start = start;
    token->start_offset = start_offset;
    token->length = length;

    /* Any token that can serve as a name carries owned text, so the parser never
     * needs access to the source buffer.
     *
     * This includes the type keywords: `Union V { Number(Number), }` uses
     * `Number` as a variant name, which is allowed by is_name_token. Restricting
     * it to TOKEN_IDENT left those tokens with a NULL text pointer, and
     * copy_ident then copied from NULL. */
    if (type == TOKEN_IDENT || type == TOKEN_NUMBER_TYPE ||
        type == TOKEN_NUMBER32_TYPE || type == TOKEN_WORD_TYPE ||
        type == TOKEN_CHARACTER_TYPE) {
        token->text = malloc(length + 1);
        if (token->text == NULL) return;
        memcpy(token->text, lexer->source + start_offset, length);
        token->text[length] = '\0';
    }
}

static void scan_number_literal(Lexer *lexer, TokenList *list) {
    Position start = lexer->position;
    size_t number_start = lexer->offset;

    while (!is_at_end(lexer) && is_digit(peek(lexer))) {
        advance(lexer);
    }

    Token *token = list_push(list);
    if (token == NULL) return;
    token->type = TOKEN_NUMBER;
    token->start = start;
    token->start_offset = number_start;
    token->length = lexer->offset - number_start;

    /* Integer digits are owned too, so the parser can parse them without ever
     * holding a pointer into the source buffer. */
    token->text = malloc(token->length + 1);
    if (token->text == NULL) return;
    memcpy(token->text, lexer->source + number_start, token->length);
    token->text[token->length] = '\0';
}

/* Scans a single-quoted character literal, e.g. 'a' or '\n'.
 *
 * Character is a byte (see research/design/type-system.md), so a literal holds
 * exactly one byte. Multi-character literals are rejected rather than silently
 * truncated, because truncation in a systems language is the kind of quiet
 * wrongness that is expensive to find later. */
static void scan_character(Lexer *lexer, TokenList *list) {
    Position start = lexer->position;

    advance(lexer); /* opening quote */

    if (is_at_end(lexer) || peek(lexer) == '\n') {
        error_at(lexer, start, "unterminated character literal");
        return;
    }

    if (peek(lexer) == '\'') {
        error_at(lexer, start, "empty character literal");
        return;
    }

    unsigned char value;
    if (peek(lexer) == '\\') {
        advance(lexer);
        if (is_at_end(lexer)) {
            error_at(lexer, start, "unterminated character literal");
            return;
        }
        char escaped = advance(lexer);
        switch (escaped) {
            case 'n':  value = '\n'; break;
            case 't':  value = '\t'; break;
            case 'r':  value = '\r'; break;
            case '0':  value = '\0'; break;
            case '\\': value = '\\'; break;
            case '\'': value = '\''; break;
            default:
                error_at(lexer, start,
                         "unknown escape sequence '\\%c' in character literal", escaped);
                return;
        }
    } else {
        value = (unsigned char)advance(lexer);
    }

    if (is_at_end(lexer) || peek(lexer) != '\'') {
        error_at(lexer, start,
                 "character literal holds more than one byte; Character is a single byte");
        return;
    }
    advance(lexer); /* closing quote */

    Token *token = list_push(list);
    if (token == NULL) return;
    token->type = TOKEN_CHARACTER;
    token->start = start;
    token->start_offset = lexer->offset;
    token->length = 1;

    char buffer[2];
    buffer[0] = (char)value;
    buffer[1] = '\0';
    token->text = malloc(2);
    if (token->text == NULL) return;
    memcpy(token->text, buffer, 2);
}

/* Scans a quoted string. Rejects unterminated strings and unescaped newlines,
 * both of which are almost always a missing closing quote, and says so rather
 * than swallowing the rest of the file. */
static void scan_string(Lexer *lexer, TokenList *list) {
    Position start = lexer->position;

    advance(lexer); /* opening quote */

    size_t content_start = lexer->offset;

    while (true) {
        if (is_at_end(lexer) || peek(lexer) == '\n') {
            error_at(lexer, start, "unterminated string literal");
            return;
        }
        if (peek(lexer) == '"') {
            size_t raw_length = lexer->offset - content_start;
            advance(lexer); /* closing quote */

            Token *token = list_push(list);
            if (token == NULL) return;
            token->type = TOKEN_STRING;
            token->start = start;
            token->start_offset = content_start;
            token->length = raw_length;

            /* Copy out of the source so the caller is not left holding a
             * pointer into a buffer whose lifetime it does not control.
             * Escape decoding arrives in Phase 3; Phase 2 keeps raw bytes so
             * the lexer stays honest about what it supports. */
            token->text = malloc(raw_length + 1);
            if (token->text == NULL) return;
            memcpy(token->text, lexer->source + content_start, raw_length);
            token->text[raw_length] = '\0';
            return;
        }

        if (peek(lexer) == '\\' && peek_next(lexer) != '\0') {
            advance(lexer); /* skip the escaped character */
        }
        advance(lexer);
    }
}

static void skip_whitespace_and_comments(Lexer *lexer) {
    while (!is_at_end(lexer)) {
        char c = peek(lexer);

        if (c == ' ' || c == '\t' || c == '\r' || c == '\n') {
            advance(lexer);
            continue;
        }

        if (c == '/' && peek_next(lexer) == '/') {
            while (!is_at_end(lexer) && peek(lexer) != '\n') {
                advance(lexer);
            }
            continue;
        }

        if (c == '/' && peek_next(lexer) == '*') {
            Position start = lexer->position;
            advance(lexer);
            advance(lexer);
            bool closed = false;
            while (!is_at_end(lexer)) {
                if (peek(lexer) == '*' && peek_next(lexer) == '/') {
                    advance(lexer);
                    advance(lexer);
                    closed = true;
                    break;
                }
                advance(lexer);
            }
            if (!closed) {
                error_at(lexer, start, "unterminated block comment");
                return;
            }
            continue;
        }

        return;
    }
}

static void scan_token(Lexer *lexer, TokenList *list) {
    skip_whitespace_and_comments(lexer);
    if (lexer->had_error) return;

    Position start = lexer->position;
    size_t start_offset = lexer->offset;
    char c = advance(lexer);

    TokenType type = TOKEN_LEX_ERROR;

    if (c == '\0' && is_at_end(lexer)) {
        type = TOKEN_EOF;
    } else if (is_ident_start(c)) {
        /* Rewind before delegating: the identifier scanners start from their
         * own first character, and `c` has already been consumed. Numbers and
         * strings below do the same for the same reason. */
        lexer->offset = start_offset;
        lexer->position = start;
        scan_identifier(lexer, list);
        return;
    } else if (is_digit(c)) {
        lexer->offset = start_offset;
        lexer->position = start;
        scan_number_literal(lexer, list);
        return;
    } else if (c == '"') {
        lexer->offset = start_offset;
        lexer->position = start;
        scan_string(lexer, list);
        return;
    } else if (c == '\'') {
        lexer->offset = start_offset;
        lexer->position = start;
        scan_character(lexer, list);
        return;
    } else {
        switch (c) {
            case '(': type = TOKEN_LPAREN; break;
            case ')': type = TOKEN_RPAREN; break;
            case '{': type = TOKEN_LBRACE; break;
            case '}': type = TOKEN_RBRACE; break;
            case ',': type = TOKEN_COMMA; break;
            case ';': type = TOKEN_SEMICOLON; break;
            case ':': type = TOKEN_COLON; break;
            /* Member access, as in Terminal.Write. Terminal is designed as a
             * value (research/design/keywords-and-io.md), so this will grow
             * into general field access rather than staying a builtin-only
             * operator. */
            case '.': type = TOKEN_DOT; break;
            case '+': type = TOKEN_PLUS; break;
            case '-': type = TOKEN_MINUS; break;
            case '*': type = TOKEN_STAR; break;
            case '/': type = TOKEN_SLASH; break;
            case '=': type = match(lexer, '=') ? TOKEN_EQ : TOKEN_ASSIGN; break;
            case '!': type = match(lexer, '=') ? TOKEN_BANG_EQ : TOKEN_BANG; break;
            case '<': type = match(lexer, '=') ? TOKEN_LT_EQ : TOKEN_LT; break;
            case '>': type = match(lexer, '=') ? TOKEN_GT_EQ : TOKEN_GT; break;
            default:
                if (c >= 32 && c < 127) {
                    error_at(lexer, start, "unexpected character '%c'", c);
                } else {
                    error_at(lexer, start, "unexpected character (byte 0x%02X)", (unsigned char)c);
                }
                return;
        }
    }

    Token *token = list_push(list);
    if (token == NULL) return;
    token->type = type;
    token->start = start;
    token->start_offset = start_offset;
    token->length = 1;
}

Token *lexer_scan(Lexer *lexer, const char *source, size_t length) {
    memset(lexer, 0, sizeof(*lexer));
    lexer->source = source;
    lexer->source_length = length;
    lexer->position.line = 1;
    lexer->position.column = 1;

    TokenList list = {0};
    list.tokens = malloc(64 * sizeof(Token));
    if (list.tokens == NULL) {
        return NULL;
    }
    list.capacity = 64;

    while (!lexer->had_error) {
        scan_token(lexer, &list);
        if (list.tokens == NULL) return NULL;
        if (lexer->had_error) break;
        if (list.tokens[list.count - 1].type == TOKEN_EOF) break;
    }

    if (lexer->had_error) {
        token_array_free(list.tokens);
        return NULL;
    }

    return list.tokens;
}

bool lexer_had_error(const Lexer *lexer) { return lexer->had_error; }
const char *lexer_error_message(const Lexer *lexer) { return lexer->error_message; }
Position lexer_error_position(const Lexer *lexer) { return lexer->error_position; }

/* Walks the token array freeing owned string payloads, then frees the array.
 *
 * The array is terminated by a TOKEN_EOF entry, which scan_token always appends
 * last, so the loop can stop there rather than needing a stored count.
 * `IDENT` and `STRING` are the only token types that own text. */
void token_array_free(Token *tokens) {
    if (tokens == NULL) return;
    for (size_t i = 0; ; i++) {
        bool at_end = tokens[i].type == TOKEN_EOF;
        free(tokens[i].text);
        if (at_end) break;
    }
    free(tokens);
}
