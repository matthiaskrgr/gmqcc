/*
 * Copyright (C) 2012
 *     Wolfgang Bumiller
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy of
 * this software and associated documentation files (the "Software"), to deal in
 * the Software without restriction, including without limitation the rights to
 * use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies
 * of the Software, and to permit persons to whom the Software is furnished to do
 * so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "gmqcc.h"
#include "ast.h"

#define ast_instantiate(T, ctx, destroyfn)                          \
    T* self = (T*)mem_a(sizeof(T));                                 \
    if (!self) {                                                    \
        return NULL;                                                \
    }                                                               \
    ast_node_init((ast_node*)self, ctx, TYPE_##T);                  \
    ( (ast_node*)self )->node.destroy = (ast_node_delete*)destroyfn

/* It must not be possible to get here. */
static GMQCC_NORETURN void _ast_node_destroy(ast_node *self)
{
    fprintf(stderr, "ast node missing destroy()\n");
    abort();
}

/* Initialize main ast node aprts */
static void ast_node_init(ast_node *self, lex_ctx ctx, int nodetype)
{
    self->node.context = ctx;
    self->node.destroy = &_ast_node_destroy;
    self->node.keep    = false;
    self->node.nodetype = nodetype;
}

/* General expression initialization */
static void ast_expression_init(ast_expression *self,
                                ast_expression_codegen *codegen)
{
    self->expression.codegen = codegen;
    self->expression.vtype   = TYPE_VOID;
    self->expression.next    = NULL;
    self->expression.outl    = NULL;
    self->expression.outr    = NULL;
    MEM_VECTOR_INIT(&self->expression, params);
}

static void ast_expression_delete(ast_expression *self)
{
    size_t i;
    if (self->expression.next)
        ast_delete(self->expression.next);
    for (i = 0; i < self->expression.params_count; ++i) {
        ast_delete(self->expression.params[i]);
    }
    MEM_VECTOR_CLEAR(&self->expression, params);
}

static void ast_expression_delete_full(ast_expression *self)
{
    ast_expression_delete(self);
    mem_d(self);
}

MEM_VEC_FUNCTIONS(ast_expression_common, ast_value*, params)

static ast_expression* ast_type_copy(lex_ctx ctx, const ast_expression *ex);
static ast_value* ast_value_copy(const ast_value *self)
{
    ast_value *cp = ast_value_new(self->expression.node.context, self->name, self->expression.vtype);
    if (self->expression.next) {
        cp->expression.next = ast_type_copy(self->expression.node.context, self->expression.next);
        if (!cp->expression.next) {
            ast_value_delete(cp);
            return NULL;
        }
    }
    return cp;
}

static ast_expression* ast_shallow_type(lex_ctx ctx, int vtype)
{
    ast_instantiate(ast_expression, ctx, ast_expression_delete_full);
    self->expression.codegen = NULL;
    self->expression.next    = NULL;
    self->expression.vtype   = vtype;
    return self;
}

static ast_expression* ast_type_copy(lex_ctx ctx, const ast_expression *ex)
{
    size_t i;
    const ast_expression_common *fromex;
    ast_expression_common *selfex;

    if (!ex)
        return NULL;
    else
    {
        ast_instantiate(ast_expression, ctx, ast_expression_delete_full);

        fromex   = &ex->expression;
        selfex = &self->expression;

        /* This may never be codegen()d */
        selfex->codegen = NULL;

        selfex->vtype = fromex->vtype;
        if (fromex->next)
        {
            selfex->next = ast_type_copy(ctx, fromex->next);
            if (!selfex->next) {
                ast_expression_delete_full(self);
                return NULL;
            }
        }
        else
            selfex->next = NULL;

        for (i = 0; i < fromex->params_count; ++i) {
            ast_value *v = ast_value_copy(fromex->params[i]);
            if (!v || !ast_expression_common_params_add(selfex, v)) {
                ast_expression_delete_full(self);
                return NULL;
            }
        }

        return self;
    }
}

ast_value* ast_value_new(lex_ctx ctx, const char *name, int t)
{
    ast_instantiate(ast_value, ctx, ast_value_delete);
    ast_expression_init((ast_expression*)self,
                        (ast_expression_codegen*)&ast_value_codegen);
    self->expression.node.keep = true; /* keep */

    self->name = name ? util_strdup(name) : NULL;
    self->expression.vtype = t;
    self->expression.next  = NULL;
    self->isconst = false;
    memset(&self->constval, 0, sizeof(self->constval));

    self->ir_v    = NULL;

    return self;
}

void ast_value_delete(ast_value* self)
{
    if (self->name)
        mem_d((void*)self->name);
    if (self->isconst) {
        switch (self->expression.vtype)
        {
        case TYPE_STRING:
            mem_d((void*)self->constval.vstring);
            break;
        case TYPE_FUNCTION:
            /* unlink us from the function node */
            self->constval.vfunc->vtype = NULL;
            break;
        /* NOTE: delete function? currently collected in
         * the parser structure
         */
        default:
            break;
        }
    }
    ast_expression_delete((ast_expression*)self);
    mem_d(self);
}

bool GMQCC_WARN ast_value_params_add(ast_value *self, ast_value *p)
{
    return ast_expression_common_params_add(&self->expression, p);
}

bool ast_value_set_name(ast_value *self, const char *name)
{
    if (self->name)
        mem_d((void*)self->name);
    self->name = util_strdup(name);
    return !!self->name;
}

ast_binary* ast_binary_new(lex_ctx ctx, int op,
                           ast_expression* left, ast_expression* right)
{
    ast_instantiate(ast_binary, ctx, ast_binary_delete);
    ast_expression_init((ast_expression*)self, (ast_expression_codegen*)&ast_binary_codegen);

    self->op = op;
    self->left = left;
    self->right = right;

    if (op >= INSTR_EQ_F && op <= INSTR_GT)
        self->expression.vtype = TYPE_FLOAT;
    else if (op == INSTR_AND || op == INSTR_OR ||
             op == INSTR_BITAND || op == INSTR_BITOR)
        self->expression.vtype = TYPE_FLOAT;
    else if (op == INSTR_MUL_VF || op == INSTR_MUL_FV)
        self->expression.vtype = TYPE_VECTOR;
    else if (op == INSTR_MUL_V)
        self->expression.vtype = TYPE_FLOAT;
    else
        self->expression.vtype = left->expression.vtype;

    return self;
}

void ast_binary_delete(ast_binary *self)
{
    ast_unref(self->left);
    ast_unref(self->right);
    ast_expression_delete((ast_expression*)self);
    mem_d(self);
}

ast_unary* ast_unary_new(lex_ctx ctx, int op,
                         ast_expression *expr)
{
    ast_instantiate(ast_unary, ctx, ast_unary_delete);
    ast_expression_init((ast_expression*)self, (ast_expression_codegen*)&ast_unary_codegen);

    self->op = op;
    self->operand = expr;

    return self;
}

void ast_unary_delete(ast_unary *self)
{
    ast_unref(self->operand);
    ast_expression_delete((ast_expression*)self);
    mem_d(self);
}

ast_return* ast_return_new(lex_ctx ctx, ast_expression *expr)
{
    ast_instantiate(ast_return, ctx, ast_return_delete);
    ast_expression_init((ast_expression*)self, (ast_expression_codegen*)&ast_return_codegen);

    self->operand = expr;

    return self;
}

void ast_return_delete(ast_return *self)
{
    ast_unref(self->operand);
    ast_expression_delete((ast_expression*)self);
    mem_d(self);
}

ast_entfield* ast_entfield_new(lex_ctx ctx, ast_expression *entity, ast_expression *field)
{
    const ast_expression *outtype;

    ast_instantiate(ast_entfield, ctx, ast_entfield_delete);

    if (field->expression.vtype != TYPE_FIELD) {
        mem_d(self);
        return NULL;
    }

    outtype = field->expression.next;
    if (!outtype) {
        mem_d(self);
        /* Error: field has no type... */
        return NULL;
    }

    ast_expression_init((ast_expression*)self, (ast_expression_codegen*)&ast_entfield_codegen);

    self->expression.vtype = outtype->expression.vtype;
    self->expression.next  = ast_type_copy(ctx, outtype->expression.next);

    self->entity = entity;
    self->field  = field;

    return self;
}

void ast_entfield_delete(ast_entfield *self)
{
    ast_unref(self->entity);
    ast_unref(self->field);
    ast_expression_delete((ast_expression*)self);
    mem_d(self);
}

ast_member* ast_member_new(lex_ctx ctx, ast_expression *owner, unsigned int field)
{
    ast_instantiate(ast_member, ctx, ast_member_delete);
    if (field >= 3) {
        mem_d(self);
        return NULL;
    }

    if (owner->expression.vtype != TYPE_VECTOR &&
        owner->expression.vtype != TYPE_FIELD) {
        printf("ast_member on an invalid owner of type %i\n", (int)owner->expression.vtype);
        mem_d(self);
        return NULL;
    }

    ast_expression_init((ast_expression*)self, (ast_expression_codegen*)&ast_member_codegen);

    if (owner->expression.vtype == TYPE_VECTOR) {
        self->expression.vtype = TYPE_FLOAT;
        self->expression.next  = NULL;
    } else {
        self->expression.vtype = TYPE_FIELD;
        self->expression.next = ast_shallow_type(ctx, TYPE_FLOAT);
    }

    self->owner = owner;
    self->field = field;

    return self;
}

void ast_member_delete(ast_member *self)
{
    ast_unref(self->owner);
    ast_expression_delete((ast_expression*)self);
    mem_d(self);
}

ast_ifthen* ast_ifthen_new(lex_ctx ctx, ast_expression *cond, ast_expression *ontrue, ast_expression *onfalse)
{
    ast_instantiate(ast_ifthen, ctx, ast_ifthen_delete);
    if (!ontrue && !onfalse) {
        /* because it is invalid */
        mem_d(self);
        return NULL;
    }
    ast_expression_init((ast_expression*)self, (ast_expression_codegen*)&ast_ifthen_codegen);

    self->cond     = cond;
    self->on_true  = ontrue;
    self->on_false = onfalse;

    return self;
}

void ast_ifthen_delete(ast_ifthen *self)
{
    ast_unref(self->cond);
    if (self->on_true)
        ast_unref(self->on_true);
    if (self->on_false)
        ast_unref(self->on_false);
    ast_expression_delete((ast_expression*)self);
    mem_d(self);
}

ast_ternary* ast_ternary_new(lex_ctx ctx, ast_expression *cond, ast_expression *ontrue, ast_expression *onfalse)
{
    ast_instantiate(ast_ternary, ctx, ast_ternary_delete);
    /* This time NEITHER must be NULL */
    if (!ontrue || !onfalse) {
        mem_d(self);
        return NULL;
    }
    ast_expression_init((ast_expression*)self, (ast_expression_codegen*)&ast_ternary_codegen);

    self->cond     = cond;
    self->on_true  = ontrue;
    self->on_false = onfalse;
    self->phi_out  = NULL;

    return self;
}

void ast_ternary_delete(ast_ternary *self)
{
    ast_unref(self->cond);
    ast_unref(self->on_true);
    ast_unref(self->on_false);
    ast_expression_delete((ast_expression*)self);
    mem_d(self);
}

ast_loop* ast_loop_new(lex_ctx ctx,
                       ast_expression *initexpr,
                       ast_expression *precond,
                       ast_expression *postcond,
                       ast_expression *increment,
                       ast_expression *body)
{
    ast_instantiate(ast_loop, ctx, ast_loop_delete);
    ast_expression_init((ast_expression*)self, (ast_expression_codegen*)&ast_loop_codegen);

    self->initexpr  = initexpr;
    self->precond   = precond;
    self->postcond  = postcond;
    self->increment = increment;
    self->body      = body;

    return self;
}

void ast_loop_delete(ast_loop *self)
{
    if (self->initexpr)
        ast_unref(self->initexpr);
    if (self->precond)
        ast_unref(self->precond);
    if (self->postcond)
        ast_unref(self->postcond);
    if (self->increment)
        ast_unref(self->increment);
    if (self->body)
        ast_unref(self->body);
    ast_expression_delete((ast_expression*)self);
    mem_d(self);
}

ast_call* ast_call_new(lex_ctx ctx,
                       ast_expression *funcexpr)
{
    ast_instantiate(ast_call, ctx, ast_call_delete);
    ast_expression_init((ast_expression*)self, (ast_expression_codegen*)&ast_call_codegen);

    MEM_VECTOR_INIT(self, params);

    self->func = funcexpr;

    return self;
}
MEM_VEC_FUNCTIONS(ast_call, ast_expression*, params)

void ast_call_delete(ast_call *self)
{
    size_t i;
    for (i = 0; i < self->params_count; ++i)
        ast_unref(self->params[i]);
    MEM_VECTOR_CLEAR(self, params);

    if (self->func)
        ast_unref(self->func);

    ast_expression_delete((ast_expression*)self);
    mem_d(self);
}

ast_store* ast_store_new(lex_ctx ctx, int op,
                         ast_expression *dest, ast_expression *source)
{
    ast_instantiate(ast_store, ctx, ast_store_delete);
    ast_expression_init((ast_expression*)self, (ast_expression_codegen*)&ast_store_codegen);

    self->op = op;
    self->dest = dest;
    self->source = source;

    return self;
}

void ast_store_delete(ast_store *self)
{
    ast_unref(self->dest);
    ast_unref(self->source);
    ast_expression_delete((ast_expression*)self);
    mem_d(self);
}

ast_block* ast_block_new(lex_ctx ctx)
{
    ast_instantiate(ast_block, ctx, ast_block_delete);
    ast_expression_init((ast_expression*)self,
                        (ast_expression_codegen*)&ast_block_codegen);

    MEM_VECTOR_INIT(self, locals);
    MEM_VECTOR_INIT(self, exprs);

    return self;
}
MEM_VEC_FUNCTIONS(ast_block, ast_value*, locals)
MEM_VEC_FUNCTIONS(ast_block, ast_expression*, exprs)

void ast_block_delete(ast_block *self)
{
    size_t i;
    for (i = 0; i < self->exprs_count; ++i)
        ast_unref(self->exprs[i]);
    MEM_VECTOR_CLEAR(self, exprs);
    for (i = 0; i < self->locals_count; ++i)
        ast_delete(self->locals[i]);
    MEM_VECTOR_CLEAR(self, locals);
    ast_expression_delete((ast_expression*)self);
    mem_d(self);
}

bool ast_block_set_type(ast_block *self, ast_expression *from)
{
    if (self->expression.next)
        ast_delete(self->expression.next);
    self->expression.vtype = from->expression.vtype;
    if (from->expression.next) {
        self->expression.next = ast_type_copy(self->expression.node.context, from->expression.next);
        if (!self->expression.next)
            return false;
    }
    return true;
}

ast_function* ast_function_new(lex_ctx ctx, const char *name, ast_value *vtype)
{
    ast_instantiate(ast_function, ctx, ast_function_delete);

    if (!vtype ||
        vtype->isconst ||
        vtype->expression.vtype != TYPE_FUNCTION)
    {
        mem_d(self);
        return NULL;
    }

    self->vtype = vtype;
    self->name = name ? util_strdup(name) : NULL;
    MEM_VECTOR_INIT(self, blocks);

    self->labelcount = 0;
    self->builtin = 0;

    self->ir_func = NULL;
    self->curblock = NULL;

    self->breakblock    = NULL;
    self->continueblock = NULL;

    vtype->isconst = true;
    vtype->constval.vfunc = self;

    return self;
}

MEM_VEC_FUNCTIONS(ast_function, ast_block*, blocks)

void ast_function_delete(ast_function *self)
{
    size_t i;
    if (self->name)
        mem_d((void*)self->name);
    if (self->vtype) {
        /* ast_value_delete(self->vtype); */
        self->vtype->isconst = false;
        self->vtype->constval.vfunc = NULL;
        /* We use unref - if it was stored in a global table it is supposed
         * to be deleted from *there*
         */
        ast_unref(self->vtype);
    }
    for (i = 0; i < self->blocks_count; ++i)
        ast_delete(self->blocks[i]);
    MEM_VECTOR_CLEAR(self, blocks);
    mem_d(self);
}

static void ast_util_hexitoa(char *buf, size_t size, unsigned int num)
{
    unsigned int base = 10;
#define checknul() do { if (size == 1) { *buf = 0; return; } } while (0)
#define addch(x) do { *buf++ = (x); --size; checknul(); } while (0)
    if (size < 1)
        return;
    checknul();
    if (!num)
        addch('0');
    else {
        while (num)
        {
            int digit = num % base;
            num /= base;
            addch('0' + digit);
        }
    }

    *buf = 0;
#undef addch
#undef checknul
}

const char* ast_function_label(ast_function *self, const char *prefix)
{
    size_t id = (self->labelcount++);
    size_t len = strlen(prefix);
    strncpy(self->labelbuf, prefix, sizeof(self->labelbuf));
    ast_util_hexitoa(self->labelbuf + len, sizeof(self->labelbuf)-len, id);
    return self->labelbuf;
}

/*********************************************************************/
/* AST codegen part
 * by convention you must never pass NULL to the 'ir_value **out'
 * parameter. If you really don't care about the output, pass a dummy.
 * But I can't imagine a pituation where the output is truly unnecessary.
 */

bool ast_value_codegen(ast_value *self, ast_function *func, bool lvalue, ir_value **out)
{
    /* NOTE: This is the codegen for a variable used in an expression.
     * It is not the codegen to generate the value. For this purpose,
     * ast_local_codegen and ast_global_codegen are to be used before this
     * is executed. ast_function_codegen should take care of its locals,
     * and the ast-user should take care of ast_global_codegen to be used
     * on all the globals.
     */
    if (!self->ir_v) {
        printf("ast_value used before generated (%s)\n", self->name);
        return false;
    }
    *out = self->ir_v;
    return true;
}

bool ast_global_codegen(ast_value *self, ir_builder *ir)
{
    ir_value *v = NULL;
    if (self->isconst && self->expression.vtype == TYPE_FUNCTION)
    {
        ir_function *func = ir_builder_create_function(ir, self->name, self->expression.next->expression.vtype);
        if (!func)
            return false;

        self->constval.vfunc->ir_func = func;
        self->ir_v = func->value;
        /* The function is filled later on ast_function_codegen... */
        return true;
    }

    if (self->expression.vtype == TYPE_FIELD) {
        v = ir_builder_create_field(ir, self->name, self->expression.next->expression.vtype);
        if (!v)
            return false;
        if (self->isconst) {
            printf("TODO: constant field pointers with value\n");
            goto error;
        }
        self->ir_v = v;
        return true;
    }

    v = ir_builder_create_global(ir, self->name, self->expression.vtype);
    if (!v) {
        printf("ir_builder_create_global failed\n");
        return false;
    }

    if (self->isconst) {
        switch (self->expression.vtype)
        {
            case TYPE_FLOAT:
                if (!ir_value_set_float(v, self->constval.vfloat))
                    goto error;
                break;
            case TYPE_VECTOR:
                if (!ir_value_set_vector(v, self->constval.vvec))
                    goto error;
                break;
            case TYPE_STRING:
                if (!ir_value_set_string(v, self->constval.vstring))
                    goto error;
                break;
            case TYPE_FUNCTION:
                printf("global of type function not properly generated\n");
                goto error;
                /* Cannot generate an IR value for a function,
                 * need a pointer pointing to a function rather.
                 */
            default:
                printf("TODO: global constant type %i\n", self->expression.vtype);
                break;
        }
    }

    /* link us to the ir_value */
    self->ir_v = v;
    return true;

error: /* clean up */
    ir_value_delete(v);
    return false;
}

bool ast_local_codegen(ast_value *self, ir_function *func, bool param)
{
    ir_value *v = NULL;
    if (self->isconst && self->expression.vtype == TYPE_FUNCTION)
    {
        /* Do we allow local functions? I think not...
         * this is NOT a function pointer atm.
         */
        return false;
    }

    v = ir_function_create_local(func, self->name, self->expression.vtype, param);
    if (!v)
        return false;

    /* A constant local... hmmm...
     * I suppose the IR will have to deal with this
     */
    if (self->isconst) {
        switch (self->expression.vtype)
        {
            case TYPE_FLOAT:
                if (!ir_value_set_float(v, self->constval.vfloat))
                    goto error;
                break;
            case TYPE_VECTOR:
                if (!ir_value_set_vector(v, self->constval.vvec))
                    goto error;
                break;
            case TYPE_STRING:
                if (!ir_value_set_string(v, self->constval.vstring))
                    goto error;
                break;
            default:
                printf("TODO: global constant type %i\n", self->expression.vtype);
                break;
        }
    }

    /* link us to the ir_value */
    self->ir_v = v;
    return true;

error: /* clean up */
    ir_value_delete(v);
    return false;
}

bool ast_function_codegen(ast_function *self, ir_builder *ir)
{
    ir_function *irf;
    ir_value    *dummy;
    ast_expression_common *ec;
    size_t    i;

    irf = self->ir_func;
    if (!irf) {
        printf("ast_function's related ast_value was not generated yet\n");
        return false;
    }

    /* fill the parameter list */
    ec = &self->vtype->expression;
    for (i = 0; i < ec->params_count; ++i)
    {
        if (!ir_function_params_add(irf, ec->params[i]->expression.vtype))
            return false;
        if (!self->builtin) {
            if (!ast_local_codegen(ec->params[i], self->ir_func, true))
                return false;
        }
    }

    if (self->builtin) {
        irf->builtin = self->builtin;
        return true;
    }

    self->curblock = ir_function_create_block(irf, "entry");
    if (!self->curblock)
        return false;

    for (i = 0; i < self->blocks_count; ++i) {
        ast_expression_codegen *gen = self->blocks[i]->expression.codegen;
        if (!(*gen)((ast_expression*)self->blocks[i], self, false, &dummy))
            return false;
    }

    /* TODO: check return types */
    if (!self->curblock->is_return)
    {
        if (!self->vtype->expression.next ||
            self->vtype->expression.next->expression.vtype == TYPE_VOID)
        {
            return ir_block_create_return(self->curblock, NULL);
        }
        else
        {
            /* error("missing return"); */
            return false;
        }
    }
    return true;
}

/* Note, you will not see ast_block_codegen generate ir_blocks.
 * To the AST and the IR, blocks are 2 different things.
 * In the AST it represents a block of code, usually enclosed in
 * curly braces {...}.
 * While in the IR it represents a block in terms of control-flow.
 */
bool ast_block_codegen(ast_block *self, ast_function *func, bool lvalue, ir_value **out)
{
    size_t i;

    /* We don't use this
     * Note: an ast-representation using the comma-operator
     * of the form: (a, b, c) = x should not assign to c...
     */
    (void)lvalue;

    /* output is NULL at first, we'll have each expression
     * assign to out output, thus, a comma-operator represention
     * using an ast_block will return the last generated value,
     * so: (b, c) + a  executed both b and c, and returns c,
     * which is then added to a.
     */
    *out = NULL;

    /* generate locals */
    for (i = 0; i < self->locals_count; ++i)
    {
        if (!ast_local_codegen(self->locals[i], func->ir_func, false))
            return false;
    }

    for (i = 0; i < self->exprs_count; ++i)
    {
        ast_expression_codegen *gen = self->exprs[i]->expression.codegen;
        if (!(*gen)(self->exprs[i], func, false, out))
            return false;
    }

    return true;
}

bool ast_store_codegen(ast_store *self, ast_function *func, bool lvalue, ir_value **out)
{
    ast_expression_codegen *cgen;
    ir_value *left, *right;

    cgen = self->dest->expression.codegen;
    /* lvalue! */
    if (!(*cgen)((ast_expression*)(self->dest), func, true, &left))
        return false;

    cgen = self->source->expression.codegen;
    /* rvalue! */
    if (!(*cgen)((ast_expression*)(self->source), func, false, &right))
        return false;

    if (!ir_block_create_store_op(func->curblock, self->op, left, right))
        return false;

    /* Theoretically, an assinment returns its left side as an
     * lvalue, if we don't need an lvalue though, we return
     * the right side as an rvalue, otherwise we have to
     * somehow know whether or not we need to dereference the pointer
     * on the left side - that is: OP_LOAD if it was an address.
     * Also: in original QC we cannot OP_LOADP *anyway*.
     */
    *out = (lvalue ? left : right);

    return true;
}

bool ast_binary_codegen(ast_binary *self, ast_function *func, bool lvalue, ir_value **out)
{
    ast_expression_codegen *cgen;
    ir_value *left, *right;

    /* In the context of a binary operation, we can disregard
     * the lvalue flag.
     */
     (void)lvalue;

    cgen = self->left->expression.codegen;
    /* lvalue! */
    if (!(*cgen)((ast_expression*)(self->left), func, false, &left))
        return false;

    cgen = self->right->expression.codegen;
    /* rvalue! */
    if (!(*cgen)((ast_expression*)(self->right), func, false, &right))
        return false;

    *out = ir_block_create_binop(func->curblock, ast_function_label(func, "bin"),
                                 self->op, left, right);
    if (!*out)
        return false;

    return true;
}

bool ast_unary_codegen(ast_unary *self, ast_function *func, bool lvalue, ir_value **out)
{
    ast_expression_codegen *cgen;
    ir_value *operand;

    /* In the context of a unary operation, we can disregard
     * the lvalue flag.
     */
    (void)lvalue;

    cgen = self->operand->expression.codegen;
    /* lvalue! */
    if (!(*cgen)((ast_expression*)(self->operand), func, false, &operand))
        return false;

    *out = ir_block_create_unary(func->curblock, ast_function_label(func, "unary"),
                                 self->op, operand);
    if (!*out)
        return false;

    return true;
}

bool ast_return_codegen(ast_return *self, ast_function *func, bool lvalue, ir_value **out)
{
    ast_expression_codegen *cgen;
    ir_value *operand;

    /* In the context of a return operation, we can disregard
     * the lvalue flag.
     */
    (void)lvalue;

    cgen = self->operand->expression.codegen;
    /* lvalue! */
    if (!(*cgen)((ast_expression*)(self->operand), func, false, &operand))
        return false;

    if (!ir_block_create_return(func->curblock, operand))
        return false;

    return true;
}

bool ast_entfield_codegen(ast_entfield *self, ast_function *func, bool lvalue, ir_value **out)
{
    ast_expression_codegen *cgen;
    ir_value *ent, *field;

    /* This function needs to take the 'lvalue' flag into account!
     * As lvalue we provide a field-pointer, as rvalue we provide the
     * value in a temp.
     */

    cgen = self->entity->expression.codegen;
    if (!(*cgen)((ast_expression*)(self->entity), func, false, &ent))
        return false;

    cgen = self->field->expression.codegen;
    if (!(*cgen)((ast_expression*)(self->field), func, false, &field))
        return false;

    if (lvalue) {
        /* address! */
        *out = ir_block_create_fieldaddress(func->curblock, ast_function_label(func, "efa"),
                                            ent, field);
    } else {
        *out = ir_block_create_load_from_ent(func->curblock, ast_function_label(func, "efv"),
                                             ent, field, self->expression.vtype);
    }
    if (!*out)
        return false;

    /* Hm that should be it... */
    return true;
}

bool ast_member_codegen(ast_member *self, ast_function *func, bool lvalue, ir_value **out)
{
    ast_expression_codegen *cgen;
    ir_value *vec;

    cgen = self->owner->expression.codegen;
    if (!(*cgen)((ast_expression*)(self->owner), func, true, &vec))
        return false;

    if (vec->vtype != TYPE_VECTOR &&
        !(vec->vtype == TYPE_FIELD && self->owner->expression.next->expression.vtype == TYPE_VECTOR))
    {
        return false;
    }

    *out = ir_value_vector_member(vec, self->field);

    return (*out != NULL);
}

bool ast_ifthen_codegen(ast_ifthen *self, ast_function *func, bool lvalue, ir_value **out)
{
    ast_expression_codegen *cgen;

    ir_value *condval;
    ir_value *dummy;

    ir_block *cond = func->curblock;
    ir_block *ontrue;
    ir_block *onfalse;
    ir_block *merge;

    /* We don't output any value, thus also don't care about r/lvalue */
    (void)out;
    (void)lvalue;

    /* generate the condition */
    func->curblock = cond;
    cgen = self->cond->expression.codegen;
    if (!(*cgen)((ast_expression*)(self->cond), func, false, &condval))
        return false;

    /* on-true path */

    if (self->on_true) {
        /* create on-true block */
        ontrue = ir_function_create_block(func->ir_func, ast_function_label(func, "ontrue"));
        if (!ontrue)
            return false;

        /* enter the block */
        func->curblock = ontrue;

        /* generate */
        cgen = self->on_true->expression.codegen;
        if (!(*cgen)((ast_expression*)(self->on_true), func, false, &dummy))
            return false;
    } else
        ontrue = NULL;

    /* on-false path */
    if (self->on_false) {
        /* create on-false block */
        onfalse = ir_function_create_block(func->ir_func, ast_function_label(func, "onfalse"));
        if (!onfalse)
            return false;

        /* enter the block */
        func->curblock = onfalse;

        /* generate */
        cgen = self->on_false->expression.codegen;
        if (!(*cgen)((ast_expression*)(self->on_false), func, false, &dummy))
            return false;
    } else
        onfalse = NULL;

    /* Merge block were they all merge in to */
    merge = ir_function_create_block(func->ir_func, ast_function_label(func, "endif"));
    if (!merge)
        return false;

    /* add jumps ot the merge block */
    if (ontrue && !ir_block_create_jump(ontrue, merge))
        return false;
    if (onfalse && !ir_block_create_jump(onfalse, merge))
        return false;

    /* we create the if here, that way all blocks are ordered :)
     */
    if (!ir_block_create_if(cond, condval,
                            (ontrue  ? ontrue  : merge),
                            (onfalse ? onfalse : merge)))
    {
        return false;
    }

    /* Now enter the merge block */
    func->curblock = merge;

    return true;
}

bool ast_ternary_codegen(ast_ternary *self, ast_function *func, bool lvalue, ir_value **out)
{
    ast_expression_codegen *cgen;

    ir_value *condval;
    ir_value *trueval, *falseval;
    ir_instr *phi;

    ir_block *cond = func->curblock;
    ir_block *ontrue;
    ir_block *onfalse;
    ir_block *merge;

    /* In theory it shouldn't be possible to pass through a node twice, but
     * in case we add any kind of optimization pass for the AST itself, it
     * may still happen, thus we remember a created ir_value and simply return one
     * if it already exists.
     */
    if (self->phi_out) {
        *out = self->phi_out;
        return true;
    }

    /* Ternary can never create an lvalue... */
    if (lvalue)
        return false;

    /* In the following, contraty to ast_ifthen, we assume both paths exist. */

    /* generate the condition */
    func->curblock = cond;
    cgen = self->cond->expression.codegen;
    if (!(*cgen)((ast_expression*)(self->cond), func, false, &condval))
        return false;

    /* create on-true block */
    ontrue = ir_function_create_block(func->ir_func, ast_function_label(func, "tern_T"));
    if (!ontrue)
        return false;
    else
    {
        /* enter the block */
        func->curblock = ontrue;

        /* generate */
        cgen = self->on_true->expression.codegen;
        if (!(*cgen)((ast_expression*)(self->on_true), func, false, &trueval))
            return false;
    }

    /* create on-false block */
    onfalse = ir_function_create_block(func->ir_func, ast_function_label(func, "tern_F"));
    if (!onfalse)
        return false;
    else
    {
        /* enter the block */
        func->curblock = onfalse;

        /* generate */
        cgen = self->on_false->expression.codegen;
        if (!(*cgen)((ast_expression*)(self->on_false), func, false, &falseval))
            return false;
    }

    /* create merge block */
    merge = ir_function_create_block(func->ir_func, ast_function_label(func, "tern_out"));
    if (!merge)
        return false;
    /* jump to merge block */
    if (!ir_block_create_jump(ontrue, merge))
        return false;
    if (!ir_block_create_jump(onfalse, merge))
        return false;

    /* create if instruction */
    if (!ir_block_create_if(cond, condval, ontrue, onfalse))
        return false;

    /* Now enter the merge block */
    func->curblock = merge;

    /* Here, now, we need a PHI node
     * but first some sanity checking...
     */
    if (trueval->vtype != falseval->vtype) {
        /* error("ternary with different types on the two sides"); */
        return false;
    }

    /* create PHI */
    phi = ir_block_create_phi(merge, ast_function_label(func, "phi"), trueval->vtype);
    if (!phi ||
        !ir_phi_add(phi, ontrue,  trueval) ||
        !ir_phi_add(phi, onfalse, falseval))
    {
        return false;
    }

    self->phi_out = ir_phi_value(phi);
    *out = self->phi_out;

    return true;
}

bool ast_loop_codegen(ast_loop *self, ast_function *func, bool lvalue, ir_value **out)
{
    ast_expression_codegen *cgen;

    ir_value *dummy      = NULL;
    ir_value *precond    = NULL;
    ir_value *postcond   = NULL;

    /* Since we insert some jumps "late" so we have blocks
     * ordered "nicely", we need to keep track of the actual end-blocks
     * of expressions to add the jumps to.
     */
    ir_block *bbody      = NULL, *end_bbody      = NULL;
    ir_block *bprecond   = NULL, *end_bprecond   = NULL;
    ir_block *bpostcond  = NULL, *end_bpostcond  = NULL;
    ir_block *bincrement = NULL, *end_bincrement = NULL;
    ir_block *bout       = NULL, *bin            = NULL;

    /* let's at least move the outgoing block to the end */
    size_t    bout_id;

    /* 'break' and 'continue' need to be able to find the right blocks */
    ir_block *bcontinue     = NULL;
    ir_block *bbreak        = NULL;

    ir_block *old_bcontinue = NULL;
    ir_block *old_bbreak    = NULL;

    ir_block *tmpblock      = NULL;

    (void)lvalue;
    (void)out;

    /* NOTE:
     * Should we ever need some kind of block ordering, better make this function
     * move blocks around than write a block ordering algorithm later... after all
     * the ast and ir should work together, not against each other.
     */

    /* initexpr doesn't get its own block, it's pointless, it could create more blocks
     * anyway if for example it contains a ternary.
     */
    if (self->initexpr)
    {
        cgen = self->initexpr->expression.codegen;
        if (!(*cgen)((ast_expression*)(self->initexpr), func, false, &dummy))
            return false;
    }

    /* Store the block from which we enter this chaos */
    bin = func->curblock;

    /* The pre-loop condition needs its own block since we
     * need to be able to jump to the start of that expression.
     */
    if (self->precond)
    {
        bprecond = ir_function_create_block(func->ir_func, ast_function_label(func, "pre_loop_cond"));
        if (!bprecond)
            return false;

        /* the pre-loop-condition the least important place to 'continue' at */
        bcontinue = bprecond;

        /* enter */
        func->curblock = bprecond;

        /* generate */
        cgen = self->precond->expression.codegen;
        if (!(*cgen)((ast_expression*)(self->precond), func, false, &precond))
            return false;

        end_bprecond = func->curblock;
    } else {
        bprecond = end_bprecond = NULL;
    }

    /* Now the next blocks won't be ordered nicely, but we need to
     * generate them this early for 'break' and 'continue'.
     */
    if (self->increment) {
        bincrement = ir_function_create_block(func->ir_func, ast_function_label(func, "loop_increment"));
        if (!bincrement)
            return false;
        bcontinue = bincrement; /* increment comes before the pre-loop-condition */
    } else {
        bincrement = end_bincrement = NULL;
    }

    if (self->postcond) {
        bpostcond = ir_function_create_block(func->ir_func, ast_function_label(func, "post_loop_cond"));
        if (!bpostcond)
            return false;
        bcontinue = bpostcond; /* postcond comes before the increment */
    } else {
        bpostcond = end_bpostcond = NULL;
    }

    bout_id = func->ir_func->blocks_count;
    bout = ir_function_create_block(func->ir_func, ast_function_label(func, "after_loop"));
    if (!bout)
        return false;
    bbreak = bout;

    /* The loop body... */
    if (self->body)
    {
        bbody = ir_function_create_block(func->ir_func, ast_function_label(func, "loop_body"));
        if (!bbody)
            return false;

        /* enter */
        func->curblock = bbody;

        old_bbreak          = func->breakblock;
        old_bcontinue       = func->continueblock;
        func->breakblock    = bbreak;
        func->continueblock = bcontinue;

        /* generate */
        cgen = self->body->expression.codegen;
        if (!(*cgen)((ast_expression*)(self->body), func, false, &dummy))
            return false;

        end_bbody = func->curblock;
        func->breakblock    = old_bbreak;
        func->continueblock = old_bcontinue;
    }

    /* post-loop-condition */
    if (self->postcond)
    {
        /* enter */
        func->curblock = bpostcond;

        /* generate */
        cgen = self->postcond->expression.codegen;
        if (!(*cgen)((ast_expression*)(self->postcond), func, false, &postcond))
            return false;

        end_bpostcond = func->curblock;
    }

    /* The incrementor */
    if (self->increment)
    {
        /* enter */
        func->curblock = bincrement;

        /* generate */
        cgen = self->increment->expression.codegen;
        if (!(*cgen)((ast_expression*)(self->increment), func, false, &dummy))
            return false;

        end_bincrement = func->curblock;
    }

    /* In any case now, we continue from the outgoing block */
    func->curblock = bout;

    /* Now all blocks are in place */
    /* From 'bin' we jump to whatever comes first */
    if      (bprecond)   tmpblock = bprecond;
    else if (bbody)      tmpblock = bbody;
    else if (bpostcond)  tmpblock = bpostcond;
    else                 tmpblock = bout;
    if (!ir_block_create_jump(bin, tmpblock))
        return false;

    /* From precond */
    if (bprecond)
    {
        ir_block *ontrue, *onfalse;
        if      (bbody)      ontrue = bbody;
        else if (bincrement) ontrue = bincrement;
        else if (bpostcond)  ontrue = bpostcond;
        else                 ontrue = bprecond;
        onfalse = bout;
        if (!ir_block_create_if(end_bprecond, precond, ontrue, onfalse))
            return false;
    }

    /* from body */
    if (bbody)
    {
        if      (bincrement) tmpblock = bincrement;
        else if (bpostcond)  tmpblock = bpostcond;
        else if (bprecond)   tmpblock = bprecond;
        else                 tmpblock = bout;
        if (!ir_block_create_jump(end_bbody, tmpblock))
            return false;
    }

    /* from increment */
    if (bincrement)
    {
        if      (bpostcond)  tmpblock = bpostcond;
        else if (bprecond)   tmpblock = bprecond;
        else if (bbody)      tmpblock = bbody;
        else                 tmpblock = bout;
        if (!ir_block_create_jump(end_bincrement, tmpblock))
            return false;
    }

    /* from postcond */
    if (bpostcond)
    {
        ir_block *ontrue, *onfalse;
        if      (bprecond)   ontrue = bprecond;
        else if (bbody)      ontrue = bbody;
        else if (bincrement) ontrue = bincrement;
        else                 ontrue = bpostcond;
        onfalse = bout;
        if (!ir_block_create_if(end_bpostcond, postcond, ontrue, onfalse))
            return false;
    }

    /* Move 'bout' to the end */
    if (!ir_function_blocks_remove(func->ir_func, bout_id) ||
        !ir_function_blocks_add(func->ir_func, bout))
    {
        ir_block_delete(bout);
        return false;
    }

    return true;
}

bool ast_call_codegen(ast_call *self, ast_function *func, bool lvalue, ir_value **out)
{
    ast_expression_codegen *cgen;
    ir_value_vector         params;
    ir_instr               *callinstr;
    size_t i;

    ir_value *funval = NULL;

    /* return values are never rvalues */
    (void)lvalue;

    cgen = self->func->expression.codegen;
    if (!(*cgen)((ast_expression*)(self->func), func, false, &funval))
        return false;
    if (!funval)
        return false;

    MEM_VECTOR_INIT(&params, v);

    /* parameters */
    for (i = 0; i < self->params_count; ++i)
    {
        ir_value *param;
        ast_expression *expr = self->params[i];

        cgen = expr->expression.codegen;
        if (!(*cgen)(expr, func, false, &param))
            goto error;
        if (!param)
            goto error;
        if (!ir_value_vector_v_add(&params, param))
            goto error;
    }

    callinstr = ir_block_create_call(func->curblock, ast_function_label(func, "call"), funval);
    if (!callinstr)
        goto error;

    for (i = 0; i < params.v_count; ++i) {
        if (!ir_call_param(callinstr, params.v[i]))
            goto error;
    }

    *out = ir_call_value(callinstr);

    MEM_VECTOR_CLEAR(&params, v);
    return true;
error:
    MEM_VECTOR_CLEAR(&params, v);
    return false;
}
