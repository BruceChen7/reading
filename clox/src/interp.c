#include "ds/list.h"
#include "eval.h"
#include "mem.h"
#include "resolve.h"

// 这里直接
void for_stmts(List* stmts, void* stmtObj)
{
    // 强制转换成Stmt
    Stmt* stmt = (Stmt*)stmtObj;
    // 对每个statement进行解析
    int resolved = resolve(stmt);
    // 如果解析正确，然后计算
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
        // 直接使用for_stmts来对每个节点做事情
        list_foreach(ctx.stmts, for_stmts);
    }
    parser_destroy(&ctx);
    toknzr_destroy(toknz);
}
