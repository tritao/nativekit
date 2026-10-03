#ifndef NATIVEKIT_PTY_H
#define NATIVEKIT_PTY_H

#include "nativekit.h"

#ifdef __cplusplus
extern "C" {
#endif

/** A child process attached to a pseudo-terminal. */
typedef uint32_t nk_pty NK_HANDLE NK_HANDLE_DESTROY(nk_pty_close);

/**
 * Spawn an executable by absolute path. argv excludes argv[0]; a null env
 * array inherits the parent environment, while an empty array clears it.
 * cwd may be null. Dimensions are character cells and must be nonzero.
 */
NK_API nk_result NK_CALL nk_pty_spawn(const char *program NK_UTF8,
    const char *const *argv NK_IN_UTF8_ARRAY(argc), uint32_t argc,
    const char *cwd NK_NULLABLE_UTF8,
    const char *const *env NK_IN_UTF8_ARRAY(envc), uint32_t envc,
    uint16_t columns, uint16_t rows, nk_pty *out_pty NK_OUT NK_OWNED);

/** Read at most 64 KiB directly into caller memory. NK_PENDING means no bytes. */
NK_API nk_result NK_CALL nk_pty_read(nk_pty pty, void *buffer,
    uint64_t capacity, uint64_t *out_read NK_OUT);
/** Write at most 64 KiB from caller memory. NK_PENDING means the PTY is full. */
NK_API nk_result NK_CALL nk_pty_write(nk_pty pty, const void *buffer,
    uint64_t size, uint64_t *out_written NK_OUT);
NK_API nk_result NK_CALL nk_pty_resize(nk_pty pty, uint16_t columns, uint16_t rows);
/** Returns NK_PENDING until the child exits; then fills exit code and signal. */
NK_API nk_result NK_CALL nk_pty_exit_status(nk_pty pty, int32_t *out_code NK_OUT,
    int32_t *out_signal NK_OUT);
NK_API nk_result NK_CALL nk_pty_kill(nk_pty pty);
/** Terminates the PTY process group, reaps the direct child, and releases the handle. */
NK_API nk_result NK_CALL nk_pty_close(nk_pty pty);

#ifdef __cplusplus
}
#endif
#endif
