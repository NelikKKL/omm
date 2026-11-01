/* parser.c - tokens -> models (bytecode). Recursive descent, errors via a flag. */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "omm_internal.h"

#define PI_VALUE  3.14159265358979323846
#define E_VALUE   2.71828182845904523536

typedef struct {                 /* names used by one statement, resolved after 'for' is known */
  char name[OMM_MAX_SYMS][OMM_MAX_ID];
  int  line[OMM_MAX_SYMS];
  int  n;
} Names;

typedef struct {                 /* loop variables of one statement */
  char   name[OMM_MAX_VARS][OMM_MAX_ID];
  double lo[OMM_MAX_VARS], hi[OMM_MAX_VARS];
  int    n;
} Vars;

typedef struct { char name[OMM_MAX_ID]; double val; } Const;

typedef struct {
  const Token* t;
  int    pos;
  Code*  out;                    /* instructions are emitted here */
  Names* names;
  int    nest;
  int    failed;
  Const  consts[OMM_MAX_CONSTS];
  int    nconst;
} Parser;

/* ---- token helpers ------------------------------------------------------ */

static const Token* cur(const Parser* p) { return &p->t[p->pos]; }
static int is(const Parser* p, TokType t) { return p->t[p->pos].type == t; }
static void adv(Parser* p) { if (p->t[p->pos].type != T_EOF) p->pos++; }

#define FAIL(p, ...) \
  do { if (!(p)->failed) { omm_set_error(cur(p)->line, __VA_ARGS__); (p)->failed = 1; } } while (0)

static const char* tokstr(const Token* t) {
  static char buf[48];
  switch (t->type) {
    case T_EOF:   return "end of input";
    case T_NL:    return "end of line";
    case T_NUM:   snprintf(buf, sizeof buf, "number %g", t->num); return buf;
    case T_ID:    snprintf(buf, sizeof buf, "'%s'", t->id); return buf;
    case T_FOR:   return "'for'";
    case T_IN:    return "'in'";
    case T_LET:   return "'let'";
    case T_PLUS:  return "'+'";
    case T_MINUS: return "'-'";
    case T_STAR:  return "'*'";
    case T_SLASH: return "'/'";
    case T_CARET: return "'^'";
    case T_LP:    return "'('";
    case T_RP:    return "')'";
    case T_COMMA: return "','";
    case T_EQ:    return "'='";
    case T_DOTDOT: return "'..'";
  }
  return "?";
}

/* ---- emitting code ------------------------------------------------------ */

static void emit(Parser* p, int op, int arg, double num) {
  Code* c = p->out;
  if (p->failed) return;
  if (c->n == c->cap) {
    int nc = c->cap ? c->cap * 2 : 16;
    Instr* ni = (Instr*)realloc(c->ins, (size_t)nc * sizeof(Instr));
    if (!ni) { FAIL(p, "out of memory"); return; }
    c->ins = ni;
    c->cap = nc;
  }
  c->ins[c->n].op = op;
  c->ins[c->n].arg = arg;
  c->ins[c->n].num = num;
  c->n++;
  switch (op) {
    case OP_CONST: case OP_VAR: case OP_SYM: c->cur++; break;
    case OP_ADD: case OP_SUB: case OP_MUL: case OP_DIV: case OP_POW: case OP_F2: c->cur--; break;
    default: break;
  }
  if (c->cur > c->depth) c->depth = c->cur;
  if (c->depth > OMM_STACK) FAIL(p, "formula is too complex");
}

static int intern(Parser* p, const char* name, int line) {
  Names* nm = p->names;
  int i;
  for (i = 0; i < nm->n; i++) if (!strcmp(nm->name[i], name)) return i;
  if (nm->n == OMM_MAX_SYMS) { FAIL(p, "too many different names in one formula"); return 0; }
  strcpy(nm->name[nm->n], name);
  nm->line[nm->n] = line;
  return nm->n++;
}

/* ---- expressions ---------------------------------------------------------
 *   expr   = term { ('+'|'-') term }
 *   term   = unary { ('*'|'/') unary | power }      <- a bare power is implicit '*': 2x, 2pi, 3(x+1)
 *   unary  = ('-'|'+') unary | power
 *   power  = atom [ '^' unary ]                      <- right-associative, -2^2 = -4, 2^-1 = 0.5
 *   atom   = number | name | func '(' expr [',' expr] ')' | '(' expr ')'
 */

static void parse_expr(Parser* p);
static void parse_unary(Parser* p);

static int starts_atom(TokType t) { return t == T_NUM || t == T_ID || t == T_LP; }

static void parse_atom(Parser* p) {
  const Token* t = cur(p);

  if (t->type == T_NUM) { emit(p, OP_CONST, 0, t->num); adv(p); return; }

  if (t->type == T_LP) {
    adv(p);
    parse_expr(p);
    if (p->failed) return;
    if (!is(p, T_RP)) { FAIL(p, "expected ')' but found %s", tokstr(cur(p))); return; }
    adv(p);
    return;
  }

  if (t->type == T_ID) {
    int f1 = omm_find_f1(t->id), f2 = omm_find_f2(t->id);
    if (f1 >= 0 || f2 >= 0) {
      if (p->t[p->pos + 1].type != T_LP) {
        FAIL(p, "function '%s' needs parentheses, e.g. %s(x)", t->id, t->id);
        return;
      }
      adv(p);
      adv(p);
      parse_expr(p);
      if (p->failed) return;
      if (f2 >= 0) {
        if (!is(p, T_COMMA)) { FAIL(p, "function '%s' takes 2 arguments", t->id); return; }
        adv(p);
        parse_expr(p);
        emit(p, OP_F2, f2, 0);
      } else {
        if (is(p, T_COMMA)) { FAIL(p, "function '%s' takes 1 argument", t->id); return; }
        emit(p, OP_F1, f1, 0);
      }
      if (p->failed) return;
      if (!is(p, T_RP)) { FAIL(p, "expected ')' but found %s", tokstr(cur(p))); return; }
      adv(p);
      return;
    }
    emit(p, OP_SYM, intern(p, t->id, t->line), 0);
    adv(p);
    return;
  }

  FAIL(p, "expected a value but found %s", tokstr(t));
}

static void parse_power(Parser* p) {
  parse_atom(p);
  if (!p->failed && is(p, T_CARET)) {
    adv(p);
    parse_unary(p);
    emit(p, OP_POW, 0, 0);
  }
}

static void parse_unary(Parser* p) {
  if (p->failed) return;
  if (p->nest >= OMM_MAX_NEST) { FAIL(p, "formula is nested too deeply"); return; }
  p->nest++;
  if (is(p, T_MINUS)) { adv(p); parse_unary(p); emit(p, OP_NEG, 0, 0); }
  else if (is(p, T_PLUS)) { adv(p); parse_unary(p); }
  else parse_power(p);
  p->nest--;
}

static void parse_term(Parser* p) {
  parse_unary(p);
  while (!p->failed) {
    TokType t = cur(p)->type;
    if (t == T_STAR || t == T_SLASH) {
      adv(p);
      parse_unary(p);
      emit(p, t == T_STAR ? OP_MUL : OP_DIV, 0, 0);
    } else if (starts_atom(t)) {
      parse_power(p);
      emit(p, OP_MUL, 0, 0);
    } else {
      break;
    }
  }
}

static void parse_expr(Parser* p) {
  parse_term(p);
  while (!p->failed && (is(p, T_PLUS) || is(p, T_MINUS))) {
    int add = is(p, T_PLUS);
    adv(p);
    parse_term(p);
    emit(p, add ? OP_ADD : OP_SUB, 0, 0);
  }
}

/* ---- names, constants --------------------------------------------------- */

static int is_func(const char* name) { return omm_find_f1(name) >= 0 || omm_find_f2(name) >= 0; }
static int is_builtin_const(const char* name) {
  return !strcmp(name, "pi") || !strcmp(name, "tau") || !strcmp(name, "e");
}

static int find_const(const Parser* p, const char* name, double* val) {
  int i;
  for (i = p->nconst - 1; i >= 0; i--) {
    if (!strcmp(p->consts[i].name, name)) { *val = p->consts[i].val; return 1; }
  }
  if (!strcmp(name, "pi"))  { *val = PI_VALUE; return 1; }
  if (!strcmp(name, "tau")) { *val = 2.0 * PI_VALUE; return 1; }
  if (!strcmp(name, "e"))   { *val = E_VALUE; return 1; }
  return 0;
}

/* Turn OP_SYM into OP_VAR (loop variable) or OP_CONST (constant). */
static int resolve(Parser* p, Code* c, const Names* nm, const Vars* vs) {
  int k, v;
  for (k = 0; k < c->n; k++) {
    Instr* in = &c->ins[k];
    const char* name;
    double val;
    int slot = -1;
    if (in->op != OP_SYM) continue;
    name = nm->name[in->arg];
    if (vs) {
      for (v = 0; v < vs->n; v++) if (!strcmp(vs->name[v], name)) { slot = v; break; }
    }
    if (slot >= 0) { in->op = OP_VAR; in->arg = slot; continue; }
    if (find_const(p, name, &val)) { in->op = OP_CONST; in->num = val; continue; }
    omm_set_error(nm->line[in->arg],
                  "unknown name '%s' (loop variables come from 'for', constants from 'let')", name);
    p->failed = 1;
    return 0;
  }
  return 1;
}

/* Parse and evaluate a constant expression (range bounds, 'let' values). */
static double parse_const_expr(Parser* p) {
  Code cc = { NULL, 0, 0, 0, 0 };
  Names nn;
  Code* saved_out = p->out;
  Names* saved_names = p->names;
  double val = NAN;

  nn.n = 0;
  p->out = &cc;
  p->names = &nn;
  parse_expr(p);
  p->out = saved_out;
  p->names = saved_names;
  if (!p->failed && resolve(p, &cc, &nn, NULL)) val = omm_eval(&cc, NULL);
  omm_code_free(&cc);
  return val;
}

static void expect_end(Parser* p) {
  if (!p->failed && !is(p, T_NL) && !is(p, T_EOF)) FAIL(p, "unexpected %s", tokstr(cur(p)));
}

/* let a = 2, b = a*3 */
static void parse_let(Parser* p) {
  adv(p); /* 'let' */
  for (;;) {
    char name[OMM_MAX_ID];
    double val;
    int i;

    if (!is(p, T_ID)) { FAIL(p, "expected a name after 'let' but found %s", tokstr(cur(p))); return; }
    strcpy(name, cur(p)->id);
    if (is_func(name) || is_builtin_const(name)) { FAIL(p, "'%s' is built in and cannot be redefined", name); return; }
    adv(p);
    if (!is(p, T_EQ)) { FAIL(p, "expected '=' after '%s' but found %s", name, tokstr(cur(p))); return; }
    adv(p);
    val = parse_const_expr(p);
    if (p->failed) return;
    if (!isfinite(val)) { FAIL(p, "'%s' is not a finite number", name); return; }

    for (i = 0; i < p->nconst; i++) if (!strcmp(p->consts[i].name, name)) break;
    if (i == p->nconst) {
      if (p->nconst == OMM_MAX_CONSTS) { FAIL(p, "too many constants"); return; }
      p->nconst++;
    }
    strcpy(p->consts[i].name, name);
    p->consts[i].val = val;

    if (is(p, T_COMMA)) { adv(p); continue; }
    break;
  }
  expect_end(p);
}

/* for x,y in -3..3        for u in 0..1, v in 0..2*pi */
static void parse_for(Parser* p, Vars* vs) {
  adv(p); /* 'for' */
  for (;;) {
    int first = vs->n, i;
    double lo, hi;

    for (;;) { /* names sharing one range */
      if (!is(p, T_ID)) { FAIL(p, "expected a variable name but found %s", tokstr(cur(p))); return; }
      if (is_func(cur(p)->id)) { FAIL(p, "'%s' is a function name and cannot be a variable", cur(p)->id); return; }
      if (vs->n == OMM_MAX_VARS) { FAIL(p, "too many variables (max %d)", OMM_MAX_VARS); return; }
      for (i = 0; i < vs->n; i++) {
        if (!strcmp(vs->name[i], cur(p)->id)) { FAIL(p, "variable '%s' is declared twice", cur(p)->id); return; }
      }
      strcpy(vs->name[vs->n++], cur(p)->id);
      adv(p);
      if (is(p, T_COMMA)) { adv(p); continue; }
      break;
    }
    if (!is(p, T_IN)) { FAIL(p, "expected 'in' but found %s", tokstr(cur(p))); return; }
    adv(p);
    lo = parse_const_expr(p);
    if (p->failed) return;
    if (!is(p, T_DOTDOT)) { FAIL(p, "expected '..' in the range (like -3..3) but found %s", tokstr(cur(p))); return; }
    adv(p);
    hi = parse_const_expr(p);
    if (p->failed) return;
    if (!isfinite(lo) || !isfinite(hi)) { FAIL(p, "range bounds must be finite numbers"); return; }
    if (!(hi > lo)) { FAIL(p, "empty range %g..%g (the first number must be smaller)", lo, hi); return; }
    for (i = first; i < vs->n; i++) { vs->lo[i] = lo; vs->hi[i] = hi; }

    if (is(p, T_COMMA)) { adv(p); continue; }
    break;
  }
}

/* ---- statements --------------------------------------------------------- */

/* Is the '(' at the current position the start of a tuple "(a, b, c)"? */
static int tuple_ahead(const Parser* p) {
  int depth = 0, i;
  for (i = p->pos; p->t[i].type != T_EOF; i++) {
    TokType t = p->t[i].type;
    if (t == T_LP) depth++;
    else if (t == T_RP) { if (--depth == 0) return 0; }
    else if (t == T_COMMA && depth == 1) return 1;
  }
  return 0;
}

static int equation_ahead(const Parser* p) {
  int i;
  for (i = p->pos; p->t[i].type != T_EOF && p->t[i].type != T_NL && p->t[i].type != T_FOR; i++) {
    if (p->t[i].type == T_EQ) return 1;
  }
  return 0;
}

static int make_var_code(Code* c, int slot) {
  c->ins = (Instr*)malloc(sizeof(Instr));
  if (!c->ins) return 0;
  c->ins[0].op = OP_VAR;
  c->ins[0].arg = slot;
  c->ins[0].num = 0;
  c->n = c->cap = 1;
  c->cur = c->depth = 1;
  return 1;
}

static void parse_surface(Parser* p, omm_scene* sc) {
  const Token* first = cur(p);
  const int line = first->line;
  Model m;
  Names names;
  Vars vs;
  int k;

  memset(&m, 0, sizeof m);
  names.n = 0;
  vs.n = 0;
  p->names = &names;

  if (first->type == T_ID && p->t[p->pos + 1].type == T_EQ && !strcmp(first->id, "z")) {
    /* z = f(x, y) */
    m.kind = OMM_EXPLICIT;
    adv(p);
    adv(p);
    p->out = &m.comp[2];
    parse_expr(p);
  } else if (first->type == T_LP && tuple_ahead(p)) {
    /* (x(u,v), y(u,v), z(u,v)) */
    m.kind = OMM_PARAMETRIC;
    adv(p);
    for (k = 0; k < 3 && !p->failed; k++) {
      p->out = &m.comp[k];
      parse_expr(p);
      if (p->failed) break;
      if (k < 2) {
        if (!is(p, T_COMMA)) { FAIL(p, "a parametric surface needs 3 components: (x, y, z)"); break; }
        adv(p);
      }
    }
    if (!p->failed) {
      if (!is(p, T_RP)) FAIL(p, "expected ')' after the third component but found %s", tokstr(cur(p)));
      else adv(p);
    }
  } else if (equation_ahead(p)) {
    FAIL(p, "implicit surfaces (F(x,y,z) = 0) are not supported yet; use z = f(x,y) or (x(u,v), y(u,v), z(u,v))");
  } else {
    FAIL(p, "cannot read this line; expected z = f(x,y), (x(u,v), y(u,v), z(u,v)) or let name = value");
  }

  if (!p->failed) { /* optional 'for' clause, may sit on the next line */
    int q = p->pos;
    while (p->t[q].type == T_NL) q++;
    if (p->t[q].type == T_FOR) { p->pos = q; parse_for(p, &vs); }
  }
  expect_end(p);
  if (p->failed) goto cleanup;

  if (m.kind == OMM_EXPLICIT) {
    if (vs.n == 0) { /* default: x, y in -5..5 */
      strcpy(vs.name[0], "x"); strcpy(vs.name[1], "y");
      vs.lo[0] = vs.lo[1] = -5.0;
      vs.hi[0] = vs.hi[1] = 5.0;
      vs.n = 2;
    } else if (vs.n != 2) {
      omm_set_error(line, "z = f(a,b) needs exactly 2 variables in 'for', e.g. for x,y in -3..3");
      p->failed = 1;
      goto cleanup;
    }
    if (!make_var_code(&m.comp[0], 0) || !make_var_code(&m.comp[1], 1)) {
      omm_set_error(line, "out of memory");
      p->failed = 1;
      goto cleanup;
    }
  } else if (vs.n != 2) {
    omm_set_error(line, vs.n == 1
      ? "curves (one parameter) are not supported yet; use two, e.g. for u in 0..1, v in 0..2*pi"
      : "a parametric surface needs exactly 2 parameters, e.g. for u in 0..1, v in 0..2*pi");
    p->failed = 1;
    goto cleanup;
  }

  for (k = 0; k < 3; k++) {
    if (!resolve(p, &m.comp[k], &names, &vs)) goto cleanup;
  }
  m.lo[0] = vs.lo[0]; m.hi[0] = vs.hi[0];
  m.lo[1] = vs.lo[1]; m.hi[1] = vs.hi[1];

  if (sc->n == OMM_MAX_MODELS) {
    omm_set_error(line, "too many models (max %d)", OMM_MAX_MODELS);
    p->failed = 1;
    goto cleanup;
  }
  if (sc->n == sc->cap) {
    int nc = sc->cap ? sc->cap * 2 : 8;
    Model* nm = (Model*)realloc(sc->models, (size_t)nc * sizeof(Model));
    if (!nm) { omm_set_error(line, "out of memory"); p->failed = 1; goto cleanup; }
    sc->models = nm;
    sc->cap = nc;
  }
  sc->models[sc->n++] = m;
  return;

cleanup:
  omm_model_free(&m);
}

omm_scene* omm_parse(const char* source) {
  Token* toks;
  int nt = 0, ok;
  Parser* p;
  omm_scene* sc;

  omm_clear_error();
  if (!source) { omm_set_error(0, "source is NULL"); return NULL; }
  toks = omm_lex(source, &nt);
  if (!toks) return NULL;

  p = (Parser*)calloc(1, sizeof *p);
  sc = (omm_scene*)calloc(1, sizeof *sc);
  if (!p || !sc) {
    omm_set_error(0, "out of memory");
    free(p); free(sc); free(toks);
    return NULL;
  }
  p->t = toks;

  while (!p->failed && !is(p, T_EOF)) {
    if (is(p, T_NL)) { adv(p); continue; }
    if (is(p, T_LET)) parse_let(p);
    else parse_surface(p, sc);
  }

  ok = !p->failed;
  free(p);
  free(toks);
  if (!ok) { omm_free(sc); return NULL; }
  return sc;
}
