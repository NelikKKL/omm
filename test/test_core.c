/* test_core.c - black-box tests of the C core through the public API.
 * Build and run with `make test-native` (AddressSanitizer + UBSan). */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "omm.h"

static int g_fail = 0, g_checks = 0;

#define CHECK(cond, ...) do { \
    g_checks++; \
    if (!(cond)) { g_fail++; printf("  FAIL %s:%d: ", __FILE__, __LINE__); printf(__VA_ARGS__); printf("\n"); } \
  } while (0)

static int near(double a, double b, double eps) { return fabs(a - b) <= eps * (1.0 + fabs(b)); }

/* Value of z at grid vertex (ix, iy) of "z = <expr>  for x,y in -1..1" built with res=2,
 * i.e. x = -1 + ix, y = -1 + iy for ix, iy in 0..2. */
static double z_at(const char* expr, int ix, int iy) {
  char src[512];
  omm_scene* s;
  double z;
  snprintf(src, sizeof src, "z = %s\n  for x,y in -1..1", expr);
  s = omm_parse(src);
  if (!s) { printf("  parse failed for '%s': %s\n", expr, omm_last_error()); g_fail++; return NAN; }
  if (omm_build(s, 0, 2) != 0) { z = NAN; }
  else z = omm_positions(s, 0)[3 * (iy * 3 + ix) + 2];
  omm_free(s);
  return z;
}

static void expr_eq(const char* expr, double want) {
  double got = z_at(expr, 1, 1); /* x = 0, y = 0 */
  CHECK(near(got, want, 1e-6), "'%s' = %.9g, expected %.9g", expr, got, want);
}

static void expr_eq_at(const char* expr, int ix, int iy, double want) {
  double got = z_at(expr, ix, iy);
  CHECK(near(got, want, 1e-6), "'%s' at (%d,%d) = %.9g, expected %.9g", expr, ix - 1, iy - 1, got, want);
}

static void must_fail(const char* src, const char* needle) {
  omm_scene* s = omm_parse(src);
  if (s) {
    CHECK(0, "expected an error for: %s", src);
    omm_free(s);
    return;
  }
  CHECK(strstr(omm_last_error(), needle) != NULL, "error for [%s] was \"%s\", expected to contain \"%s\"",
        src, omm_last_error(), needle);
}

static void test_arithmetic(void) {
  puts("arithmetic and precedence");
  expr_eq("1+2*3", 7);
  expr_eq("(1+2)*3", 9);
  expr_eq("2^3^2", 512);         /* right-associative */
  expr_eq("-2^2", -4);           /* unary minus binds weaker than ^ */
  expr_eq("2^-1", 0.5);
  expr_eq("10/4", 2.5);
  expr_eq("10-3-2", 5);          /* left-associative */
  expr_eq("2*-3", -6);
  expr_eq("--3", 3);
  expr_eq("+3", 3);
  expr_eq("1e3 + .5", 1000.5);
  expr_eq("2**3", 8);
}

static void test_implicit_mul(void) {
  puts("implicit multiplication");
  expr_eq("2pi", 2 * 3.14159265358979);
  expr_eq("2(3+1)", 8);
  expr_eq("(1+1)(2+1)", 6);
  expr_eq("sin(0)cos(0)", 0);
  expr_eq_at("3x", 2, 1, 3);                 /* x = 1 */
  expr_eq_at("x(x+1)", 2, 1, 2);             /* x = 1 */
  expr_eq_at("2x^2", 0, 1, 2);               /* x = -1: 2*(x^2) */
  expr_eq_at("1/2x", 2, 1, 0.5);             /* (1/2)*x */
  expr_eq_at("x y", 2, 2, 1);                /* x = 1, y = 1 */
}

static void test_functions_constants(void) {
  puts("functions and constants");
  expr_eq("sqrt(16)", 4);
  expr_eq("atan2(1,1)", 3.14159265358979 / 4);
  expr_eq("max(2,3)", 3);
  expr_eq("min(2,3)", 2);
  expr_eq("mod(-1,3)", 2);
  expr_eq("hypot(3,4)", 5);
  expr_eq("ln(e)", 1);
  expr_eq("log10(1000)", 3);
  expr_eq("abs(-5)", 5);
  expr_eq("sign(-9)", -1);
  expr_eq("floor(-0.5)", -1);
  expr_eq("pow(2,10)", 1024);
  expr_eq("tau", 2 * 3.14159265358979);
  expr_eq("cos(pi)", -1);
}

static void test_let_unicode(void) {
  omm_scene* s;
  puts("let constants, unicode, line endings");
  s = omm_parse("let R = 2, r = 0.5\nlet k = R*r\nz = k*10  for x,y in 0..1\n");
  CHECK(s != NULL, "let: %s", omm_last_error());
  if (s) {
    CHECK(omm_build(s, 0, 1) == 0, "build");
    CHECK(near(omm_positions(s, 0)[2], 10.0, 1e-6), "let chain gives %g", omm_positions(s, 0)[2]);
    omm_free(s);
  }
  expr_eq("\xCF\x80", 3.14159265358979);                 /* pi */
  expr_eq("2\xC3\x97" "3", 6);                           /* times */
  expr_eq("6\xC3\xB7" "4", 1.5);                         /* divide */
  expr_eq("\xE2\x88\x92" "1", -1);                       /* minus sign */
  expr_eq_at("x\xC2\xB2", 0, 1, 1);                      /* x^2 at x = -1 */
  expr_eq_at("x\xC2\xB3", 0, 1, -1);                     /* x^3 at x = -1 */

  s = omm_parse("\xEF\xBB\xBF# bom + CRLF\r\nz = x*y\r\n  for x,y in 0..2\r\n");
  CHECK(s != NULL && omm_model_count(s) == 1, "BOM/CRLF: %s", omm_last_error());
  omm_free(s);
}

static void test_scene_layout(void) {
  omm_scene* s;
  puts("scene layout");
  s = omm_parse(
      "# three models\n\n"
      "z = x*y          for x,y in -1..1   # saddle\n"
      "\n\n"
      "(sin(v)*cos(u),\n   sin(v)*sin(u),\n   cos(v))\n"
      "   for u in 0..2*pi,\n       v in 0..pi\n"
      "z = sin(x)*cos(y)\n");   /* no 'for' -> default -5..5 */
  CHECK(s != NULL, "parse: %s", omm_last_error());
  if (!s) return;
  CHECK(omm_model_count(s) == 3, "model count %d", omm_model_count(s));
  CHECK(omm_build(s, 2, 8) == 0, "default range model builds");
  if (omm_model_count(s) == 3) {
    const float* b = omm_bounds(s, 2);
    CHECK(near(b[0], -5, 1e-6) && near(b[3], 5, 1e-6), "default x range is %g..%g", b[0], b[3]);
  }
  omm_free(s);

  s = omm_parse("");
  CHECK(s != NULL && omm_model_count(s) == 0, "empty source is an empty scene");
  omm_free(s);
  s = omm_parse("# only a comment\n\n");
  CHECK(s != NULL && omm_model_count(s) == 0, "comment-only source");
  omm_free(s);

  /* a line that ends with an operator or comma continues on the next line */
  s = omm_parse("z = x +\n    y *\n    2\n  for x,\n      y in 0..1");
  CHECK(s != NULL && omm_model_count(s) == 1, "continuation: %s", omm_last_error());
  if (s) {
    CHECK(omm_build(s, 0, 1) == 0, "build");
    CHECK(near(omm_positions(s, 0)[3 * 3 + 2], 3.0, 1e-6), "x + y*2 at (1,1) = %g", omm_positions(s, 0)[3 * 3 + 2]);
    omm_free(s);
  }
  s = omm_parse("let a = 1,\n    b = 2\nz = a + b  for x,y in 0..1");
  CHECK(s != NULL, "let continuation: %s", omm_last_error());
  omm_free(s);
}

static void test_mesh_explicit(void) {
  omm_scene* s;
  int i, nv, ni;
  const uint32_t* idx;
  const float* nor;
  const float* b;
  puts("explicit mesh");
  s = omm_parse("z = x^2 - y^2   for x,y in -1..1");
  CHECK(s != NULL, "%s", omm_last_error());
  if (!s) return;
  CHECK(omm_build(s, 0, 4) == 0, "build");
  nv = omm_vertex_count(s, 0);
  ni = omm_index_count(s, 0);
  CHECK(nv == 25 && ni == 96, "nv=%d ni=%d (want 25, 96)", nv, ni);
  idx = omm_indices(s, 0);
  nor = omm_normals(s, 0);
  for (i = 0; i < ni; i++) if (idx[i] >= (uint32_t)nv) { CHECK(0, "index %d out of range", i); break; }
  for (i = 0; i < nv; i++) {
    double l = sqrt(nor[3*i]*nor[3*i] + nor[3*i+1]*nor[3*i+1] + nor[3*i+2]*nor[3*i+2]);
    if (!near(l, 1.0, 1e-4)) { CHECK(0, "normal %d has length %g", i, l); break; }
  }
  CHECK(nor[2] > 0, "explicit surfaces face up (+z), got %g", nor[2]);
  b = omm_bounds(s, 0);
  CHECK(near(b[2], -1, 1e-6) && near(b[5], 1, 1e-6), "z bounds %g..%g", b[2], b[5]);
  /* z = x^2 - y^2 at the corner (-1,-1) is 0 and its gradient is (-2, 2): normal ~ (2,-2,1)/3 */
  CHECK(near(nor[0], 2.0 / 3.0, 1e-3) && near(nor[1], -2.0 / 3.0, 1e-3) && near(nor[2], 1.0 / 3.0, 1e-3),
        "corner normal (%g, %g, %g)", nor[0], nor[1], nor[2]);

  CHECK(omm_build(s, 0, 1) == 0 && omm_vertex_count(s, 0) == 4 && omm_index_count(s, 0) == 6, "res=1");
  CHECK(omm_build(s, 0, 64) == 0 && omm_vertex_count(s, 0) == 65 * 65, "rebuild at res=64");
  CHECK(omm_build(s, 0, 0) != 0, "res=0 must fail");
  CHECK(omm_build(s, 0, 513) != 0, "res=513 must fail");
  CHECK(strstr(omm_last_error(), "resolution") != NULL, "message mentions resolution: %s", omm_last_error());
  CHECK(omm_build(s, 5, 8) != 0, "bad model index must fail");
  omm_free(s);
}

static void test_mesh_parametric(void) {
  omm_scene* s;
  int i, nv;
  const float *p, *n, *b;
  puts("parametric meshes");

  /* unit sphere */
  s = omm_parse("(sin(v)*cos(u), sin(v)*sin(u), cos(v)) for u in 0..2*pi, v in 0..pi");
  CHECK(s != NULL, "%s", omm_last_error());
  if (!s) return;
  CHECK(omm_build(s, 0, 32) == 0, "build sphere");
  nv = omm_vertex_count(s, 0);
  p = omm_positions(s, 0);
  n = omm_normals(s, 0);
  for (i = 0; i < nv; i++) {
    double r = sqrt(p[3*i]*p[3*i] + p[3*i+1]*p[3*i+1] + p[3*i+2]*p[3*i+2]);
    double l = sqrt(n[3*i]*n[3*i] + n[3*i+1]*n[3*i+1] + n[3*i+2]*n[3*i+2]);
    double d = fabs(p[3*i]*n[3*i] + p[3*i+1]*n[3*i+1] + p[3*i+2]*n[3*i+2]);
    if (!near(r, 1.0, 1e-5)) { CHECK(0, "vertex %d radius %g", i, r); break; }
    if (!near(l, 1.0, 1e-4)) { CHECK(0, "normal %d length %g (poles must not be NaN)", i, l); break; }
    if (!(d > 0.99)) { CHECK(0, "normal %d is not radial (|dot| = %g)", i, d); break; }
  }
  omm_free(s);

  /* torus R=2 r=0.5: x,y in [-2.5,2.5], z in [-0.5,0.5] */
  s = omm_parse("let R = 2, r = 0.5\n"
                "((R + r*cos(v))*cos(u), (R + r*cos(v))*sin(u), r*sin(v))\n"
                "  for u in 0..2*pi, v in 0..2*pi");
  CHECK(s != NULL, "%s", omm_last_error());
  if (!s) return;
  CHECK(omm_build(s, 0, 64) == 0, "build torus");
  b = omm_bounds(s, 0);
  CHECK(near(b[0], -2.5, 1e-4) && near(b[3], 2.5, 1e-4) && near(b[2], -0.5, 1e-4) && near(b[5], 0.5, 1e-4),
        "torus bounds x %g..%g z %g..%g", b[0], b[3], b[2], b[5]);
  /* seam: vertex (i=0,j) and (i=res,j) coincide and must shade the same */
  {
    int res = 64, j = 17;
    const float* nn = omm_normals(s, 0);
    int a = j * (res + 1), c = a + res;
    CHECK(near(nn[3*a], nn[3*c], 1e-3) && near(nn[3*a+1], nn[3*c+1], 1e-3) && near(nn[3*a+2], nn[3*c+2], 1e-3),
          "torus seam normals differ: (%g,%g,%g) vs (%g,%g,%g)",
          nn[3*a], nn[3*a+1], nn[3*a+2], nn[3*c], nn[3*c+1], nn[3*c+2]);
  }
  omm_free(s);
}

static void test_invalid_points(void) {
  omm_scene* s;
  int i, ni, nv;
  const uint32_t* idx;
  const float* p;
  puts("non-finite points are skipped");
  s = omm_parse("z = sqrt(x)  for x,y in -1..1");
  CHECK(s != NULL, "%s", omm_last_error());
  if (!s) return;
  CHECK(omm_build(s, 0, 8) == 0, "build");
  ni = omm_index_count(s, 0);
  nv = omm_vertex_count(s, 0);
  idx = omm_indices(s, 0);
  p = omm_positions(s, 0);
  CHECK(ni > 0 && ni < 8 * 8 * 6, "ni=%d: some, but not all, triangles", ni);
  for (i = 0; i < ni; i++) {
    uint32_t k = idx[i];
    int ix = (int)(k % 9);               /* x = -1 + ix/4 must be >= 0 */
    if (k >= (uint32_t)nv || ix < 4 || !isfinite(p[3 * k + 2])) { CHECK(0, "triangle uses an invalid vertex (%u)", k); break; }
  }
  omm_free(s);

  s = omm_parse("z = sqrt(-1 - x*x)  for x,y in -1..1");
  CHECK(s != NULL, "parse ok");
  if (s) {
    CHECK(omm_build(s, 0, 4) != 0, "a model with no finite points must fail");
    CHECK(strstr(omm_last_error(), "model 1") != NULL && strstr(omm_last_error(), "finite") != NULL,
          "message: %s", omm_last_error());
    omm_free(s);
  }
}

static void test_errors(void) {
  char deep[2048];
  int i;
  puts("error messages");
  must_fail("z = sin(", "unclosed '('");
  must_fail("z = sin()", "expected a value");
  must_fail("z = foo(x)  for x,y in 0..1", "unknown name 'foo'");
  must_fail("z = q  for x,y in 0..1", "unknown name 'q'");
  must_fail("z = x +  for x,y in 0..1", "expected a value");
  must_fail("(1, 2)  for u in 0..1, v in 0..1", "3 components");
  must_fail("(u, v, 0)  for u in 0..1", "curves");
  must_fail("(u, v, 0)  for u,v,w in 0..1", "exactly 2 parameters");
  must_fail("z = x  for x in 0..1", "exactly 2 variables");
  must_fail("z = x*y  for x,y in 1..0", "empty range");
  must_fail("z = x*y  for x,y in 0..0", "empty range");
  must_fail("z = x*y  for x,y in 0 1", "expected '..'");
  must_fail("z = x*y  for x,x in 0..1", "declared twice");
  must_fail("z = x*y  for sin,y in 0..1", "function name");
  must_fail("x^2 + y^2 + z^2 = 1  for x,y,z in -2..2", "implicit");
  must_fail("hello world 12 +", "cannot read");
  must_fail("let pi = 3", "built in");
  must_fail("let sin = 3", "built in");
  must_fail("let a 3", "expected '='");
  must_fail("let a = 1/0", "finite");
  must_fail("z = sin x", "needs parentheses");
  must_fail("z = sin(1,2)", "1 argument");
  must_fail("z = atan2(1)", "2 arguments");
  must_fail("z = (1 + 2", "unclosed '('");
  must_fail("z = 1 + 2)", "unexpected");
  must_fail("z = x @ y", "unexpected character '@'");
  must_fail("z = 1 for x,y in 0..1 foo", "unknown name 'foo'");
  must_fail("z = abcdefghijklmnopqrstuvwxyz0123456789", "too long");
  must_fail("z = 1\nz = x +\n", "line 2");
  must_fail("z = 1\n\n\nz = ?", "line 4");

  for (i = 0; i < 1000; i++) deep[i] = '(';
  deep[1000] = '\0';
  must_fail(deep, "unclosed");
  strcpy(deep, "z = ");
  for (i = 0; i < 300; i++) strcat(deep, "(");
  strcat(deep, "1");
  for (i = 0; i < 300; i++) strcat(deep, ")");
  must_fail(deep, "nested too deeply");
  strcpy(deep, "z = ");
  for (i = 0; i < 300; i++) strcat(deep, "-");
  strcat(deep, "1");
  must_fail(deep, "nested too deeply");
  strcpy(deep, "z = 1");
  for (i = 0; i < 300; i++) strcat(deep, "^2");
  must_fail(deep, "nested too deeply");

  /* stack depth of the evaluator: 1+(1+(1+(... 40 levels fits, 70 does not */
  strcpy(deep, "z = ");
  for (i = 0; i < 70; i++) strcat(deep, "1+(");
  strcat(deep, "1");
  for (i = 0; i < 70; i++) strcat(deep, ")");
  must_fail(deep, "nested too deeply");

  CHECK(omm_parse(NULL) == NULL, "NULL source");
}

static void test_null_safety(void) {
  puts("NULL safety");
  omm_free(NULL);
  CHECK(omm_model_count(NULL) == 0, "count");
  CHECK(omm_vertex_count(NULL, 0) == 0 && omm_index_count(NULL, 0) == 0, "counts");
  CHECK(omm_positions(NULL, 0) == NULL && omm_normals(NULL, 0) == NULL && omm_indices(NULL, 0) == NULL, "ptrs");
  CHECK(omm_bounds(NULL, 0) == NULL, "bounds");
  CHECK(omm_build(NULL, 0, 4) != 0, "build");
}

/* Deterministic fuzz: token soup plus mutations of valid sources must never crash,
 * hang or leak (this is what the ASan/UBSan build is for). */
static unsigned long g_seed = 12345UL;
static unsigned long rnd(void) { g_seed = g_seed * 1103515245UL + 12345UL; return g_seed >> 8; }

static void fuzz_one(const char* src, int* parsed, int* built) {
  omm_scene* s = omm_parse(src);
  if (s) {
    int m;
    (*parsed)++;
    for (m = 0; m < omm_model_count(s); m++) if (omm_build(s, m, 6) == 0) (*built)++;
    omm_free(s);
  }
}

static void test_fuzz(void) {
  static const char* PIECES[] = {
    "z", "x", "y", "u", "v", "t", "pi", "e", "sin", "cos", "sqrt", "atan2", "pow", "ln",
    "0", "1", "2.5", "1e3", ".5", "100",
    "+", "-", "*", "/", "^", "**", "(", ")", ",", "=", "..", "for", "in", "let",
    " ", " ", "\n", "\n  ", "# c\n", "\xCF\x80", "\xC2\xB2", "\xC3\x97", "@", "."
  };
  static const char* BASES[] = {
    "z = sin(x)*cos(y)  for x,y in -3..3",
    "let R = 2, r = 0.5\n((R + r*cos(v))*cos(u), (R + r*cos(v))*sin(u), r*sin(v))\n  for u in 0..2*pi, v in 0..2*pi",
    "(sin(v)*cos(u), sin(v)*sin(u), cos(v)) for u in 0..2pi, v in 0..pi",
    "z = sqrt(1 - x^2 - y^2)  for x,y in -1..1\nz = x*y"
  };
  const int NP = (int)(sizeof PIECES / sizeof PIECES[0]);
  const int NB = (int)(sizeof BASES / sizeof BASES[0]);
  int iter, k, parsed = 0, built = 0;
  char src[512];
  puts("fuzz (token soup + mutations)");

  for (iter = 0; iter < 10000; iter++) {          /* pure token soup */
    int len = 1 + (int)(rnd() % 24);
    src[0] = '\0';
    for (k = 0; k < len; k++) strcat(src, PIECES[rnd() % (unsigned long)NP]);
    fuzz_one(src, &parsed, &built);
  }
  for (iter = 0; iter < 20000; iter++) {          /* mutated valid sources */
    int nm = 1 + (int)(rnd() % 3);
    strcpy(src, BASES[rnd() % (unsigned long)NB]);
    for (k = 0; k < nm; k++) {
      size_t len = strlen(src), pos = (size_t)(rnd() % (len + 1));
      switch (rnd() % 3) {
        case 0: {                                  /* insert a piece */
          const char* piece = PIECES[rnd() % (unsigned long)NP];
          size_t pl = strlen(piece);
          if (len + pl < sizeof src) { memmove(src + pos + pl, src + pos, len - pos + 1); memcpy(src + pos, piece, pl); }
          break;
        }
        case 1: {                                  /* delete 1..4 bytes */
          size_t dl = 1 + (size_t)(rnd() % 4);
          if (pos + dl > len) dl = len - pos;
          memmove(src + pos, src + pos + dl, len - pos - dl + 1);
          break;
        }
        default:                                   /* replace a byte */
          if (pos < len) src[pos] = (char)(32 + rnd() % 95);
          break;
      }
    }
    fuzz_one(src, &parsed, &built);
  }
  printf("  %d sources parsed, %d meshes built\n", parsed, built);
  CHECK(parsed > 500 && built > 500, "fuzzer should reach the mesher (parsed %d, built %d)", parsed, built);
}

int main(void) {
  test_arithmetic();
  test_implicit_mul();
  test_functions_constants();
  test_let_unicode();
  test_scene_layout();
  test_mesh_explicit();
  test_mesh_parametric();
  test_invalid_points();
  test_errors();
  test_null_safety();
  test_fuzz();
  printf("\n%d checks, %d failed\n", g_checks, g_fail);
  return g_fail ? 1 : 0;
}
