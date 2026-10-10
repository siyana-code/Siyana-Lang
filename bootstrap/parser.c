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

    bool failed;
    char message[160];
    Position error_position;
} Parser;

static Stmt *parse_statement(Parser *parser);
static Expr *parse_expression(Parser *parser);
static Expr *parse_assignment(Parser *parser);

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
    free(expr->string);
    free(expr->name);
    expr_free(expr->left);
    expr_free(expr->right);
    expr_free(expr->callee);
    for (size_t i = 0; i < expr->argument_count; i++) {
        expr_free(expr->arguments[i]);
    }
    free(expr->arguments);
    free(expr);
}

static void stmt_free(Stmt *stmt) {
    if (stmt == NULL) return;
    expr_free(stmt->expression);
    expr_free(stmt->initializer);
    stmt_free(stmt->else_branch);

    for (size_t i = 0; i < stmt->body_count; i++) stmt_free(stmt->body[i]);
    free(stmt->body);

    for (size_t i = 0; i < stmt->then_count; i++) stmt_free(stmt->then_body[i]);
    free(stmt->then_body);

    for (size_t i = 0; i < stmt->loop_count; i++) stmt_free(stmt->loop_body_statements[i]);
    free(stmt->loop_body_statements);

    for (size_t i = 0; i < stmt->parameter_count; i++) free(stmt->parameters[i].name);
    free(stmt->parameters);

    free(stmt->name);
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

static bool parse_type(Parser *parser, TypeKind *out_type) {
    if (match(parser, TOKEN_INT_TYPE)) {
        *out_type = TYPE_INT;
        return true;
    }
    if (match(parser, TOKEN_BOOL)) {
        *out_type = TYPE_BOOL;
        return true;
    }
    fail(parser, current(parser)->start,
         "expected type 'int' or 'bool', found %s",
         token_type_description(current(parser)->type));
    return false;
}

/* --- Primary expressions -------------------------------------------------- */

static Expr *parse_primary(Parser *parser) {
    const Token *token = current(parser);

    /* `print` is a builtin keyword rather than an identifier, so it is
     * recognised here and lowered to a call node with a NULL callee. The
     * interpreter dispatches on that shape. */
    if (match(parser, TOKEN_PRINT)) {
        Expr *call = expr_new(parser, EXPR_CALL, token->start);
        if (call == NULL) return NULL;

        if (!consume(parser, TOKEN_LPAREN, "'(' after 'print'")) {
            expr_free(call);
            return NULL;
        }

        call->arguments = malloc(sizeof(Expr *));
        if (call->arguments == NULL) {
            fail(parser, token->start, "out of memory");
            expr_free(call);
            return NULL;
        }
        call->argument_count = 1;
        call->arguments[0] = parse_expression(parser);
        if (parser->failed) {
            expr_free(call);
            return NULL;
        }

        if (!consume(parser, TOKEN_RPAREN, "closing ')'")) {
            expr_free(call);
            return NULL;
        }
        return call;
    }

    if (match(parser, TOKEN_INT)) {
        Expr *expr = expr_new(parser, EXPR_INT, token->start);
        if (expr == NULL) return NULL;
        /* Digits arrive as owned text, so no access to the source buffer. */
        expr->integer = strtoll(token->text, NULL, 10);
        return expr;
    }

    if (match(parser, TOKEN_TRUE) || match(parser, TOKEN_FALSE)) {
        Expr *expr = expr_new(parser, EXPR_BOOL, token->start);
        if (expr == NULL) return NULL;
        expr->boolean = (token->type == TOKEN_TRUE);
        return expr;
    }

    if (match(parser, TOKEN_STRING)) {
        Expr *expr = expr_new(parser, EXPR_STRING, token->start);
        if (expr == NULL) return NULL;
        expr->string = copy_text(token->text, token->length);
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
/* --- Call, unary, and the precedence climbing levels ------------------------ */

static Expr *parse_call(Parser *parser) {
    Position start = current(parser)->start;
    Expr *callee = parse_primary(parser);
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
            call->arguments[call->argument_count++] = argument;
        } while (match(parser, TOKEN_COMMA));
    }

    if (!consume(parser, TOKEN_RPAREN, "closing ')'")) {
        expr_free(call);
        return NULL;
    }
    return call;
}

static Expr *parse_unary(Parser *parser) {
    Position start = current(parser)->start;

    if (match(parser, TOKEN_BANG) || match(parser, TOKEN_MINUS)) {
        /* Read the operator back off the token just consumed, rather than
         * re-testing current() which has already advanced. */
        bool is_not = parser->tokens[parser->position - 1].type == TOKEN_BANG;

        Expr *expr = expr_new(parser, EXPR_UNARY, start);
        if (expr == NULL) return NULL;
        expr->op = is_not ? OP_NOT : OP_NEGATE;
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
    return parse_binary(parser, level, sizeof(level) / sizeof(level[0]), parse_term);
}

static Expr *parse_equality(Parser *parser) {
    static const BinaryOperator level[] = {
        {TOKEN_EQ, OP_EQUAL}, {TOKEN_BANG_EQ, OP_NOT_EQUAL}
    };
    return parse_binary(parser, level, sizeof(level) / sizeof(level[0]), parse_comparison);
}

/* Assignment is right-associative: `a = b = 1` assigns 1 to b then to a. */
static Expr *parse_assignment(Parser *parser) {
    if (check(parser, TOKEN_IDENT) &&
        parser->tokens[parser->position + 1].type == TOKEN_ASSIGN) {
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
    return parse_equality(parser);
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

static Stmt *parse_var_declaration(Parser *parser) {
    Position start = current(parser)->start;
    advance_token(parser); /* 'var' */

    if (!check(parser, TOKEN_IDENT)) {
        fail(parser, current(parser)->start, "expected variable name after 'var', found %s",
             token_type_description(current(parser)->type));
        return NULL;
    }
    const Token *name = advance_token(parser);

    Stmt *stmt = stmt_new(parser, STMT_VAR, start);
    if (stmt == NULL) return NULL;
    stmt->name = copy_ident(name);

    if (!consume(parser, TOKEN_COLON, "':' after variable name") ||
        !parse_type(parser, &stmt->declared_type)) {
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

    Stmt *stmt = stmt_new(parser, STMT_FN, start);
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
            parameter->name = copy_ident(parameter_name);
            parameter->position = parameter_name->start;

            if (!consume(parser, TOKEN_COLON, "':' after parameter name") ||
                !parse_type(parser, &parameter->type)) {
                stmt_free(stmt);
                return NULL;
            }
        } while (match(parser, TOKEN_COMMA));
    }

    if (!consume(parser, TOKEN_RPAREN, "closing ')'")) {
        stmt_free(stmt);
        return NULL;
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

    stmt->expression = parse_expression(parser);
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

static Stmt *parse_while(Parser *parser) {
    Position start = current(parser)->start;
    advance_token(parser); /* 'while' */

    Stmt *stmt = stmt_new(parser, STMT_WHILE, start);
    if (stmt == NULL) return NULL;

    stmt->expression = parse_expression(parser);
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

static Stmt *parse_statement(Parser *parser) {
    /* Dispatch on the upcoming token. Each branch owns its own leading token,
     * so nothing is pre-consumed here. */
    switch (current(parser)->type) {
        case TOKEN_VAR:   return parse_var_declaration(parser);
        case TOKEN_FN:    return parse_function_declaration(parser);
        case TOKEN_IF:    return parse_if(parser);
        case TOKEN_WHILE: return parse_while(parser);
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
