/*
 * GraphCalc 3DS - a Desmos-style graphing calculator for the Nintendo 3DS.
 *   Top screen    : graph (grid, axes, curves, points, trace)
 *   Bottom screen : expression list + touch keypad
 */
#include <3ds.h>
#include <ctype.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "calc.h"
#include "font.h"

#define TOP_W 400
#define BOT_W 320
#define SCR_H 240
#define MAXE 8
#define LIST_ROWS 4
#define ROW_H 20
#define KEY_Y0 80
#define KEY_W 40
#define KEY_H 32
#define VX 23 /* 'x' - 'a' */
#define VY 24 /* 'y' - 'a' */

/* ------------------------------------------------------------------ */
/* Drawing primitives                                                   */
/* ------------------------------------------------------------------ */
typedef struct {
    u8 *fb;
    int w;
} Surf;

static inline void px(Surf *s, int x, int y, u32 c)
{
    if ((unsigned)x >= (unsigned)s->w || (unsigned)y >= (unsigned)SCR_H) return;
    u8 *p = s->fb + 3 * (SCR_H * x + (SCR_H - 1 - y));
    p[0] = c & 0xFF;
    p[1] = (c >> 8) & 0xFF;
    p[2] = (c >> 16) & 0xFF;
}

static void rectf(Surf *s, int x, int y, int w, int h, u32 c)
{
    int x0 = x < 0 ? 0 : x, y0 = y < 0 ? 0 : y;
    int x1 = x + w > s->w ? s->w : x + w, y1 = y + h > SCR_H ? SCR_H : y + h;
    for (int i = x0; i < x1; i++) {
        u8 *p = s->fb + 3 * (SCR_H * i + (SCR_H - 1 - y0));
        for (int j = y0; j < y1; j++, p -= 3) {
            p[0] = c & 0xFF;
            p[1] = (c >> 8) & 0xFF;
            p[2] = (c >> 16) & 0xFF;
        }
    }
}

static void rect_outline(Surf *s, int x, int y, int w, int h, u32 c)
{
    rectf(s, x, y, w, 1, c);
    rectf(s, x, y + h - 1, w, 1, c);
    rectf(s, x, y, 1, h, c);
    rectf(s, x + w - 1, y, 1, h, c);
}

static void circle_fill(Surf *s, int cx, int cy, int r, u32 c)
{
    for (int dy = -r; dy <= r; dy++)
        for (int dx = -r; dx <= r; dx++)
            if (dx * dx + dy * dy <= r * r + r / 2) px(s, cx + dx, cy + dy, c);
}

static void circle_ring(Surf *s, int cx, int cy, int r, u32 c)
{
    for (int dy = -r; dy <= r; dy++)
        for (int dx = -r; dx <= r; dx++) {
            int d = dx * dx + dy * dy;
            if (d <= r * r + r / 2 && d >= (r - 1) * (r - 1)) px(s, cx + dx, cy + dy, c);
        }
}

/* 2x2 brush for curves */
static inline void dot(Surf *s, int x, int y, u32 c)
{
    px(s, x, y, c);
    px(s, x + 1, y, c);
    px(s, x, y + 1, c);
    px(s, x + 1, y + 1, c);
}

static void line(Surf *s, int x0, int y0, int x1, int y1, u32 c)
{
    if ((y0 < -4 && y1 < -4) || (y0 > SCR_H + 4 && y1 > SCR_H + 4)) return;
    int dx = abs(x1 - x0), sx = x0 < x1 ? 1 : -1;
    int dy = -abs(y1 - y0), sy = y0 < y1 ? 1 : -1;
    int err = dx + dy;
    for (;;) {
        dot(s, x0, y0, c);
        if (x0 == x1 && y0 == y1) break;
        int e2 = 2 * err;
        if (e2 >= dy) { err += dy; x0 += sx; }
        if (e2 <= dx) { err += dx; y0 += sy; }
    }
}

static void chr(Surf *s, int x, int y, char c, u32 col, int sc)
{
    if (c >= 'a' && c <= 'z') c -= 32;
    if (c < 32 || c > 95) c = '?';
    const unsigned char *g = font5x7[c - 32];
    for (int i = 0; i < 5; i++)
        for (int j = 0; j < 7; j++)
            if ((g[i] >> j) & 1) rectf(s, x + i * sc, y + j * sc, sc, sc, col);
}

static int twidth(const char *t, int sc) { return (int)strlen(t) * 6 * sc; }

static void text(Surf *s, int x, int y, const char *t, u32 col, int sc)
{
    for (; *t; t++, x += 6 * sc) chr(s, x, y, *t, col, sc);
}

/* ------------------------------------------------------------------ */
/* Expressions                                                          */
/* ------------------------------------------------------------------ */
enum { K_EMPTY, K_ERR, K_FUNC, K_IMPL, K_SLIDER, K_POINT, K_CALC };

typedef struct {
    char text[64];
    int len, cur;
    bool visible;
    int kind;
    Node *a, *b;
    int slider;
    double result;
} Expr;

static Expr ex[MAXE];
static int nex = 0, sel = 0, listTop = 0;
static double V[26];

static const u32 COLORS[6] = {0xC74440, 0x2D70B3, 0x388C46, 0x6042A6, 0xFA7E19, 0x000000};

/* view */
static double cx = 0, cy = 0, ppu = 20; /* centre, pixels per unit */
static bool trace = false;
static double tracex = 0;

static bool dirtyTop = true, dirtyBot = true;
static int page = 0;
static int pressedKey = -1;

static void ex_free(Expr *e)
{
    calc_free(e->a);
    calc_free(e->b);
    e->a = e->b = NULL;
}

static Node *parse_sub(const char *s, int len)
{
    char er[32];
    return calc_parse(s, len, er, sizeof er);
}

static void ex_compile(Expr *e)
{
    ex_free(e);
    e->kind = K_EMPTY;
    const char *t = e->text;
    int n = e->len;

    int ts = 0, te = n;
    while (ts < te && t[ts] == ' ') ts++;
    while (te > ts && t[te - 1] == ' ') te--;
    if (ts == te) return;

    int eq = -1;
    for (int i = 0; i < n; i++)
        if (t[i] == '=') { eq = i; break; }

    if (eq >= 0) {
        int ls = ts, le = eq;
        while (le > ls && t[le - 1] == ' ') le--;
        if (le - ls == 1 && isalpha((unsigned char)t[ls])) {
            char c = (char)tolower((unsigned char)t[ls]);
            Node *r = parse_sub(t + eq + 1, n - eq - 1);
            if (!r) { e->kind = K_ERR; return; }
            unsigned used = calc_vars_used(r);
            if (c == 'y' && !(used & (1u << VY))) {
                e->kind = K_FUNC;
                e->a = r;
                return;
            }
            if (c != 'x' && c != 'y' && c != 'e' && !(used & ((1u << VX) | (1u << VY)))) {
                e->kind = K_SLIDER;
                e->slider = c - 'a';
                e->a = r;
                return;
            }
            calc_free(r);
        }
        Node *l = parse_sub(t + ts, eq - ts);
        Node *r = parse_sub(t + eq + 1, n - eq - 1);
        if (!l || !r) {
            calc_free(l);
            calc_free(r);
            e->kind = K_ERR;
            return;
        }
        e->a = calc_sub(l, r);
        e->kind = K_IMPL;
        return;
    }

    /* point: (expr, expr) */
    if (t[ts] == '(' && t[te - 1] == ')') {
        int depth = 0, comma = -1, commas = 0;
        bool closedEarly = false;
        for (int i = ts; i < te; i++) {
            if (t[i] == '(') depth++;
            else if (t[i] == ')') {
                depth--;
                if (depth == 0 && i < te - 1) closedEarly = true;
            } else if (t[i] == ',' && depth == 1) {
                comma = i;
                commas++;
            }
        }
        if (!closedEarly && commas == 1) {
            Node *px_ = parse_sub(t + ts + 1, comma - ts - 1);
            Node *py_ = parse_sub(t + comma + 1, te - 1 - comma - 1);
            if (px_ && py_) {
                e->a = px_;
                e->b = py_;
                e->kind = K_POINT;
                return;
            }
            calc_free(px_);
            calc_free(py_);
            e->kind = K_ERR;
            return;
        }
    }

    Node *a = parse_sub(t + ts, te - ts);
    if (!a) { e->kind = K_ERR; return; }
    e->a = a;
    e->kind = (calc_vars_used(a) & (1u << VX)) ? K_FUNC : K_CALC;
}

static void ex_init(Expr *e, const char *t)
{
    memset(e, 0, sizeof *e);
    strncpy(e->text, t, 62);
    e->len = (int)strlen(e->text);
    e->cur = e->len;
    e->visible = true;
    ex_compile(e);
}

static void apply_sliders(void)
{
    for (int i = 0; i < nex; i++)
        if (ex[i].kind == K_SLIDER) {
            double v = calc_eval(ex[i].a, V);
            if (isfinite(v)) V[ex[i].slider] = v;
        }
}

static void refresh(void)
{
    apply_sliders();
    for (int i = 0; i < nex; i++)
        if (ex[i].kind == K_CALC) ex[i].result = calc_eval(ex[i].a, V);
    dirtyTop = dirtyBot = true;
}

/* Create "a=1" style sliders for letters used but never defined. */
static void ensure_sliders(void)
{
    unsigned used = 0, def = 0;
    for (int i = 0; i < nex; i++) {
        if (ex[i].kind < K_FUNC) continue;
        used |= calc_vars_used(ex[i].a) | calc_vars_used(ex[i].b);
        if (ex[i].kind == K_SLIDER) def |= 1u << ex[i].slider;
    }
    used &= ~((1u << VX) | (1u << VY));
    used &= ~def;
    for (int l = 0; l < 26 && nex < MAXE; l++)
        if ((used >> l) & 1) {
            char t[8];
            snprintf(t, sizeof t, "%c=1", 'a' + l);
            ex_init(&ex[nex++], t);
        }
}

static void commit(void)
{
    ensure_sliders();
    refresh();
}

static void select_row(int i)
{
    if (i != sel) {
        commit();
        sel = i;
    }
    dirtyBot = dirtyTop = true;
}

static void new_expr(void)
{
    commit();
    if (nex < MAXE) {
        ex_init(&ex[nex], "");
        sel = nex++;
    }
    dirtyBot = true;
}

static void delete_expr(int i)
{
    ex_free(&ex[i]);
    for (int j = i; j < nex - 1; j++) ex[j] = ex[j + 1];
    nex--;
    memset(&ex[nex], 0, sizeof ex[nex]);
    if (nex == 0) ex_init(&ex[nex++], "");
    if (i < sel) sel--;
    if (sel >= nex) sel = nex - 1;
    refresh();
}

static void ed_insert(const char *s)
{
    Expr *e = &ex[sel];
    int l = (int)strlen(s);
    if (e->len + l > 62) return;
    memmove(e->text + e->cur + l, e->text + e->cur, e->len - e->cur + 1);
    memcpy(e->text + e->cur, s, l);
    e->len += l;
    e->cur += l;
    ex_compile(e);
    refresh();
}

static void ed_backspace(void)
{
    Expr *e = &ex[sel];
    if (e->cur == 0) return;
    memmove(e->text + e->cur - 1, e->text + e->cur, e->len - e->cur + 1);
    e->cur--;
    e->len--;
    ex_compile(e);
    refresh();
}

static void ed_set(const char *t)
{
    Expr *e = &ex[sel];
    strncpy(e->text, t, 62);
    e->text[62] = 0;
    e->len = (int)strlen(e->text);
    e->cur = e->len;
    ex_compile(e);
    refresh();
}

static void slider_adjust(int dir, double step)
{
    Expr *e = &ex[sel];
    if (e->kind != K_SLIDER || calc_vars_used(e->a)) return;
    double v = calc_eval(e->a, V);
    v = round((v + dir * step) * 1000.0) / 1000.0;
    char t[40];
    snprintf(t, sizeof t, "%c=%.6g", 'a' + e->slider, v);
    ed_set(t);
}

/* ------------------------------------------------------------------ */
/* Keypad                                                               */
/* ------------------------------------------------------------------ */
typedef struct {
    const char *lab, *ins;
    int style; /* 0 white, 1 grey, 2 dark grey, 3 blue */
} Key;

static const Key PAGE0[5][8] = {
    {{"x", "x", 0}, {"y", "y", 0}, {"a", "a", 0}, {"b", "b", 0}, {"7", "7", 0}, {"8", "8", 0}, {"9", "9", 0}, {"/", "/", 1}},
    {{"x^2", "^2", 1}, {"^", "^", 1}, {"sqrt", "sqrt(", 1}, {"abs", "abs(", 1}, {"4", "4", 0}, {"5", "5", 0}, {"6", "6", 0}, {"*", "*", 1}},
    {{"(", "(", 1}, {")", ")", 1}, {",", ",", 1}, {"=", "=", 1}, {"1", "1", 0}, {"2", "2", 0}, {"3", "3", 0}, {"-", "-", 1}},
    {{"pi", "pi", 1}, {"e", "e", 1}, {"<", "@L", 2}, {">", "@R", 2}, {"0", "0", 0}, {".", ".", 0}, {"DEL", "@D", 2}, {"+", "+", 1}},
    {{"FN", "@P", 3}, {"NEW", "@N", 3}, {"CLR", "@C", 2}, {"c", "c", 0}, {"d", "d", 0}, {"k", "k", 0}, {"m", "m", 0}, {"n", "n", 0}},
};

static const Key PAGE1[5][8] = {
    {{"sin", "sin(", 1}, {"cos", "cos(", 1}, {"tan", "tan(", 1}, {"ln", "ln(", 1}, {"asin", "asin(", 1}, {"acos", "acos(", 1}, {"atan", "atan(", 1}, {"log", "log(", 1}},
    {{"exp", "exp(", 1}, {"floor", "floor(", 1}, {"ceil", "ceil(", 1}, {"round", "round(", 1}, {"min", "min(", 1}, {"max", "max(", 1}, {"mod", "mod(", 1}, {"sign", "sign(", 1}},
    {{"sinh", "sinh(", 1}, {"cosh", "cosh(", 1}, {"tanh", "tanh(", 1}, {"sqrt", "sqrt(", 1}, {"cbrt", "cbrt(", 1}, {"abs", "abs(", 1}, {"sec", "sec(", 1}, {"csc", "csc(", 1}},
    {{"cot", "cot(", 1}, {"pi", "pi", 1}, {"e", "e", 1}, {"<", "@L", 2}, {">", "@R", 2}, {",", ",", 1}, {"DEL", "@D", 2}, {"+", "+", 1}},
    {{"123", "@P", 3}, {"NEW", "@N", 3}, {"CLR", "@C", 2}, {"^", "^", 1}, {"(", "(", 1}, {")", ")", 1}, {"=", "=", 1}, {"-", "-", 1}},
};

static const Key *key_at(int r, int c) { return page ? &PAGE1[r][c] : &PAGE0[r][c]; }

static void press_key(int r, int c)
{
    const Key *k = key_at(r, c);
    Expr *e = &ex[sel];
    if (k->ins[0] == '@') {
        switch (k->ins[1]) {
        case 'L': if (e->cur > 0) e->cur--; break;
        case 'R': if (e->cur < e->len) e->cur++; break;
        case 'D': ed_backspace(); break;
        case 'P': page ^= 1; break;
        case 'N': new_expr(); break;
        case 'C': ed_set(""); break;
        }
    } else
        ed_insert(k->ins);
    dirtyBot = true;
}

/* ------------------------------------------------------------------ */
/* Bottom screen                                                        */
/* ------------------------------------------------------------------ */
static void row_layout(const Expr *e, bool selected, char *res, int rn, int *maxch)
{
    res[0] = 0;
    if (e->kind == K_CALC) {
        char t[32];
        calc_format(e->result, 8, t, sizeof t);
        snprintf(res, rn, "=%s", t);
    } else if (e->kind == K_ERR)
        snprintf(res, rn, "ERR");
    else if (e->kind == K_SLIDER && selected)
        snprintf(res, rn, "L/R");
    int rw = res[0] ? (int)strlen(res) * 6 + 6 : 0;
    *maxch = (300 - rw - 28) / 6;
}

static int dstart(const Expr *e, int maxch) { return e->cur > maxch ? e->cur - maxch : 0; }

static void render_bot(Surf *s)
{
    if (sel < listTop) listTop = sel;
    if (sel >= listTop + LIST_ROWS) listTop = sel - LIST_ROWS + 1;

    rectf(s, 0, 0, BOT_W, SCR_H, 0xE6E9ED);

    for (int r = 0; r < LIST_ROWS; r++) {
        int i = listTop + r, y = r * ROW_H;
        rectf(s, 0, y, BOT_W, ROW_H, 0xFFFFFF);
        rectf(s, 0, y + ROW_H - 1, BOT_W, 1, 0xD0D0D0);
        if (i >= nex) continue;
        Expr *e = &ex[i];
        bool selected = (i == sel);

        rectf(s, 0, y, 22, ROW_H - 1, 0xF2F2F2);
        rectf(s, 21, y, 1, ROW_H, 0xD8D8D8);
        u32 col = COLORS[i % 6];
        if (e->kind == K_CALC || e->kind == K_EMPTY || e->kind == K_ERR) col = 0x9A9A9A;
        if (e->visible) circle_fill(s, 11, y + 10, 6, col);
        else circle_ring(s, 11, y + 10, 6, col);
        if (e->kind == K_ERR) chr(s, 8, y + 7, '!', 0xFFFFFF, 1);

        if (selected) rectf(s, 0, y, 3, ROW_H - 1, 0x2D70B3);

        char res[40];
        int maxch;
        row_layout(e, selected, res, sizeof res, &maxch);
        int ds = dstart(e, maxch);
        for (int k = ds; k < e->len && k < ds + maxch; k++)
            chr(s, 28 + (k - ds) * 6, y + 7, e->text[k], 0x000000, 1);
        if (selected) rectf(s, 27 + (e->cur - ds) * 6, y + 4, 1, 12, 0x2D70B3);
        if (res[0]) text(s, 300 - twidth(res, 1), y + 7, res, e->kind == K_ERR ? 0xC74440 : 0x777777, 1);
        text(s, 307, y + 7, "X", 0xAAAAAA, 1);
    }

    /* keypad */
    for (int r = 0; r < 5; r++)
        for (int c = 0; c < 8; c++) {
            const Key *k = key_at(r, c);
            int x = c * KEY_W, y = KEY_Y0 + r * KEY_H;
            u32 bg, fg = 0x222222;
            switch (k->style) {
            case 0: bg = 0xFFFFFF; break;
            case 1: bg = 0xD9DEE4; break;
            case 2: bg = 0xC4CBD3; break;
            default: bg = 0x2D70B3; fg = 0xFFFFFF; break;
            }
            if (pressedKey == r * 8 + c) bg = 0x9FB6D0;
            rectf(s, x + 1, y + 1, KEY_W - 2, KEY_H - 2, bg);
            int sc = strlen(k->lab) == 1 ? 2 : 1;
            text(s, x + (KEY_W - twidth(k->lab, sc)) / 2 + (sc == 2 ? 1 : 0),
                 y + (KEY_H - 7 * sc) / 2, k->lab, fg, sc);
        }
}

/* ------------------------------------------------------------------ */
/* Top screen: graph                                                    */
/* ------------------------------------------------------------------ */
static inline int to_sx(double x) { return (int)lround(TOP_W / 2 + (x - cx) * ppu); }
static inline int to_sy(double y) { return (int)lround(SCR_H / 2 - (y - cy) * ppu); }

static double clampd(double v, double lo, double hi) { return v < lo ? lo : (v > hi ? hi : v); }

static void draw_grid(Surf *s)
{
    rectf(s, 0, 0, TOP_W, SCR_H, 0xFFFFFF);

    double raw = 50.0 / ppu;
    double p10 = pow(10.0, floor(log10(raw)));
    double m = raw / p10, step, minor;
    if (m <= 1) { step = p10; minor = step / 5; }
    else if (m <= 2) { step = 2 * p10; minor = step / 4; }
    else if (m <= 5) { step = 5 * p10; minor = step / 5; }
    else { step = 10 * p10; minor = step / 5; }
    if (minor * ppu < 8) minor = step;
    long ratio = lround(step / minor);
    if (ratio < 1) ratio = 1;

    double xmin = cx - TOP_W / 2.0 / ppu, xmax = cx + TOP_W / 2.0 / ppu;
    double ymin = cy - SCR_H / 2.0 / ppu, ymax = cy + SCR_H / 2.0 / ppu;

    for (long k = (long)ceil(xmin / minor); k <= (long)floor(xmax / minor); k++)
        rectf(s, to_sx(k * minor), 0, 1, SCR_H, (k % ratio == 0) ? 0xCCCCCC : 0xEBEBEB);
    for (long k = (long)ceil(ymin / minor); k <= (long)floor(ymax / minor); k++)
        rectf(s, 0, to_sy(k * minor), TOP_W, 1, (k % ratio == 0) ? 0xCCCCCC : 0xEBEBEB);

    int ax = to_sx(0), ay = to_sy(0);
    rectf(s, ax, 0, 1, SCR_H, 0x555555);
    rectf(s, 0, ay, TOP_W, 1, 0x555555);

    /* labels */
    char buf[32];
    for (long k = (long)ceil(xmin / step); k <= (long)floor(xmax / step); k++) {
        if (k == 0) continue;
        calc_format(k * step, 8, buf, sizeof buf);
        int w = twidth(buf, 1);
        int lx = to_sx(k * step) - w / 2;
        int ly = (int)clampd(ay + 4, 2, SCR_H - 10);
        if (lx < 1 || lx + w > TOP_W - 1) continue;
        text(s, lx, ly, buf, 0x555555, 1);
    }
    for (long k = (long)ceil(ymin / step); k <= (long)floor(ymax / step); k++) {
        if (k == 0) continue;
        calc_format(k * step, 8, buf, sizeof buf);
        int w = twidth(buf, 1);
        int lx = (int)clampd(ax - 4 - w, 2, TOP_W - w - 2);
        int ly = to_sy(k * step) - 3;
        if (ly < 1 || ly + 7 > SCR_H - 1) continue;
        text(s, lx, ly, buf, 0x555555, 1);
    }
    calc_format(0, 1, buf, sizeof buf);
    text(s, (int)clampd(ax - 10, 2, TOP_W - 10), (int)clampd(ay + 4, 2, SCR_H - 10), "0", 0x555555, 1);
}

static inline double sy_unclamped(double y) { return SCR_H / 2.0 - (y - cy) * ppu; }

static void draw_func(Surf *s, Expr *e, u32 col)
{
    bool have = false;
    int py0 = 0;
    for (int c = 0; c <= TOP_W; c++) {
        double x = cx + (c - TOP_W / 2.0) / ppu;
        V[VX] = x;
        double y = calc_eval(e->a, V);
        if (!isfinite(y)) { have = false; continue; }
        int py = (int)lround(clampd(sy_unclamped(y), -3000, 3000));
        if (have) {
            int dy = abs(py - py0);
            bool ok = dy <= SCR_H;
            if (!ok) { /* steep: connect only if function is continuous in between */
                V[VX] = x - 0.5 / ppu;
                double ym = calc_eval(e->a, V);
                double pm = sy_unclamped(ym);
                double lo = py < py0 ? py : py0, hi = py < py0 ? py0 : py;
                double eps = 0.1 * (hi - lo);
                ok = isfinite(ym) && pm >= lo - eps && pm <= hi + eps;
            }
            if (ok) line(s, c - 1, py0, c, py, col);
            else dot(s, c, py, col);
        } else
            dot(s, c, py, col);
        py0 = py;
        have = true;
    }
}

static void draw_impl(Surf *s, Expr *e, u32 col)
{
    enum { S = 2, NX = TOP_W / S + 1 };
    static double r0[NX], r1[NX];
    double *cur = r0, *nxt = r1;

    V[VY] = cy - (0 - SCR_H / 2.0) / ppu;
    for (int i = 0; i < NX; i++) {
        V[VX] = cx + (i * S - TOP_W / 2.0) / ppu;
        cur[i] = calc_eval(e->a, V);
    }
    for (int j = 0; j * S < SCR_H; j++) {
        double yy = cy - ((j + 1) * S - SCR_H / 2.0) / ppu;
        V[VY] = yy;
        for (int i = 0; i < NX; i++) {
            V[VX] = cx + (i * S - TOP_W / 2.0) / ppu;
            nxt[i] = calc_eval(e->a, V);
        }
        for (int i = 0; i + 1 < NX; i++) {
            double A = cur[i], B = cur[i + 1], C = nxt[i], D = nxt[i + 1];
            if (!(isfinite(A) && isfinite(B) && isfinite(C) && isfinite(D))) continue;
            double x0 = i * S, y0 = j * S, pxs[4], pys[4];
            int n = 0;
            if ((A < 0) != (B < 0)) { pxs[n] = x0 + S * A / (A - B); pys[n++] = y0; }
            if ((B < 0) != (D < 0)) { pxs[n] = x0 + S; pys[n++] = y0 + S * B / (B - D); }
            if ((C < 0) != (D < 0)) { pxs[n] = x0 + S * C / (C - D); pys[n++] = y0 + S; }
            if ((A < 0) != (C < 0)) { pxs[n] = x0; pys[n++] = y0 + S * A / (A - C); }
            if (n == 2)
                line(s, (int)lround(pxs[0]), (int)lround(pys[0]), (int)lround(pxs[1]), (int)lround(pys[1]), col);
            else if (n == 4) {
                line(s, (int)lround(pxs[0]), (int)lround(pys[0]), (int)lround(pxs[1]), (int)lround(pys[1]), col);
                line(s, (int)lround(pxs[2]), (int)lround(pys[2]), (int)lround(pxs[3]), (int)lround(pys[3]), col);
            }
        }
        double *tmp = cur; cur = nxt; nxt = tmp;
    }
}

static void render_top(Surf *s)
{
    apply_sliders();
    draw_grid(s);

    for (int i = 0; i < nex; i++) {
        Expr *e = &ex[i];
        if (!e->visible) continue;
        u32 col = COLORS[i % 6];
        if (e->kind == K_FUNC) draw_func(s, e, col);
        else if (e->kind == K_IMPL) draw_impl(s, e, col);
        else if (e->kind == K_POINT) {
            double x = calc_eval(e->a, V), y = calc_eval(e->b, V);
            if (isfinite(x) && isfinite(y)) {
                int sx = to_sx(x), sy = to_sy(y);
                circle_fill(s, sx, sy, 5, 0xFFFFFF);
                circle_fill(s, sx, sy, 4, col);
            }
        }
    }

    if (trace && sel < nex && ex[sel].kind == K_FUNC) {
        V[VX] = tracex;
        double y = calc_eval(ex[sel].a, V);
        if (isfinite(y)) {
            int sx = to_sx(tracex), sy = to_sy(y);
            circle_fill(s, sx, sy, 6, 0xFFFFFF);
            circle_fill(s, sx, sy, 4, COLORS[sel % 6]);
            char a[24], b[24], lab[64];
            calc_format(tracex, 5, a, sizeof a);
            calc_format(y, 5, b, sizeof b);
            snprintf(lab, sizeof lab, "(%s, %s)", a, b);
            int w = twidth(lab, 1) + 8;
            int lx = (int)clampd(sx + 8, 2, TOP_W - w - 2);
            int ly = (int)clampd(sy - 18, 2, SCR_H - 18);
            rectf(s, lx, ly, w, 14, 0xFFFFFF);
            rect_outline(s, lx, ly, w, 14, 0x888888);
            text(s, lx + 4, ly + 4, lab, 0x222222, 1);
        }
    }

    text(s, 4, 4, "A/B ZOOM  Y TRACE  X RESET  SELECT KEYBOARD", 0xAAAAAA, 1);
}

/* ------------------------------------------------------------------ */
/* Input                                                                */
/* ------------------------------------------------------------------ */
static void handle_touch(int x, int y)
{
    if (y < KEY_Y0) {
        int i = listTop + y / ROW_H;
        if (i >= nex) return;
        if (x < 22) {
            ex[i].visible = !ex[i].visible;
            dirtyTop = dirtyBot = true;
        } else if (x >= 302) {
            delete_expr(i);
        } else {
            select_row(i);
            Expr *e = &ex[sel];
            char res[40];
            int maxch;
            row_layout(e, true, res, sizeof res, &maxch);
            int idx = dstart(e, maxch) + (x - 28 + 3) / 6;
            e->cur = idx < 0 ? 0 : (idx > e->len ? e->len : idx);
        }
    } else {
        int c = x / KEY_W, r = (y - KEY_Y0) / KEY_H;
        if (c > 7) c = 7;
        if (r > 4) r = 4;
        pressedKey = r * 8 + c;
        press_key(r, c);
    }
    dirtyBot = true;
}

static void open_keyboard(void)
{
    SwkbdState sw;
    char buf[80] = {0};
    swkbdInit(&sw, SWKBD_TYPE_NORMAL, 2, 62);
    swkbdSetInitialText(&sw, ex[sel].text);
    swkbdSetHintText(&sw, "Expression, e.g. y=sin(x)");
    if (swkbdInputText(&sw, buf, sizeof buf) == SWKBD_BUTTON_CONFIRM) ed_set(buf);
    dirtyTop = dirtyBot = true;
}

static void reset_view(void)
{
    cx = cy = 0;
    ppu = 20;
    dirtyTop = true;
}

int main(void)
{
    gfxInitDefault();
    gfxSetDoubleBuffering(GFX_TOP, false);
    gfxSetDoubleBuffering(GFX_BOTTOM, false);

    for (int i = 0; i < 26; i++) V[i] = 1.0;
    ex_init(&ex[nex++], "y=a*sin(x)");
    ex_init(&ex[nex++], "a=2");
    ex_init(&ex[nex++], "x^2+y^2=9");
    sel = 0;
    refresh();

    int hold = 0;
    while (aptMainLoop()) {
        hidScanInput();
        u32 kd = hidKeysDown(), kh = hidKeysHeld(), ku = hidKeysUp();
        if (kd & KEY_START) break;

        if (kd & KEY_TOUCH) {
            touchPosition t;
            hidTouchRead(&t);
            handle_touch(t.px, t.py);
        }
        if (ku & KEY_TOUCH) {
            pressedKey = -1;
            dirtyBot = true;
        }
        if (kd & KEY_SELECT) open_keyboard();
        if (kd & KEY_X) reset_view();
        if (kd & KEY_Y) {
            trace = !trace;
            tracex = cx;
            dirtyTop = true;
        }

        /* zoom */
        if (kh & KEY_A) { ppu = clampd(ppu * 1.04, 0.02, 200000); dirtyTop = true; }
        if (kh & KEY_B) { ppu = clampd(ppu / 1.04, 0.02, 200000); dirtyTop = true; }

        /* pan / trace */
        circlePosition cp;
        hidCircleRead(&cp);
        double mx = 0, my = 0;
        if (kh & KEY_DUP) my += 6;
        if (kh & KEY_DDOWN) my -= 6;
        if (abs(cp.dx) > 20) mx += cp.dx * 8.0 / 156.0;
        if (abs(cp.dy) > 20) my += cp.dy * 8.0 / 156.0;
        bool traceOn = trace && sel < nex && ex[sel].kind == K_FUNC;
        if (traceOn) {
            double d = 0;
            if (kh & KEY_DLEFT) d -= 3;
            if (kh & KEY_DRIGHT) d += 3;
            if (d != 0) {
                tracex += d / ppu;
                if (fabs(tracex - cx) * ppu > 190) cx = tracex - (tracex > cx ? 190 : -190) / ppu;
                dirtyTop = true;
            }
        } else {
            if (kh & KEY_DLEFT) mx -= 6;
            if (kh & KEY_DRIGHT) mx += 6;
        }
        if (mx != 0 || my != 0) {
            cx += mx / ppu;
            cy += my / ppu;
            dirtyTop = true;
        }

        /* slider adjust */
        if (kh & (KEY_L | KEY_R)) {
            hold++;
            if (hold == 1 || (hold > 12 && hold % 2 == 0))
                slider_adjust((kh & KEY_R) ? 1 : -1, hold > 50 ? 0.5 : 0.1);
        } else
            hold = 0;

        if (dirtyTop) {
            Surf t = {gfxGetFramebuffer(GFX_TOP, GFX_LEFT, NULL, NULL), TOP_W};
            render_top(&t);
            dirtyTop = false;
        }
        if (dirtyBot) {
            Surf b = {gfxGetFramebuffer(GFX_BOTTOM, GFX_LEFT, NULL, NULL), BOT_W};
            render_bot(&b);
            dirtyBot = false;
        }

        gfxFlushBuffers();
        gfxSwapBuffers();
        gspWaitForVBlank();
    }

    for (int i = 0; i < nex; i++) ex_free(&ex[i]);
    gfxExit();
    return 0;
}
