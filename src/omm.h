/*
 * omm - turn math formulas into 3D meshes.
 *
 * Public C API. Everything returned by the functions below is owned by the
 * scene and stays valid until the next omm_build() of the same model or
 * omm_free(). Not thread-safe: the last error lives in one global buffer.
 */
#ifndef OMM_H
#define OMM_H

#include <stdint.h>

#ifdef __EMSCRIPTEN__
#include <emscripten.h>
#define OMM_API EMSCRIPTEN_KEEPALIVE
#else
#define OMM_API
#endif

#ifdef __cplusplus
extern "C" {
#endif

typedef struct omm_scene omm_scene;

/* Parse .omm source text. Returns NULL on error (see omm_last_error). */
OMM_API omm_scene*  omm_parse(const char* source);
OMM_API void        omm_free(omm_scene* scene);
OMM_API const char* omm_last_error(void);

OMM_API int omm_model_count(const omm_scene* scene);

/* (Re)build the mesh of one model: `resolution` cells per axis, 1..512.
 * Returns 0 on success, -1 on error (see omm_last_error). */
OMM_API int omm_build(omm_scene* scene, int index, int resolution);

OMM_API int             omm_vertex_count(const omm_scene* scene, int index);
OMM_API int             omm_index_count(const omm_scene* scene, int index);
OMM_API const float*    omm_positions(const omm_scene* scene, int index); /* xyz per vertex */
OMM_API const float*    omm_normals(const omm_scene* scene, int index);   /* xyz per vertex */
OMM_API const uint32_t* omm_indices(const omm_scene* scene, int index);   /* triangle list  */
OMM_API const float*    omm_bounds(const omm_scene* scene, int index);    /* min xyz, max xyz */

#ifdef __cplusplus
}
#endif

#endif /* OMM_H */
