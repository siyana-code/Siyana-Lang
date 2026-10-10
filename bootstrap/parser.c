/* parser.c -- recursive-descent parser for Siyana Lang.
 *
 * Grammar, in precedence order (lowest first):
 *
 *   program     := statement* EOF
 *   statement   := varDecl | fnDecl | ifStmt | whileStmt | returnStmt
 *                | block | exprStmt
 *   varDecl     := 'var' IDENT ':' type ('=' expression)? ';'
 *   fnDecl      := 'fn' IDENT '(' params? ')' block
 *   params      := param (',' param)*
 *   param       := IDENT ':' type
 *   type        := 'int' | 'bool'
 *   ifStmt      := 'if' expression block ('else' (ifStmt | block))?
 *   whileStmt   := 'while' expression block
 *   returnStmt  := 'return' expression? ';'
 *   block       := '{' statement* '}'
 *   exprStmt    := expression ';'
 *
 *   expression  := assignment
 *   assignment  := IDENT '=' assignment | equality
 *   equality    := comparison (('=='|'!=') comparison)?
 *   comparison  := term (('<'|'<='|'>'|'>=') term)?
 *   term        := factor (('+'|'-') factor)*
 *   factor      := unary (('*'|'/') unary)*
 *   unary       := ('!'|'-') unary | call
 *   call        := primary ('(' arguments? ')')?
 *   primary     := INT | 'true' | 'false' | STRING | IDENT | 'print'
 *                | '(' expression ')'
 */

#include "parser.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    const Token *tokens;
    size_t position;

    Program *program;

    /* Set while parsing a position where a `{` opens a block rather than a
     * struct literal: an If or Loop condition, and a Match subject and body.
     *
     * This is a real ambiguity, not a workaround. `If (x) { ... }` and
     * `Match v { V.A { ... } }` both put an identifier directly in front of a
     * brace, which is exactly the shape of `Type { field: value }`. Without the
     * flag, `If (one) { ... }` parses `one { Terminal.Write(...) }` as a struct
     * literal and reports a field name it never saw.
     *
     * A struct literal is only meaningful when the identifier names a type, and
     * that is not knowable here. The context is the only signal available. */
    bool suppress_struct_literal;

    bool failed;
    char message[160];
    Position error_position;
} Parser;

static Stmt *parse_statement(Parser *parser);
static Expr *parse_expression(Parser *parser);
static Expr *parse_assignment(Parser *parser);

/* Defined next to the aggregate declarations, but needed by parse_postfix
 * above them: a field or variant may be named after a type keyword. */
static bool is_name_token(TokenType type);

static bool parse_block(Parser *parser, Stmt ***body, size_t *count);
static Expr *parse_postfix(Parser *parser, Expr *base);
static Expr *parse_shift(Parser *parser);

/* expr_free releases Catch arms, which hold statements, so the two recurse into
 * each other and one of them has to be declared first. */
static void stmt_free(Stmt *stmt);

static void fail(Parser *parser, Position position, const char *fmt, ...) {
    if (parser->failed) return;
    parser->failed = true;
    parser->error_position = position;
    va_list args;
    va_start(args, fmt);
    vsnprintf(parser->message, sizeof(parser->message), fmt, args);
    va_end(args);
}

static const Token *current(const Parser *parser) {
    return &parser->tokens[parser->position];
}

static const Token *advance_token(Parser *parser) {
    const Token *token = &parser->tokens[parser->position];
    if (token->type != TOKEN_EOF) parser->position++;
    return token;
}

static bool check(const Parser *parser, TokenType type) {
    return current(parser)->type == type;
}

static bool match(Parser *parser, TokenType type) {
    if (!check(parser, type)) return false;
    advance_token(parser);
    return true;
}

static bool consume(Parser *parser, TokenType type, const char *context) {
    if (check(parser, type)) {
        advance_token(parser);
        return true;
    }
    fail(parser, current(parser)->start, "expected %s%s, found %s",
         context, type == TOKEN_SEMICOLON ? " ';'" : token_type_description(type),
         token_type_description(current(parser)->type));
    return false;
}

/* Copies an identifier's text. Identifiers and strings both carry owned text
 * from the lexer, so this never touches the source buffer. */
static char *copy_text(const char *text, size_t length) {
    char *copy = malloc(length + 1);
    if (copy == NULL) return NULL;
    memcpy(copy, text, length);
    copy[length] = '\0';
    return copy;
}

static char *copy_ident(const Token *token) {
    return copy_text(token->text, token->length);
}

/* Non-owning view of a token's text, for comparisons only. Valid while the
 * token array lives, which for the parser is the whole parse. */
static const char *token_text_of(const Token *token) {
    return token->text;
}

/* --- Node constructors ---------------------------------------------------- */

static Stmt *stmt_new(Parser *parser, NodeType type, Position position) {
    Stmt *stmt = calloc(1, sizeof(Stmt));
    if (stmt == NULL) {
        fail(parser, position, "out of memory");
        return NULL;
    }
    stmt->type = type;
    stmt->position = position;
    return stmt;
}

static Expr *expr_new(Parser *parser, NodeType type, Position position) {
    Expr *expr = calloc(1, sizeof(Expr));
    if (expr == NULL) {
        fail(parser, position, "out of memory");
        return NULL;
    }
    expr->type = type;
    expr->position = position;
    return expr;
}

/* --- Memory cleanup ------------------------------------------------------- */

static void expr_free(Expr *expr) {
    if (expr == NULL) return;
    free(expr->text);
    free(expr->name);
    free(expr->variant);
    expr_free(expr->left);
    expr_free(expr->right);
    expr_free(expr->callee);
    expr_free(expr->payload);
    for (size_t i = 0; i < expr->argument_count; i++) {
        expr_free(expr->arguments[i]);
    }
    free(expr->arguments);

    /* EXPR_CATCH reuses the Match arm layout, so it is released the same way. */
    for (size_t i = 0; i < expr->arm_count; i++) {
        free(expr->arms[i].variant);
        free(expr->arms[i].binding);
        for (size_t j = 0; j < expr->arms[i].body_count; j++) {
            stmt_free(expr->arms[i].body[j]);
        }
        free(expr->arms[i].body);
    }
    free(expr->arms);
    free(expr);
}

static void stmt_free(Stmt *stmt) {
    if (stmt == NULL) return;
    expr_free(stmt->expression);
    expr_free(stmt->initializer);
    stmt_free(stmt->else_branch);

    for (size_t i = 0; i < stmt->field_count; i++) free(stmt->fields[i].name);
    free(stmt->fields);

    for (size_t i = 0; i < stmt->arm_count; i++) {
        free(stmt->arms[i].variant);
        free(stmt->arms[i].binding);
        for (size_t j = 0; j < stmt->arms[i].body_count; j++) {
            stmt_free(stmt->arms[i].body[j]);
        }
        free(stmt->arms[i].body);
    }
    free(stmt->arms);

    for (size_t i = 0; i < stmt->body_count; i++) stmt_free(stmt->body[i]);
    free(stmt->body);

    for (size_t i = 0; i < stmt->then_count; i++) stmt_free(stmt->then_body[i]);
    free(stmt->then_body);

    for (size_t i = 0; i < stmt->loop_count; i++) stmt_free(stmt->loop_body_statements[i]);
    free(stmt->loop_body_statements);

    for (size_t i = 0; i < stmt->parameter_count; i++) {
        free(stmt->parameters[i].name);
        free(stmt->parameters[i].type_name);
    }
    free(stmt->parameters);

    free(stmt->name);
    free(stmt->type_name);
    free(stmt->result_ok_name);
    free(stmt->result_error_name);
    free(stmt);
}

void program_free(Program *program) {
    if (program == NULL) return;
    for (size_t i = 0; i < program->count; i++) {
        stmt_free(program->statements[i]);
    }
    free(program->statements);
    program->statements = NULL;
    program->count = 0;
}

/* --- Types ---------------------------------------------------------------- */

/* Parses a type annotation.
 *
 * Only the built-in integer types are accepted here. Struct and union names
 * are also types, but they are resolved at run time rather than at parse time:
 * the parser has no symbol table, and a declaration may appear after its use.
 * A named type in a `Variable x: Foo` annotation is therefore accepted as a
 * special case below and checked when the declaration runs.
 *
 * `Result{T, E}` is accepted here too, but only the function-return caller
 * passes an `out_result`; every other caller rejects it, so a variable of that
 * type cannot be written. That restriction is the point -- see
 * research/design/error-syntax.md. */
static bool parse_type(Parser *parser, TypeKind *out_type, char **out_named) {
    if (match(parser, TOKEN_RESULT)) {
        fail(parser, current(parser)->start,
             "'Result{T, E}' may only be a function return type");
        return false;
    }

    if (match(parser, TOKEN_ALLOCATOR)) {
        *out_type = TYPE_ALLOCATOR;
        return true;
    }

    if (match(parser, TOKEN_NUMBER_TYPE)) {
        *out_type = TYPE_NUMBER;
        return true;
    }
    if (match(parser, TOKEN_NUMBER32_TYPE)) {
        *out_type = TYPE_NUMBER32;
        return true;
    }
    if (match(parser, TOKEN_WORD_TYPE)) {
        *out_type = TYPE_WORD;
        return true;
    }
    if (match(parser, TOKEN_CHARACTER_TYPE)) {
        *out_type = TYPE_CHARACTER;
        return true;
    }

    /* A named type: `Variable p: Point`. Recorded as TYPE_NAMED, with the name
     * in `out_named`; `out_type` is set to TYPE_NAMED so the caller knows to
     * defer the check. */
    if (check(parser, TOKEN_IDENT)) {
        if (out_named != NULL) *out_named = copy_ident(current(parser));
        advance_token(parser);
        if (out_type != NULL) *out_type = TYPE_NAMED;
        return true;
    }

    fail(parser, current(parser)->start,
         "expected a type name, found %s",
         token_type_description(current(parser)->type));
    return false;
}

/* Parses `Result{T, E}` and reports the two type names separately.
 *
 * Called after `Result` has already been matched, so it consumes the brace but
 * not the keyword.
 *
 * Only a function's return type may be a Result, so this is not part of
 * parse_type: the surrounding rules differ (a Return may propagate an error;
 * a Variable may not) and folding it in would need a flag threaded through
 * every call site to say which context it is in. */
static bool parse_result_type(Parser *parser, Stmt *stmt) {
    Position start = current(parser)->start;

    if (!consume(parser, TOKEN_LBRACE, "'{' after 'Result'")) return false;

    stmt->declared_type = TYPE_RESULT;
    if (!parse_type(parser, &stmt->result_ok_type, &stmt->result_ok_name)) return false;

    if (!consume(parser, TOKEN_COMMA, "',' between the success type and the error set")) {
        return false;
    }

    /* The error set is always a name -- a declared Error or Union. An inline
     * `error{...}` spelling was considered and rejected: it would put a second
     * way to write a type in a position that must resolve to one declaration. */
    if (!check(parser, TOKEN_IDENT)) {
        fail(parser, current(parser)->start,
             "expected an error set name after the comma, found %s",
             token_type_description(current(parser)->type));
        return false;
    }
    stmt->result_error_name = copy_ident(advance_token(parser));

    if (!consume(parser, TOKEN_RBRACE, "closing '}' of a Result type")) return false;
    if (stmt->result_error_name == NULL) {
        fail(parser, start, "out of memory");
        return false;
    }
    return true;
}

/* --- Primary expressions -------------------------------------------------- */

static Expr *parse_primary(Parser *parser) {
    const Token *token = current(parser);

    /* `Allocator(bytes)` -- the constructor. Distinguished from the
     * `allocator(x) { }` block form, which is a statement, by being parsed
     * here among the primary expressions. */
    if (match(parser, TOKEN_ALLOCATOR)) {
        Position start = current(parser)->start;
        if (!consume(parser, TOKEN_LPAREN, "'(' after 'Allocator'")) return NULL;

        Expr *construct = expr_new(parser, EXPR_ALLOCATOR_NEW, start);
        if (construct == NULL) return NULL;

        parser->suppress_struct_literal = true;
        construct->right = parse_expression(parser);
        parser->suppress_struct_literal = false;
        if (parser->failed) {
            expr_free(construct);
            return NULL;
        }
        if (!consume(parser, TOKEN_RPAREN, "closing ')' after an Allocator capacity")) {
            expr_free(construct);
            return NULL;
        }
        return parse_postfix(parser, construct);
    }

    /* Builtins are recognised here and lowered to a call node carrying a
     * builtin token type rather than a callee. Terminal is designed as a value
     * (research/design/keywords-and-io.md) so that it can later be passed and
     * stored; at this phase it has exactly two methods. */
    if (match(parser, TOKEN_TERMINAL)) {
        if (!consume(parser, TOKEN_DOT, "'.' after 'Terminal'")) return NULL;
        if (!check(parser, TOKEN_WRITE) && !check(parser, TOKEN_READ_LINE)) {
            fail(parser, current(parser)->start,
                 "expected 'Write' or 'ReadLine' after 'Terminal.', found %s",
                 token_type_description(current(parser)->type));
            return NULL;
        }
        TokenType builtin = current(parser)->type;
        advance_token(parser); /* Write or ReadLine */

        Expr *call = expr_new(parser, EXPR_BUILTIN, token->start);
        if (call == NULL) return NULL;
        call->builtin = builtin;

        if (!consume(parser, TOKEN_LPAREN, "'(' after Terminal method")) {
            expr_free(call);
            return NULL;
        }

        /* Terminal.Write is variadic; Terminal.ReadLine takes none. Both forms are
         * accepted here and the arity is checked at run time, which keeps the
         * grammar identical for every builtin. */
        if (!check(parser, TOKEN_RPAREN)) {
            size_t capacity = 4;
            call->arguments = malloc(capacity * sizeof(Expr *));
            if (call->arguments == NULL) {
                fail(parser, token->start, "out of memory");
                expr_free(call);
                return NULL;
            }

            do {
                if (call->argument_count + 1 > capacity) {
                    capacity *= 2;
                    Expr **grown = realloc(call->arguments, capacity * sizeof(Expr *));
                    if (grown == NULL) {
                        fail(parser, token->start, "out of memory");
                        expr_free(call);
                        return NULL;
                    }
                    call->arguments = grown;
                }
                Expr *argument = parse_expression(parser);
                if (parser->failed) {
                    expr_free(call);
                    return NULL;
                }
                call->arguments[call->argument_count++] = argument;
            } while (match(parser, TOKEN_COMMA));
        }

        if (!consume(parser, TOKEN_RPAREN, "closing ')'")) {
            expr_free(call);
            return NULL;
        }
        return call;
    }

    if (match(parser, TOKEN_NUMBER)) {
        Expr *expr = expr_new(parser, EXPR_NUMBER, token->start);
        if (expr == NULL) return NULL;
        /* Digits arrive as owned text, so no access to the source buffer. */
        expr->number = strtoll(token->text, NULL,
                             token->length >= 2 && token->text[0] == '0' &&
                             (token->text[1] == 'x' || token->text[1] == 'X')
                                 ? 16
                                 : 10);
        return expr;
    }

    if (match(parser, TOKEN_CHARACTER)) {
        Expr *expr = expr_new(parser, EXPR_CHARACTER, token->start);
        if (expr == NULL) return NULL;
        expr->text = copy_text(token->text, 1);
        expr->text_length = 1;
        return expr;
    }

    /* True and False are Number literals 1 and 0 (decision 17), not a distinct
     * boolean type. The node is still marked EXPR_BOOL so the interpreter can
     * print them in their source form rather than as digits. */
    if (match(parser, TOKEN_TRUE) || match(parser, TOKEN_FALSE)) {
        Expr *expr = expr_new(parser, EXPR_BOOL, token->start);
        if (expr == NULL) return NULL;
        expr->boolean = (token->type == TOKEN_TRUE);
        return expr;
    }

    if (match(parser, TOKEN_STRING)) {
        Expr *expr = expr_new(parser, EXPR_STRING, token->start);
        if (expr == NULL) return NULL;
        expr->text = copy_text(token->text, token->length);
        expr->text_length = token->length;
        return expr;
    }

    if (match(parser, TOKEN_IDENT)) {
        Expr *expr = expr_new(parser, EXPR_IDENT, token->start);
        if (expr == NULL) return NULL;
        expr->name = copy_ident(token);
        return expr;
    }

    if (match(parser, TOKEN_LPAREN)) {
        Expr *inner = parse_expression(parser);
        if (parser->failed) return NULL;
        if (!consume(parser, TOKEN_RPAREN, "closing ')'")) return NULL;
        return inner;
    }

    fail(parser, token->start, "expected expression, found %s",
         token_type_description(token->type));
    return NULL;
}

/* --- Postfix: field access, struct literals, union construction ------------ */

/* Handles `expr.field`, `Type { field: value, ... }`, and `Type.Variant(...)`.
 *
 * These are all postfix: they attach to whatever expression precedes them, so
 * they sit below assignment in the precedence chain and are reached from
 * parse_call. */
/* --- Call, unary, and the precedence climbing levels ------------------------ */

/* --- Postfix: field access, struct literals, union construction ------------ */

/* Handles `expr.field`, `Type { field: value, ... }`, and `Type.Variant(...)`.
 *
 * These are all postfix -- they attach to whatever expression precedes them --
 * so they sit above calls in the precedence chain and are reached from
 * parse_call.
 *
 * Three shapes share one production, which is why the runtime has to resolve
 * them:
 *
 *   `p.x`              a struct field read
 *   `Value.Number(n)`  a union construction, one payload
 *   `a.Allocate(n)`    a method call, any number of arguments
 *
 * `Name.Variant` is `IDENT . IDENT`, and `a.Allocate` is too. No syntactic
 * lookahead separates them. Both produce one node and the interpreter decides
 * from the declared types, which is the only place the answer exists. */
/* Parses the arms of a `Catch`:
 *
 *     Catch {
 *         NotFound { Terminal.Write("missing"); 0 }
 *         Denied {
 *             Terminal.Write("denied");
 *             0
 *         }
 *     }
 *
 * An arm is a variant name, an optional payload binding, and a block -- the
 * same shape as a Match arm, so there is one grammar for exhaustiveness rather
 * than two. The one difference is checked by the caller: a Catch arm's block
 * must end with an expression, and that expression's value is the arm's result.
 *
 * Unlike a Match arm there is no `Type.Variant`, because the error set is
 * already known from the callee's declared return type. Naming it per arm would
 * repeat what the surrounding expression has said once.
 */
static bool parse_catch_arms(Parser *parser, Expr *handle) {
    if (!consume(parser, TOKEN_LBRACE, "'{' to open a Catch body")) return false;

    size_t capacity = 4;
    handle->arms = malloc(capacity * sizeof(MatchArm));
    if (handle->arms == NULL) {
        fail(parser, handle->position, "out of memory");
        return false;
    }

    while (!check(parser, TOKEN_RBRACE)) {
        if (check(parser, TOKEN_EOF)) {
            fail(parser, handle->position, "unclosed '{' -- missing '}'");
            return false;
        }

        if (!check(parser, TOKEN_IDENT)) {
            fail(parser, current(parser)->start,
                 "expected an error name at the start of a Catch arm, found %s",
                 token_type_description(current(parser)->type));
            return false;
        }
        const Token *variant_name = advance_token(parser);

        if (handle->arm_count + 1 > capacity) {
            capacity *= 2;
            MatchArm *grown = realloc(handle->arms, capacity * sizeof(MatchArm));
            if (grown == NULL) {
                fail(parser, handle->position, "out of memory");
                return false;
            }
            handle->arms = grown;
        }

        MatchArm *arm = &handle->arms[handle->arm_count];
        memset(arm, 0, sizeof(*arm));
        arm->position = variant_name->start;
        arm->variant = copy_ident(variant_name);
        if (arm->variant == NULL) {
            fail(parser, variant_name->start, "out of memory");
            return false;
        }
        handle->arm_count++;

        /* The binding slot is accepted now, before error payloads exist, so
         * adding them later does not change this grammar. */
        if (match(parser, TOKEN_LPAREN)) {
            if (!check(parser, TOKEN_IDENT)) {
                fail(parser, current(parser)->start,
                     "expected a name to bind the payload to, found %s",
                     token_type_description(current(parser)->type));
                return false;
            }
            arm->binding = copy_ident(advance_token(parser));
            if (!consume(parser, TOKEN_RPAREN, "closing ')' after a payload binding")) {
                return false;
            }
        }

        if (!parse_block(parser, &arm->body, &arm->body_count)) return false;

        /* Every arm must produce a value, because the Catch as a whole stands in
         * for the success value. `Fail` is also accepted as the last statement:
         * it gives up and propagates that error rather than recovering, so there
         * is no value to produce. */
        if (arm->body_count == 0 ||
            (arm->body[arm->body_count - 1]->type != STMT_EXPR &&
             arm->body[arm->body_count - 1]->type != STMT_FAIL)) {
            fail(parser, arm->position,
                 "Catch arm '%s' must end with an expression giving the value to "
                 "use, or with Fail", arm->variant);
            return false;
        }

        /* A comma between arms is optional; each arm ends with its own block. */
        match(parser, TOKEN_COMMA);
    }

    if (!consume(parser, TOKEN_RBRACE, "closing '}' of a Catch body")) return false;

    if (handle->arm_count == 0) {
        fail(parser, handle->position, "Catch must have at least one arm");
        return false;
    }
    return true;
}

static Expr *parse_postfix(Parser *parser, Expr *base) {
    if (parser->failed || base == NULL) return NULL;

    /* `Name.Variant` with an optional parenthesised payload: a union
     * construction when Name is a union type, a field read otherwise.
     *
     * is_name_token rather than TOKEN_IDENT because a variant may be named
     * after a type keyword -- `Value.Number(42)` is the natural spelling. */
    if (base->type == EXPR_IDENT && check(parser, TOKEN_DOT) &&
        is_name_token(parser->tokens[parser->position + 1].type)) {
        advance_token(parser); /* '.' */
        const Token *member = advance_token(parser);

        Expr *access = expr_new(parser, EXPR_FIELD, member->start);
        if (access == NULL) return NULL;
        access->left = base;
        access->name = copy_ident(member);

        /* A '(' turns it into a construction or a method call, which is how
         * `Value.Number(42)` and `a.Allocate(16)` are both written.
         *
         * The full argument list is parsed here rather than assuming one,
         * because methods take several arguments and a union variant takes
         * exactly one. */
        if (match(parser, TOKEN_LPAREN)) {
            /* Zero arguments is legal: `arena.Used()` takes none. */
            if (!check(parser, TOKEN_RPAREN)) {
                do {
                if (access->argument_count == 0) {
                    access->arguments = malloc(sizeof(Expr *));
                } else {
                    access->arguments = realloc(access->arguments,
                                                (access->argument_count + 1) * sizeof(Expr *));
                }
                if (access->arguments == NULL) {
                    fail(parser, access->position, "out of memory");
                    expr_free(access);
                    return NULL;
                }

                Expr *argument = parse_expression(parser);
                if (parser->failed) {
                    expr_free(access);
                    return NULL;
                }
                argument = parse_postfix(parser, argument);
                if (parser->failed) {
                    expr_free(argument);
                    expr_free(access);
                    return NULL;
                }
                access->arguments[access->argument_count++] = argument;
                } while (match(parser, TOKEN_COMMA));
            }

            if (!consume(parser, TOKEN_RPAREN, "closing ')' after a payload or arguments")) {
                expr_free(access);
                return NULL;
            }

            /* A union variant carries exactly one payload, so its single
             * argument is what `payload` names. Deliberately the *same* node,
             * not a second pointer to it: expr_free releases both fields, and a
             * shared pointer there is a double free. */
            if (access->argument_count == 1) {
                access->payload = access->arguments[0];
                access->arguments[0] = NULL;
            }
        }

        return parse_postfix(parser, access);
    }

    /* `expr.field` */
    if (match(parser, TOKEN_DOT)) {
        if (!check(parser, TOKEN_IDENT)) {
            fail(parser, current(parser)->start,
                 "expected a field name after '.', found %s",
                 token_type_description(current(parser)->type));
            return NULL;
        }
        const Token *member = advance_token(parser);

        Expr *access = expr_new(parser, EXPR_FIELD, member->start);
        if (access == NULL) return NULL;
        access->left = base;
        access->name = copy_ident(member);
        return parse_postfix(parser, access);
    }

    /* `Type { field: value, ... }` -- a struct literal.
     *
     * Suppressed inside a Match arm, where `V.A { ... }` would otherwise be
     * read as a literal of a type named `V` with a field named `A`. */
    if (check(parser, TOKEN_LBRACE) && base->type == EXPR_IDENT &&
        !parser->suppress_struct_literal) {
        Expr *literal = expr_new(parser, EXPR_STRUCT_INIT, base->position);
        if (literal == NULL) return NULL;
        literal->name = base->name;
        base->name = NULL;
        expr_free(base);

        advance_token(parser); /* '{' */

        size_t capacity = 4;
        literal->arguments = malloc(capacity * sizeof(Expr *));
        if (literal->arguments == NULL) {
            fail(parser, literal->position, "out of memory");
            expr_free(literal);
            return NULL;
        }

        while (!check(parser, TOKEN_RBRACE)) {
            if (check(parser, TOKEN_EOF)) {
                fail(parser, literal->position, "unclosed '{' -- missing '}'");
                expr_free(literal);
                return NULL;
            }
            if (!check(parser, TOKEN_IDENT)) {
                fail(parser, current(parser)->start,
                     "expected a field name, found %s",
                     token_type_description(current(parser)->type));
                expr_free(literal);
                return NULL;
            }
            const Token *field_name = advance_token(parser);
            if (!consume(parser, TOKEN_COLON, "':' after a field name")) {
                expr_free(literal);
                return NULL;
            }

            /* Each field is an EXPR_ASSIGN, so the name travels with its
             * value and order does not matter at the call site. */
            Expr *field = expr_new(parser, EXPR_ASSIGN, field_name->start);
            if (field == NULL) {
                expr_free(literal);
                return NULL;
            }
            field->name = copy_ident(field_name);
            field->right = parse_expression(parser);
            if (parser->failed) {
                expr_free(field);
                expr_free(literal);
                return NULL;
            }

            if (literal->argument_count + 1 > capacity) {
                capacity *= 2;
                Expr **grown = realloc(literal->arguments, capacity * sizeof(Expr *));
                if (grown == NULL) {
                    expr_free(field);
                    expr_free(literal);
                    fail(parser, literal->position, "out of memory");
                    return NULL;
                }
                literal->arguments = grown;
            }
            literal->arguments[literal->argument_count++] = field;

            if (!match(parser, TOKEN_COMMA)) break;
        }

        if (!consume(parser, TOKEN_RBRACE, "closing '}' of a struct literal")) {
            expr_free(literal);
            return NULL;
        }
        if (literal->argument_count == 0) {
            fail(parser, literal->position, "struct literal must set at least one field");
            expr_free(literal);
            return NULL;
        }

        return parse_postfix(parser, literal);
    }

    /* `expr Try` -- propagate the error from the enclosing function.
     *
     * Postfix rather than a `Try(expr)` prefix so there is exactly one spelling
     * of the operator, and so it composes: it can end a Return, sit under a
     * Catch, or initialise a variable. */
    if (match(parser, TOKEN_TRY)) {
        Expr *propagate = expr_new(parser, EXPR_TRY, current(parser)->start);
        if (propagate == NULL) {
            expr_free(base);
            return NULL;
        }
        propagate->position = base->position;
        propagate->left = base;

        /* Rule 1: a fallible call is handled here, so record that on the call
         * itself. The parser is the only place this relationship is visible. */
        if (base->type == EXPR_CALL || base->type == EXPR_FIELD) base->handled = true;

        return parse_postfix(parser, propagate);
    }

    /* `expr Catch { Variant { ... } ... }` -- handle the error locally. */
    if (match(parser, TOKEN_CATCH)) {
        Expr *handle = expr_new(parser, EXPR_CATCH, current(parser)->start);
        if (handle == NULL) {
            expr_free(base);
            return NULL;
        }
        handle->position = base->position;
        handle->left = base;
        if (base->type == EXPR_CALL || base->type == EXPR_FIELD) base->handled = true;

        if (!parse_catch_arms(parser, handle)) {
            expr_free(handle);
            return NULL;
        }
        return parse_postfix(parser, handle);
    }

    return base;
}


static Expr *parse_call(Parser *parser) {
    Position start = current(parser)->start;
    Expr *callee = parse_primary(parser);
    if (parser->failed) return NULL;
    /* Postfix forms attach after a primary, so `Type { ... }` and `a.field`
     * are handled here. parse_postfix returns the base unchanged when neither
     * a dot nor a literal brace follows, which is the common case. */
    callee = parse_postfix(parser, callee);
    if (parser->failed) return NULL;

    if (!check(parser, TOKEN_LPAREN)) return callee;

    advance_token(parser); /* consume '(' */

    Expr *call = expr_new(parser, EXPR_CALL, start);
    if (call == NULL) {
        expr_free(callee);
        return NULL;
    }
    call->callee = callee;

    /* Phase 2 resolves calls by name only. Rejecting the general case here
     * gives a specific message instead of a confusing runtime failure. */
    if (callee->type != EXPR_IDENT) {
        fail(parser, start, "can only call named functions in Phase 2");
        expr_free(call);
        return NULL;
    }

    if (!check(parser, TOKEN_RPAREN)) {
        size_t capacity = 4;
        call->arguments = malloc(capacity * sizeof(Expr *));
        if (call->arguments == NULL) {
            fail(parser, start, "out of memory");
            expr_free(call);
            return NULL;
        }

        do {
            if (call->argument_count + 1 > capacity) {
                capacity *= 2;
                Expr **grown = realloc(call->arguments, capacity * sizeof(Expr *));
                if (grown == NULL) {
                    fail(parser, start, "out of memory");
                    expr_free(call);
                    return NULL;
                }
                call->arguments = grown;
            }
            Expr *argument = parse_expression(parser);
            if (parser->failed) {
                expr_free(call);
                return NULL;
            }
            /* Through the postfix chain, so that a union construction or a
             * struct literal parses as an argument: `f(Value.Number(2))` and
             * `f(Point { x: 1 })` both start with an identifier followed by a
             * dot or a brace, which parse_expression alone does not consume. */
            argument = parse_postfix(parser, argument);
            if (parser->failed) {
                expr_free(argument);
                expr_free(call);
                return NULL;
            }
            call->arguments[call->argument_count++] = argument;
        } while (match(parser, TOKEN_COMMA));
    }

    if (!consume(parser, TOKEN_RPAREN, "closing ')'")) {
        expr_free(call);
        return NULL;
    }
    return parse_postfix(parser, call);
}

static Expr *parse_unary(Parser *parser) {
    Position start = current(parser)->start;

    if (match(parser, TOKEN_BANG) || match(parser, TOKEN_MINUS) || match(parser, TOKEN_TILDE)) {
        /* Read the operator back off the token just consumed, rather than
         * re-testing current() which has already advanced. */
        TokenType applied = parser->tokens[parser->position - 1].type;
        Operator op = applied == TOKEN_BANG    ? OP_NOT
                    : applied == TOKEN_MINUS  ? OP_NEGATE
                    : applied == TOKEN_TILDE  ? OP_BITWISE_NOT
                                              : OP_NOT;

        Expr *expr = expr_new(parser, EXPR_UNARY, start);
        if (expr == NULL) return NULL;
        expr->op = op;
        expr->right = parse_unary(parser);
        if (parser->failed) {
            expr_free(expr);
            return NULL;
        }
        return expr;
    }

    return parse_call(parser);
}

/* Binary levels, ordered by precedence. Each level loops until the next token
 * is not part of it, which makes every operator here left-associative.
 *
 * `tighter` parses one level down: it is the precedence-climbing parameter that
 * makes the chain explicit rather than hard-coded. */
typedef struct {
    TokenType token;
    Operator op;
} BinaryOperator;

typedef Expr *(*SubParser)(Parser *parser);

static Expr *parse_binary(Parser *parser, const BinaryOperator *level, size_t level_count,
                          SubParser tighter) {
    Expr *left = tighter(parser);
    if (parser->failed) return NULL;

    while (true) {
        const BinaryOperator *matched = NULL;
        for (size_t i = 0; i < level_count; i++) {
            if (check(parser, level[i].token)) {
                matched = &level[i];
                break;
            }
        }
        if (matched == NULL) return left;

        advance_token(parser);
        Expr *node = expr_new(parser, EXPR_BINARY, left->position);
        if (node == NULL) {
            expr_free(left);
            return NULL;
        }
        node->op = matched->op;
        node->left = left;
        node->right = tighter(parser);
        if (parser->failed) {
            expr_free(node);
            return NULL;
        }
        left = node;
    }
}

static Expr *parse_factor(Parser *parser) {
    static const BinaryOperator level[] = {
        {TOKEN_STAR, OP_MULTIPLY}, {TOKEN_SLASH, OP_DIVIDE}
    };
    return parse_binary(parser, level, sizeof(level) / sizeof(level[0]), parse_unary);
}

static Expr *parse_term(Parser *parser) {
    static const BinaryOperator level[] = {
        {TOKEN_PLUS, OP_ADD}, {TOKEN_MINUS, OP_SUBTRACT}
    };
    return parse_binary(parser, level, sizeof(level) / sizeof(level[0]), parse_factor);
}

static Expr *parse_comparison(Parser *parser) {
    static const BinaryOperator level[] = {
        {TOKEN_LT, OP_LESS},    {TOKEN_LT_EQ, OP_LESS_EQUAL},
        {TOKEN_GT, OP_GREATER}, {TOKEN_GT_EQ, OP_GREATER_EQUAL}
    };
    return parse_binary(parser, level, sizeof(level) / sizeof(level[0]), parse_shift);
}

static Expr *parse_equality(Parser *parser) {
    static const BinaryOperator level[] = {
        {TOKEN_EQ, OP_EQUAL}, {TOKEN_BANG_EQ, OP_NOT_EQUAL}
    };
    return parse_binary(parser, level, sizeof(level) / sizeof(level[0]), parse_comparison);
}

/* Shifts bind tighter than comparison and looser than additive, as in C. */
static Expr *parse_shift(Parser *parser) {
    static const BinaryOperator level[] = {
        {TOKEN_SHIFT_LEFT, OP_SHIFT_LEFT}, {TOKEN_SHIFT_RIGHT, OP_SHIFT_RIGHT}
    };
    return parse_binary(parser, level, sizeof(level) / sizeof(level[0]), parse_term);
}

static Expr *parse_bitwise_and(Parser *parser) {
    static const BinaryOperator level[] = {
        {TOKEN_AMPERSAND, OP_BITWISE_AND}
    };
    return parse_binary(parser, level, sizeof(level) / sizeof(level[0]), parse_equality);
}

static Expr *parse_bitwise_xor(Parser *parser) {
    static const BinaryOperator level[] = {
        {TOKEN_CARET, OP_BITWISE_XOR}
    };
    return parse_binary(parser, level, sizeof(level) / sizeof(level[0]), parse_bitwise_and);
}

static Expr *parse_bitwise_or(Parser *parser) {
    static const BinaryOperator level[] = {
        {TOKEN_PIPE, OP_BITWISE_OR}
    };
    return parse_binary(parser, level, sizeof(level) / sizeof(level[0]), parse_bitwise_xor);
}

/* Assignment is right-associative: `a = b = 1` assigns 1 to b then to a.
 *
 * Two forms. A bare identifier assigns to a variable; an EXPR_FIELD assigns to
 * a struct field. Both are represented by EXPR_ASSIGN, distinguished by
 * whether `left` is set. */
static Expr *parse_assignment(Parser *parser) {
    if (check(parser, TOKEN_IDENT)) {
        /* `ident = ...` */
        if (parser->tokens[parser->position + 1].type == TOKEN_ASSIGN) {
            Position start = current(parser)->start;
            const Token *name = advance_token(parser);
            advance_token(parser); /* '=' */

            Expr *node = expr_new(parser, EXPR_ASSIGN, start);
            if (node == NULL) return NULL;
            node->name = copy_ident(name);
            node->right = parse_assignment(parser);
            if (parser->failed) {
                expr_free(node);
                return NULL;
            }
            return node;
        }

        /* `ident.field = ...` -- build the field access, then assign to it. The
         * dot is known to be there, so the target is parsed directly rather
         * than speculatively through the postfix chain. */
        /* The lookahead must confirm the whole shape `ident . ident =`. Checking only
         * for a dot would swallow `V.A` in `Variable v: V = V.A;` and mistake the
         * union construction for a field assignment target. */
        if (parser->tokens[parser->position + 1].type == TOKEN_DOT &&
            parser->tokens[parser->position + 2].type == TOKEN_IDENT &&
            parser->tokens[parser->position + 3].type == TOKEN_ASSIGN) {
            Position start = current(parser)->start;
            const Token *object = advance_token(parser);
            advance_token(parser); /* '.' */

            if (!check(parser, TOKEN_IDENT)) {
                fail(parser, current(parser)->start,
                     "expected a field name after '.', found %s",
                     token_type_description(current(parser)->type));
                return NULL;
            }
            const Token *member = advance_token(parser);

            Expr *node = expr_new(parser, EXPR_ASSIGN, start);
            if (node == NULL) return NULL;

            Expr *base = expr_new(parser, EXPR_IDENT, object->start);
            if (base == NULL) {
                expr_free(node);
                return NULL;
            }
            base->name = copy_ident(object);

            node->left = expr_new(parser, EXPR_FIELD, member->start);
            if (node->left == NULL) {
                expr_free(base);
                expr_free(node);
                return NULL;
            }
            node->left->left = base;
            node->left->name = copy_ident(member);

            if (!consume(parser, TOKEN_ASSIGN, "'=' after a field name")) {
                expr_free(node);
                return NULL;
            }
            node->right = parse_assignment(parser);
            if (parser->failed) {
                expr_free(node);
                return NULL;
            }
            return node;
        }
    }

    return parse_bitwise_or(parser);
}

static Expr *parse_expression(Parser *parser) {
    return parse_assignment(parser);
}

/* --- Statements ------------------------------------------------------------ */

/* Parses `{ statement* }` into a heap-owned array. The caller takes ownership
 * of the array but not of the block wrapper, if any. */
static bool parse_block(Parser *parser, Stmt ***out_body, size_t *out_count) {
    Position start = current(parser)->start;
    if (!consume(parser, TOKEN_LBRACE, "'{'")) return false;

    Stmt **body = NULL;
    size_t count = 0;
    size_t capacity = 0;

    while (!check(parser, TOKEN_RBRACE)) {
        if (check(parser, TOKEN_EOF)) {
            fail(parser, start, "unclosed '{' -- missing '}'");
            free(body);
            return false;
        }

        Stmt *statement = parse_statement(parser);
        if (parser->failed) {
            free(body);
            return false;
        }
        if (statement == NULL) continue;

        if (count + 1 > capacity) {
            capacity = capacity ? capacity * 2 : 8;
            Stmt **grown = realloc(body, capacity * sizeof(Stmt *));
            if (grown == NULL) {
                stmt_free(statement);
                free(body);
                fail(parser, start, "out of memory");
                return false;
            }
            body = grown;
        }
        body[count++] = statement;
    }

    advance_token(parser); /* '}' */

    *out_body = body;
    *out_count = count;
    return true;
}

static Stmt *parse_variable_declaration(Parser *parser) {
    Position start = current(parser)->start;
    advance_token(parser); /* 'var' */

    if (!check(parser, TOKEN_IDENT)) {
        fail(parser, current(parser)->start, "expected variable name after 'var', found %s",
             token_type_description(current(parser)->type));
        return NULL;
    }
    const Token *name = advance_token(parser);

    Stmt *stmt = stmt_new(parser, STMT_VARIABLE, start);
    if (stmt == NULL) return NULL;
    stmt->name = copy_ident(name);

    if (!consume(parser, TOKEN_COLON, "':' after variable name") ||
        !parse_type(parser, &stmt->declared_type, &stmt->type_name)) {
        stmt_free(stmt);
        return NULL;
    }

    if (match(parser, TOKEN_ASSIGN)) {
        stmt->initializer = parse_expression(parser);
        if (parser->failed) {
            stmt_free(stmt);
            return NULL;
        }
    }

    if (!consume(parser, TOKEN_SEMICOLON, "")) {
        stmt_free(stmt);
        return NULL;
    }
    return stmt;
}

static Stmt *parse_function_declaration(Parser *parser) {
    Position start = current(parser)->start;
    advance_token(parser); /* 'fn' */

    if (!check(parser, TOKEN_IDENT)) {
        fail(parser, current(parser)->start, "expected function name after 'fn', found %s",
             token_type_description(current(parser)->type));
        return NULL;
    }
    const Token *name = advance_token(parser);

    Stmt *stmt = stmt_new(parser, STMT_FUNCTION, start);
    if (stmt == NULL) return NULL;
    stmt->name = copy_ident(name);

    if (!consume(parser, TOKEN_LPAREN, "'(' after function name")) {
        stmt_free(stmt);
        return NULL;
    }

    if (!check(parser, TOKEN_RPAREN)) {
        size_t capacity = 4;
        stmt->parameters = malloc(capacity * sizeof(Parameter));
        if (stmt->parameters == NULL) {
            stmt_free(stmt);
            fail(parser, start, "out of memory");
            return NULL;
        }

        do {
            if (stmt->parameter_count + 1 > capacity) {
                capacity *= 2;
                Parameter *grown = realloc(stmt->parameters, capacity * sizeof(Parameter));
                if (grown == NULL) {
                    stmt_free(stmt);
                    fail(parser, start, "out of memory");
                    return NULL;
                }
                stmt->parameters = grown;
            }

            if (!check(parser, TOKEN_IDENT)) {
                fail(parser, current(parser)->start, "expected parameter name, found %s",
                     token_type_description(current(parser)->type));
                stmt_free(stmt);
                return NULL;
            }
            const Token *parameter_name = advance_token(parser);
            Parameter *parameter = &stmt->parameters[stmt->parameter_count++];
            /* Zeroed because the array comes from malloc, not calloc, and
             * parse_type only writes type_name on the named-type path. Left
             * uninitialised, a primitive parameter such as `n: Number` holds
             * whatever was on the heap, and stmt_free later frees that. It
             * reads as harmless because the field is only read on one path --
             * the free does not know which. */
            memset(parameter, 0, sizeof(*parameter));
            parameter->name = copy_ident(parameter_name);
            parameter->position = parameter_name->start;

            if (!consume(parser, TOKEN_COLON, "':' after parameter name") ||
                !parse_type(parser, &parameter->type, &parameter->type_name)) {
                stmt_free(stmt);
                return NULL;
            }
        } while (match(parser, TOKEN_COMMA));
    }

    if (!consume(parser, TOKEN_RPAREN, "closing ')'")) {
        stmt_free(stmt);
        return NULL;
    }

    /* Optional return type: `-> Number`. Omitting it means the function
     * returns nothing, which is the common case for early phases and makes an
     * accidental `Return value` in a void function a clear error later. */
    if (match(parser, TOKEN_MINUS)) {
        if (!consume(parser, TOKEN_GT, "'>' to complete '->'")) {
            stmt_free(stmt);
            return NULL;
        }
        if (match(parser, TOKEN_RESULT)) {
            if (!parse_result_type(parser, stmt)) {
                stmt_free(stmt);
                return NULL;
            }
        } else if (!parse_type(parser, &stmt->declared_type, &stmt->type_name)) {
            stmt_free(stmt);
            return NULL;
        }
        stmt->declares_return = true;
    }

    if (!parse_block(parser, &stmt->body, &stmt->body_count)) {
        stmt_free(stmt);
        return NULL;
    }
    return stmt;
}

static Stmt *parse_if(Parser *parser) {
    Position start = current(parser)->start;
    advance_token(parser); /* 'if' */

    Stmt *stmt = stmt_new(parser, STMT_IF, start);
    if (stmt == NULL) return NULL;

    /* The '{' after the condition opens the body, not a struct literal. */
    parser->suppress_struct_literal = true;
    stmt->expression = parse_expression(parser);
    parser->suppress_struct_literal = false;
    if (parser->failed) {
        stmt_free(stmt);
        return NULL;
    }

    if (!parse_block(parser, &stmt->then_body, &stmt->then_count)) {
        stmt_free(stmt);
        return NULL;
    }

    if (match(parser, TOKEN_ELSE)) {
        if (check(parser, TOKEN_IF)) {
            stmt->else_branch = parse_if(parser);
        } else {
            Stmt *else_block = stmt_new(parser, STMT_BLOCK, current(parser)->start);
            if (else_block == NULL) {
                stmt_free(stmt);
                return NULL;
            }
            if (!parse_block(parser, &else_block->body, &else_block->body_count)) {
                stmt_free(else_block);
                stmt_free(stmt);
                return NULL;
            }
            stmt->else_branch = else_block;
        }
        if (parser->failed) {
            stmt_free(stmt);
            return NULL;
        }
    }
    return stmt;
}

static Stmt *parse_loop(Parser *parser) {
    Position start = current(parser)->start;
    advance_token(parser); /* 'while' */

    Stmt *stmt = stmt_new(parser, STMT_LOOP, start);
    if (stmt == NULL) return NULL;

    /* As with If: the '{' opens the body. */
    parser->suppress_struct_literal = true;
    stmt->expression = parse_expression(parser);
    parser->suppress_struct_literal = false;
    if (parser->failed) {
        stmt_free(stmt);
        return NULL;
    }

    if (!parse_block(parser, &stmt->loop_body_statements, &stmt->loop_count)) {
        stmt_free(stmt);
        return NULL;
    }
    return stmt;
}

/* --- Aggregate declarations ------------------------------------------------ */

/* Shared by Struct, Union, and Error. All three are `Name { ... }`, differing
 * only in what a field means: a struct field is `name: Type`, while a union
 * variant is either a bare `Name` or `Name(Type)`. */
/* True when the token may be used as a field or variant name.
 *
 * Type keywords are allowed here even though they cannot be identifiers
 * elsewhere: `Number(Number)` is the natural way to name a union variant, and
 * forbidding it would force every such union to pick a contrived name. The
 * keyword table gives these tokens their own text, so the name survives. */
static bool is_name_token(TokenType type) {
    switch (type) {
        case TOKEN_IDENT:
        case TOKEN_NUMBER_TYPE:
        case TOKEN_NUMBER32_TYPE:
        case TOKEN_WORD_TYPE:
        case TOKEN_CHARACTER_TYPE:
            return true;
        default:
            return false;
    }
}


static Stmt *parse_aggregate(Parser *parser, DeclKind kind, TokenType keyword) {
    Position start = current(parser)->start;
    advance_token(parser); /* Struct / Union / Error */

    if (!check(parser, TOKEN_IDENT)) {
        fail(parser, current(parser)->start,
             "expected a name after '%s', found %s", token_type_name(keyword),
             token_type_description(current(parser)->type));
        return NULL;
    }
    const Token *name = advance_token(parser);

    Stmt *stmt = stmt_new(parser,
                          (kind == DECL_STRUCT) ? STMT_STRUCT : STMT_UNION, start);
    if (stmt == NULL) return NULL;
    stmt->decl_kind = kind;
    stmt->name = copy_ident(name);

    if (!consume(parser, TOKEN_LBRACE, "'{'")) {
        stmt_free(stmt);
        return NULL;
    }

    size_t capacity = 4;
    stmt->fields = malloc(capacity * sizeof(Field));
    if (stmt->fields == NULL) {
        stmt_free(stmt);
        fail(parser, start, "out of memory");
        return NULL;
    }

    while (!check(parser, TOKEN_RBRACE)) {
        if (check(parser, TOKEN_EOF)) {
            fail(parser, start, "unclosed '{' -- missing '}'");
            stmt_free(stmt);
            return NULL;
        }

        if (!is_name_token(current(parser)->type)) {
            fail(parser, current(parser)->start,
                 "expected a field or variant name, found %s",
                 token_type_description(current(parser)->type));
            stmt_free(stmt);
            return NULL;
        }
        const Token *field_name = advance_token(parser);

        if (stmt->field_count + 1 > capacity) {
            capacity *= 2;
            Field *grown = realloc(stmt->fields, capacity * sizeof(Field));
            if (grown == NULL) {
                stmt_free(stmt);
                fail(parser, start, "out of memory");
                return NULL;
            }
            stmt->fields = grown;
        }

        Field *field = &stmt->fields[stmt->field_count];
        memset(field, 0, sizeof(*field));
        field->name = copy_ident(field_name);
        field->position = field_name->start;
        field->type = TYPE_NUMBER; /* placeholder; not meaningful without a payload */
        stmt->field_count++;

        if (kind == DECL_STRUCT) {
            if (!consume(parser, TOKEN_COLON, "':' after a struct field name") ||
                !parse_type(parser, &field->type, NULL)) {
                stmt_free(stmt);
                return NULL;
            }
        } else if (match(parser, TOKEN_LPAREN)) {
            /* A union variant with a payload: Name(Type). */
            field->has_payload = true;
            if (!parse_type(parser, &field->type, NULL)) {
                stmt_free(stmt);
                return NULL;
            }
            if (!consume(parser, TOKEN_RPAREN, "closing ')' after a variant payload type")) {
                stmt_free(stmt);
                return NULL;
            }
        }

        if (!match(parser, TOKEN_COMMA)) break;
    }

    if (!consume(parser, TOKEN_RBRACE, "closing '}'")) {
        stmt_free(stmt);
        return NULL;
    }

    /* A trailing comma before '}' is common and harmless; the loop above
     * handles it by breaking out, so nothing more is needed here. */

    if (stmt->field_count == 0) {
        fail(parser, start, "'%s' must declare at least one field", stmt->name);
        stmt_free(stmt);
        return NULL;
    }

    /* Duplicate field names would make construction ambiguous. */
    for (size_t i = 0; i < stmt->field_count; i++) {
        for (size_t j = i + 1; j < stmt->field_count; j++) {
            if (strcmp(stmt->fields[i].name, stmt->fields[j].name) == 0) {
                fail(parser, stmt->fields[j].position,
                     "'%s' is declared twice in '%s'", stmt->fields[j].name, stmt->name);
                stmt_free(stmt);
                return NULL;
            }
        }
    }

    return stmt;
}

/* --- Match ----------------------------------------------------------------- */

/* Match subject { Type.Variant(binding) { ... } ... }
 *
 * The type name is repeated on every arm rather than inferred from a leading
 * `Match v {`, which keeps each arm self-describing and makes the exhaustiveness
 * check a simple comparison against the declaration. */
/* `Fail Variant;` -- raise one error from the enclosing function.
 *
 * The variant name is written bare, not `Io.NotFound`, because the enclosing
 * function's signature already names the error set and there is only one. The
 * set is fixed by `-> Result{T, Io}` above the body, so repeating it on every
 * Fail would say nothing the reader does not already have.
 *
 * This is what lets a leaf function fail at all. Try only propagates, so
 * without a way to originate an error no function could ever return one and the
 * whole mechanism would be untestable.
 */
/* `allocator(x) { ... }` -- binds `x` as the current allocator for the block.
 *
 * The expression is evaluated once, on entry. `allocator(scratch.Get())` and
 * `allocator(next_free_arena())` are therefore both well defined, which a
 * binding of the name alone would not be.
 */
static Stmt *parse_allocator_block(Parser *parser) {
    Position start = current(parser)->start;
    advance_token(parser); /* 'Allocator' */

    Stmt *stmt = stmt_new(parser, STMT_ALLOCATOR, start);
    if (stmt == NULL) return NULL;

    if (!consume(parser, TOKEN_LPAREN, "'(' after 'allocator'")) {
        stmt_free(stmt);
        return NULL;
    }

    /* Suppressed for the same reason as an If condition: `allocator(x) { ... }`
     * puts an identifier in front of a brace. */
    bool saved = parser->suppress_struct_literal;
    parser->suppress_struct_literal = true;
    stmt->expression = parse_expression(parser);
    parser->suppress_struct_literal = saved;

    if (parser->failed || !consume(parser, TOKEN_RPAREN, "closing ')' after an allocator")) {
        stmt_free(stmt);
        return NULL;
    }

    if (!parse_block(parser, &stmt->body, &stmt->body_count)) {
        stmt_free(stmt);
        return NULL;
    }
    return stmt;
}

/* `Allocator(bytes)` -- a region of that capacity. */
static Stmt *parse_fail(Parser *parser) {
    Position start = current(parser)->start;
    advance_token(parser); /* 'Fail' */

    if (!check(parser, TOKEN_IDENT)) {
        fail(parser, current(parser)->start,
             "expected an error name after 'Fail', found %s",
             token_type_description(current(parser)->type));
        return NULL;
    }

    Stmt *stmt = stmt_new(parser, STMT_FAIL, start);
    if (stmt == NULL) return NULL;
    stmt->name = copy_ident(advance_token(parser));
    if (stmt->name == NULL) {
        fail(parser, start, "out of memory");
        stmt_free(stmt);
        return NULL;
    }

    if (!consume(parser, TOKEN_SEMICOLON, "';' after a Fail")) {
        stmt_free(stmt);
        return NULL;
    }
    return stmt;
}

static Stmt *parse_match(Parser *parser) {
    Position start = current(parser)->start;
    advance_token(parser); /* Match */

    Stmt *stmt = stmt_new(parser, STMT_MATCH, start);
    if (stmt == NULL) return NULL;

    /* Arms read `Type.Variant { ... }`, which the postfix parser would
     * otherwise treat as a struct literal of a type named `Type`. The flag is
     * set before the subject is parsed so that the '{' opening the Match body
     * is never mistaken for the start of a literal either. */
    parser->suppress_struct_literal = true;

    stmt->expression = parse_expression(parser);
    if (parser->failed) {
        stmt_free(stmt);
        return NULL;
    }

    if (!consume(parser, TOKEN_LBRACE, "'{' to open a Match body")) {
        stmt_free(stmt);
        return NULL;
    }

    size_t capacity = 4;
    stmt->arms = malloc(capacity * sizeof(MatchArm));
    if (stmt->arms == NULL) {
        stmt_free(stmt);
        fail(parser, start, "out of memory");
        return NULL;
    }

    while (!check(parser, TOKEN_RBRACE)) {
        if (check(parser, TOKEN_EOF)) {
            fail(parser, start, "unclosed '{' -- missing '}'");
            stmt_free(stmt);
            return NULL;
        }

        /* Each arm names its type: `Value.Number`. */
        if (!check(parser, TOKEN_IDENT)) {
            fail(parser, current(parser)->start,
                 "expected a type name at the start of a Match arm, found %s",
                 token_type_description(current(parser)->type));
            stmt_free(stmt);
            return NULL;
        }
        const Token *type_name = advance_token(parser);
        if (!consume(parser, TOKEN_DOT, "'.' between a Match arm's type and variant")) {
            stmt_free(stmt);
            return NULL;
        }
        if (!is_name_token(current(parser)->type)) {
            fail(parser, current(parser)->start,
                 "expected a variant name after '.', found %s",
                 token_type_description(current(parser)->type));
            stmt_free(stmt);
            return NULL;
        }
        const Token *variant_name = advance_token(parser);

        if (stmt->arm_count + 1 > capacity) {
            capacity *= 2;
            MatchArm *grown = realloc(stmt->arms, capacity * sizeof(MatchArm));
            if (grown == NULL) {
                stmt_free(stmt);
                fail(parser, start, "out of memory");
                return NULL;
            }
            stmt->arms = grown;
        }

        MatchArm *arm = &stmt->arms[stmt->arm_count];
        memset(arm, 0, sizeof(*arm));
        arm->position = type_name->start;
        arm->variant = copy_ident(variant_name);
        stmt->arm_count++;

        /* The declared type name is kept on the statement so the interpreter
         * can verify every arm names the same type. */
        if (stmt->arm_count == 1) {
            stmt->name = copy_ident(type_name);
        } else if (stmt->name != NULL &&
                   strcmp(stmt->name, token_text_of(type_name)) != 0) {
            fail(parser, type_name->start,
                 "Match arms must all name the same type; this arm names a different one");
            stmt_free(stmt);
            return NULL;
        }

        /* Optional binding: `Value.Number(n)`. */
        if (match(parser, TOKEN_LPAREN)) {
            if (!check(parser, TOKEN_IDENT)) {
                fail(parser, current(parser)->start,
                     "expected a name to bind the payload to, found %s",
                     token_type_description(current(parser)->type));
                stmt_free(stmt);
                return NULL;
            }
            arm->binding = copy_ident(advance_token(parser));
            if (!consume(parser, TOKEN_RPAREN, "closing ')' after a payload binding")) {
                stmt_free(stmt);
                return NULL;
            }
        }

        if (!parse_block(parser, &arm->body, &arm->body_count)) {
            stmt_free(stmt);
            return NULL;
        }

        /* Comma between arms is optional: each arm ends with its own block,
         * so the next `Type.Variant` is unambiguous. Accepting both costs
         * nothing and the no-comma form reads better. */
        match(parser, TOKEN_COMMA);
    }

    parser->suppress_struct_literal = false;

    if (!consume(parser, TOKEN_RBRACE, "closing '}'")) {
        stmt_free(stmt);
        return NULL;
    }

    if (stmt->arm_count == 0) {
        fail(parser, start, "Match must have at least one arm");
        stmt_free(stmt);
        return NULL;
    }

    return stmt;
}

static Stmt *parse_statement(Parser *parser) {
    /* Dispatch on the upcoming token. Each branch owns its own leading token,
     * so nothing is pre-consumed here. */
    switch (current(parser)->type) {
        case TOKEN_VARIABLE:   return parse_variable_declaration(parser);
        case TOKEN_FUNCTION:    return parse_function_declaration(parser);
        case TOKEN_STRUCT:     return parse_aggregate(parser, DECL_STRUCT, TOKEN_STRUCT);
        case TOKEN_UNION:      return parse_aggregate(parser, DECL_UNION, TOKEN_UNION);
        /* `Error` is a union whose variants carry no payload. Parsed the same
         * way so a variant list reads identically. */
        case TOKEN_ERROR:      return parse_aggregate(parser, DECL_UNION, TOKEN_ERROR);
        case TOKEN_MATCH:      return parse_match(parser);
        case TOKEN_FAIL:      return parse_fail(parser);
        case TOKEN_ALLOCATOR:  return parse_allocator_block(parser);
        case TOKEN_IF:    return parse_if(parser);
    case TOKEN_LOOP:   return parse_loop(parser);
        case TOKEN_RETURN: {
            Position start = current(parser)->start;
            advance_token(parser);
            Stmt *stmt = stmt_new(parser, STMT_RETURN, start);
            if (stmt == NULL) return NULL;
            if (!check(parser, TOKEN_SEMICOLON)) {
                stmt->expression = parse_expression(parser);
                if (parser->failed) {
                    stmt_free(stmt);
                    return NULL;
                }
            }
            if (!consume(parser, TOKEN_SEMICOLON, "")) {
                stmt_free(stmt);
                return NULL;
            }
            return stmt;
        }
        case TOKEN_LBRACE: {
            Position start = current(parser)->start;
            Stmt *stmt = stmt_new(parser, STMT_BLOCK, start);
            if (stmt == NULL) return NULL;
            if (!parse_block(parser, &stmt->body, &stmt->body_count)) {
                stmt_free(stmt);
                return NULL;
            }
            return stmt;
        }
        default: {
            Position start = current(parser)->start;
            Stmt *stmt = stmt_new(parser, STMT_EXPR, start);
            if (stmt == NULL) return NULL;
            stmt->expression = parse_expression(parser);
            if (parser->failed) {
                stmt_free(stmt);
                return NULL;
            }
            if (!consume(parser, TOKEN_SEMICOLON, "")) {
                stmt_free(stmt);
                return NULL;
            }
            return stmt;
        }
    }
}

/* --- Entry point ----------------------------------------------------------- */

bool parser_parse(const Token *tokens, Program *out_program, Lexer *error_sink) {
    Parser parser = {0};
    parser.tokens = tokens;
    parser.program = out_program;

    size_t capacity = 8;
    out_program->statements = malloc(capacity * sizeof(Stmt *));
    if (out_program->statements == NULL) {
        error_sink->had_error = true;
        snprintf(error_sink->error_message, sizeof(error_sink->error_message),
                 "out of memory");
        return false;
    }

    while (!check(&parser, TOKEN_EOF) && !parser.failed) {
        Stmt *statement = parse_statement(&parser);
        if (parser.failed) break;
        if (statement == NULL) continue;

        if (out_program->count + 1 > capacity) {
            capacity *= 2;
            Stmt **grown = realloc(out_program->statements, capacity * sizeof(Stmt *));
            if (grown == NULL) {
                stmt_free(statement);
                fail(&parser, statement->position, "out of memory");
                break;
            }
            out_program->statements = grown;
        }
        out_program->statements[out_program->count++] = statement;
    }

    if (parser.failed) {
        error_sink->had_error = true;
        error_sink->error_position = parser.error_position;
        /* Bound given explicitly: the compiler cannot prove the source is
         * shorter than the destination, and warns about truncation without it. */
        snprintf(error_sink->error_message, sizeof(error_sink->error_message),
                 "%.*s", (int)sizeof(error_sink->error_message) - 1, parser.message);
        return false;
    }
    return true;
}
