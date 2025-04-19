#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <ctype.h>
#include <string.h>

typedef unsigned int State;
typedef void* Item;
typedef struct { unsigned int length; Item* items; } List;
typedef struct { State s; List* data; } Result;

typedef Result* (*BNF)(State);
typedef Result* (*BNF_c)(State, void*);
typedef struct { BNF_c f; void* ctx; } BNF_clo;

typedef Result* (*PostProc)(Result*);
typedef Result* (*PostProc_c)(Result*, void*);
typedef struct { PostProc_c f; void* ctx; } PostProc_clo;

typedef unsigned int (*GoodData)(List*);
typedef unsigned int (*GoodData_c)(List*, void*);
typedef struct { GoodData_c f; void* ctx; } GoodData_clo;

typedef unsigned int (*ItemCond)(Item);
typedef unsigned int (*ItemCond_c)(Item, void*);
typedef struct { ItemCond_c f; void* ctx; } ItemCond_clo;

typedef struct { BNF_clo* f; PostProc_clo* proc; } PPContext;
typedef struct { BNF_clo* f; BNF_clo* g; } BNF2Context;
typedef struct { char* name; char* value; } PairToken;
typedef struct { char* name; List* data; } IndentPair;

typedef BNF_clo* (*BinOp)(BNF_clo*, BNF_clo*);

Result* wrong;

BNF_clo* nothing;
//nothing.ctx = NULL; //does not depend on context

BNF_clo* get_token;

List* tokens;

void print_tokens(List* tokens) {
    printf("tokens:\n");
    for (int i = 0; i < tokens->length; i ++) {
        PairToken* pair = (PairToken*) tokens->items[i];
        printf("(%s, %s)\n", pair->name, pair->value);
    }
}

//BNF_c pp_return;
Result* pp_return(State s, void* ctx) {
    PPContext* context = (PPContext*) ctx;
    BNF_clo* f = context->f;
    PostProc_clo* proc = context->proc;
    if (f->f == NULL) {
        printf("NULL in pp_return f\n");
        exit(1);
    }
    Result* pr = f->f(s, f->ctx);
    if (!pr->s) {
        return wrong;
    }
    if (proc->f == NULL) {
        printf("NULL in pp_return proc\n");
        exit(1);
    }
    return proc->f(pr, proc->ctx);
}

BNF_clo* post_process(BNF_clo* f, PostProc_clo* proc) {
    PPContext* ctx = malloc(sizeof(PPContext));
    ctx->f = f;
    ctx->proc = proc;
    BNF_clo* cps = malloc(sizeof(BNF_clo));
    cps->f = pp_return;
    cps->ctx = ctx;
    return cps;
}

//BNF_c par2_return;
Result* par2_return(State s, void* ctx) {
    BNF2Context* context = (BNF2Context*) ctx;
    BNF_clo* f = context->f;
    BNF_clo* g = context->g;
    if (f->f == NULL) {
        printf("NULL in par2_return f\n");
        exit(1);
    }
    Result* pr = f->f(s, f->ctx);
    if (pr->s) {
        return pr;
    }
    if (g->f == NULL) {
        printf("NULL in par2_return g\n");
        exit(1);
    }
    return g->f(s, g->ctx);
}

BNF_clo* par2(BNF_clo* f, BNF_clo* g) {
    BNF2Context* ctx = malloc(sizeof(BNF2Context));
    ctx->f = f;
    ctx->g = g;
    BNF_clo* cps = malloc(sizeof(BNF_clo));
    cps->f = par2_return;
    cps->ctx = ctx;
    return cps;
}

//BNF_c not_then_return;
Result* not_then_return(State s, void* ctx) {
    BNF2Context* context = (BNF2Context*) ctx;
    BNF_clo* f = context->f;
    BNF_clo* g = context->g;
    if (f->f == NULL || f->ctx == NULL) {
        printf("NULL in not_then_return f\n");
        exit(1);
    }
    Result* pr = f->f(s, f->ctx);
    if (pr->s) {
        return wrong;
    }
    if (g->f == NULL) {
        printf("NULL in not_then_return g\n");
        exit(1);
    }
    return g->f(s, g->ctx);
}

BNF_clo* not_then(BNF_clo* f, BNF_clo* g) {
    BNF2Context* ctx = malloc(sizeof(BNF2Context));
    ctx->f = f;
    ctx->g = g;
    BNF_clo* cps = malloc(sizeof(BNF_clo));
    cps->f = not_then_return;
    cps->ctx = ctx;
    return cps;
}

//PostProc_c seq2_proc;
Result* seq2_proc(Result* pr, void* ctx) {
    BNF_clo* g = (BNF_clo*) ctx;
    if (g->f == NULL || g->ctx == NULL) {
        printf("NULL in seq2_proc g\n");
        exit(1);
    }
    Result* pt = g->f(pr->s, g->ctx);
    if (!pt->s) {
        return wrong;
    }
    Result* x = malloc(sizeof(Result));
    x->s = pt->s;
    //x->data;
    x->data = malloc(sizeof(List));
    x->data->length = pr->data->length + pt->data->length;
    x->data->items = malloc(x->data->length * sizeof(Item));
    int index = 0;
    for (int i = 0; i < pr->data->length; i ++, index ++) {
        x->data->items[index] = pr->data->items[i];
    }
    for (int i = 0; i < pt->data->length; i ++, index ++) {
        x->data->items[index] = pt->data->items[i];
    }
    return x;
}

BNF_clo* seq2(BNF_clo* f, BNF_clo* g) {
    PostProc_clo *proc = malloc(sizeof(PostProc_clo));
    proc->f = seq2_proc;
    proc->ctx = g;
    return post_process(f, proc);
}

BNF_clo* reduce(BinOp op, List* bnfs) {
    unsigned int n = bnfs->length;
    BNF_clo* result = (BNF_clo*) bnfs->items[0];
    for (unsigned int i = 1; i < n; i ++) {
        BNF_clo* next = (BNF_clo*) bnfs->items[i];
        result = op(result, next);
    }
    return result;
}

BNF_clo* seq(BNF_clo* f, ...) {
    va_list args;
    va_start(args, f);
    List* bnfs = malloc(sizeof(List));
    bnfs->length = 0;
    bnfs->items = NULL;
    for (BNF_clo* next = f; next != NULL;) {
        bnfs->length ++;
        bnfs->items = realloc(bnfs->items, bnfs->length * sizeof(Item));
        bnfs->items[bnfs->length - 1] = next;
        next = va_arg(args, BNF_clo*);
    }
    va_end(args);
    return reduce(seq2, bnfs);
}

BNF_clo* par(BNF_clo* f, ...) {
    va_list args;
    va_start(args, f);
    List* bnfs = malloc(sizeof(List));
    bnfs->length = 0;
    bnfs->items = NULL;
    for (BNF_clo* next = f; next != NULL;) {
        bnfs->length ++;
        bnfs->items = realloc(bnfs->items, bnfs->length * sizeof(Item));
        bnfs->items[bnfs->length - 1] = next;
        next = va_arg(args, BNF_clo*);
    }
    va_end(args);
    return reduce(par2, bnfs);
}

//BNF_c nothing_f;
Result* nothing_f(State s, void* ctx) {
    Result *pr = malloc(sizeof(Result));
    pr->s = s;
    pr->data = malloc(sizeof(List));
    pr->data->length = 0;
    return pr;
}

BNF_clo* opt(BNF_clo* f) {
    return par2(f, nothing);
}

//BNF_c zmo_return;
Result* zmo_return(State s, void* ctx) {
    BNF_clo* f = (BNF_clo*) ctx;
    if (f->f == NULL || f->ctx == NULL) {
        printf("NULL in zmo_return f\n");
        exit(1);
    }

    List* data = malloc(sizeof(List));
    data->length = 0;
    data->items = NULL;
    while (1) {
        Result* pr = f->f(s, f->ctx);
        if (!pr->s) {
            break;
        }
        s = pr->s;
        data->length += pr->data->length;
        data->items = realloc(data->items, data->length * sizeof(Item));
        for (int i = 0; i < pr->data->length; i ++) {
            data->items[data->length - pr->data->length + i] = pr->data->items[i];
        }
    }

    Result* result = malloc(sizeof(Result));
    result->s = s;
    result->data = data;
    return result;
}

BNF_clo* zmo(BNF_clo* f) {
    BNF_clo* cps = malloc(sizeof(BNF_clo));
    cps->f = zmo_return;
    cps->ctx = f;
    return cps;
}

BNF_clo* mor(BNF_clo* f) {
    return seq2(f, zmo(f));
}

//BNF_c get_token_f;
Result* get_token_f(State s, void* ctx) {
    if (s > 0 && s <= tokens->length) {
        Result* pr = malloc(sizeof(Result));
        pr->s = s+1;
        pr->data = malloc(sizeof(List));
        pr->data->length = 1;
        pr->data->items = malloc(sizeof(Item));
        pr->data->items[0] = tokens->items[s-1];
        return pr;
    }
    return wrong;
}

//PostProc_c check_result_proc;
Result* check_result_proc(Result* pr, void* ctx) {
    GoodData_clo* context = (GoodData_clo*) ctx;
    if (context->f == NULL) {
        printf("NULL in check_result_proc f\n");
        exit(1);
    }
    unsigned int good = context->f(pr->data, context->ctx);
    /*
    if (in_yacc) {
        printf("good? %d \n", good);
        printf("pr->s: %d\n", pr->s);
        printf("pr->data->length: %d\n", pr->data->length);
        print_tokens(pr->data);
    }
    */
    if (good) {
        return pr;
    }
    return wrong;
}

BNF_clo* check_result(BNF_clo* f, GoodData_clo* good) {
    PostProc_clo* proc = malloc(sizeof(PostProc_clo));
    proc->f = check_result_proc;
    proc->ctx = good;
    return post_process(f, proc);
}

/////////////////////////////////////////////////////////
// lex auxiliary functions
/////////////////////////////////////////////////////////

char* number_token = "number";
char* string_token = "string";
char* identifier_token = "identifier";
char* spaces_token = "spaces";
char* comment_token = "comment";
char* symbol_token = "symbol";
char* keyword_token = "keyword";
char* newline_token = "newline";
char* indent_token = "indent";

//GoodData_c check_char_cond;
unsigned int check_char_cond(List* data, void* ctx) {
    ItemCond_clo* context = (ItemCond_clo*) ctx;
    if (context->f == NULL) {
        printf("NULL in check_char_cond f\n");
        exit(1);
    }
    Item item = data->items[0];
    unsigned int result = context->f(item, context->ctx);
    return result;
}

BNF_clo* check_char(ItemCond_clo* char_cond) {
    GoodData_clo* good = malloc(sizeof(GoodData_clo));
    good->f = check_char_cond;
    //printf("in check_char, f: %p\n", good->f);
    good->ctx = char_cond;
    return check_result(get_token, good);
}

//ItemCond_c char_is_f;
unsigned int char_is_f(Item item, void* ctx) {
    char* ch = (char*) item;
    char* c = (char*) ctx;
    if (c == NULL) {
        printf("NULL in char_is_f\n");
        exit(1);
    }
    return *ch == *c;
}

BNF_clo* char_is(char c) {
    ItemCond_clo* char_cond = malloc(sizeof(ItemCond_clo));
    char_cond->f = char_is_f;
    char* ch = malloc(sizeof(char));
    *ch = c;
    char_cond->ctx = ch;
    return check_char(char_cond);
}

//no context
ItemCond_clo* is_alpha;
ItemCond_clo* is_digit;
ItemCond_clo* is_space;

//ItemCond_c is_alpha_f;
unsigned int is_alpha_f(Item item, void* ctx) {
    char* ch = (char*) item;
    return isalpha(*ch);
}
//ItemCond_c is_digit_f;
unsigned int is_digit_f(Item item, void* ctx) {
    char* ch = (char*) item;
    return isdigit(*ch);
}
//ItemCond_c is_space_f;
unsigned int is_space_f(Item item, void* ctx) {
    char* ch = (char*) item;
    return isspace(*ch);
}

//PostProc_c token_type_proc;
Result* token_type_proc(Result* pr, void* ctx) {
    char* token_name = (char*) ctx;
    if (token_name == NULL) {
        printf("NULL in token_type_proc\n");
        exit(1);
    }
    PairToken* pair = malloc(sizeof(PairToken));
    pair->name = token_name;
    pair->value = (char*) malloc(pr->data->length +1);
    for (int i = 0; i < pr->data->length; i ++) {
        char* ch = (char*) pr->data->items[i];
        pair->value[i] = *ch;
    }
    pair->value[pr->data->length] = '\0';

    List* data = malloc(sizeof(List));
    data->length = 1;
    data->items = malloc(sizeof(Item));
    data->items[0] = pair;
    Result* x = malloc(sizeof(Result));
    x->data = data;
    x->s = pr->s;
    return x;
}

BNF_clo* token_type(char* token_name, BNF_clo* f) {
    PostProc_clo* proc = (PostProc_clo*) malloc(sizeof(PostProc_clo));
    proc->f = token_type_proc;
    proc->ctx = token_name;
    return post_process(f, proc);
}

/////////////////////////////////////////////////////////
// parser auxiliary functions
/////////////////////////////////////////////////////////

//GoodData_c token_type_is_f;
unsigned int token_type_is_f(List* data, void* ctx) {
    char* token_name = (char*) ctx;
    PairToken* pair = (PairToken*) data->items[0];
    if (pair == NULL) {
        printf("NULL in token_type_is_f\n");
        exit(1);
    }
    unsigned int result = strcmp(pair->name, token_name) == 0;
    return result;
}

BNF_clo* token_type_is(char* token_name) {
    GoodData_clo* good = malloc(sizeof(GoodData_clo));
    good->f = token_type_is_f;
    good->ctx = token_name;
    return check_result(get_token, good);
}

//GoodData_c is_keyword_f;
unsigned int is_keyword_f(List* data, void* ctx) {
    char* key = (char*) ctx;
    PairToken* pair = (PairToken*) data->items[0];
    //case insensitive
    unsigned int result = _stricmp(pair->value, key) == 0;
    return result;
}

BNF_clo* is_keyword(char* key) {
    GoodData_clo* good = malloc(sizeof(GoodData_clo));
    good->f = is_keyword_f;
    good->ctx = key;
    return check_result(token_type_is(identifier_token), good);
}

//PostProc_c label_keyword;
Result* label_keyword(Result* pr, void* ctx) {
    PairToken* pair = (PairToken*) pr->data->items[0];
    pair->name = keyword_token;
    return pr;
}
    
BNF_clo* keyword(char* key) {
    PostProc_clo* proc = malloc(sizeof(PostProc_clo));
    proc->f = label_keyword;
    proc->ctx = NULL;
    return post_process(is_keyword(key), proc);
}

//GoodData_c symbol_is_f;
unsigned int symbol_is_f(List* data, void* ctx) {
    char* ch = (char*) ctx;
    PairToken* pair = (PairToken*) data->items[0];
    unsigned int result = *pair->value == *ch;
    return result;
}

BNF_clo* symbol_is(char ch) {
    GoodData_clo* good = malloc(sizeof(GoodData_clo));
    good->f = symbol_is_f;
    char* c = malloc(sizeof(char));
    *c = ch;
    good->ctx = c;
    return check_result(token_type_is(symbol_token), good);
}

/////////////////////////////////////////////////////////
// main functions
/////////////////////////////////////////////////////////

char* read_file_to_string(const char* filename) {
    FILE* file = fopen(filename, "rb");
    if (!file) {
        perror("Failed to open file");
        return NULL;
    }

    // Get the file size
    fseek(file, 0, SEEK_END);
    long size = ftell(file);
    rewind(file);

    // Allocate memory (+1 for null-terminator)
    char* buffer = malloc(size + 1);
    if (!buffer) {
        perror("Failed to allocate memory");
        fclose(file);
        return NULL;
    }

    // Read file into buffer
    fread(buffer, 1, size, file);
    buffer[size] = '\0'; // Null-terminate the string

    fclose(file);
    return buffer;
}

void init(void) {
    
    wrong = malloc(sizeof(Result));
    wrong->s = 0;
    wrong->data = malloc(sizeof(List));
    wrong->data->length = 0;
    wrong->data->items = NULL;

    nothing = malloc(sizeof(BNF_clo));
    nothing->f = nothing_f;
    nothing->ctx = NULL;

    get_token = malloc(sizeof(BNF_clo));
    get_token->f = get_token_f;
    get_token->ctx = NULL;

    is_alpha = malloc(sizeof(BNF_clo));
    is_alpha->f = is_alpha_f;
    is_alpha->ctx = NULL;
    is_digit = malloc(sizeof(BNF_clo));
    is_digit->f = is_digit_f;
    is_digit->ctx = NULL;
    is_space = malloc(sizeof(BNF_clo));
    is_space->f = is_space_f;
    is_space->ctx = NULL;

}

char* content;
void init_read_file(void) {

    content = read_file_to_string("example.sql");
    //content = read_file_to_string("C:\\Users\\T162880\\Documents\\example.sql");
    printf("filesize: %d\n", (int) strlen(content));
    tokens = (List*) malloc(sizeof(List));
    tokens->length = strlen(content);
    tokens->items = malloc(tokens->length * sizeof(Item));
    for (int i = 0; i < tokens->length; i ++) {
        tokens->items[i] = &content[i];
    }    

}

void lex(void) {

    BNF_clo* dot = char_is('.');
    BNF_clo* digit = check_char(is_digit);
    BNF_clo* number = seq2(mor(digit), opt(seq2(dot, mor(digit))));

    BNF_clo* single_quote = char_is('\'');
    BNF_clo* escape_single_quote = seq2(single_quote, single_quote);
    BNF_clo* string_char = par2(escape_single_quote, not_then(single_quote, get_token));
    BNF_clo* string = seq(single_quote, zmo(string_char), single_quote, NULL);

    BNF_clo* letter = check_char(is_alpha);
    BNF_clo* underscore = char_is('_');
    BNF_clo* identifier_start = par2(letter, underscore);
    BNF_clo* identifier_rest = par(letter, underscore, digit, NULL);
    BNF_clo* identifier = seq2(identifier_start, zmo(identifier_rest));

    BNF_clo* space = check_char(is_space);
    //BNF_clo* space = par2(char_is(' '), par2(char_is('\t'), par2(char_is('\r'), char_is('\n'))));
    BNF_clo* spaces = mor(space);    

    BNF_clo* CR = char_is('\r');
    BNF_clo* LF = char_is('\n');
    BNF_clo* line_end = par(seq2(CR, LF), CR, LF, NULL);
    BNF_clo* line_comment_begin = seq2(char_is('-'), char_is('-'));
    BNF_clo* line_comment_char = not_then(line_end, get_token);
    BNF_clo* line_comment = seq(line_comment_begin, zmo(line_comment_char), line_end, NULL);
    BNF_clo* block_comment_begin = seq2(char_is('/'), char_is('*'));
    BNF_clo* block_comment_end = seq2(char_is('*'), char_is('/'));
    BNF_clo* block_comment_char = not_then(block_comment_end, get_token);
    BNF_clo* block_comment = seq(block_comment_begin, zmo(block_comment_char), block_comment_end, NULL);
    BNF_clo* comment = par2(block_comment, line_comment);    

    BNF_clo* symbol = get_token;

    BNF_clo* token = par(
        token_type(number_token, number), 
        token_type(string_token, string), 
        token_type(identifier_token, identifier), 
        token_type(spaces_token, spaces), 
        token_type(comment_token, comment), 
        token_type(symbol_token, symbol),
        NULL);

    BNF_clo* a = zmo(token);

    Result* pr = a->f(1, a->ctx);
    if (!pr->s) {
        tokens->length = 0;
        free(tokens->items);
        printf("lex failed\n");
    }
    printf("lex finished: %d\n", pr->s);

    //remove spaces and comments
    List* data = malloc(sizeof(List));
    data->length = 0;
    data->items = NULL;
    for (int i = 0, j = 0; i < pr->data->length; i ++, j ++) {
        PairToken* pair = (PairToken*) pr->data->items[i];
        if (strcmp(pair->name, spaces_token) == 0) {
            continue;
        }
        if (strcmp(pair->name, comment_token) == 0) {
            continue;
        }
        data->length ++;
        data->items = realloc(data->items, data->length * sizeof(Item));
        data->items[data->length - 1] = pair;
    }
    //free(pr->data->items);
    //free(pr->data);
    //free(tokens->items);
    //free(tokens);
    tokens = data;

    //print_tokens();

    /*
    if (pr->s) {
        printf("%d, %d\n", pr->s, pr->data->length);
        for (int i = 0; i < pr->data->length; i ++) {
            PairToken* pair = (PairToken*) pr->data->items[i];
            if (strcmp(pair->name, spaces_token) == 0) {
                continue;
            }
            if (strcmp(pair->name, comment_token) == 0) {
                continue;
            }
            printf("(%s, %s)\n", pair->name, pair->value);
        }
    }
    */

    /*
    if (pr->s) {
        printf("s: %d\n", pr->s);
        printf("length: %d\n", pr->data->length);
        for (int i = 0; i < pr->data->length; i ++) {
            //printf("%d\n", i);
            char* ch = (char*) pr->data->items[i];
            //printf("%c\n", *ch);
            printf("%c", *ch);
        }
        printf("\n");
    } else {
        printf("wrong\n");
    }
    */

}

void yacc(void) {

    //these are recursive, so they must be declared first
    //the pointers will not change so we can reference them
    //the actual initialization will fill in the content by using *sql = *seq(...) format
    //so the initialization will not change the pointer itself

    BNF_clo* sql = malloc(sizeof(BNF_clo));
    BNF_clo* expression = malloc(sizeof(BNF_clo));
    BNF_clo* condition = malloc(sizeof(BNF_clo));

    BNF_clo* name = token_type_is(identifier_token);

    BNF_clo* column_alias = seq2(opt(keyword("AS")), 
        not_then(par(is_keyword("FROM"), is_keyword("ORDER"), NULL), 
        name));
    BNF_clo* column = seq2(expression, opt(column_alias));
    BNF_clo* next_column = seq2(symbol_is(','), column);
    BNF_clo* first_column = column;
    BNF_clo* column_list = seq2(first_column, zmo(next_column));

    BNF_clo* table_name = token_type_is(identifier_token);
    BNF_clo* subquery = seq(symbol_is('('), sql, symbol_is(')'), NULL);
    BNF_clo* table_alias = seq2(opt(keyword("AS")), name);
    BNF_clo* table_struct = seq2(par2(table_name, subquery), opt(table_alias));
    BNF_clo* from_clause = seq2(keyword("FROM"), table_struct);

    BNF_clo* case_end = keyword("END");
    BNF_clo* else_branch = seq2(keyword("ELSE"), expression);
    BNF_clo* when_branch = seq(keyword("WHEN"), condition, keyword("THEN"), expression, NULL);
    BNF_clo* case_rest = seq(mor(when_branch), opt(else_branch), case_end, NULL);
    BNF_clo* case_expression = seq2(keyword("CASE"), case_rest);
    BNF_clo* special_construct = case_expression;

    BNF_clo* qualifier = seq2(name, symbol_is('.'));
    BNF_clo* qualified_name = seq2(zmo(qualifier), name);
    BNF_clo* function_name = qualified_name;
    BNF_clo* function_arg = expression;
    BNF_clo* next_function_arg = seq2(symbol_is(','), function_arg);
    BNF_clo* function_args = seq2(function_arg, zmo(next_function_arg));
    BNF_clo* function = seq(function_name, symbol_is('('), opt(function_args), symbol_is(')'), NULL);

    BNF_clo* number = token_type_is(number_token);
    BNF_clo* string = token_type_is(string_token);

    BNF_clo* expression_atom = par(
        number,
        string,
        special_construct,
        function,
        qualified_name,
        NULL);

    BNF_clo* plus_minus = par2(symbol_is('+'), symbol_is('-'));
    BNF_clo* times_divide = par2(symbol_is('*'), symbol_is('/'));
    BNF_clo* expression_group = seq(symbol_is('('), expression, symbol_is(')'), NULL);
    BNF_clo* expression_factor = par2(expression_atom, expression_group);
    BNF_clo* next_expression_factor = seq2(times_divide, expression_factor);
    BNF_clo* expression_term = par2(expression_factor, zmo(next_expression_factor));
    BNF_clo* next_expression_term = seq2(plus_minus, expression_term);
    *expression = *seq2(expression_term, zmo(next_expression_term));

    BNF_clo* is_null_condition = seq(expression, keyword("IS"), opt(keyword("NOT")), keyword("NULL"), NULL);
    BNF_clo* special_condition = is_null_condition;
    BNF_clo* comparison_op = par(
        seq2(symbol_is('>'), opt(symbol_is('='))),
        seq2(symbol_is('<'), opt(symbol_is('='))),
        seq2(symbol_is('!'), symbol_is('=')),
        seq2(symbol_is('<'), symbol_is('>')),
        symbol_is('='),
        NULL);
    BNF_clo* comparison_condition = seq(expression, comparison_op, expression, NULL);
    BNF_clo* boolean_atom = par(comparison_condition, special_condition, NULL);
    BNF_clo* boolean_group = seq(symbol_is('('), condition, symbol_is(')'), NULL);
    BNF_clo* boolean_term = seq2(zmo(keyword("NOT")), par2(boolean_atom, boolean_group));
    BNF_clo* boolean_op = par2(keyword("AND"), keyword("OR"));
    BNF_clo* next_boolean_term = seq2(boolean_op, boolean_term);
    BNF_clo* boolean_rest = zmo(next_boolean_term);
    *condition = *seq2(boolean_term, boolean_rest);
    
    BNF_clo* join_clause = seq(keyword("JOIN"), table_struct, keyword("ON"), condition, NULL);
    BNF_clo* joins = mor(join_clause);

    BNF_clo* where_clause = seq2(keyword("WHERE"), condition);
    BNF_clo* group_by_clause = seq(keyword("GROUP"), keyword("BY"), column_list, NULL);
    BNF_clo* having_clause = seq2(keyword("HAVING"), condition);
    BNF_clo* order_by_clause = seq(keyword("ORDER"), keyword("BY"), column_list, NULL);

    BNF_clo* cte_columns = seq(symbol_is('('), zmo(seq2(symbol_is(','), name)), symbol_is(')'), NULL);
    BNF_clo* cte = seq(name, opt(cte_columns), keyword("AS"), subquery, NULL);
    BNF_clo* next_cte = seq2(symbol_is(','), cte);
    BNF_clo* with_cte = seq(keyword("WITH"), cte, zmo(next_cte), NULL);

    *sql = *seq(
        opt(with_cte),
        keyword("SELECT"), 
        column_list,
        from_clause,
        opt(joins),
        opt(where_clause),
        opt(seq2(group_by_clause, opt(having_clause))),
        opt(order_by_clause),
        NULL);

    //print_tokens();


    BNF_clo* a;
    a = get_token;
    a = token_type_is(identifier_token);
    a = name;
    a = column;
    a = first_column;
    a = next_column;
    a = join_clause;
    a = where_clause;
    a = group_by_clause;
    a = order_by_clause;
    a = keyword("SELECT");
    a = column_list;
    a = where_clause;
    a = opt(where_clause);
    a = opt(seq2(group_by_clause, opt(having_clause)));
    a = opt(order_by_clause);
    a = joins;
    a = from_clause;
    a = seq(
        keyword("SELECT"), 
        column_list,
        from_clause,
        opt(joins),
        opt(where_clause),
        opt(seq2(group_by_clause, opt(having_clause))),
        opt(order_by_clause),
        NULL);

    a = function_arg;
    a = function_args;
    a = function;
    a = case_expression;
    a = keyword("CASE");
    a = expression;
    a = column;
    a = first_column;
    a = keyword("CASE");
    a = keyword("WHEN");
    a = condition;
    a = expression;
    a = comparison_op;
    a = expression;
    a = comparison_condition;
    a = when_branch;
    a = mor(when_branch);
    a = case_expression;
    a = next_column;
    a = column_list;
    a = join_clause;
    a = keyword("JOIN");
    a = subquery;
    a = table_struct;
    a = expression_atom;
    a = with_cte;
    a = sql;

    Result* pr = a->f(1, a->ctx);
    printf("%d\n", pr->s);
    print_tokens(pr->data);

}

//PostProc_c insert_newline_proc;
Result* insert_newline_proc(Result* pr, void* ctx) {
    List* data = malloc(sizeof(List));
    data->length = pr->data->length + 1;
    data->items = malloc(data->length * sizeof(Item));
    PairToken* newline = malloc(sizeof(PairToken));
    newline->name = newline_token;
    newline->value = malloc(2 * sizeof(char));
    newline->value[0] = '\n';
    newline->value[1] = '\0';
    data->items[0] = newline;
    for (int i = 0; i < pr->data->length; i ++) {
        data->items[i + 1] = pr->data->items[i];
    }
    free(pr->data->items);
    free(pr->data);
    pr->data = data;
    pr->data->length = data->length;
    return pr;
}

BNF_clo* insert_newline(BNF_clo* f) {
    PostProc_clo* proc = malloc(sizeof(PostProc_clo));
    proc->f = insert_newline_proc;
    proc->ctx = NULL;
    return post_process(f, proc);
}

//PostProc_c insert_newline_after_proc;
Result* insert_newline_after_proc(Result* pr, void* ctx) {
    List* data = malloc(sizeof(List));
    data->length = pr->data->length + 1;
    data->items = malloc(data->length * sizeof(Item));
    PairToken* newline = malloc(sizeof(PairToken));
    newline->name = newline_token;
    newline->value = malloc(2 * sizeof(char));
    newline->value[0] = '\n';
    newline->value[1] = '\0';
    data->items[data->length - 1] = newline;
    for (int i = 0; i < pr->data->length; i ++) {
        data->items[i] = pr->data->items[i];
    }
    free(pr->data->items);
    free(pr->data);
    pr->data = data;
    pr->data->length = data->length;
    return pr;
}

BNF_clo* insert_newline_after(BNF_clo* f) {
    PostProc_clo* proc = malloc(sizeof(PostProc_clo));
    proc->f = insert_newline_after_proc;
    proc->ctx = NULL;
    return post_process(f, proc);
}

//PostProc_c insert_indent_proc;
Result* insert_indent_proc(Result* pr, void* ctx) {
    Result* pr2 = malloc(sizeof(Result));
    pr2->s = pr->s;
    pr2->data = malloc(sizeof(List));
    pr2->data->length = 1;
    pr2->data->items = malloc(sizeof(Item));
    IndentPair* indent = malloc(sizeof(IndentPair));
    indent->name = indent_token;
    indent->data = pr->data;
    pr2->data->items[0] = indent;
    return pr2;
}

BNF_clo* insert_indent(BNF_clo* f) {
    PostProc_clo* proc = malloc(sizeof(PostProc_clo));
    proc->f = insert_indent_proc;
    proc->ctx = NULL;
    return post_process(f, proc);
}

char* to_upper_copy(const char* src) {
    char* dest = malloc(strlen(src) + 1);
    if (!dest) return NULL;

    for (int i = 0; src[i]; i++) {
        dest[i] = toupper((unsigned char)src[i]);
    }
    dest[strlen(src)] = '\0';
    return dest;
}

char* replace_substring(const char* original, const char* old_sub, const char* new_sub) {
    const char* pos;
    int count = 0;
    size_t old_len = strlen(old_sub);
    size_t new_len = strlen(new_sub);

    // Count how many times old_sub appears
    for (pos = strstr(original, old_sub); pos; pos = strstr(pos + old_len, old_sub)) {
        count++;
    }

    // Allocate memory for the new string
    size_t result_size = strlen(original) + count * (new_len - old_len) + 1;
    char* result = malloc(result_size);
    if (!result) return NULL;

    char* dest = result;
    const char* src = original;

    while ((pos = strstr(src, old_sub))) {
        // Copy characters before the match
        size_t len = pos - src;
        memcpy(dest, src, len);
        dest += len;

        // Copy replacement
        memcpy(dest, new_sub, new_len);
        dest += new_len;

        // Advance source pointer past the match
        src = pos + old_len;
    }

    // Copy the rest of the original string
    strcpy(dest, src);

    return result;
}

char* format(List* data) {
    char* text = malloc(1);
    text[0] = '\0';
    for (int i = 0; i < data->length; i ++) {
        PairToken* pair = (PairToken*) data->items[i];
        if (strcmp(pair->name, indent_token) == 0) {
            IndentPair* indent = (IndentPair*) pair;
            char* sub = format(indent->data);
            char* sub2 = replace_substring(sub, "\n", "\n    ");
            text = realloc(text, strlen(text) + strlen(sub2) + 1);
            strcat(text, sub2);
        } else if (strcmp(pair->name, newline_token) == 0) {
            text = realloc(text, strlen(text) + 2);
            strcat(text, "\n");
        } else if (strcmp(pair->name, keyword_token) == 0) {
            char* upper = to_upper_copy(pair->value);
            text = realloc(text, strlen(text) + strlen(upper) + 2);
            strcat(text, " ");
            strcat(text, upper);
        } else {
            text = realloc(text, strlen(text) + strlen(pair->value) + 2);
            strcat(text, " ");
            strcat(text, pair->value);
        }
    }
    return text;
}

void formatter(void) {

    //these are recursive, so they must be declared first
    //the pointers will not change so we can reference them
    //the actual initialization will fill in the content by using *sql = *seq(...) format
    //so the initialization will not change the pointer itself

    BNF_clo* sql = malloc(sizeof(BNF_clo));
    BNF_clo* expression = malloc(sizeof(BNF_clo));
    BNF_clo* condition = malloc(sizeof(BNF_clo));

    BNF_clo* name = token_type_is(identifier_token);

    BNF_clo* column_alias = seq2(opt(keyword("AS")), 
        not_then(par(is_keyword("FROM"), is_keyword("ORDER"), NULL), 
        name));
    BNF_clo* column = seq2(expression, opt(column_alias));
    BNF_clo* next_column = insert_newline(
        seq2(symbol_is(','), column));
    BNF_clo* first_column = column;
    BNF_clo* column_list = insert_indent(insert_newline(
        seq2(first_column, zmo(next_column))));

    BNF_clo* table_name = token_type_is(identifier_token);
    BNF_clo* subquery_sql = insert_indent(insert_newline(
        sql));
    BNF_clo* subquery_end = insert_newline(
        symbol_is(')'));
    BNF_clo* subquery = seq(symbol_is('('), subquery_sql, subquery_end, NULL);
    BNF_clo* table_alias = seq2(opt(keyword("AS")), name);
    BNF_clo* table_struct = seq2(par2(table_name, subquery), opt(table_alias));
    BNF_clo* from_clause = insert_newline(
        seq2(keyword("FROM"), table_struct));

    BNF_clo* case_end = insert_newline(
        keyword("END"));
    BNF_clo* else_branch = insert_newline(
        seq2(keyword("ELSE"), expression));
    BNF_clo* when_branch = insert_newline(
        seq(keyword("WHEN"), condition, keyword("THEN"), expression, NULL));
    BNF_clo* case_rest = insert_indent(
        seq(mor(when_branch), opt(else_branch), case_end, NULL));
    BNF_clo* case_expression = seq2(keyword("CASE"), case_rest);
    BNF_clo* special_construct = case_expression;

    BNF_clo* qualifier = seq2(name, symbol_is('.'));
    BNF_clo* qualified_name = seq2(zmo(qualifier), name);
    BNF_clo* function_name = qualified_name;
    BNF_clo* function_arg = expression;
    BNF_clo* next_function_arg = seq2(symbol_is(','), function_arg);
    BNF_clo* function_args = seq2(function_arg, zmo(next_function_arg));
    BNF_clo* function = seq(function_name, symbol_is('('), opt(function_args), symbol_is(')'), NULL);

    BNF_clo* number = token_type_is(number_token);
    BNF_clo* string = token_type_is(string_token);

    BNF_clo* expression_atom = par(
        number,
        string,
        special_construct,
        function,
        qualified_name,
        NULL);

    BNF_clo* plus_minus = par2(symbol_is('+'), symbol_is('-'));
    BNF_clo* times_divide = par2(symbol_is('*'), symbol_is('/'));
    BNF_clo* expression_group = seq(symbol_is('('), expression, symbol_is(')'), NULL);
    BNF_clo* expression_factor = par2(expression_atom, expression_group);
    BNF_clo* next_expression_factor = seq2(times_divide, expression_factor);
    BNF_clo* expression_term = par2(expression_factor, zmo(next_expression_factor));
    BNF_clo* next_expression_term = seq2(plus_minus, expression_term);
    *expression = *seq2(expression_term, zmo(next_expression_term));

    BNF_clo* is_null_condition = seq(expression, keyword("IS"), opt(keyword("NOT")), keyword("NULL"), NULL);
    BNF_clo* special_condition = is_null_condition;
    BNF_clo* comparison_op = par(
        seq2(symbol_is('>'), opt(symbol_is('='))),
        seq2(symbol_is('<'), opt(symbol_is('='))),
        seq2(symbol_is('!'), symbol_is('=')),
        seq2(symbol_is('<'), symbol_is('>')),
        symbol_is('='),
        NULL);
    BNF_clo* comparison_condition = seq(expression, comparison_op, expression, NULL);
    BNF_clo* boolean_atom = par(comparison_condition, special_condition, NULL);
    BNF_clo* boolean_group = seq(symbol_is('('), condition, symbol_is(')'), NULL);
    BNF_clo* boolean_term = seq2(zmo(keyword("NOT")), par2(boolean_atom, boolean_group));
    BNF_clo* boolean_op = par2(keyword("AND"), keyword("OR"));
    BNF_clo* next_boolean_term = insert_newline(
        seq2(boolean_op, boolean_term));
    BNF_clo* boolean_rest = insert_indent(
        zmo(next_boolean_term));
    *condition = *seq2(boolean_term, boolean_rest);
    
    BNF_clo* join_clause = insert_newline(
        seq(keyword("JOIN"), table_struct, keyword("ON"), condition, NULL));
    BNF_clo* joins = insert_indent(
        mor(join_clause));

    BNF_clo* where_clause = insert_newline(
        seq2(keyword("WHERE"), condition));
    BNF_clo* group_by_clause = insert_newline(
        seq(keyword("GROUP"), keyword("BY"), column_list, NULL));
    BNF_clo* having_clause = insert_indent(insert_newline(
        seq2(keyword("HAVING"), condition)));
    BNF_clo* order_by_clause = insert_newline(
        seq(keyword("ORDER"), keyword("BY"), column_list, NULL));

    BNF_clo* cte_columns = seq(symbol_is('('), zmo(seq2(symbol_is(','), name)), symbol_is(')'), NULL);
    BNF_clo* cte = insert_newline(
        seq(name, opt(cte_columns), keyword("AS"), subquery, NULL));
    BNF_clo* next_cte = insert_newline(
        seq2(symbol_is(','), cte));
    BNF_clo* with_cte = insert_newline_after(
        seq(keyword("WITH"), cte, zmo(next_cte), NULL));

    *sql = *seq(
        opt(with_cte),
        keyword("SELECT"), 
        column_list,
        from_clause,
        opt(joins),
        opt(where_clause),
        opt(seq2(group_by_clause, opt(having_clause))),
        opt(order_by_clause),
        NULL);

    //print_tokens();


    BNF_clo* a;
    a = with_cte;
    a = sql;

    Result* pr = a->f(1, a->ctx);
    printf("formatted tokens: %d\n", pr->s - 1);
    char* text = format(pr->data);
    printf("%s\n", text);

}

int main() {
    //printf("begin\n");

    init();
    init_read_file();

    //test();
    lex();
    //yacc();
    formatter();

    return 0;
}


