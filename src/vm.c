/* vm.c - evaluates compiled formulas (small stack machine) */
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include "omm_internal.h"

static double f_sign(double x) { return (double)((x > 0) - (x < 0)); }
static double f_frac(double x) { return x - floor(x); }
static double f_min(double a, double b) { return (a != a || b != b) ? NAN : (a < b ? a : b); }
static double f_max(double a, double b) { return (a != a || b != b) ? NAN : (a > b ? a : b); }
/* mathematical modulo: result has the sign of b, mod(-1, 3) = 2 */
static double f_mod(double a, double b) { return a - b * floor(a / b); }

typedef double (*Fn1)(double);
typedef double (*Fn2)(double, double);

static const struct { const char* name; Fn1 fn; } T1[] = {
  { "sin", sin },   { "cos", cos },   { "tan", tan },
  { "asin", asin }, { "acos", acos }, { "atan", atan },
  { "sinh", sinh }, { "cosh", cosh }, { "tanh", tanh },
  { "exp", exp },   { "ln", log },    { "log", log },
  { "log2", log2 }, { "log10", log10 },
  { "sqrt", sqrt }, { "cbrt", cbrt }, { "abs", fabs },
  { "floor", floor }, { "ceil", ceil }, { "round", round },
  { "sign", f_sign }, { "frac", f_frac }
};

static const struct { const char* name; Fn2 fn; } T2[] = {
  { "atan2", atan2 }, { "pow", pow }, { "hypot", hypot },
  { "min", f_min },   { "max", f_max }, { "mod", f_mod }
};

#define N1 ((int)(sizeof T1 / sizeof T1[0]))
#define N2 ((int)(sizeof T2 / sizeof T2[0]))

int omm_find_f1(const char* name) {
  int i;
  for (i = 0; i < N1; i++) if (!strcmp(T1[i].name, name)) return i;
  return -1;
}

int omm_find_f2(const char* name) {
  int i;
  for (i = 0; i < N2; i++) if (!strcmp(T2[i].name, name)) return i;
  return -1;
}

double omm_eval(const Code* c, const double* vars) {
  double st[OMM_STACK];
  int sp = 0, k;
  const Instr* in = c->ins;

  for (k = 0; k < c->n; k++, in++) {
    switch (in->op) {
      case OP_CONST: st[sp++] = in->num; break;
      case OP_VAR:   st[sp++] = vars[in->arg]; break;
      case OP_ADD:   sp--; st[sp - 1] += st[sp]; break;
      case OP_SUB:   sp--; st[sp - 1] -= st[sp]; break;
      case OP_MUL:   sp--; st[sp - 1] *= st[sp]; break;
      case OP_DIV:   sp--; st[sp - 1] /= st[sp]; break;
      case OP_POW:   sp--; st[sp - 1] = pow(st[sp - 1], st[sp]); break;
      case OP_NEG:   st[sp - 1] = -st[sp - 1]; break;
      case OP_F1:    st[sp - 1] = T1[in->arg].fn(st[sp - 1]); break;
      case OP_F2:    sp--; st[sp - 1] = T2[in->arg].fn(st[sp - 1], st[sp]); break;
      default:       return NAN; /* unresolved name: cannot happen after parsing */
    }
  }
  return sp == 1 ? st[0] : NAN;
}

void omm_code_free(Code* c) {
  free(c->ins);
  c->ins = NULL;
  c->n = c->cap = c->cur = c->depth = 0;
}
