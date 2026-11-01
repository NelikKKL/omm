/* omm.c - public API: error buffer, accessors, cleanup */
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "omm_internal.h"

static char g_err[320];

void omm_clear_error(void) { g_err[0] = '\0'; }

void omm_set_error(int line, const char* fmt, ...) {
  char msg[256];
  va_list ap;
  va_start(ap, fmt);
  vsnprintf(msg, sizeof msg, fmt, ap);
  va_end(ap);
  if (line > 0) snprintf(g_err, sizeof g_err, "line %d: %s", line, msg);
  else snprintf(g_err, sizeof g_err, "%s", msg);
}

const char* omm_last_error(void) { return g_err; }

void omm_model_free(Model* m) {
  int k;
  for (k = 0; k < 3; k++) omm_code_free(&m->comp[k]);
  free(m->pos);
  free(m->nor);
  free(m->idx);
  m->pos = NULL;
  m->nor = NULL;
  m->idx = NULL;
  m->nv = m->ni = 0;
}

void omm_free(omm_scene* s) {
  int i;
  if (!s) return;
  for (i = 0; i < s->n; i++) omm_model_free(&s->models[i]);
  free(s->models);
  free(s);
}

static const Model* model_at(const omm_scene* s, int i) {
  return (s && i >= 0 && i < s->n) ? &s->models[i] : NULL;
}

int omm_model_count(const omm_scene* s) { return s ? s->n : 0; }

int omm_build(omm_scene* s, int i, int resolution) {
  Model* m = (Model*)model_at(s, i);
  if (!m) { omm_set_error(0, "no such model: %d", i); return -1; }
  if (omm_mesh_build(m, resolution) != 0) {
    char tmp[320];
    snprintf(tmp, sizeof tmp, "model %d: %.250s", i + 1, g_err); /* say which model failed */
    strcpy(g_err, tmp);
    return -1;
  }
  return 0;
}

int omm_vertex_count(const omm_scene* s, int i) { const Model* m = model_at(s, i); return m ? m->nv : 0; }
int omm_index_count(const omm_scene* s, int i)  { const Model* m = model_at(s, i); return m ? m->ni : 0; }
const float* omm_positions(const omm_scene* s, int i) { const Model* m = model_at(s, i); return m ? m->pos : NULL; }
const float* omm_normals(const omm_scene* s, int i)   { const Model* m = model_at(s, i); return m ? m->nor : NULL; }
const uint32_t* omm_indices(const omm_scene* s, int i) { const Model* m = model_at(s, i); return m ? m->idx : NULL; }
const float* omm_bounds(const omm_scene* s, int i)    { const Model* m = model_at(s, i); return m ? m->bounds : NULL; }
