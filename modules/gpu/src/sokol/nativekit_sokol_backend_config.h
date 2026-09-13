#ifndef NATIVEKIT_SOKOL_BACKEND_CONFIG_H
#define NATIVEKIT_SOKOL_BACKEND_CONFIG_H

#if defined(NK_SOKOL_BACKEND_GLCORE)
#define SOKOL_GLCORE
#elif defined(NK_SOKOL_BACKEND_GLES3)
#define SOKOL_GLES3
#else
#error "NativeKit Sokol backend configuration is missing"
#endif

#endif
