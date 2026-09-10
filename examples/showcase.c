#include "nativekit.h"
#include "nativekit_clipboard.h"
#include "nativekit_dialog.h"
#include "nativekit_input.h"
#include "nativekit_notification.h"
#include "nativekit_system.h"
#include "nativekit_webview.h"
#include "nativekit_window.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if defined(_WIN32)
#include <windows.h>
#else
#include <spawn.h>
#include <signal.h>
#include <time.h>
extern char **environ;
#endif

typedef struct app {
    nk_handle window;
    nk_handle webview;
    nk_handle browser_window;
    nk_handle browser_webview;
    nk_request_id showcase_eval_request;
    int fullscreen;
    int running;
} app;

static const char page[] =
    "<!doctype html><meta charset=utf-8><meta name=viewport content='width=device-width,initial-scale=1'>"
    "<title>NativeKit Lab</title><style>"
    ":root{color-scheme:dark;font:15px system-ui;--bg:#0d1117;--panel:#161b22;--line:#30363d;"
    "--ink:#e6edf3;--muted:#8b949e;--accent:#58a6ff}*{box-sizing:border-box}body{margin:0;"
    "background:var(--bg);color:var(--ink)}header{padding:24px 28px;border-bottom:1px solid var(--line);"
    "background:linear-gradient(120deg,#172033,#161b22)}h1{margin:0 0 5px;font-size:25px}header p{margin:0;"
    "color:var(--muted)}main{display:grid;grid-template-columns:minmax(420px,1fr) minmax(330px,.75fr);"
    "gap:18px;padding:18px}.features{display:grid;grid-template-columns:repeat(auto-fit,minmax(210px,1fr));"
    "gap:14px}.card,.log{border:1px solid var(--line);border-radius:10px;background:var(--panel);padding:16px}"
    "h2{font-size:14px;text-transform:uppercase;letter-spacing:.08em;color:var(--muted);margin:0 0 12px}"
    "button{width:100%;text-align:left;margin:4px 0;padding:9px 11px;border:1px solid var(--line);"
    "border-radius:6px;background:#21262d;color:var(--ink);cursor:pointer}button:hover{border-color:var(--accent);"
    "color:var(--accent)}.log{min-height:480px;overflow:auto}.entry{padding:7px 0;border-bottom:1px solid #21262d;"
    "font:12px ui-monospace,monospace;overflow-wrap:anywhere}.entry b{color:var(--accent)}#caps{color:var(--muted);"
    "font:12px ui-monospace,monospace}@media(max-width:850px){main{grid-template-columns:1fr}.log{min-height:250px}}"
    "</style><header><h1>NativeKit Lab</h1><p>Interactive desktop API showcase · C host + HTML UI</p>"
    "<div id=caps>Loading backend capabilities…</div></header><main><section class=features>"
    "<div class=card><h2>Dialogs</h2><button data-c=dialog.open>Open files…</button>"
    "<button data-c=dialog.save>Save file…</button><button data-c=dialog.directory>Select directory…</button>"
    "<button data-c=dialog.message>Message dialog…</button></div>"
    "<div class=card><h2>Clipboard & drops</h2><button data-c=clipboard.copy>Copy sample text</button>"
    "<button data-c=clipboard.read>Read clipboard text</button><button data-c=drops.toggle>Enable file/text drops</button></div>"
    "<div class=card><h2>Windows</h2><button data-c=window.child>Create utility window</button>"
    "<button data-c=window.attention>Request attention</button><button data-c=window.fullscreen>Toggle fullscreen</button></div>"
    "<div class=card><h2>Web & system</h2><button data-c=browser.open>Open sample browser window</button>"
    "<button data-c=web.eval>Evaluate JavaScript</button><button data-c=system.info>Query system information</button>"
    "<button data-c=shell.open>Open NativeKit website</button></div>"
    "<div class=card><h2>Graphics</h2><button data-c=graphics.opengl>Launch OpenGL triangle</button>"
    "<button data-c=graphics.vulkan>Launch Vulkan triangle</button></div>"
    "<div class=card><h2>Notifications</h2><button data-c=notification.show>Show notification</button></div>"
    "</section><aside class=log><h2>Live event log</h2><div id=events></div></aside></main><script>"
    "const bridge=window.webkit.messageHandlers.nativekit;document.addEventListener('click',e=>{const c=e.target.dataset.c;"
    "if(c)bridge.postMessage(c)});window.nativeLog=(kind,text)=>{const row=document.createElement('div');row.className='entry';"
    "const name=document.createElement('b');name.textContent=kind+' ';row.append(name,document.createTextNode(text));"
    "document.querySelector('#events').prepend(row)};window.nativeCaps=x=>document.querySelector('#caps').textContent=x;"
    "nativeLog('ready','Choose an action or interact with the window.')</script>";

static void sleep_milliseconds(unsigned milliseconds) {
#if defined(_WIN32)
    Sleep(milliseconds);
#else
    struct timespec delay = {(time_t)(milliseconds / 1000),
                             (long)(milliseconds % 1000) * 1000000L};
    nanosleep(&delay, NULL);
#endif
}

static int check(app *state, const char *name, nk_result result) {
    if (result == NK_OK)
        return 1;
    fprintf(stderr, "%s failed (%d): %s\n", name, result, nk_last_error());
    state->running = 0;
    return 0;
}

static char *js_string(const void *data, size_t size) {
    const unsigned char *input = data;
    char *result = malloc(size * 6 + 3);
    if (!result)
        return NULL;
    char *out = result;
    *out++ = '"';
    for (size_t i = 0; i < size; ++i) {
        const unsigned char c = input[i];
        if (c == '"' || c == '\\') {
            *out++ = '\\';
            *out++ = (char)c;
        } else if (c == '\n') {
            *out++ = '\\';
            *out++ = 'n';
        } else if (c < 0x20) {
            out += sprintf(out, "\\u%04x", c);
        } else {
            *out++ = (char)c;
        }
    }
    *out++ = '"';
    *out = 0;
    return result;
}

static void log_data(app *state, const char *kind, const void *data, size_t size) {
    char *js_kind = js_string(kind, strlen(kind));
    char *js_data = js_string(data ? data : "", size);
    if (!js_kind || !js_data) {
        free(js_kind);
        free(js_data);
        return;
    }
    const size_t length = strlen(js_kind) + strlen(js_data) + 24;
    char *script = malloc(length);
    if (script) {
        snprintf(script, length, "nativeLog(%s,%s)", js_kind, js_data);
        nk_request_id ignored = NK_INVALID_REQUEST_ID;
        nk_webview_eval(state->webview, script, &ignored);
        free(script);
    }
    free(js_kind);
    free(js_data);
}

static void log_text(app *state, const char *kind, const char *text) {
    log_data(state, kind, text, strlen(text));
}

static void launch_sample(app *state, const char *path, const char *name) {
#if defined(_WIN32)
    char command[2048];
    snprintf(command, sizeof(command), "\"%s\"", path);
    STARTUPINFOA startup = {.cb = sizeof(startup)};
    PROCESS_INFORMATION process = {0};
    if (CreateProcessA(NULL, command, NULL, NULL, FALSE, 0, NULL, NULL, &startup, &process)) {
        CloseHandle(process.hThread);
        CloseHandle(process.hProcess);
        log_text(state, "graphics", name);
    } else {
        log_text(state, "error", "Could not launch graphics sample");
    }
#else
    pid_t child = 0;
    char *const arguments[] = {(char *)path, NULL};
    if (posix_spawn(&child, path, NULL, NULL, arguments, environ) == 0)
        log_text(state, "graphics", name);
    else
        log_text(state, "error", "Could not launch graphics sample");
#endif
}

static int command_is(const nk_event *event, const char *command) {
    const size_t length = strlen(command);
    return event->data_size == length + 2 && ((const char *)event->data)[0] == '"' &&
           memcmp((const char *)event->data + 1, command, length) == 0;
}

static void open_browser(app *state) {
    if (state->browser_window != NK_INVALID_HANDLE) {
        nk_window_activate(state->browser_window);
        return;
    }
    nk_window_options window = {0};
    window.struct_size = sizeof(window);
    window.flags = NK_WINDOW_RESIZABLE;
    window.width = 900;
    window.height = 600;
    window.title = "NativeKit Sample Browser";
    window.owner = state->window;
    if (!check(state, "nk_window_create", nk_window_create(&window, &state->browser_window)))
        return;
    nk_webview_options webview = {0};
    webview.struct_size = sizeof(webview);
    webview.flags = NK_WEBVIEW_DEVTOOLS;
    webview.width = window.width;
    webview.height = window.height;
    webview.initial_url = "https://example.com";
    if (!check(state, "nk_webview_create",
               nk_webview_create(state->browser_window, &webview, &state->browser_webview)))
        return;
    log_text(state, "browser", "Opened https://example.com in a second native window");
}

static void create_utility_window(app *state) {
    nk_window_options window = {0};
    window.struct_size = sizeof(window);
    window.flags = NK_WINDOW_RESIZABLE;
    window.width = 360;
    window.height = 220;
    window.title = "Owned utility window";
    window.owner = state->window;
    window.kind = NK_WINDOW_UTILITY;
    nk_handle child = NK_INVALID_HANDLE;
    if (nk_window_create(&window, &child) == NK_OK)
        log_text(state, "window", "Created an owned utility window");
    else
        log_text(state, "error", nk_last_error());
}

static void start_file_dialog(app *state, uint32_t kind) {
    const nk_dialog_filter filters[] = {{"Images", "*.png;*.jpg;*.jpeg"}, {"All files", "*"}};
    nk_file_dialog_options options = {0};
    options.struct_size = sizeof(options);
    options.title = "NativeKit Lab";
    if (kind != NK_DIALOG_SELECT_DIRECTORY) {
        options.filters = filters;
        options.filter_count = 2;
    }
    if (kind == NK_DIALOG_SAVE_FILE)
        options.suggested_name = "nativekit-demo.txt";
    nk_request_id request = NK_INVALID_REQUEST_ID;
    nk_result result = NK_ERROR_INVALID_ARGUMENT;
    if (kind == NK_DIALOG_OPEN_FILE) {
        options.flags = NK_DIALOG_ALLOW_MULTIPLE;
        result = nk_dialog_open_file(state->window, &options, &request);
    } else if (kind == NK_DIALOG_SAVE_FILE) {
        options.flags = NK_DIALOG_CONFIRM_OVERWRITE;
        result = nk_dialog_save_file(state->window, &options, &request);
    } else {
        result = nk_dialog_select_directory(state->window, &options, &request);
    }
    if (result != NK_OK)
        log_text(state, "error", nk_last_error());
}

static void system_info(app *state) {
    char locale[128] = {0};
    uint32_t locale_size = sizeof(locale);
    char home[1024] = {0};
    uint32_t home_size = sizeof(home);
    nk_system_appearance appearance = {0};
    appearance.struct_size = sizeof(appearance);
    if (nk_system_locale(locale, &locale_size) != NK_OK ||
        nk_system_directory(NK_DIRECTORY_HOME, home, &home_size) != NK_OK ||
        nk_system_get_appearance(&appearance) != NK_OK) {
        log_text(state, "error", nk_last_error());
        return;
    }
    char summary[1400];
    snprintf(summary, sizeof(summary), "locale=%s, home=%s, theme=%s, high contrast=%s", locale,
             home, appearance.color_scheme == NK_COLOR_SCHEME_DARK ? "dark" : "light",
             appearance.high_contrast ? "yes" : "no");
    log_text(state, "system", summary);
}

static void dispatch(app *state, const nk_event *event) {
    if (command_is(event, "dialog.open"))
        start_file_dialog(state, NK_DIALOG_OPEN_FILE);
    else if (command_is(event, "dialog.save"))
        start_file_dialog(state, NK_DIALOG_SAVE_FILE);
    else if (command_is(event, "dialog.directory"))
        start_file_dialog(state, NK_DIALOG_SELECT_DIRECTORY);
    else if (command_is(event, "dialog.message")) {
        nk_message_dialog_options options = {0};
        options.struct_size = sizeof(options);
        options.kind = NK_MESSAGE_QUESTION;
        options.buttons = NK_MESSAGE_BUTTON_YES | NK_MESSAGE_BUTTON_NO;
        options.title = "NativeKit Lab";
        options.message = "Did this native dialog appear correctly?";
        nk_request_id request = NK_INVALID_REQUEST_ID;
        if (nk_dialog_message(state->window, &options, &request) != NK_OK)
            log_text(state, "error", nk_last_error());
    } else if (command_is(event, "clipboard.copy")) {
        if (nk_clipboard_set_text("Hello from NativeKit Lab") == NK_OK)
            log_text(state, "clipboard", "Copied sample text");
        else
            log_text(state, "error", nk_last_error());
    } else if (command_is(event, "clipboard.read")) {
        nk_request_id request = NK_INVALID_REQUEST_ID;
        if (nk_clipboard_read_text(&request) != NK_OK)
            log_text(state, "error", nk_last_error());
    } else if (command_is(event, "drops.toggle")) {
        if (nk_window_set_drop_enabled(state->window, 1) == NK_OK)
            log_text(state, "drops", "Enabled; drop files or text onto this window");
        else
            log_text(state, "error", nk_last_error());
    } else if (command_is(event, "window.child")) {
        create_utility_window(state);
    } else if (command_is(event, "window.attention")) {
        if (nk_window_request_attention(state->window) == NK_OK)
            log_text(state, "window", "Requested user attention");
        else
            log_text(state, "error", nk_last_error());
    } else if (command_is(event, "window.fullscreen")) {
        state->fullscreen = !state->fullscreen;
        if (nk_window_set_fullscreen(state->window, (uint32_t)state->fullscreen) == NK_OK)
            log_text(state, "window", state->fullscreen ? "Entered fullscreen" : "Left fullscreen");
        else
            log_text(state, "error", nk_last_error());
    } else if (command_is(event, "browser.open")) {
        open_browser(state);
    } else if (command_is(event, "web.eval")) {
        if (nk_webview_eval(state->webview, "({answer: 6 * 7, engine: navigator.userAgent})",
                            &state->showcase_eval_request) != NK_OK)
            log_text(state, "error", nk_last_error());
    } else if (command_is(event, "system.info")) {
        system_info(state);
    } else if (command_is(event, "shell.open")) {
        if (nk_shell_open_url("https://github.com/") == NK_OK)
            log_text(state, "shell", "Asked the desktop to open the URL");
        else
            log_text(state, "error", nk_last_error());
    } else if (command_is(event, "graphics.opengl")) {
        launch_sample(state, NATIVEKIT_OPENGL_SAMPLE, "Launched the OpenGL triangle");
    } else if (command_is(event, "graphics.vulkan")) {
#if defined(NATIVEKIT_VULKAN_SAMPLE)
        launch_sample(state, NATIVEKIT_VULKAN_SAMPLE, "Launched the Vulkan triangle");
#else
        log_text(state, "graphics", "Vulkan sample was not built (Vulkan SDK and glslc required)");
#endif
    } else if (command_is(event, "notification.show")) {
        nk_notification_options options = {0};
        options.struct_size = sizeof(options);
        options.title = "NativeKit Lab";
        options.body = "Notifications are connected to the same event loop.";
        nk_request_id request = NK_INVALID_REQUEST_ID;
        if (nk_notification_show(&options, &request) != NK_OK)
            log_text(state, "error", nk_last_error());
    }
}

static void report_dialog(app *state, const nk_event *event) {
    if (event->result != NK_OK) {
        log_data(state, "dialog error", event->data, (size_t)event->data_size);
        return;
    }
    if (event->flags == NK_DIALOG_MESSAGE) {
        const nk_dialog_message_result *result = event->data;
        char text[64];
        snprintf(text, sizeof(text), "button result=%u", result ? result->button : 0);
        log_text(state, "dialog", text);
        return;
    }
    if (!event->data_count) {
        log_text(state, "dialog", "Cancelled");
        return;
    }
    for (uint32_t i = 0; i < event->data_count; ++i) {
        const char *path = NULL;
        uint32_t length = 0;
        if (nk_dialog_event_path(event, i, &path, &length) == NK_OK)
            log_data(state, "selected", path, length);
    }
}

static void show_capabilities(app *state) {
    const nk_capabilities caps = nk_get_capabilities();
    char text[256];
    snprintf(text, sizeof(text), "capabilities 0x%llx · window %s · webview %s · dialogs %s · clipboard %s · notifications %s",
             (unsigned long long)caps, caps & NK_CAP_WINDOW ? "yes" : "no",
             caps & NK_CAP_WEBVIEW ? "yes" : "no", caps & NK_CAP_FILE_DIALOG ? "yes" : "no",
             caps & NK_CAP_CLIPBOARD ? "yes" : "no", caps & NK_CAP_NOTIFICATION ? "yes" : "no");
    char *encoded = js_string(text, strlen(text));
    if (encoded) {
        const size_t length = strlen(encoded) + 20;
        char *script = malloc(length);
        if (script) {
            snprintf(script, length, "nativeCaps(%s)", encoded);
            nk_request_id ignored = NK_INVALID_REQUEST_ID;
            nk_webview_eval(state->webview, script, &ignored);
            free(script);
        }
        free(encoded);
    }
}

static void handle_event(app *state, const nk_event *event) {
    if (event->kind == NK_EVENT_WINDOW_CLOSE) {
        if (event->source == state->window)
            state->running = 0;
        else if (event->source == state->browser_window) {
            nk_window_destroy(state->browser_window);
            state->browser_window = NK_INVALID_HANDLE;
            state->browser_webview = NK_INVALID_HANDLE;
            log_text(state, "browser", "Browser window closed");
        } else {
            nk_window_destroy(event->source);
        }
    } else if (event->kind == NK_EVENT_WINDOW_RESIZE &&
               event->data_size >= sizeof(nk_window_resize_event)) {
        const nk_window_resize_event *size = event->data;
        if (event->source == state->window)
            nk_webview_set_bounds(state->webview, 0, 0, size->width, size->height);
        else if (event->source == state->browser_window)
            nk_webview_set_bounds(state->browser_webview, 0, 0, size->width, size->height);
    } else if (event->kind == NK_EVENT_WEBVIEW_READY && event->source == state->webview) {
        show_capabilities(state);
    } else if (event->kind == NK_EVENT_WEBVIEW_MESSAGE && event->source == state->webview) {
        dispatch(state, event);
    } else if (event->kind == NK_EVENT_DIALOG_COMPLETE) {
        report_dialog(state, event);
    } else if (event->kind == NK_EVENT_CLIPBOARD_TEXT_COMPLETE) {
        log_data(state, event->result == NK_OK ? "clipboard" : "clipboard error", event->data,
                 (size_t)event->data_size);
    } else if (event->kind == NK_EVENT_DROP_TEXT) {
        log_data(state, "text drop", event->data, (size_t)event->data_size);
    } else if (event->kind == NK_EVENT_DROP_FILES) {
        for (uint32_t i = 0; i < event->data_count; ++i) {
            const char *path = NULL;
            uint32_t length = 0;
            if (nk_drop_event_item(event, i, &path, &length) == NK_OK)
                log_data(state, "file drop", path, length);
        }
    } else if (event->kind == NK_EVENT_WEBVIEW_EVAL_COMPLETE &&
               event->request_id == state->showcase_eval_request && event->data_size) {
        state->showcase_eval_request = NK_INVALID_REQUEST_ID;
        log_data(state, event->result == NK_OK ? "eval result" : "eval error", event->data,
                 (size_t)event->data_size);
    } else if (event->kind >= NK_EVENT_NOTIFICATION_DELIVERED &&
               event->kind <= NK_EVENT_NOTIFICATION_FAILED) {
        char text[80];
        snprintf(text, sizeof(text), "event=%u request=%llu result=%d", event->kind,
                 (unsigned long long)event->request_id, event->result);
        log_text(state, "notification", text);
    }
}

int main(void) {
#if !defined(_WIN32)
    signal(SIGCHLD, SIG_IGN);
#endif
    app state = {0};
    state.running = 1;
    nk_init_options init = {0};
    init.struct_size = sizeof(init);
    init.api_version = NK_API_VERSION;
    if (!check(&state, "nk_init", nk_init(&init)))
        return 1;

    nk_window_options window = {0};
    window.struct_size = sizeof(window);
    window.flags = NK_WINDOW_RESIZABLE;
    window.width = 1100;
    window.height = 720;
    window.title = "NativeKit Lab";
    if (!check(&state, "nk_window_create", nk_window_create(&window, &state.window))) {
        nk_shutdown();
        return 1;
    }
    nk_webview_options webview = {0};
    webview.struct_size = sizeof(webview);
    webview.flags = NK_WEBVIEW_DEVTOOLS;
    webview.width = window.width;
    webview.height = window.height;
    if (!check(&state, "nk_webview_create",
               nk_webview_create(state.window, &webview, &state.webview)) ||
        !check(&state, "nk_webview_set_html", nk_webview_set_html(state.webview, page, NULL))) {
        nk_shutdown();
        return 1;
    }

    while (state.running) {
        nk_event event = {0};
        event.struct_size = sizeof(event);
        if (!check(&state, "nk_poll_event", nk_poll_event(&event)))
            break;
        if (event.kind == NK_EVENT_NONE)
            sleep_milliseconds(8);
        else
            handle_event(&state, &event);
        nk_event_release(&event);
    }
    nk_shutdown();
    return 0;
}
