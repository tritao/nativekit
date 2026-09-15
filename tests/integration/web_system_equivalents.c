#include "nativekit.h"
#include "nativekit_joystick.h"
#include "nativekit_system.h"
#include "nativekit_window.h"

#include <assert.h>

int main(void) {
    nk_init_options init = {0};
    init.struct_size = sizeof(init);
    init.api_version = NK_API_VERSION;
    assert(nk_init(&init) == NK_OK);

    const nk_capabilities capabilities = nk_get_capabilities();
    assert((capabilities & NK_CAP_SHELL) != 0);
    assert((capabilities & NK_CAP_SYSTEM_APPEARANCE) != 0);
    assert((capabilities & NK_CAP_NOTIFICATION) != 0);
    assert((capabilities & NK_CAP_JOYSTICK) != 0);

    nk_system_appearance appearance = {0};
    appearance.struct_size = sizeof(appearance);
    assert(nk_system_get_appearance(&appearance) == NK_OK);
    assert(appearance.color_scheme == NK_COLOR_SCHEME_LIGHT ||
           appearance.color_scheme == NK_COLOR_SCHEME_DARK);

    uint32_t joystick_count = 0;
    const nk_result joystick_result = nk_joystick_list(NULL, &joystick_count);
    assert(joystick_result == NK_OK || joystick_result == NK_ERROR_BUFFER_TOO_SMALL);

    assert(nk_shell_open_url("not a URI") == NK_ERROR_INVALID_ARGUMENT);
    nk_shutdown();
    return 0;
}
