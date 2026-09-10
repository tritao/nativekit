#ifndef NATIVEKIT_UI_H
#define NATIVEKIT_UI_H

#include <stdint.h>

#if defined(_WIN32)
#if defined(NKUI_BUILDING_LIBRARY)
#define NKUI_API __declspec(dllexport)
#else
#define NKUI_API __declspec(dllimport)
#endif
#else
#define NKUI_API __attribute__((visibility("default")))
#endif

#ifdef __cplusplus
extern "C" {
#endif

#define NKUI_API_VERSION UINT32_C(1)

/* The initial stable seam while the retained-tree transaction ABI is designed. */
NKUI_API uint32_t nkui_api_version(void);

#ifdef __cplusplus
}
#endif

#endif
