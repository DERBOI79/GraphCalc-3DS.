#ifndef CALC_H
#define CALC_H

#include <stdbool.h>

typedef struct Node Node;

/* Parse an expression of `len` chars. Variables are the letters a-z
 * (e and pi are constants). Returns NULL on error and writes a message to err. */
Node *calc_parse(const char *s, int len, char *err, int errsz);

/* Evaluate with vars[0..25] holding a..z. */
double calc_eval(const Node *n, const double *vars);

/* Bitmask of variable letters used (bit 0 = a ... bit 25 = z). */
unsigned calc_vars_used(const Node *n);

/* Build a node computing (a - b). Takes ownership of both. */
Node *calc_sub(Node *a, Node *b);

void calc_free(Node *n);

/* Format a number for display with `prec` significant digits. */
void calc_format(double v, int prec, char *buf, int n);

#endif
