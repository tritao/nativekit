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

/*
 * The vendored Sokol GL loader has no transfer/readback entry points. Keep
 * these functions private to the NativeKit runtime extension; they are only
 * added to Sokol's Win32 function-pointer loader and do not change sg_api.
 */
#if defined(_WIN32) && (defined(NK_SOKOL_BACKEND_GLCORE) || defined(NK_SOKOL_BACKEND_GLES3))
#ifndef SG_GL_FUNCS_EXT
#define SG_GL_FUNCS_EXT                                                                            \
    _SG_XMACRO(glCopyBufferSubData, void,                                                          \
               (GLenum read_target, GLenum write_target, GLintptr read_offset,                     \
                GLintptr write_offset, GLsizeiptr size))                                           \
    _SG_XMACRO(glMapBufferRange, void *,                                                           \
               (GLenum target, GLintptr offset, GLsizeiptr length, GLbitfield access))             \
    _SG_XMACRO(glUnmapBuffer, GLboolean, (GLenum target))                                          \
    _SG_XMACRO(glFenceSync, void *, (GLenum condition, GLbitfield flags))                          \
    _SG_XMACRO(glClientWaitSync, GLenum, (void *sync, GLbitfield flags, GLuint64 timeout))         \
    _SG_XMACRO(glDeleteSync, void, (void *sync))
#endif
#endif

#endif
