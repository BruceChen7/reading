#include "eval.h"
#include "global.h"
#include "interp.h"
#include "mem.h"
#include "vm/chunk.h"
#include "vm/debug.h"
#include "vm/vm.h"
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct argvalues {
    int treewalk;
    int repl;
    char* filename;
    int help;
    int error;
} ArgValues;

typedef enum {
    MODE_TREEWALK,
    MODE_VM
} Mode;

// 函数指针的集合
typedef struct runnable_mode {
    Mode mode;
    void (*codeRunner)(const char* code);
} RunnableMode;

void usage(char* name);
void header(char* name);
char* read_line(const char* prompt);
char* read_file(const char* filepath);

void run_treewalk_chunk(const char* code);
void run_treewalk_file(const char* code);
void run_vm_chunk(const char* code);
void run_vm_file(const char* code);
void vm_chunk_test();

ArgValues argparse(int argc, const char* argv[])
{
    // 命令行参数
    ArgValues values;
    memset(&values, 0, sizeof(struct argvalues));
    // 默认采用VM方式
    values.treewalk = 0;
    if (argc > 3) {
        values.error = 1;
    } else if (argc == 3) {
        values.repl = 0;
        // 3个命令行参数，最后输入的是文件名
        values.filename = (char*)argv[2];
        if (strncmp(argv[1], "--tree-walk", 12) == 0) {
            values.treewalk = 1;
        } else if (strncmp(argv[1], "--vm", 5) == 0) {
            values.treewalk = 0;
        } else if (strncmp(argv[1], "--help", 7) == 0) {
            values.help = 1;
        } else {
            values.error = 1;
        }
    } else {
        values.repl = 1;
        if (argc == 2) {
            if (strncmp(argv[1], "--tree-walk", 12) == 0) {
                values.treewalk = 1;
            } else if (strncmp(argv[1], "--vm", 5) == 0) {
                values.treewalk = 0;
            } else if (strncmp(argv[1], "--help", 7) == 0) {
                values.help = 1;
            } else {
                values.repl = 0;
                values.filename = (char*)argv[1];
            }
        }
    }

    return values;
}

int main(int argc, const char* argv[])
{
#ifdef _WIN32
    char separator = '\\';
#else
    char separator = '/';
#endif
    // 找到最后一个分割符号
    char* name = strrchr(argv[0], separator);
    char* line = NULL;
    char* buf = NULL;
    RunnableMode mode;
    ArgValues values = argparse(argc, argv);
    name = name != NULL ? name + 1 : name;

    if (values.error) {
        usage(name);
        return EXIT_FAILURE;
    }

    if (values.help) {
        usage(name);
        return EXIT_SUCCESS;
    }

    header(name);
    mode.mode = values.treewalk ? MODE_TREEWALK : MODE_VM;
    // repl模式
    if (values.repl) {
        mode.codeRunner = values.treewalk ? run_treewalk_chunk : run_vm_chunk;

        switch (mode.mode) {
        case MODE_TREEWALK:
            break;
        case MODE_VM:
            vm_init();
        }

        printf("Type 'exit()' to exit\n");
        for (line = read_line("> "); line != NULL && strcmp(line, "exit()\n") != 0; line = read_line("> ")) {
            mode.codeRunner(line);
            fr(line);
        }

        switch (mode.mode) {
        case MODE_TREEWALK:
            break;
        case MODE_VM:
            vm_free();
            break;
        }
    } else {
        buf = read_file(values.filename);
        if (buf == NULL) {
            fprintf(stderr, "%s\n", strerror(errno));
            exit(74);
        } else {
            mode.codeRunner = values.treewalk ? run_treewalk_file : run_vm_file;
            mode.codeRunner(buf);
        }
        fr(buf);
    }
    return EXIT_SUCCESS;
}

void usage(char* name)
{
    header(name);
    printf("`%s <filename>` or just `%s` to launch REPL interpreter.\n", name, name);
    printf("    --tree-walk    runs clox in tree walk mode\n");
    printf("    --vm           runs clox in bytecode mode (default)\n");
    printf("    --help         shows this help text\n");
}

void header(char* name)
{
    if (name != NULL) {
        printf("%s %s\n", name, VERSION);
    } else {
        printf("lox %s\n", VERSION);
    }
}

char* read_file(const char* filepath)
{
    size_t readBytes = 0, length = 0;
    char* buf = NULL;
    // 以二进制的协议的方式来读取整个文件
    FILE* fp = fopen(filepath, "rb");

    if (fp == NULL) {
        fprintf(stderr, "Cannot open file %s\n", filepath);
        return NULL;
    }

    if (!fseek(fp, SEEK_SET, SEEK_END)) {
        length = ftell(fp);
        rewind(fp);
    }

    buf = (char*)malloc(length + 1);
    if (buf == NULL) {
        fprintf(stderr, "No enough memory to read file: %s\n", filepath);
        exit(74);
    }

    memset(buf, 0, length + 1);
    readBytes = fread(buf, 1, length, fp);
    if (readBytes < length) {
        fprintf(stderr, "Cannot read file: %s\n", filepath);
        exit(74);
    }

    if (!fclose(fp)) {
        return buf;
    }

    fr(buf);
    fprintf(stderr, "Error opening or reading file %s\n", filepath);
    return NULL;
}

char* read_line(const char* prompt)
{
    int size = sizeof(char) * LINEBUFSIZE;
    char* line = (char*)alloc(size);
    memset((void*)line, 0, size);
    printf("%s", prompt);
    fflush(stdin);
    // 重标准输入中读取一行
    fgets(line, size, stdin);
    return line;
}

// 采用树的结构来跑代码
void run_treewalk_chunk(const char* code)
{
    interp(code);
}

void run_treewalk_file(const char* code)
{
    env_init_global();
    // 遍历tree
    run_treewalk_chunk(code);
    env_destroy(&GlobalExecutionEnvironment);
}

void run_vm_chunk(const char* code)
{
    vm_interpret(code);
}

void run_vm_file(const char* code)
{
    VmInterpretResult result;

    vm_init();
    result = vm_interpret(code);
    vm_free();

    if (result == INTERPRET_COMPILE_ERROR) {
        exit(65);
    }

    if (result == INTERPRET_RUNTIME_ERROR) {
        exit(70);
    }

    getchar();
}
