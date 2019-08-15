#include "ds/list.h"
#include "eval.h"
#include "mem.h"
#include "resolve.h"

void for_stmts(List* stmts, void* stmtObj)
{
    Stmt* stmt = (Stmt*)stmtObj;
    int resolved = resolve(stmt);
    if (resolved) {
        eval(stmt);
    }
}

// 解释代码
void interp(const char* code)
{
    // Scanner扫描代码
    Tokenization toknz = toknzr(code, 1);
    ParsingContext ctx = parse(toknz);
    if (ctx.stmts != NULL) {
        list_foreach(ctx.stmts, for_stmts);
    }
    parser_destroy(&ctx);
    toknzr_destroy(toknz);
}
