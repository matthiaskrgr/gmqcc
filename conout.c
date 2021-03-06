/*
 * Copyright (C) 2012, 2013
 *     Dale Weiler
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
#include "gmqcc.h"

/*
 * isatty/STDERR_FILENO/STDOUT_FILNO
 * + some other things likewise.
 */
#ifndef _WIN32
#   include <unistd.h>
#else
#   include <io.h>
    /*
     * Windows and it's posix underscore bullshit.  We simply fix this
     * with yay, another macro :P
     */
#   define isatty _isatty
#endif

#define GMQCC_IS_STDOUT(X) ((FILE*)((void*)X) == stdout)
#define GMQCC_IS_STDERR(X) ((FILE*)((void*)X) == stderr)
#define GMQCC_IS_DEFINE(X) (GMQCC_IS_STDERR(X) || GMQCC_IS_STDOUT(X))

typedef struct {
    FILE *handle_err;
    FILE *handle_out;

    int   color_err;
    int   color_out;
} con_t;

/*
 * Doing colored output on windows is fucking stupid.  The linux way is
 * the real way. So we emulate it on windows :)
 */
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

/*
 * Windows doesn't have constants for FILENO, sadly but the docs tell
 * use the constant values.
 */
#undef  STDERR_FILENO
#undef  STDOUT_FILENO
#define STDERR_FILENO 2
#define STDOUT_FILENO 1

enum {
    RESET = 0,
    BOLD  = 1,
    BLACK = 30,
    RED,
    GREEN,
    YELLOW,
    BLUE,
    MAGENTA,
    CYAN,
    GRAY,
    WHITE = GRAY
};

enum {
    WBLACK,
    WBLUE,
    WGREEN   = 2,
    WRED     = 4,
    WINTENSE = 8,
    WCYAN    = WBLUE  | WGREEN,
    WMAGENTA = WBLUE  | WRED,
    WYELLOW  = WGREEN | WRED,
    WWHITE   = WBLUE  | WGREEN | WRED
};

static const int ansi2win[] = {
    WBLACK,
    WRED,
    WGREEN,
    WYELLOW,
    WBLUE,
    WMAGENTA,
    WCYAN,
    WWHITE
};

static int win_fputs(FILE *h, const char *str) {
    /* state for translate */
    int acolor = 0;
    int wcolor = 0;
    int icolor = 0;
    int state  = 0;

    /* attributes */
    int intense  =  -1;
    int colors[] = {-1, -1 };
    int colorpos = 1;
    int length   = 0;
    CONSOLE_SCREEN_BUFFER_INFO cinfo;
    GetConsoleScreenBufferInfo (
        (GMQCC_IS_STDOUT(h)) ?
            GetStdHandle(STD_OUTPUT_HANDLE) :
            GetStdHandle(STD_ERROR_HANDLE), &cinfo
    );
    icolor = cinfo.wAttributes;

    while (*str) {
        if (*str == '\x1B')
            state = '\x1B';
        else if (state == '\x1B' && *str == '[')
            state = '[';
        else if (state == '[') {
            if (*str != 'm') {
                colors[colorpos] = *str;
                colorpos--;
            } else {
                int find;
                int mult;
                for (find = colorpos + 1, acolor = 0, mult = 1; find < 2; find++) {
                    acolor += (colors[find] - 48) * mult;
                    mult   *= 10;
                }

                /* convert to windows color */
                if (acolor == BOLD)
                    intense = WINTENSE;
                else if (acolor == RESET) {
                    intense = WBLACK;
                    wcolor  = icolor;
                }
                else if (BLACK <= acolor && acolor <= WHITE)
                    wcolor = ansi2win[acolor - 30];
                else if (acolor == 90) {
                    /* special gray really white man */
                    wcolor  = WWHITE;
                    intense = WBLACK;
                }

                SetConsoleTextAttribute (
                    (GMQCC_IS_STDOUT(h)) ?
                    GetStdHandle(STD_OUTPUT_HANDLE) :
                    GetStdHandle(STD_ERROR_HANDLE),

                    wcolor | intense | (icolor & 0xF0)
                );
                colorpos =  1;
                state    = -1;
            }
        } else {
            fs_file_write(str, 1, 1, stdout);
            length ++;
        }
        str++;
    }
    /* restore */
    SetConsoleTextAttribute(
        (GMQCC_IS_STDOUT(h)) ?
        GetStdHandle(STD_OUTPUT_HANDLE) :
        GetStdHandle(STD_ERROR_HANDLE),
        icolor
    );
    return length;
}
#endif

/*
 * We use standard files as default. These can be changed at any time
 * with con_change(F, F)
 */
static con_t console;

/*
 * Enables color on output if supported.
 * NOTE: The support for checking colors is NULL.  On windows this will
 * always work, on *nix it depends if the term has colors.
 *
 * NOTE: This prevents colored output to piped stdout/err via isatty
 * checks.
 */
static void con_enablecolor(void) {
    if (console.handle_err == stderr || console.handle_err == stdout)
        console.color_err = !!(isatty(STDERR_FILENO));
    if (console.handle_out == stderr || console.handle_out == stdout)
        console.color_out = !!(isatty(STDOUT_FILENO));
}

/*
 * Does a write to the handle with the format string and list of
 * arguments.  This colorizes for windows as well via translate
 * step.
 */
static int con_write(FILE *handle, const char *fmt, va_list va) {
    int      ln;
    #ifndef _WIN32
    ln = vfprintf(handle, fmt, va);
    #else
    {
        char data[4096];
        memset(data, 0, sizeof(data));
#ifdef _MSC_VER
        vsnprintf_s(data, sizeof(data), sizeof(data), fmt, va);
#else
        vsnprintf(data, sizeof(data), fmt, va);
#endif
        ln = (GMQCC_IS_DEFINE(handle)) ? win_fputs(handle, data) : fs_file_puts(handle, data);
    }
    #endif
    return ln;
}

/**********************************************************************
 * EXPOSED INTERFACE BEGINS
 *********************************************************************/

void con_close() {
    if (!GMQCC_IS_DEFINE(console.handle_err))
        fs_file_close(console.handle_err);
    if (!GMQCC_IS_DEFINE(console.handle_out))
        fs_file_close(console.handle_out);
}

void con_color(int state) {
    if (state)
        con_enablecolor();
    else {
        console.color_err = 0;
        console.color_out = 0;
    }
}

void con_init() {
    console.handle_err = stderr;
    console.handle_out = stdout;
    con_enablecolor();
}

void con_reset() {
    con_close();
    con_init ();
}

/*
 * This is clever, say you want to change the console to use two
 * files for out/err.  You pass in two strings, it will properly
 * close the existing handles (if they're not std* handles) and
 * open them.  Now say you want TO use stdout and stderr, this
 * allows you to do that so long as you cast them to (char*).
 * Say you need stdout for out, but want a file for error, you can
 * do this too, just cast the stdout for (char*) and stick to a
 * string for the error file.
 */
int con_change(const char *out, const char *err) {
    con_close();

    if (!out) out = (const char *)((!console.handle_out) ? stdout : console.handle_out);
    if (!err) err = (const char *)((!console.handle_err) ? stderr : console.handle_err);

    if (GMQCC_IS_DEFINE(out)) {
        console.handle_out = GMQCC_IS_STDOUT(out) ? stdout : stderr;
        con_enablecolor();
    } else if (!(console.handle_out = fs_file_open(out, "w"))) return 0;

    if (GMQCC_IS_DEFINE(err)) {
        console.handle_err = GMQCC_IS_STDOUT(err) ? stdout : stderr;
        con_enablecolor();
    } else if (!(console.handle_err = fs_file_open(err, "w"))) return 0;

    return 1;
}

/*
 * Defaultizer because stdio.h shouldn't be used anywhere except here
 * and inside file.c To prevent mis-match of wrapper-interfaces.
 */
FILE *con_default_out() {
    return (console.handle_out = stdout);
}
FILE *con_default_err() {
    return (console.handle_err = stderr);
}

int con_verr(const char *fmt, va_list va) {
    return con_write(console.handle_err, fmt, va);
}
int con_vout(const char *fmt, va_list va) {
    return con_write(console.handle_out, fmt, va);
}

/*
 * Standard stdout/stderr printf functions used generally where they need
 * to be used.
 */
int con_err(const char *fmt, ...) {
    va_list  va;
    int      ln = 0;
    va_start(va, fmt);
    con_verr(fmt, va);
    va_end  (va);
    return   ln;
}
int con_out(const char *fmt, ...) {
    va_list  va;
    int      ln = 0;
    va_start(va, fmt);
    con_vout(fmt, va);
    va_end  (va);
    return   ln;
}

#ifndef QCVM_EXECUTOR
/*
 * Utility console message writes for lexer contexts.  These will allow
 * for reporting of file:line based on lexer context, These are used
 * heavily in the parser/ir/ast.
 */
static void con_vprintmsg_c(int level, const char *name, size_t line, size_t column, const char *msgtype, const char *msg, va_list ap, const char *condname) {
    /* color selection table */
    static int sel[] = {
        CON_WHITE,
        CON_CYAN,
        CON_RED
    };

    int  err                         = !!(level == LVL_ERROR);
    int  color                       = (err) ? console.color_err : console.color_out;
    int (*print) (const char *, ...)  = (err) ? &con_err          : &con_out;
    int (*vprint)(const char *, va_list) = (err) ? &con_verr : &con_vout;

    if (color)
        print("\033[0;%dm%s:%d:%d: \033[0;%dm%s: \033[0m", CON_CYAN, name, (int)line, (int)column, sel[level], msgtype);
    else
        print("%s:%d:%d: %s: ", name, (int)line, (int)column, msgtype);

    vprint(msg, ap);
    if (condname)
        print(" [%s]\n", condname);
    else
        print("\n");
}

void con_vprintmsg(int level, const char *name, size_t line, size_t column, const char *msgtype, const char *msg, va_list ap) {
    con_vprintmsg_c(level, name, line, column, msgtype, msg, ap, NULL);
}

void con_printmsg(int level, const char *name, size_t line, size_t column, const char *msgtype, const char *msg, ...) {
    va_list   va;
    va_start(va, msg);
    con_vprintmsg(level, name, line, column, msgtype, msg, va);
    va_end  (va);
}

void con_cvprintmsg(lex_ctx_t ctx, int lvl, const char *msgtype, const char *msg, va_list ap) {
    con_vprintmsg(lvl, ctx.file, ctx.line, ctx.column, msgtype, msg, ap);
}

void con_cprintmsg(lex_ctx_t ctx, int lvl, const char *msgtype, const char *msg, ...) {
    va_list   va;
    va_start(va, msg);
    con_cvprintmsg(ctx, lvl, msgtype, msg, va);
    va_end  (va);
}

/* General error interface */
size_t compile_errors   = 0;
size_t compile_warnings = 0;
size_t compile_Werrors  = 0;
static lex_ctx_t first_werror;

void compile_show_werrors()
{
    con_cprintmsg(first_werror, LVL_ERROR, "first warning", "was here");
}

void vcompile_error(lex_ctx_t ctx, const char *msg, va_list ap)
{
    ++compile_errors;
    con_cvprintmsg(ctx, LVL_ERROR, "error", msg, ap);
}

void compile_error(lex_ctx_t ctx, const char *msg, ...)
{
    va_list ap;
    va_start(ap, msg);
    vcompile_error(ctx, msg, ap);
    va_end(ap);
}

bool GMQCC_WARN vcompile_warning(lex_ctx_t ctx, int warntype, const char *fmt, va_list ap)
{
    const char *msgtype = "warning";
    int         lvl     = LVL_WARNING;
    char        warn_name[1024];

    if (!OPTS_WARN(warntype))
        return false;

    warn_name[0] = '-';
    warn_name[1] = 'W';
    (void)util_strtononcmd(opts_warn_list[warntype].name, warn_name+2, sizeof(warn_name)-2);

    ++compile_warnings;
    if (OPTS_WERROR(warntype)) {
        if (!compile_Werrors)
            first_werror = ctx;
        ++compile_Werrors;
        msgtype = "Werror";
        if (OPTS_FLAG(BAIL_ON_WERROR)) {
            msgtype = "error";
            ++compile_errors;
        }
        lvl = LVL_ERROR;
    }

    con_vprintmsg_c(lvl, ctx.file, ctx.line, ctx.column, msgtype, fmt, ap, warn_name);

    return OPTS_WERROR(warntype) && OPTS_FLAG(BAIL_ON_WERROR);
}

bool GMQCC_WARN compile_warning(lex_ctx_t ctx, int warntype, const char *fmt, ...)
{
    bool r;
    va_list ap;
    va_start(ap, fmt);
    r = vcompile_warning(ctx, warntype, fmt, ap);
    va_end(ap);
    return r;
}
#endif
