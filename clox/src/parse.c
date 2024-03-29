#include "parse.h"
#include "ds/list.h"
#include "global.h"
#include "mem.h"
#include "tokenizer.h"
#include "visitor.h"
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

static Expr* expression(Node** node);
static Expr* assignment(Node** node);
static Expr* equality(Node** node);
static Expr* comparison(Node** node);
static Expr* addition(Node** node);
static Expr* mutiplication(Node** node);
static Expr* unary(Node** node);
static Expr* call(Node** node);
static Expr* primary(Node** node);
static Expr* logicOr(Node** node);
static Expr* logicAnd(Node** node);

static Stmt* block_statements(Node** node);
static Stmt* if_statement(Node** node);
static Stmt* for_statement(Node** node);
static Stmt* while_statement(Node** node);
static Stmt* fun_statement(const char* type, Node** node);
static Stmt* return_statement(Node** node);
static Stmt* class_statement(Node** node);

static void expr_destroy(Expr* expr);
static void stmt_destroy(Stmt* stmt);

static int match(TokenType type, TokenType types[], int n, Node** node)
{
    int i = 0;
    for (i = 0; i < n; i++) {
        if (MATCH(type, types[i])) {
            (*node) = (*node)->next;
            return 1;
        }
    }
    return 0;
}

// consume将移动到下一个节点，然后返回符合当前匹配的节点
// 否则直接报错
static Node** consume(Node** node, TokenType type, const char* msg)
{
    // 获取响应
    Token* tkn = (Token*)(*node)->data;
    if (MATCH(tkn->type, type)) {
        // 移动到下一个节点
        (*node) = (*node)->next;
        // 放回当前节点
        return &(*node)->prev;
    }
    // 只是简单打印错误
    parse_error(tkn, msg);
    return NULL;
}

static Expr* new_expr(ExpressionType type, void* realExpr)
{
    Expr* expr = (Expr*)alloc(sizeof(Expr));
    expr->expr = realExpr;
    expr->type = type;
    expr->order = 0;
    return expr;
}

static LiteralExpr* new_literal(void* value, LiteralType type, size_t size)
{
    LiteralExpr* expr = (LiteralExpr*)alloc(sizeof(LiteralExpr));
    expr->value = value;
    expr->type = type;
    expr->valueSize = size;
    return expr;
}

static UnaryExpr* new_unary(Token op, Expr* internalExpr)
{
    UnaryExpr* expr = (UnaryExpr*)alloc(sizeof(UnaryExpr));
    expr->op = op;
    expr->expr = internalExpr;
    return expr;
}

static BinaryExpr* new_binary(Token op, Expr* left, Expr* right)
{
    BinaryExpr* expr = (BinaryExpr*)alloc(sizeof(BinaryExpr));
    expr->leftExpr = (Expr*)left;
    expr->rightExpr = (Expr*)right;
    expr->op = op;
    return expr;
}

static GroupingExpr* new_grouping(Expr* internalExpr)
{
    GroupingExpr* expr = (GroupingExpr*)alloc(sizeof(GroupingExpr));
    expr->expr = (Expr*)internalExpr;
    return expr;
}

static VariableExpr* new_variable(Token variableName)
{
    VariableExpr* expr = (VariableExpr*)alloc(sizeof(VariableExpr));
    expr->variableName = variableName;
    return expr;
}

static AssignmentExpr* new_assignment(Token variableName, Expr* rightExpr)
{
    AssignmentExpr* expr = (AssignmentExpr*)alloc(sizeof(AssignmentExpr));
    expr->rightExpr = rightExpr;
    expr->variableName = variableName;
    return expr;
}

// Binary Expression  -> Expression (MatchToken  Expression)*
static Expr* binary_production(Node** node, Expr* (*rule)(Node** t), TokenType matchTokens[], int n)
{
    Expr *expr = rule(node), *exprRight = NULL;
    const Token* tknPrev = NULL;
    while (match(((Token*)(*node)->data)->type, matchTokens, n, node)) {
        tknPrev = (Token*)(*node)->prev->data;
        exprRight = rule(node);
        expr = new_expr(EXPR_BINARY, new_binary(*tknPrev, expr, exprRight));
    }
    return expr;
}

// true表达式
LiteralExpr* new_true()
{
    char* value = (char*)alloc(sizeof(char));
    *value = 1;
    return new_literal(value, LITERAL_BOOL, 1);
}

LiteralExpr* new_false()
{
    char* value = (char*)alloc(sizeof(char));
    *value = 0;
    return new_literal(value, LITERAL_BOOL, 1);
}

LiteralExpr* new_nil()
{
    return new_literal(NULL, LITERAL_NIL, 0);
}

// primary -> "true" | "false" | "nil" | number | string | (expresion)
static Expr* primary(Node** node)
{
    Expr* groupedExpr = NULL;
    Node** n = NULL;
    Token* tkn = (Token*)(*node)->data;
    double* doubleLiteral = NULL;
    ThisExpr* this = NULL;
    SuperExpr* super = NULL;

    // 如果是叶子节点，直接返回创建叶子节点
    if (MATCH(tkn->type, TOKEN_TRUE)) {
        (*node) = (*node)->next;
        return new_expr(EXPR_LITERAL, (void*)new_true());
    }

    if (MATCH(tkn->type, TOKEN_FALSE)) {
        (*node) = (*node)->next;
        return new_expr(EXPR_LITERAL, (void*)new_false());
    }

    // 空值
    if (MATCH(tkn->type, TOKEN_NIL)) {
        (*node) = (*node)->next;
        return new_expr(EXPR_LITERAL, (void*)new_nil());
    }

    // 字符串
    if (MATCH(tkn->type, TOKEN_STRING)) {
        (*node) = (*node)->next;
        return new_expr(EXPR_LITERAL, new_literal(tkn->literal, LITERAL_STRING, strlen(tkn->literal) + 1));
    }

    // 数量
    if (MATCH(tkn->type, TOKEN_NUMBER)) {
        // 移动到下一个节点
        (*node) = (*node)->next;
        doubleLiteral = (double*)alloc(sizeof(double));
        *doubleLiteral = atof(tkn->literal);
        return new_expr(EXPR_LITERAL, new_literal(doubleLiteral, LITERAL_NUMBER, sizeof(double)));
    }

    // 直接(
    if (MATCH(tkn->type, TOKEN_LEFT_PAREN)) {
        *node = (*node)->next;
        groupedExpr = expression(node);
        n = consume(node, TOKEN_RIGHT_PAREN, "Expect ')' after expression.");
        if (n == NULL) {
            return NULL;
        }
        return new_expr(EXPR_GROUPING, (void*)new_grouping(groupedExpr));
    }

    // super
    if (MATCH(tkn->type, TOKEN_SUPER)) {
        super = (SuperExpr*)alloc(sizeof(SuperExpr));
        super->keyword = *(Token*)(*node)->prev->data;
        if (consume(node, TOKEN_DOT, "Expect '.' after 'super'.") == NULL) {
            fr(super);
            return NULL;
        }
        n = consume(node, TOKEN_IDENTIFIER, "Expect superclass method name.");
        if (n == NULL) {
            fr(super);
            return NULL;
        }
        super->method = *((Token*)(*n)->data);
        return new_expr(EXPR_SUPER, super);
    }

    // this
    if (MATCH(tkn->type, TOKEN_THIS)) {
        this = (ThisExpr*)alloc(sizeof(ThisExpr));
        this->keyword = *(Token*)(*node)->prev->data;
        return new_expr(EXPR_THIS, this);
    }

    // 
    if (MATCH(tkn->type, TOKEN_IDENTIFIER)) {
        *node = (*node)->next;
        return new_expr(EXPR_VARIABLE, new_variable(*(Token*)(*node)->prev->data));
    }
    parse_error(tkn, UNKNOWN_IDENTIFIER);
    return NULL;
}

static CallExpr* new_call(Expr* callee, List* args, Token paren)
{
    CallExpr* EXPR_CALL = alloc(sizeof(CallExpr));
    EXPR_CALL->callee = callee;
    EXPR_CALL->paren = paren;
    EXPR_CALL->args = args;
    return EXPR_CALL;
}

static Expr* finish_call(Node** node, Expr* callee)
{
    Token *tkn = NULL, *paren = NULL;
    List* args = list();
    Expr* arg = NULL;
    Node** temp = NULL;
    do {
        (*node) = (*node)->next;
        tkn = (Token*)(*node)->data;

        if (args->count > MAX_ARGS) {
            parse_error(tkn, "Cannot have more than %d args");
            list_destroy(args);
            expr_destroy(callee);
            return NULL;
        }

        if (!MATCH(tkn->type, TOKEN_RIGHT_PAREN)) {
            arg = expression(node);
        }

        if (arg != NULL) {
            list_push(args, arg);
        }

        tkn = (Token*)(*node)->data;
    } while (MATCH(tkn->type, TOKEN_COMMA));
    temp = consume(node, TOKEN_RIGHT_PAREN, "Expect ')' for function EXPR_CALL");
    paren = (Token*)(*temp)->data;
    return new_expr(EXPR_CALL, new_call(callee, args, *paren));
}

// call expression
static Expr* call(Node** node)
{
    Expr* expr = primary(node);
    Token *tkn = (Token*)(*node)->data, name;
    GetExpr* get = NULL;
    Node** temp = NULL;

    while (1) {
        if (MATCH(tkn->type, TOKEN_LEFT_PAREN)) {
            expr = finish_call(node, expr);
        } else if (MATCH(tkn->type, TOKEN_DOT)) {
            (*node) = (*node)->next;
            temp = consume(node, TOKEN_IDENTIFIER, "Expect property name after '.'.");
            if (temp != NULL) {
                name = *(Token*)((*temp)->data);
                get = (GetExpr*)alloc(sizeof(GetExpr));
                get->name = name;
                get->object = expr;
                expr = new_expr(EXPR_GET, get);
            }
        } else {
            break;
        }
        tkn = (Token*)(*node)->data;
    }
    return expr;
}

// unary expression -> [minus exprssion | bang expression ]?
static Expr* unary(Node** node)
{
    Expr* rightExpr = NULL;
    const Token *tkn = (Token*)(*node)->data, *tknPrev = NULL;
    TokenType unaryTokens[] = {
        TOKEN_MINUS,
        TOKEN_BANG
    };
    if (match(tkn->type, unaryTokens, 2, node)) {
        tknPrev = (Token*)(*node)->prev->data;
        rightExpr = unary(node);
        return new_expr(EXPR_UNARY, (void*)new_unary(*tknPrev, rightExpr));
    }

    return call(node);
}

// mutiplication-> ["*"|"/"]unary expression
static Expr* mutiplication(Node** node)
{
    TokenType multiplicationTokens[] = {
        TOKEN_SLASH,
        TOKEN_STAR
    };
    return binary_production(node, unary, multiplicationTokens, 2);
}

// addition -> mutiplication expression [+ / ] [muliexprssion]
static Expr* addition(Node** node)
{
    TokenType additionTokens[] = {
        TOKEN_MINUS,
        TOKEN_PLUS
    };
    return binary_production(node, mutiplication, additionTokens, 2);
}

// equalexpresson -> addtion expression [> | >= | <= | <] addition expression
static Expr* comparison(Node** node)
{
    TokenType comparisonTokens[] = {
        TOKEN_GREATER,
        TOKEN_GREATER_EQUAL,
        TOKEN_LESS,
        TOKEN_LESS_EQUAL
    };
    return binary_production(node, addition, comparisonTokens, 4);
}

static Expr* equality(Node** node)
{
    TokenType equalityTokens[] = {
        // !=
        TOKEN_BANG_EQUAL,
        // ==
        TOKEN_EQUAL_EQUAL
    };
    return binary_production(node, comparison, equalityTokens, 2);
}

// Assignment -> OrExpression ("=" Assignment)?
static Expr* assignment(Node** node)
{
    // 先计算逻辑或的表达式
    Expr *expr = logicOr(node), *value = NULL;
    Node* eqals = *node;
    GetExpr* get = NULL;
    SetExpr* set = NULL;

    if (MATCH(((Token*)equals->data)->type, TOKEN_EQUAL)) {
        (*node) = (*node)->next;
        // assignment
        value = assignment(node);
        // 如果 Orexpression 生成的是一个变量
        if (expr != NULL && expr->type == EXPR_VARIABLE) {
            // 那么生成一个 赋值表达式
            return new_expr(EXPR_ASSIGNMENT, new_assignment(((VariableExpr*)expr->expr)->variableName, value));
        } else if (expr->type == EXPR_GET) {
            get = (GetExpr*)expr->expr;
            set = (SetExpr*)alloc(sizeof(SetExpr));
            set->object = get->object;
            set->name = get->name;
            set->value = value;
            fr(get);
            expr->expr = set;
            expr->type = EXPR_SET;
            return expr;
        }
        parse_error((Token*)equals->data, "Invalid Assignment Target");
    }

    return expr;
}

static Expr* new_logical(Expr* left, Token op, Expr* right)
{
    LogicalExpr* logicalExpr = (LogicalExpr*)alloc(sizeof(LogicalExpr));
    logicalExpr->op = op;
    logicalExpr->left = left;
    logicalExpr->right = right;
    return new_expr(EXPR_LOGICAL, logicalExpr);
}

// OrExpression -> AndExpression  ("||" AndExpression)*
static Expr* logicOr(Node** node)
{
    Expr *expr = logicAnd(node), *right = NULL;
    const Token* tkn = (Token*)(*node)->data;
    Token* operatorTkn = NULL;

    while (MATCH(tkn->type, TOKEN_OR)) {
        operatorTkn = (Token*)(*node)->data;
        (*node) = (*node)->next;
        tkn = (Token*)(*node)->data;
        right = logicAnd(node);
        expr = new_logical(expr, *operatorTkn, right);
    }
    return expr;
}

// AndExpression -> EqualityExpression ( " && "  EqualityExpression)*
static Expr* logicAnd(Node** node)
{
    Expr *expr = equality(node), *right = NULL;
    Token *tkn = (Token*)(*node)->data, *operatorTkn = NULL;

    while (MATCH(tkn->type, TOKEN_AND)) {
        operatorTkn = (Token*)(*node)->data;
        (*node) = (*node)->next;
        tkn = (Token*)(*node)->data;
        right = equality(node);
        expr = new_logical(expr, *operatorTkn, right);
    }
    return expr;
}

// Expression -> assignment
static Expr* expression(Node** node)
{
    return assignment(node);
}

// 这里同步指的是将错误进行消化
static void synchronize(Node** node)
{
    Token *token = NULL, *prevToken;
    (*node) = (*node)->next;
    if (*node != NULL) {
        token = (Token*)(*node)->data;
        while (!END_OF_TOKENS(token->type)) {
            // 获取上一个token
            prevToken = (Token*)(*node)->prev->data;
            // 如果是;，那么是空语句直接结束
            if (prevToken->type == TOKEN_SEMICOLON)
                return;

            switch (token->type) {
            case TOKEN_CLASS:
            case TOKEN_FUN:
            case TOKEN_VAR:
            case TOKEN_FOR:
            case TOKEN_IF:
            case TOKEN_WHILE:
            case TOKEN_PRINT:
            case TOKEN_RETURN:
                // 这里也是直接返回
                return;
            }
            // 其他节点类型直接指向下一个
            (*node) = (*node)->next;
        }
    }
}

static Node** terminated_statement(Node** node)
{
    return consume(node, TOKEN_SEMICOLON, "Expect ';' after value");
}

// 更具type来实现重载的意思
static Stmt* new_statement(StmtType type, void* realStmt)
{
    Stmt* stmt = (Stmt*)alloc(sizeof(Stmt));
    memset(stmt, 0, sizeof(Stmt));
    // statement类型
    stmt->type = type;
    // 这里是实际的statement
    stmt->realStmt = realStmt;
    return stmt;
}

// 创建终结符节点
static Stmt* new_terminated_statement(Node** node, StmtType type, void* realStmt)
{
    // 先消耗分号
    if (terminated_statement(node) != NULL) {
        // 然后创建statement
        return new_statement(type, realStmt);
    }

    return NULL;
}

static Stmt* print_statement(Node** node)
{
    Expr* expr = expression(node);
    PrintStmt* stmt = (PrintStmt*)alloc(sizeof(PrintStmt));
    stmt->expr = expr;
    return new_terminated_statement(node, STMT_PRINT, stmt);
}

static Stmt* expression_statement(Node** node)
{
    Expr* expr = expression(node);
    ExprStmt* stmt = (ExprStmt*)alloc(sizeof(ExprStmt));
    stmt->expr = expr;
    return new_terminated_statement(node, STMT_EXPR, stmt);
}

// 创建var statement的叶子节点
static Stmt* var_statement(Node** node, Expr* initializer, Token variableName)
{
    // 分配内存
    VarDeclarationStmt* stmt = (VarDeclarationStmt*)alloc(sizeof(VarDeclarationStmt));
    // 表达式
    stmt->initializer = initializer;
    // 变量名称
    stmt->varName = variableName;
    // 创建一个statment
    // stmt是实际的statement
    // 但是返回一个统一的Stmt来实现多态
    return new_terminated_statement(node, STMT_VAR_DECLARATION, stmt);
}

// Var -> Identifier (= Expression)?
static Stmt* var_declaration(Node** node)
{
    // identifierNode 是当前节点，
    // 而且node此时执行的是下一个节点
    Node** identifierNode = consume(node, TOKEN_IDENTIFIER, "Expected a EXPR_VARIABLE name");
    Token* name = NULL;
    Expr* initializer = NULL;

    if (identifierNode == NULL) {
        return NULL;
    }
    // 变量名
    name = (Token*)(*identifierNode)->data;

    if (MATCH(((Token*)(*node)->data)->type, TOKEN_EQUAL)) {
        (*node) = (*node)->next;
        initializer = expression(node);
    }

    return var_statement(node, initializer, *name);
}

static Stmt* statement(Node** node)
{
    const Token* tkn = (Token*)((*node)->data);
    if (MATCH(tkn->type, TOKEN_PRINT)) {
        (*node) = (*node)->next;
        return print_statement(node);
    } else if (MATCH(tkn->type, TOKEN_LEFT_BRACE)) {
        (*node) = (*node)->next;
        return block_statements(node);
    } else if (MATCH(tkn->type, TOKEN_IF)) {
        (*node) = (*node)->next;
        return if_statement(node);
    } else if (MATCH(tkn->type, TOKEN_FOR)) {
        (*node) = (*node)->next;
        return for_statement(node);
    } else if (MATCH(tkn->type, TOKEN_WHILE)) {
        (*node) = (*node)->next;
        return while_statement(node);
    } else if (MATCH(tkn->type, TOKEN_RETURN)) {
        (*node) = (*node)->next;
        return return_statement(node);
    }

    return expression_statement(node);
}

static Stmt* class_statement(Node** node)
{
    ClassStmt* stmt = NULL;
    // classNameNode
    Node** classNameNode = consume(node, TOKEN_IDENTIFIER, "Expect class name");
    Token *name = NULL, *temp = NULL;
    List* methods = NULL;
    Expr* superClassExpr = NULL;
    VariableExpr* superClass = NULL;

    if (classNameNode == NULL) {
        return NULL;
    }
    name = (Token*)(*classNameNode)->data;
    temp = (Token*)(*node)->data;

    // 匹配到 < 
    if (MATCH(temp->type, TOKEN_LESS)) {
        (*node) = (*node)->next;

        if (consume(node, TOKEN_IDENTIFIER, "Expect super class") == NULL) {
            return NULL;
        }
        superClass = (VariableExpr*)alloc(sizeof(VariableExpr));
        superClass->variableName = *((Token*)(*node)->prev->data);
        superClassExpr = new_expr(EXPR_VARIABLE, superClass);
    }

    consume(node, TOKEN_LEFT_BRACE, "Expect '{' before class body");
    temp = (Token*)(*node)->data;
    methods = list();
    while (!MATCH(temp->type, TOKEN_RIGHT_BRACE) && !END_OF_TOKENS(temp->type)) {
        list_push(methods, fun_statement("method", node));
        temp = (Token*)(*node)->data;
    }
    consume(node, TOKEN_RIGHT_BRACE, "Expect '}' after class body");
    stmt = (ClassStmt*)alloc(sizeof(ClassStmt));
    stmt->methods = methods;
    stmt->name = *name;
    stmt->super = superClassExpr;
    return new_statement(STMT_CLASS, stmt);
}

// declaration -> "class" statment | "func " statement | var statement | statement
static Stmt* declaration(Node** node)
{
    // 采用LL来解析statement
    const Token* tkn = (Token*)((*node)->data);
    Stmt* stmt = NULL;
    // 如果是以class开头
    if (MATCH(tkn->type, TOKEN_CLASS)) {
        // 直接解析下个节点
        (*node) = (*node)->next;
        return class_statement(node);
    } else if (MATCH(tkn->type, TOKEN_FUN)) {
        // 前进一个节点，然后继续解析
        (*node) = (*node)->next;
        return fun_statement("function", node);
    } else if (MATCH(tkn->type, TOKEN_VAR)) {
        // 这里都要移动到下一个节点
        (*node) = (*node)->next;
        // var breakfast = "beignets"这种;
        stmt = var_declaration(node);
    } else {
        stmt = statement(node);
    }
    // 如果是失败
    if (stmt == NULL) {
        synchronize(node);
    }

    return stmt;
}

static Stmt* block_statements(Node** node)
{
    Token* token = NULL;
    BlockStmt* stmt = (BlockStmt*)alloc(sizeof(BlockStmt));
    stmt->innerStmts = list();
    token = (Token*)(*node)->data;
    while (token->type != TOKEN_RIGHT_BRACE && token->type != TOKEN_ENDOFFILE) {
        list_push(stmt->innerStmts, declaration(node));
        token = (Token*)(*node)->data;
    }
    consume(node, TOKEN_RIGHT_BRACE, "Expect '}' after block.");
    return new_statement(STMT_BLOCK, (void*)stmt);
}

static Stmt* if_statement(Node** node)
{
    Stmt *thenStmt = NULL, *elseStmt = NULL;
    IfElseStmt* realStmt = NULL;
    Token* tkn = (Token*)(*node)->data;
    Expr* condition = NULL;
    consume(node, TOKEN_LEFT_PAREN, "Expect '(' after 'if'.");
    condition = expression(node);
    consume(node, TOKEN_RIGHT_PAREN, "Expect ')' after condition.");
    thenStmt = statement(node);
    tkn = (Token*)(*node)->data;
    if (MATCH(tkn->type, TOKEN_ELSE)) {
        (*node) = (*node)->next;
        elseStmt = statement(node);
    }
    realStmt = (IfElseStmt*)alloc(sizeof(IfElseStmt));
    realStmt->condition = condition;
    realStmt->elseStmt = elseStmt;
    realStmt->thenStmt = thenStmt;
    return new_statement(STMT_IF_ELSE, realStmt);
}

static Stmt* for_statement(Node** node)
{
    Stmt *initializer = NULL, *body = NULL;
    Expr *condition = NULL, *step = NULL;
    BlockStmt *wrappedBody = NULL, *wrappedForAndInit = NULL;
    WhileStmt* wrappedFor = NULL;
    ExprStmt* wrappedStep = NULL;
    Token* tkn = NULL;
    consume(node, TOKEN_LEFT_PAREN, "Expect '(' after for");
    tkn = (Token*)(*node)->data;
    if (MATCH(tkn->type, TOKEN_SEMICOLON)) {
        (*node) = (*node)->next;
    } else {
        if (MATCH(tkn->type, TOKEN_VAR)) {
            (*node) = (*node)->next;
            initializer = var_declaration(node);
        } else {
            (*node) = (*node)->next;
            initializer = expression_statement(node);
        }
    }
    tkn = (Token*)(*node)->data;
    if (!MATCH(tkn->type, TOKEN_SEMICOLON)) {
        condition = expression(node);
    }
    consume(node, TOKEN_SEMICOLON, "Expect ';' after for condition");
    if (!MATCH(tkn->type, TOKEN_RIGHT_PAREN)) {
        step = expression(node);
    }
    consume(node, TOKEN_RIGHT_PAREN, "Expect ')' for 'for' closing");
    body = statement(node);
    if (step != NULL) {
        wrappedBody = alloc(sizeof(BlockStmt));
        wrappedBody->innerStmts = list();
        wrappedStep = alloc(sizeof(ExprStmt));
        wrappedStep->expr = step;
        list_push(wrappedBody->innerStmts, body);
        list_push(wrappedBody->innerStmts, new_statement(STMT_EXPR, wrappedStep));
        body = new_statement(STMT_BLOCK, wrappedBody);
    }

    if (condition == NULL) {
        condition = new_expr(EXPR_LITERAL, new_true());
    }
    wrappedFor = alloc(sizeof(WhileStmt));
    wrappedFor->condition = condition;
    wrappedFor->body = body;
    body = new_statement(STMT_WHILE, wrappedFor);
    if (initializer != NULL) {
        wrappedForAndInit = alloc(sizeof(BlockStmt));
        wrappedForAndInit->innerStmts = list();
        list_push(wrappedForAndInit->innerStmts, initializer);
        list_push(wrappedForAndInit->innerStmts, body);
        body = new_statement(STMT_BLOCK, wrappedForAndInit);
    }
    return body;
}

static Stmt* while_statement(Node** node)
{
    WhileStmt* realStmt = NULL;
    Expr* condition = NULL;
    Stmt* bodyStmt = NULL;
    consume(node, TOKEN_LEFT_PAREN, "Expect '(' after 'while'.");
    condition = expression(node);
    consume(node, TOKEN_RIGHT_PAREN, "Expect ')' after condition.");
    bodyStmt = statement(node);
    realStmt = (WhileStmt*)alloc(sizeof(WhileStmt));
    realStmt->condition = condition;
    realStmt->body = bodyStmt;
    return new_statement(STMT_WHILE, realStmt);
}

static Stmt* fun_statement(const char* kind, Node** node)
{
    Token *name = NULL, *tkn = NULL;
    Node** temp = NULL;
    List* params = NULL;
    Stmt* body = NULL;
    FunStmt* fnStmt = NULL;
    char buf[LINEBUFSIZE];
    memset(buf, 0, LINEBUFSIZE);
    sprintf(buf, "Expect %s name.", kind);
    temp = consume(node, TOKEN_IDENTIFIER, buf);
    if (temp != NULL) {

        name = (Token*)(*temp)->data;
        memset(buf, 0, LINEBUFSIZE);
        sprintf(buf, "Expect '(' after %s name.", kind);
        consume(node, TOKEN_LEFT_PAREN, buf);
        params = list();
        tkn = (Token*)(*node)->data;
        if (!MATCH(tkn->type, TOKEN_RIGHT_PAREN)) {
            do {
                if (params->count > MAX_ARGS) {
                    parse_error(tkn, "Cannot have more than 8 parameters.");
                }
                temp = consume(node, TOKEN_IDENTIFIER, "Expect parameter name.");
                tkn = (Token*)(*temp)->data;
                list_push(params, tkn);
                tkn = (Token*)(*node)->data;
                if (!MATCH(tkn->type, TOKEN_RIGHT_PAREN)) {
                    (*node) = (*node)->next;
                }
            } while (MATCH(tkn->type, TOKEN_COMMA));
        }
        consume(node, TOKEN_RIGHT_PAREN, "Expect ')' after parameters.");
        memset(buf, 0, LINEBUFSIZE);
        sprintf(buf, "Expect '{' before %s body.", kind);
        consume(node, TOKEN_LEFT_BRACE, buf);
        body = block_statements(node);
        fnStmt = alloc(sizeof(FunStmt));
        fnStmt->name = *name;
        fnStmt->body = body;
        fnStmt->args = params;
        return new_statement(STMT_FUN, fnStmt);
    }
    return NULL;
}

static void literal_destroy(LiteralExpr* expr)
{
    switch (expr->type) {
    case LITERAL_BOOL:
    case LITERAL_NUMBER:
        fr(expr->value);
        break;
    case LITERAL_NIL:
        break;
    case LITERAL_STRING:
        break;
    }
    fr(expr);
}

// 根据表达式不同的类型来删除对应的内存节点
static void expr_destroy(Expr* expr)
{
    Expr* ex = NULL;
    SetExpr* set = NULL;
    GetExpr* get = NULL;
    LiteralExpr* literal = NULL;

    switch (expr->type) {
    case EXPR_LITERAL:
        literal = (LiteralExpr*)expr->expr;
        literal_destroy(literal);
        break;
    case EXPR_UNARY:
        ex = ((UnaryExpr*)expr->expr)->expr;
        expr_destroy(ex);
        break;
    case EXPR_BINARY:
        ex = ((BinaryExpr*)expr->expr)->leftExpr;
        expr_destroy(ex);
        ex = ((BinaryExpr*)expr->expr)->rightExpr;
        expr_destroy(ex);
        break;
    case EXPR_GROUPING:
        ex = ((GroupingExpr*)expr->expr)->expr;
        expr_destroy(ex);
        break;
    case EXPR_ASSIGNMENT:
        ex = ((AssignmentExpr*)expr->expr)->rightExpr;
        expr_destroy(ex);
        break;
    case EXPR_LOGICAL:
        ex = ((LogicalExpr*)expr->expr)->left;
        expr_destroy(ex);
        ex = ((LogicalExpr*)expr->expr)->right;
        expr_destroy(ex);
        break;
    case EXPR_CALL:
        ex = ((CallExpr*)expr->expr)->callee;
        expr_destroy(ex);
        list_destroy(((CallExpr*)expr->expr)->args);
        break;
        break;
        break;
    case EXPR_SET:
        set = (SetExpr*)expr->expr;
        expr_destroy(set->object);
        expr_destroy(set->value);
        break;
    case EXPR_GET:
        get = (GetExpr*)expr->expr;
        expr_destroy(get->object);
        break;
    case EXPR_SUPER:
    case EXPR_THIS:
    case EXPR_VARIABLE:
    default:
        break;
    }
    fr(expr);
}

static void stmts_foreach_stmt(List* stmts, void* stmtObj)
{
    Stmt* stmt = (Stmt*)stmtObj;
    stmt_destroy(stmt);
}

static void stmt_destroy(Stmt* stmt)
{
    IfElseStmt* ifStmt = NULL;
    WhileStmt* whileStmt = NULL;
    FunStmt* fnStmt = NULL;

    if (stmt != NULL) {
        switch (stmt->type) {
        case STMT_BLOCK:
            list_foreach(((BlockStmt*)stmt->realStmt)->innerStmts, stmts_foreach_stmt);
            break;
        case STMT_PRINT:
            expr_destroy(((PrintStmt*)stmt->realStmt)->expr);
            break;
        case STMT_EXPR:
            expr_destroy(((ExprStmt*)stmt->realStmt)->expr);
            break;
        case STMT_VAR_DECLARATION:
            expr_destroy(((VarDeclarationStmt*)stmt->realStmt)->initializer);
            break;
        case STMT_IF_ELSE:
            ifStmt = (IfElseStmt*)stmt->realStmt;
            stmt_destroy(ifStmt->thenStmt);
            stmt_destroy(ifStmt->elseStmt);
            expr_destroy(ifStmt->condition);
            break;
        case STMT_WHILE:
            whileStmt = (WhileStmt*)stmt->realStmt;
            stmt_destroy(whileStmt->body);
            expr_destroy(whileStmt->condition);
            break;
        case STMT_FUN:
            fnStmt = (FunStmt*)stmt->realStmt;
            list_destroy(fnStmt->args);
            stmt_destroy(fnStmt->body);
            break;
        case STMT_RETURN:
            expr_destroy(((ReturnStmt*)stmt->realStmt)->value);
            break;
        case STMT_CLASS:
            list_foreach(((ClassStmt*)stmt->realStmt)->methods, stmts_foreach_stmt);
            list_destroy(((ClassStmt*)stmt->realStmt)->methods);
            break;
        }
        fr((void*)stmt);
    }
}

static Stmt* return_statement(Node** node)
{
    Token *keyword = (Token*)(*node)->prev->data, *tkn = (Token*)(*node)->data;
    Expr* value = NULL;
    ReturnStmt* returnStmt = NULL;
    if (!MATCH(tkn->type, TOKEN_SEMICOLON)) {
        value = expression(node);
    }
    consume(node, TOKEN_SEMICOLON, "Expect ';' after return");
    returnStmt = (ReturnStmt*)alloc(sizeof(ReturnStmt));
    returnStmt->keyword = *keyword;
    returnStmt->value = value;
    return new_statement(STMT_RETURN, returnStmt);
}

// 循环list，各个删除
static void stmts_destroy(List* stmts)
{
    if (stmts->count != 0) {
        list_foreach(stmts, stmts_foreach_stmt);
    }
    list_destroy(stmts);
}

void parser_destroy(ParsingContext* ctx)
{
    // 分成两大类：statement和expression
    if (ctx->stmts != NULL) {
        stmts_destroy(ctx->stmts);
        ctx->stmts = NULL;
    }

    if (ctx->expr != NULL) {
        expr_destroy(ctx->expr);
        ctx->expr = NULL;
    }
}

ParsingContext parse(Tokenization toknz)
{
    ParsingContext ctx = { NULL, NULL };
    List* stmts = NULL;
    List* tokens = toknz.values;
    int nbTokens = 0;
    Node* head = NULL;
    Stmt* stmt = NULL;

    if (tokens != NULL) {
        // list of statements
        stmts = list();
        // 这里看其实没有什么用
        nbTokens = tokens->count;
        // 获取head node
        head = tokens->head;

        while (!END_OF_TOKENS(((Token*)head->data)->type)) {
            // 从declaration开始解析
            stmt = declaration(&head);
            if (stmt != NULL) {
                // 这里list中的每个node为Stmt
                list_push(stmts, stmt);
            } else {
                stmts_destroy(stmts);
                stmts = NULL;
                break;
            }
        }
    }
    ctx.stmts = stmts;
    return ctx;
}

void parse_error(Token* token, const char* msg, ...)
{
    char buff[LINEBUFSIZE];
    va_list list;
    va_start(list, msg);

    memset(buff, 0, sizeof(buff));

    if (token->type == TOKEN_ENDOFFILE) {
        fprintf(stderr, ERROR_AT_EOF, msg);
    } else {
        sprintf(buff, ERROR_AT_LINE, token->line, msg, token->lexeme);
        vfprintf(stderr, msg, list);
    }
    va_end(list);
}
