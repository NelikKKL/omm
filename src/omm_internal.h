/* Internal types shared between the .c files. Not part of the public API. */
#ifndef OMM_INTERNAL_H
#define OMM_INTERNAL_H

#include <stdint.h>
#include "omm.h"

#define OMM_MAX_ID      32    /* identifier length, including the NUL      */
#define OMM_MAX_SYMS    32    /* distinct names inside one formula          */
#define OMM_MAX_VARS    3     /* loop variables in one 'for' clause         */
#define OMM_MAX_CONSTS  64    /* 'let' constants                            */
#define OMM_MAX_MODELS  256
#define OMM_STACK       64    /* VM stack depth                             */
#define OMM_MAX_NEST    64    /* parser nesting limit (keeps C stack small) */
#define OMM_MAX_RES     512

/* ---- errors (omm.c) ---------------------------------------------------- */
void omm_clear_error(void);
void omm_set_error(int line, const char* fmt, ...); /* "line N: msg" if line > 0 */

/* ---- lexer (lexer.c) --------------------------------------------------- */
typedef enum {
  T_EOF, T_NL, T_NUM, T_ID, T_FOR, T_IN, T_LET,
  T_PLUS, T_MINUS, T_STAR, T_SLASH, T_CARET,
  T_LP, T_RP, T_COMMA, T_EQ, T_DOTDOT
} TokType;

typedef struct {
  TokType type;
  double  num;
  char    id[OMM_MAX_ID];
  int     line;
} Token;

/* Returns a malloc'ed array ending with T_EOF, or NULL (error already set). */
Token* omm_lex(const char* src, int* count);

/* ---- bytecode + VM (vm.c) ---------------------------------------------- */
typedef enum {
  OP_CONST, OP_VAR, OP_SYM,
  OP_ADD, OP_SUB, OP_MUL, OP_DIV, OP_POW, OP_NEG,
  OP_F1, OP_F2
} OpCode;

typedef struct { int op; int arg; double num; } Instr;
typedef struct { Instr* ins; int n, cap; int cur, depth; } Code;

double omm_eval(const Code* c, const double* vars);
int    omm_find_f1(const char* name);  /* index into the table, or -1 */
int    omm_find_f2(const char* name);
void   omm_code_free(Code* c);

/* ---- model + mesher (omm.c, mesh.c) ------------------------------------ */
enum { OMM_EXPLICIT = 0, OMM_PARAMETRIC = 1 };

typedef struct {
  int    kind;
  Code   comp[3];      /* x(u,v), y(u,v), z(u,v); explicit: x=u, y=v        */
  double lo[2], hi[2]; /* parameter ranges                                  */
  int    nv, ni;       /* mesh: vertex count, index count                   */
  float* pos;
  float* nor;
  uint32_t* idx;
  float  bounds[6];
} Model;

struct omm_scene { Model* models; int n, cap; };

int  omm_mesh_build(Model* m, int resolution);  /* 0 ok, -1 error (set) */
void omm_model_free(Model* m);

#endif /* OMM_INTERNAL_H */
