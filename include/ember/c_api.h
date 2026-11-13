#ifndef EMBER_C_API_H
#define EMBER_C_API_H
#include <stdint.h>
#if defined(_WIN32) && defined(EMBER_SHARED)
#ifdef EMBER_BUILD
#define EMBER_API __declspec(dllexport)
#else
#define EMBER_API __declspec(dllimport)
#endif
#else
#define EMBER_API
#endif
#ifdef __cplusplus
extern "C" {
#endif
typedef struct EmberHandle EmberHandle;
/* C ABI v1. All objects belong to a handle; serialized access by the caller.
   Errors never cross the ABI. Returned strings/pixels are borrowed. */
EMBER_API uint32_t ember_abi_version(void);
EMBER_API EmberHandle *ember_create(const char *config_path, const char *backend_override);
EMBER_API void ember_destroy(EmberHandle *);
EMBER_API const char *ember_last_error(void);
EMBER_API int ember_add_box(EmberHandle *, float x, float y, float z, float sx, float sy, float sz);
EMBER_API int ember_set_position(EmberHandle *, int object, float x, float y, float z);
EMBER_API int ember_set_camera(EmberHandle *, float x, float y, float z, float tx, float ty, float tz);
EMBER_API int ember_add_model(EmberHandle *, const char *path, float x, float y, float z);
EMBER_API int ember_play(EmberHandle *, int object, const char *clip, int loop, float fade_seconds);
EMBER_API int ember_seek(EmberHandle *, int object, float seconds);
EMBER_API int ember_pause_animation(EmberHandle *, int object, int paused);
EMBER_API int ember_tick(EmberHandle *, double elapsed_seconds);
EMBER_API int ember_attach(EmberHandle *, int child, int parent, const char *bone);
EMBER_API int ember_render(EmberHandle *);
EMBER_API const uint32_t *ember_pixels(EmberHandle *, int *width, int *height);
#ifdef __cplusplus
}
#endif
#endif
