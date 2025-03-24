# bnf: state --> state, data
# where state is an integer and data is a list

def seq(*fs):
    def fn(s):
        data = []
        for f in fs:
            s, d = f(s)
            if not s: 
                return 0, []
            data += d
        return s, data
    return fn

def par(*fs):
    def fn(s):
        for f in fs:
            t, d = f(s)
            if t:
                return t, d
        return 0, []
    return fn

def nothing(s): return s, []
def opt(f): return par(f, nothing)
def mor(f): return seq(f, zmo(f))
#def zmo(f): return opt(lambda s: mor(f)(s))

#change to loop for depth
def zmo(f):
    def fn(s):
        data = []
        while True:
            t, d = f(s)
            if not t:
                break
            s = t
            data += d
        return s, data
    return fn

########################

def post_process(f, proc):
    def fn(s):
        s, d = f(s)
        if s:
            return proc(s, d)
        return 0, []
    return fn

def seq2(f, g):
    def proc(s, d):
        s, d2 = g(s)
        if s:
            return s, d+d2
        return 0, []
    return post_process(f, proc)

def par2(f, g):
    def fn(s):
        t, d = f(s)
        if t:
            return t, d
        return g(s)
    return fn

#import functools
#def seq(*fs): return functools.reduce(seq2, fs)
#def par(*fs): return functools.reduce(par2, fs)

def conditional(cond, f):
    def fn(s):
        if cond(s):
            return f(s)
        return 0, []
    return fn

def not_then(f, g):
    def cond(s):
        s, _ = f(s)
        return not s
    return conditional(cond, g)

def on_success(f, g):
    def proc(s, d):
        g(d)
        return s, d
    return post_process(f, proc)

def message(f, msg):
    def fn(data):
        print(msg, data)
    return on_success(f, fn)

def check_result(f, good):
    def proc(s, d):
        if good(d):
            return s, d
        return 0, []
    return post_process(f, proc)

def end_token(s):
    if s > len(tokens):
        return s, []
    return 0, []

def error_token(s):
    if not s:
        return 1, []
    return 0, []

def get_any(s):
    return s+1, [tokens[s-1]]

#get_one = not_then(error_token, get_any)
#get_token = not_then(end_token, get_one)

def get_token(s):
    if 0 < s <= len(tokens):
        return s+1, [tokens[s-1]]
    return 0, []

########################

def lex():

    def token_type(token_name):
        def fn(f):
            def proc(s, d):
                return s, [(token_name, "".join(d))]
            return post_process(f, proc)
        return fn
            
    def token(s): return par(number, string, identifier, spaces, comment, symbol)(s)
    @token_type("number")
    def number(s): return seq(mor(digit), opt(seq(dot, mor(digit))))(s)
    @token_type("string")
    def string(s): return seq(single_quote, zmo(string_char), single_quote)(s)
    def string_char(s): return par(escape_single_quote, not_then(single_quote, get_token))(s)
    def escape_single_quote(s): return seq(single_quote, single_quote)(s)
    @token_type("identifier")
    def identifier(s): return seq(identifier_start, zmo(identifier_rest))(s)
    def identifier_start(s): return par(letter, underscore)(s)
    def identifier_rest(s): return par(letter, digit, underscore)(s)
    @token_type("spaces")
    def spaces(s): return mor(space)(s)
    @token_type("comment")
    def comment(s): return par(block_comment, line_comment)(s)
    def block_comment(s): return seq(block_comment_begin, zmo(block_comment_char), block_comment_end)(s)
    def block_comment_begin(s): return seq(char_is("/"), char_is("*"))(s)
    def block_comment_char(s): return not_then(block_comment_end, get_token)(s)
    def block_comment_end(s): return seq(char_is("*"), char_is("/"))(s)
    def line_comment(s): return seq(line_comment_begin, zmo(line_comment_char), line_end)(s)
    def line_comment_begin(s): return seq(char_is("-"), char_is("-"))(s)
    def line_comment_char(s): return not_then(line_end, get_token)(s)
    def line_end(s): return par(seq(CR, LF), CR, LF)(s)
    @token_type("symbol")
    def symbol(s): return get_token(s)

    def dot(s): return char_is(".")(s)
    def single_quote(s): return char_is("'")(s)
    def underscore(s): return char_is("_")(s)
    def CR(s): return char_is("\r")(s)
    def LF(s): return char_is("\n")(s)
    def space(s): return check_char(lambda c: c.isspace())(s)
    def letter(s): return check_char(lambda c: c.isalpha())(s)
    def digit(s): return check_char(lambda c: c.isdigit())(s)

    #lex utility functions

    def check_char(f):
        def good(d):
            return f(d[0])
        return check_result(get_token, good)

    def char_is(c): return check_char(lambda x: x == c)
 
    return zmo(token)(1)

########################

def yacc():

    def newline(f):
        def proc(s, d):
            return s, [("newline", "\n"), *d]
        return post_process(f, proc)

    def newline_after(f):
        def proc(s, d):
            return s, [*d, ("newline", "\n")]
        return post_process(f, proc)

    def indent(f):
        def proc(s, d):
            return s, [("indent", d)]
        return post_process(f, proc)

    #sql

    def sql(s): return seq(
                opt(with_cte),
                SELECT, column_list, 
                from_clause, 
                joins,
                opt(where), 
                opt(seq(group_by, 
                        opt(having))), 
                opt(order_by)
                )(s)
    
    def SELECT(s): return keyword("select")(s)

    @indent
    @newline
    def column_list(s): return seq(first_column, zmo(next_column))(s)
    @indent
    def joins(s): return zmo(join)(s)

    @newline
    def next_column(s): return seq(symbol(","), column)(s)
    @newline
    def from_clause(s): return seq(keyword("from"), table_struct)(s)
    @newline
    def join(s): return seq(keyword("join"), table_struct, keyword("on"), condition)(s)
    @newline
    def where(s): return seq(keyword("where"), condition)(s)
    @newline
    def group_by(s): return seq(keyword("group"), keyword("by"), column_list)(s)
    @indent
    @newline
    def having(s): return seq(keyword("having"), condition)(s)
    @newline
    def order_by(s): return seq(keyword("order"), keyword("by"), column_list)(s)

    def table_struct(s): return seq(par(table_name, subquery), opt(table_alias))(s)
    def subquery(s): return seq(symbol("("), subquery_sql, subquery_end)(s)
    @indent
    @newline
    def subquery_sql(s): return sql(s)
    @newline
    def subquery_end(s): return symbol(")")(s)
    def table_alias(s): return seq(opt(keyword("as")), name)(s)
    def table_name(s): return token_type("identifier")(s)
    def name(s): return token_type("identifier")(s)
    def first_column(s): return column(s)
    def column(s): return seq(expression, opt(column_alias))(s)
    def column_alias(s): return seq(opt(keyword("as")), not_then(
                par(keyword("from"), keyword("order")), 
                name))(s)

    @newline_after
    def with_cte(s): return seq(keyword("with"), cte, zmo(next_cte))(s)
    @newline
    def cte(s): return seq(name, opt(cte_columns), keyword("as"), subquery)(s)
    @newline
    def next_cte(s): return seq(symbol(","), cte)(s)
    def cte_columns(s): return seq(symbol("("), name, zmo(seq(symbol(","), name)), symbol(")"))(s)

    #boolean expression

    def condition(s): return seq(boolean_term, boolean_rest)(s)
    @indent
    def boolean_rest(s): return zmo(next_boolean_term)(s)
    @newline
    def next_boolean_term(s): return seq(boolean_op, boolean_term)(s)
    def boolean_op(s): return par(keyword("and"), keyword("or"))(s)
    def boolean_term(s): return seq(zmo(keyword("not")), par(boolean_atom, boolean_group))(s)
    def boolean_atom(s): return par(comparison_condition, special_condition)(s)
    def boolean_group(s): return seq(symbol("("), condition, symbol(")"))(s)
    def special_condition(s): return is_null_condition(s)
    def is_null_condition(s): return seq(expression, keyword("is"), opt(keyword("not")), keyword("null"))(s)
    def comparison_condition(s): return seq(expression, comparison_op, expression)(s)
    def comparison_op(s): return par(
                seq(symbol(">"), symbol("=")),
                seq(symbol("<"), symbol("=")),
                seq(symbol("!"), symbol("=")),
                seq(symbol("<"), symbol(">")),
                symbol("="),
                symbol(">"),
                symbol("<"),
                )(s)

    #expression

    def expression(s): return seq(expression_term, zmo(next_expression_term))(s)
    def next_expression_term(s): return seq(plus_minus, expression_term)(s)
    def expression_term(s): return seq(expression_factor, zmo(next_expression_factor))(s)
    def next_expression_factor(s): return seq(times_divide, expression_factor)(s)
    def expression_factor(s): return par(expression_atom, expression_group)(s)
    def expression_group(s): return seq(left_parenthesis, expression, right_parenthesis)(s)
    def expression_atom(s): return par(number, string, special_construct, function, qualified_name)(s)
    def special_construct(s): return case_expression(s)
    def case_expression(s): return seq(keyword("case"), case_rest)(s)
    @indent
    def case_rest(s): return seq(mor(when_branch), opt(else_branch), END)(s)
    @newline
    def END(s): return keyword("end")(s)
    @newline
    def when_branch(s): return seq(keyword("when"), condition, keyword("then"), expression)(s)
    @newline
    def else_branch(s): return seq(keyword("else"), expression)(s)

    def function(s): return seq(function_name, left_parenthesis, opt(function_parameters), right_parenthesis)(s)
    def function_parameters(s): return seq(function_parameter, zmo(function_next_parameter))(s)
    def function_parameter(s): return expression(s)
    def function_next_parameter(s): return seq(symbol(","), function_parameter)(s)
    def function_name(s): return qualified_name(s)
    def qualified_name(s): return seq(zmo(qualifier), name)(s)
    def qualifier(s): return seq(name, dot)(s)

    def plus_minus(s): return par(plus, minus)(s)
    def times_divide(s): return par(times, divide)(s)
    def number(s): return token_type("number")(s)
    def string(s): return token_type("string")(s)
    def left_parenthesis(s): return symbol("(")(s)
    def right_parenthesis(s): return symbol(")")(s)
    def plus(s): return symbol("+")(s)
    def minus(s): return symbol("-")(s)
    def times(s): return symbol("*")(s)
    def divide(s): return symbol("/")(s)
    def dot(s): return symbol(".")(s)

    #yacc utility functions

    def token_type(tname):
        def good(d):
            t, _ = d[0]
            return t == tname
        return check_result(get_token, good)

    def keyword(key):
        def is_keyword(d):
            _, value = d[0]
            return value.upper() == key.upper()
        confirm_keyword = check_result(token_type("identifier"), is_keyword)
        def proc(s, _):
            return s, [("keyword", key.upper())]
        return post_process(confirm_keyword, proc)

    def symbol(ch):
        def good(d):
            _, value = d[0]
            return value == ch
        return check_result(token_type("symbol"), good)

    return sql(1)

########################

def format(d):
    text = ""
    for t,v in d:
        if t == "indent":
            text += format(v).replace("\n", "\n    ")
        elif t == "newline":
            text += v
        else:
            text += " " + v
    return text

########################

if __name__ == "__main__":

    input = """
    with company as (
    select name, address
    from company
    )
    select 
        concat(first_name, ' ', last_name) as EmployeeName
        , address HomeAddress
        , company
        , case
        when name = 'Richard' then 'New York'
        when name = 'Steven' then 'New Jersey'
        else 'Boston'
        end State
    from employee as e
    join (
        select name, manager
        from department
    ) d on e.name = d.name
    join company co on co.name = e.company
    where 1=1
        and name = 'Richard'
        and State = 'New York'
        and not address is null
    group by name
    order by name
"""

    tokens = input
    s, data = lex()
    print(s, len(data))
    data = [d for d in data if d[0] != "spaces"]
    print(s, len(data))
    tokens = [d for d in data if d[0] != "comment"]
    print(s, len(tokens))
    #for d in tokens: print(d)
    s, data = yacc()
    print(s)
    #for d in data: print(d)
    print()
    print(format(data))

