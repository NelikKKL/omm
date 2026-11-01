/* lexer.c - .omm text -> tokens */
#include <ctype.h>
#include <stdlib.h>
#include <string.h>
#include "omm_internal.h"

typedef struct { Token* v; int n, cap; } TokList;

static int push(TokList* L, TokType type, double num, const char* id, int line) {
  Token* k;
  if (L->n == L->cap) {
    int nc = L->cap ? L->cap * 2 : 64;
    Token* nv = (Token*)realloc(L->v, (size_t)nc * sizeof(Token));
    if (!nv) { omm_set_error(0, "out of memory"); return 0; }
    L->v = nv;
    L->cap = nc;
  }
  k = &L->v[L->n++];
  k->type = type;
  k->num = num;
  k->line = line;
  k->id[0] = '\0';
  if (id) {
    strncpy(k->id, id, OMM_MAX_ID - 1);
    k->id[OMM_MAX_ID - 1] = '\0';
  }
  return 1;
}

/* tokens after which a line break does not end the statement */
static int continues(TokType t) {
  switch (t) {
    case T_PLUS: case T_MINUS: case T_STAR: case T_SLASH: case T_CARET:
    case T_COMMA: case T_EQ: case T_DOTDOT: case T_IN: case T_FOR: case T_LET:
      return 1;
    default:
      return 0;
  }
}

#define EMIT(t, len) \
  do { if (!push(&L, (t), 0, NULL, line)) goto fail; p += (len); } while (0)

Token* omm_lex(const char* src, int* count) {
  TokList L = { NULL, 0, 0 };
  const unsigned char* p = (const unsigned char*)src;
  int line = 1, depth = 0, open_line = 0;

  if (p[0] == 0xEF && p[1] == 0xBB && p[2] == 0xBF) p += 3; /* UTF-8 BOM */

  while (*p) {
    unsigned char c = *p;

    /* ---- whitespace, comments, newlines ---- */
    if (c == '\n') {
      /* A newline ends a statement, except inside parentheses and after an
       * operator or comma (the formula simply continues). Blank lines collapse. */
      if (depth == 0 && L.n > 0 && L.v[L.n - 1].type != T_NL && !continues(L.v[L.n - 1].type)) {
        if (!push(&L, T_NL, 0, NULL, line)) goto fail;
      }
      line++;
      p++;
      continue;
    }
    if (c == ' ' || c == '\t' || c == '\r' || c == '\f' || c == '\v') { p++; continue; }
    if (c == 0xC2 && p[1] == 0xA0) { p += 2; continue; } /* no-break space */
    if (c == '#') { while (*p && *p != '\n') p++; continue; }

    /* ---- numbers: 12, 3.5, .5, 1e-3 (but "0..2" is 0 .. 2) ---- */
    if (isdigit(c) || (c == '.' && isdigit(p[1]))) {
      const unsigned char* q = p;
      char buf[64];
      size_t len;
      while (isdigit(*q)) q++;
      if (*q == '.' && q[1] != '.') {
        q++;
        while (isdigit(*q)) q++;
      }
      if ((*q == 'e' || *q == 'E') &&
          (isdigit(q[1]) || ((q[1] == '+' || q[1] == '-') && isdigit(q[2])))) {
        q += 2;
        while (isdigit(*q)) q++;
      }
      len = (size_t)(q - p);
      if (len >= sizeof buf) { omm_set_error(line, "number is too long"); goto fail; }
      memcpy(buf, p, len);
      buf[len] = '\0';
      if (!push(&L, T_NUM, strtod(buf, NULL), NULL, line)) goto fail;
      p = q;
      continue;
    }

    /* ---- names and keywords ---- */
    if (isalpha(c) || c == '_') {
      const unsigned char* q = p;
      char id[OMM_MAX_ID];
      size_t len;
      TokType tt = T_ID;
      while (isalnum(*q) || *q == '_') q++;
      len = (size_t)(q - p);
      if (len >= OMM_MAX_ID) {
        omm_set_error(line, "name is too long (max %d characters)", OMM_MAX_ID - 1);
        goto fail;
      }
      memcpy(id, p, len);
      id[len] = '\0';
      if (!strcmp(id, "for")) tt = T_FOR;
      else if (!strcmp(id, "in")) tt = T_IN;
      else if (!strcmp(id, "let")) tt = T_LET;
      if (!push(&L, tt, 0, id, line)) goto fail;
      p = q;
      continue;
    }

    /* ---- math symbols copied from papers and docs ---- */
    if (c == 0xCF && p[1] == 0x80) {                    /* pi */
      if (!push(&L, T_ID, 0, "pi", line)) goto fail;
      p += 2;
      continue;
    }
    if (c == 0xC3 && p[1] == 0x97) { EMIT(T_STAR, 2); continue; }   /* x (times) */
    if (c == 0xC2 && p[1] == 0xB7) { EMIT(T_STAR, 2); continue; }   /* middle dot */
    if (c == 0xC3 && p[1] == 0xB7) { EMIT(T_SLASH, 2); continue; }  /* division sign */
    if (c == 0xE2 && p[1] == 0x88 && p[2] == 0x92) { EMIT(T_MINUS, 3); continue; } /* minus sign */
    if (c == 0xC2 && (p[1] == 0xB2 || p[1] == 0xB3)) {  /* superscript 2 and 3 */
      double e = (p[1] == 0xB2) ? 2.0 : 3.0;
      if (!push(&L, T_CARET, 0, NULL, line) || !push(&L, T_NUM, e, NULL, line)) goto fail;
      p += 2;
      continue;
    }

    /* ---- operators ---- */
    switch (c) {
      case '+': EMIT(T_PLUS, 1); continue;
      case '-': EMIT(T_MINUS, 1); continue;
      case '/': EMIT(T_SLASH, 1); continue;
      case '^': EMIT(T_CARET, 1); continue;
      case '=': EMIT(T_EQ, 1); continue;
      case ',': EMIT(T_COMMA, 1); continue;
      case '*':
        if (p[1] == '*') EMIT(T_CARET, 2); /* ** is a power too */
        else EMIT(T_STAR, 1);
        continue;
      case '(':
        if (depth++ == 0) open_line = line;
        EMIT(T_LP, 1);
        continue;
      case ')':
        if (depth > 0) depth--;
        EMIT(T_RP, 1);
        continue;
      case '.':
        if (p[1] == '.') { EMIT(T_DOTDOT, 2); continue; }
        break;
      default:
        break;
    }

    if (c < 0x80) omm_set_error(line, "unexpected character '%c'", c);
    else omm_set_error(line, "unexpected non-ASCII character (allowed: \xCF\x80 \xC3\x97 \xC2\xB7 \xC3\xB7 \xE2\x88\x92 \xC2\xB2 \xC2\xB3)");
    goto fail;
  }

  if (depth > 0) { omm_set_error(open_line, "unclosed '('"); goto fail; }
  if (!push(&L, T_EOF, 0, NULL, L.n > 0 ? L.v[L.n - 1].line : 1)) goto fail;
  *count = L.n;
  return L.v;

fail:
  free(L.v);
  return NULL;
}
