#ifndef NATIVEKIT_SOKOL_BACKEND_CONFIG_H
#define NATIVEKIT_SOKOL_BACKEND_CONFIG_H

#if defined(NK_SOKOL_BACKEND_GLCORE)
#define SOKOL_GLCORE
#elif defined(NK_SOKOL_BACKEND_GLES3)
#define SOKOL_GLES3
#elif defined(NK_SOKOL_BACKEND_D3D11)
#define SOKOL_D3D11
#elif defined(NK_SOKOL_BACKEND_METAL)
#define SOKOL_METAL
#else
#error "NativeKit Sokol backend configuration is missing"
#endif

#endif
