#ifndef NATIVEKIT_HAXEON_IMPORT_H
#define NATIVEKIT_HAXEON_IMPORT_H

#include "nativekit.h"
#include "nativekit_monitor.h"
#include "nativekit_window.h"
#include "nativekit_input.h"
#include "nativekit_joystick.h"
#include "nativekit_gamepad.h"
#include "nativekit_graphics.h"
#include "nativekit_resource.h"

/* Binding-only semantic spelling for UTF-8 input; ABI-identical to const char *. */
typedef const char *hxi_utf8;
NK_API nk_result NK_CALL nk_clipboard_set_text(hxi_utf8 text);
NK_API nk_result NK_CALL nk_clipboard_read_text(nk_request_id *out_request NK_OUT);
NK_API nk_result NK_CALL nk_clipboard_read_files(nk_request_id *out_request NK_OUT);
NK_API nk_result NK_CALL nk_webview_eval(nk_handle webview, hxi_utf8 script,
                                         nk_request_id *out_request NK_OUT);

#endif
