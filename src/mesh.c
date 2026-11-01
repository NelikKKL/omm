/* mesh.c - model -> triangle mesh (explicit and parametric surfaces).
 *
 * The (u,v) rectangle is sampled on a (res+1) x (res+1) grid. Normals come
 * from finite differences of the formula itself (not from the grid), so
 * seams of closed surfaces such as the torus shade smoothly. Points where
 * the formula is not finite (sqrt of a negative number, 1/0, ...) are
 * skipped together with every triangle that touches them.
 */
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include "omm_internal.h"

#define COORD_LIMIT 1e6   /* |x|,|y|,|z| above this count as "not finite" */

static int eval_point(const Model* m, double u, double v, double P[3]) {
  double vars[2];
  int k;
  vars[0] = u;
  vars[1] = v;
  for (k = 0; k < 3; k++) {
    double x = omm_eval(&m->comp[k], vars);
    if (!isfinite(x) || fabs(x) > COORD_LIMIT) return 0;
    P[k] = x;
  }
  return 1;
}

/* dP/d(axis) at (u,v). Tries a central, then forward, then backward stencil
 * that stays inside the parameter range. P0 = P(u,v), already known. */
static int deriv(const Model* m, int axis, double u, double v, const double P0[3], double out[3]) {
  static const double ST[3][2] = { { -1.0, 1.0 }, { 0.0, 1.0 }, { -1.0, 0.0 } };
  const double lo = m->lo[axis], hi = m->hi[axis];
  const double h = (hi - lo) * 1e-4;
  const double t = axis == 0 ? u : v;
  int s, c;

  for (s = 0; s < 3; s++) {
    double a = t + ST[s][0] * h, b = t + ST[s][1] * h;
    double pa[3], pb[3];
    if (a < lo || b > hi) continue;
    if (ST[s][0] == 0.0) memcpy(pa, P0, sizeof pa);
    else if (!(axis == 0 ? eval_point(m, a, v, pa) : eval_point(m, u, a, pa))) continue;
    if (ST[s][1] == 0.0) memcpy(pb, P0, sizeof pb);
    else if (!(axis == 0 ? eval_point(m, b, v, pb) : eval_point(m, u, b, pb))) continue;
    for (c = 0; c < 3; c++) out[c] = (pb[c] - pa[c]) / (b - a);
    return 1;
  }
  return 0;
}

/* Unit normal = (dP/du x dP/dv) / |...|. Returns 0 at degenerate points. */
static int normal_at(const Model* m, double u, double v, const double P0[3], double n[3]) {
  double Pu[3], Pv[3], len, su, sv;
  if (!deriv(m, 0, u, v, P0, Pu) || !deriv(m, 1, u, v, P0, Pv)) return 0;
  n[0] = Pu[1] * Pv[2] - Pu[2] * Pv[1];
  n[1] = Pu[2] * Pv[0] - Pu[0] * Pv[2];
  n[2] = Pu[0] * Pv[1] - Pu[1] * Pv[0];
  len = sqrt(n[0] * n[0] + n[1] * n[1] + n[2] * n[2]);
  su = sqrt(Pu[0] * Pu[0] + Pu[1] * Pu[1] + Pu[2] * Pu[2]);
  sv = sqrt(Pv[0] * Pv[0] + Pv[1] * Pv[1] + Pv[2] * Pv[2]);
  if (!(len > 1e-9 * su * sv)) return 0;
  n[0] /= len; n[1] /= len; n[2] /= len;
  return 1;
}

/* Normal for a grid vertex. At poles (sphere) the cross product vanishes, so
 * look a hair towards the middle of the domain; as a last resort use +z. */
static void vertex_normal(const Model* m, double u, double v, const double P[3], float out[3]) {
  double n[3];
  if (!normal_at(m, u, v, P, n)) {
    double du = 1e-3 * (m->hi[0] - m->lo[0]), dv = 1e-3 * (m->hi[1] - m->lo[1]);
    double u1 = u + (u < 0.5 * (m->lo[0] + m->hi[0]) ? du : -du);
    double v1 = v + (v < 0.5 * (m->lo[1] + m->hi[1]) ? dv : -dv);
    double P1[3];
    if (!(eval_point(m, u1, v1, P1) && normal_at(m, u1, v1, P1, n))) {
      n[0] = 0.0; n[1] = 0.0; n[2] = 1.0;
    }
  }
  out[0] = (float)n[0];
  out[1] = (float)n[1];
  out[2] = (float)n[2];
}

int omm_mesh_build(Model* m, int res) {
  const int n1 = res + 1;
  size_t nv, nidx, ncell;
  unsigned char* ok = NULL;
  int i, j, c, any = 0, ni = 0;
  double ur, vr;
  float bmin[3], bmax[3];

  if (res < 1 || res > OMM_MAX_RES) {
    omm_set_error(0, "resolution must be between 1 and %d", OMM_MAX_RES);
    return -1;
  }

  free(m->pos); free(m->nor); free(m->idx);
  m->pos = NULL; m->nor = NULL; m->idx = NULL;
  m->nv = m->ni = 0;

  nv = (size_t)n1 * (size_t)n1;
  ncell = (size_t)res * (size_t)res;
  nidx = ncell * 6;
  m->pos = (float*)malloc(nv * 3 * sizeof(float));
  m->nor = (float*)malloc(nv * 3 * sizeof(float));
  m->idx = (uint32_t*)malloc(nidx * sizeof(uint32_t));
  ok = (unsigned char*)malloc(nv);
  if (!m->pos || !m->nor || !m->idx || !ok) {
    free(ok);
    free(m->pos); free(m->nor); free(m->idx);
    m->pos = NULL; m->nor = NULL; m->idx = NULL;
    omm_set_error(0, "out of memory");
    return -1;
  }

  ur = m->hi[0] - m->lo[0];
  vr = m->hi[1] - m->lo[1];
  bmin[0] = bmin[1] = bmin[2] = 0.0f;
  bmax[0] = bmax[1] = bmax[2] = 0.0f;

  for (j = 0; j <= res; j++) {
    const double v = (j == res) ? m->hi[1] : m->lo[1] + vr * j / res;
    for (i = 0; i <= res; i++) {
      const double u = (i == res) ? m->hi[0] : m->lo[0] + ur * i / res;
      const size_t k = (size_t)j * (size_t)n1 + (size_t)i;
      float* pp = m->pos + 3 * k;
      float* nn = m->nor + 3 * k;
      double P[3];

      if (eval_point(m, u, v, P)) {
        ok[k] = 1;
        for (c = 0; c < 3; c++) {
          pp[c] = (float)P[c];
          if (!any || pp[c] < bmin[c]) bmin[c] = pp[c];
          if (!any || pp[c] > bmax[c]) bmax[c] = pp[c];
        }
        any = 1;
        vertex_normal(m, u, v, P, nn);
      } else {
        ok[k] = 0;
        pp[0] = pp[1] = pp[2] = 0.0f;
        nn[0] = 0.0f; nn[1] = 0.0f; nn[2] = 1.0f;
      }
    }
  }

  if (!any) {
    free(ok);
    free(m->pos); free(m->nor); free(m->idx);
    m->pos = NULL; m->nor = NULL; m->idx = NULL;
    omm_set_error(0, "the formula has no finite values in the given range");
    return -1;
  }

  /* Two triangles per cell. With a=(i,j) b=(i+1,j) c=(i,j+1) d=(i+1,j+1) the
   * winding (a,b,d) (a,d,c) matches the normal dP/du x dP/dv. */
  for (j = 0; j < res; j++) {
    for (i = 0; i < res; i++) {
      const uint32_t a = (uint32_t)(j * n1 + i);
      const uint32_t b = a + 1;
      const uint32_t cc = a + (uint32_t)n1;
      const uint32_t d = cc + 1;
      if (ok[a] && ok[b] && ok[d]) { m->idx[ni++] = a; m->idx[ni++] = b; m->idx[ni++] = d; }
      if (ok[a] && ok[d] && ok[cc]) { m->idx[ni++] = a; m->idx[ni++] = d; m->idx[ni++] = cc; }
    }
  }

  for (c = 0; c < 3; c++) { m->bounds[c] = bmin[c]; m->bounds[3 + c] = bmax[c]; }
  m->nv = (int)nv;
  m->ni = ni;
  free(ok);
  return 0;
}
