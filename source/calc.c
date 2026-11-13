#include "calc.h"

#include <ctype.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif
#ifndef M_E
#define M_E 2.71828182845904523536
#endif

enum { N_NUM, N_VAR, N_ADD, N_SUB, N_MUL, N_DIV, N_POW, N_NEG, N_F1, N_F2 };

typedef double (*Fn1)(double);
typedef double (*Fn2)(double, double);

struct Node {
    int type;
    double val;
    int idx;
    Fn1 f1;
    Fn2 f2;
    Node *a, *b;
};

/* ---- math helpers -------------------------------------------------- */
static double f_sec(double x) { return 1.0 / cos(x); }
static double f_csc(double x) { return 1.0 / sin(x); }
static double f_cot(double x) { return 1.0 / tan(x); }
static double f_sign(double x) { return (x > 0) - (x < 0); }
static double f_fact(double x) { return (x < 0 || x > 170) ? NAN : tgamma(x + 1.0); }
static double f_min(double a, double b) { return a < b ? a : b; }
static double f_max(double a, double b) { return a > b ? a : b; }
static double f_mod(double a, double b)
{
    if (b == 0) return NAN;
    double r = fmod(a, b);
    if (r != 0 && ((r < 0) != (b < 0))) r += b;
    return r;
}

static const struct { const char *name; Fn1 fn; } F1S[] = {
    {"sin", sin},   {"cos", cos},     {"tan", tan},     {"asin", asin},
    {"acos", acos}, {"atan", atan},   {"sinh", sinh},   {"cosh", cosh},
    {"tanh", tanh}, {"ln", log},      {"log", log10},   {"exp", exp},
    {"sqrt", sqrt}, {"cbrt", cbrt},   {"abs", fabs},    {"floor", floor},
    {"ceil", ceil}, {"round", round}, {"sign", f_sign}, {"sec", f_sec},
    {"csc", f_csc}, {"cot", f_cot},   {"fact", f_fact},
};
static const struct { const char *name; Fn2 fn; } F2S[] = {
    {"min", f_min}, {"max", f_max}, {"mod", f_mod},
};
#define N_F1S (int)(sizeof F1S / sizeof F1S[0])
#define N_F2S (int)(sizeof F2S / sizeof F2S[0])

/* ---- parser -------------------------------------------------------- */
typedef struct {
    const char *p, *end;
    char *err;
    int errsz;
    bool bad;
} P;

static Node *p_expr(P *p);

static void fail(P *p, const char *m)
{
    if (!p->bad) {
        p->bad = true;
        snprintf(p->err, p->errsz, "%s", m);
    }
}
static Node *mk(int t)
{
    Node *n = calloc(1, sizeof *n);
    n->type = t;
    return n;
}
static Node *num(double v)
{
    Node *n = mk(N_NUM);
    n->val = v;
    return n;
}
static Node *bin(int t, Node *a, Node *b)
{
    Node *n = mk(t);
    n->a = a;
    n->b = b;
    return n;
}
static void ws(P *p)
{
    while (p->p < p->end && *p->p == ' ') p->p++;
}
static int peekc(P *p)
{
    ws(p);
    return p->p < p->end ? (unsigned char)*p->p : 0;
}
static void close_paren(P *p)
{
    if (peekc(p) == ')') p->p++;
    else if (p->p < p->end) fail(p, "expected )");
}

static Node *p_ident(P *p)
{
    const char *s = p->p;
    while (p->p < p->end && isalpha((unsigned char)*p->p)) p->p++;
    int n = (int)(p->p - s);
    if (n > 30) {
        fail(p, "name too long");
        return num(NAN);
    }
    char w[32];
    for (int i = 0; i < n; i++) w[i] = (char)tolower((unsigned char)s[i]);
    w[n] = 0;

    int paren = (peekc(p) == '(');
    Node *acc = NULL;
    int i = 0;
    while (i < n) {
        Node *nd = NULL;
        if (paren) {
            for (int k = 0; k < N_F1S && !nd; k++) {
                if (!strcmp(w + i, F1S[k].name)) {
                    p->p++; /* '(' */
                    nd = mk(N_F1);
                    nd->f1 = F1S[k].fn;
                    nd->a = p_expr(p);
                    close_paren(p);
                    i = n;
                }
            }
            for (int k = 0; k < N_F2S && !nd; k++) {
                if (!strcmp(w + i, F2S[k].name)) {
                    p->p++;
                    nd = mk(N_F2);
                    nd->f2 = F2S[k].fn;
                    nd->a = p_expr(p);
                    if (peekc(p) == ',') {
                        p->p++;
                        nd->b = p_expr(p);
                    } else {
                        fail(p, "expected ,");
                        nd->b = num(NAN);
                    }
                    close_paren(p);
                    i = n;
                }
            }
        }
        if (!nd) {
            if (w[i] == 'p' && w[i + 1] == 'i') {
                nd = num(M_PI);
                i += 2;
            } else if (w[i] == 'e') {
                nd = num(M_E);
                i++;
            } else {
                nd = mk(N_VAR);
                nd->idx = w[i] - 'a';
                i++;
            }
        }
        acc = acc ? bin(N_MUL, acc, nd) : nd;
    }
    return acc;
}

static Node *p_primary(P *p)
{
    int c = peekc(p);
    if (isdigit(c) || c == '.') {
        char buf[40];
        int n = 0;
        while (p->p < p->end && (isdigit((unsigned char)*p->p) || *p->p == '.') && n < 38)
            buf[n++] = *p->p++;
        buf[n] = 0;
        return num(strtod(buf, NULL));
    }
    if (c == '(') {
        p->p++;
        Node *e = p_expr(p);
        close_paren(p);
        return e;
    }
    if (isalpha(c)) return p_ident(p);
    fail(p, c ? "unexpected character" : "unexpected end");
    if (p->p < p->end) p->p++;
    return num(NAN);
}

static Node *p_unary(P *p);

static Node *p_power(P *p)
{
    Node *base = p_primary(p);
    if (peekc(p) == '^') {
        p->p++;
        Node *ex = p_unary(p);
        return bin(N_POW, base, ex);
    }
    return base;
}

static Node *p_unary(P *p)
{
    int c = peekc(p);
    if (c == '-') {
        p->p++;
        Node *n = mk(N_NEG);
        n->a = p_unary(p);
        return n;
    }
    if (c == '+') {
        p->p++;
        return p_unary(p);
    }
    return p_power(p);
}

static bool starts_primary(int c)
{
    return isdigit(c) || c == '.' || isalpha(c) || c == '(';
}

static Node *p_term(P *p)
{
    Node *l = p_unary(p);
    while (!p->bad) {
        int c = peekc(p);
        if (c == '*' || c == '/') {
            p->p++;
            Node *r = p_unary(p);
            l = bin(c == '*' ? N_MUL : N_DIV, l, r);
        } else if (starts_primary(c)) {
            Node *r = p_unary(p); /* implicit multiplication */
            l = bin(N_MUL, l, r);
        } else
            break;
    }
    return l;
}

static Node *p_expr(P *p)
{
    Node *l = p_term(p);
    while (!p->bad) {
        int c = peekc(p);
        if (c == '+' || c == '-') {
            p->p++;
            Node *r = p_term(p);
            l = bin(c == '+' ? N_ADD : N_SUB, l, r);
        } else
            break;
    }
    return l;
}

Node *calc_parse(const char *s, int len, char *err, int errsz)
{
    P p = {s, s + len, err, errsz, false};
    if (errsz > 0) err[0] = 0;
    ws(&p);
    if (p.p >= p.end) {
        snprintf(err, errsz, "empty");
        return NULL;
    }
    Node *r = p_expr(&p);
    ws(&p);
    if (!p.bad && p.p < p.end) fail(&p, "unexpected character");
    if (p.bad) {
        calc_free(r);
        return NULL;
    }
    return r;
}

Node *calc_sub(Node *a, Node *b) { return bin(N_SUB, a, b); }

/* ---- evaluation ---------------------------------------------------- */
double calc_eval(const Node *n, const double *v)
{
    switch (n->type) {
    case N_NUM: return n->val;
    case N_VAR: return v[n->idx];
    case N_ADD: return calc_eval(n->a, v) + calc_eval(n->b, v);
    case N_SUB: return calc_eval(n->a, v) - calc_eval(n->b, v);
    case N_MUL: return calc_eval(n->a, v) * calc_eval(n->b, v);
    case N_DIV: return calc_eval(n->a, v) / calc_eval(n->b, v);
    case N_POW: return pow(calc_eval(n->a, v), calc_eval(n->b, v));
    case N_NEG: return -calc_eval(n->a, v);
    case N_F1: return n->f1(calc_eval(n->a, v));
    case N_F2: return n->f2(calc_eval(n->a, v), calc_eval(n->b, v));
    }
    return NAN;
}

unsigned calc_vars_used(const Node *n)
{
    if (!n) return 0;
    unsigned m = 0;
    if (n->type == N_VAR) m |= 1u << n->idx;
    return m | calc_vars_used(n->a) | calc_vars_used(n->b);
}

void calc_free(Node *n)
{
    if (!n) return;
    calc_free(n->a);
    calc_free(n->b);
    free(n);
}

void calc_format(double v, int prec, char *buf, int n)
{
    if (isnan(v)) snprintf(buf, n, "undefined");
    else if (isinf(v)) snprintf(buf, n, v > 0 ? "inf" : "-inf");
    else {
        if (fabs(v) < 1e-12) v = 0;
        snprintf(buf, n, "%.*g", prec, v);
    }
}
