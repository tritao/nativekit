#include "nativekit.h"
#include "nativekit_resource.h"
#include "nativekit_graphics.h"
#include "nativekit_input.h"
#include "nativekit_ui.h"
#include "nativekit_window.h"

#if defined(__EMSCRIPTEN__)
#include <emscripten/emscripten.h>
#endif

#include <algorithm>
#include <chrono>
#include <cstring>
#include <thread>
#include <vector>

#ifndef NKUI_TEST_FONT_PATH
#error NKUI_TEST_FONT_PATH is required
#endif

namespace {

template <class T> void append(std::vector<uint8_t> &bytes, const T &value) {
    const size_t offset = bytes.size();
    bytes.resize(offset + sizeof(value));
    std::memcpy(bytes.data() + offset, &value, sizeof(value));
}

nkui_command_header header(nkui_command_opcode opcode, uint32_t size) {
    return {opcode, NKUI_COMMAND_VERSION, size};
}

void set_paint(std::vector<uint8_t> &commands, nkui_resource paint) {
    append(commands, nkui_resource_command{
                         header(NKUI_COMMAND_SET_PAINT, sizeof(nkui_resource_command)), paint});
}

void draw_path(std::vector<uint8_t> &commands, nkui_resource path) {
    append(commands, nkui_resource_command{
                         header(NKUI_COMMAND_DRAW_PATH, sizeof(nkui_resource_command)), path});
}

void draw_text(std::vector<uint8_t> &commands, nkui_resource text, float x, float y) {
    append(commands, nkui_draw_rect_command{
                         header(NKUI_COMMAND_DRAW_TEXT_LAYOUT, sizeof(nkui_draw_rect_command)),
                         text, x, y, 0.0f, 0.0f});
}

nkui_resource make_rect(float x, float y, float width, float height) {
    const nkui_path_element elements[] = {
        {NKUI_PATH_MOVE_TO, {x, y}},
        {NKUI_PATH_LINE_TO, {x + width, y}},
        {NKUI_PATH_LINE_TO, {x + width, y + height}},
        {NKUI_PATH_LINE_TO, {x, y + height}},
        {NKUI_PATH_CLOSE, {}},
    };
    nkui_resource path{};
    if (nkui_path_create(elements, 5, &path) != NKUI_OK)
        return {};
    return path;
}

nkui_resource make_paint(float red, float green, float blue, float alpha = 1.0f) {
    nkui_resource paint{};
    if (nkui_paint_create_solid({red, green, blue, alpha}, &paint) != NKUI_OK)
        return {};
    return paint;
}

struct CApiShowcase {
    nkui_renderer renderer{};
    nkui_display_list list{};
    nkui_resource fonts{};
    std::vector<nkui_resource> resources;
    std::vector<uint8_t> commands;
    bool frame_failed = false;
    int rendered_frames = 0;
    int last_width = 0;
    int last_height = 0;

    static void NK_CALL draw_frame(nk_handle surface, int32_t width, int32_t height,
                                   void *user_data) {
        auto &showcase = *static_cast<CApiShowcase *>(user_data);
        if (!showcase.render_frame(surface, width, height))
            showcase.frame_failed = true;
        else
            ++showcase.rendered_frames;
    }

    bool render_frame(nk_handle surface, int32_t framebuffer_width, int32_t framebuffer_height) {
        return resize(framebuffer_width, framebuffer_height) &&
               nkui_renderer_render(renderer, list, surface) == NKUI_OK;
    }

    bool create(int framebuffer_width, int framebuffer_height, const uint8_t *font_data = nullptr,
                uint32_t font_bytes = 0) {
        if (nkui_renderer_create(&renderer) != NKUI_OK ||
            nkui_display_list_create(&list) != NKUI_OK ||
            nkui_font_collection_create(&fonts) != NKUI_OK)
            return false;
        if (font_data && font_bytes) {
            if (nkui_font_collection_add_data(fonts, "IBMPlexSans-Regular", font_data, font_bytes,
                                              NKUI_FONT_FAMILY_DEFAULT) != NKUI_OK)
                return false;
        } else if (nkui_font_collection_add(fonts, NKUI_TEST_FONT_PATH,
                                            NKUI_FONT_FAMILY_DEFAULT) != NKUI_OK) {
            return false;
        }

        const auto retain = [this](nkui_resource resource) {
            if (resource.id)
                resources.push_back(resource);
            return resource;
        };
        const nkui_resource background = retain(make_rect(0.0f, 0.0f, 900.0f, 600.0f));
        const nkui_resource panel = retain(make_rect(48.0f, 48.0f, 804.0f, 504.0f));
        const nkui_resource badge = retain(make_rect(72.0f, 72.0f, 136.0f, 32.0f));
        const nkui_resource navy = retain(make_paint(0.025f, 0.04f, 0.09f));
        const nkui_resource panel_paint = retain(make_paint(0.09f, 0.13f, 0.23f));
        const nkui_resource green = retain(make_paint(0.12f, 0.72f, 0.48f));
        const nkui_resource cyan = retain(make_paint(0.12f, 0.67f, 0.9f));
        if (!background.id || !panel.id || !badge.id || !navy.id || !panel_paint.id ||
            !green.id || !cyan.id)
            return false;

        nkui_resource title{}, subtitle{}, details{};
        if (nkui_text_layout_create(fonts, "NativeKit C UI", 600.0f, 34.0f, &title) != NKUI_OK ||
            nkui_text_layout_create(fonts, "The intentionally small opaque-handle ABI example",
                                    600.0f, 18.0f, &subtitle) != NKUI_OK ||
            nkui_text_layout_create(fonts,
                                    "create resources\nencode a transaction\nsubmit and render\ndestroy",
                                    240.0f, 22.0f, &details) != NKUI_OK)
            return false;
        resources.push_back(title);
        resources.push_back(subtitle);
        resources.push_back(details);

        std::vector<uint8_t> checker(16 * 16 * 4);
        for (int y = 0; y < 16; ++y)
            for (int x = 0; x < 16; ++x) {
                const bool bright = ((x / 4) + (y / 4)) % 2 == 0;
                const size_t pixel = static_cast<size_t>(y * 16 + x) * 4;
                checker[pixel + 0] = bright ? 55 : 25;
                checker[pixel + 1] = bright ? 180 : 75;
                checker[pixel + 2] = bright ? 230 : 155;
                checker[pixel + 3] = 255;
            }
        nkui_resource image{};
        if (nkui_image_create(16, 16, NKUI_IMAGE_RGBA8, checker.data(), checker.size(), &image) !=
            NKUI_OK)
            return false;
        resources.push_back(image);

        append(commands, nkui_transform_command{
                             header(NKUI_COMMAND_SET_TRANSFORM, sizeof(nkui_transform_command)),
                             {1.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f}});
        set_paint(commands, navy);
        draw_path(commands, background);
        set_paint(commands, panel_paint);
        draw_path(commands, panel);
        set_paint(commands, green);
        draw_path(commands, badge);
        draw_text(commands, title, 72.0f, 126.0f);
        draw_text(commands, subtitle, 74.0f, 170.0f);
        draw_text(commands, details, 104.0f, 270.0f);

        append(commands,
               nkui_command_header{header(NKUI_COMMAND_PUSH_STATE, sizeof(nkui_command_header))});
        append(commands,
               nkui_rect_command{header(NKUI_COMMAND_CLIP_RECT, sizeof(nkui_rect_command)), 480.0f,
                                 238.0f, 300.0f, 220.0f});
        append(commands,
               nkui_layer_command{header(NKUI_COMMAND_BEGIN_LAYER, sizeof(nkui_layer_command)),
                                  0.72f, NKUI_COMPOSITE_SOURCE_OVER});
        set_paint(commands, cyan);
        draw_path(commands, badge);
        append(commands,
               nkui_draw_rect_command{
                   header(NKUI_COMMAND_DRAW_IMAGE, sizeof(nkui_draw_rect_command)), image, 492.0f,
                   250.0f, 276.0f, 196.0f});
        append(commands,
               nkui_command_header{header(NKUI_COMMAND_END_LAYER, sizeof(nkui_command_header))});
        append(commands,
               nkui_command_header{header(NKUI_COMMAND_POP_STATE, sizeof(nkui_command_header))});

        return resize(framebuffer_width, framebuffer_height);
    }

    bool resize(int framebuffer_width, int framebuffer_height) {
        if (commands.size() < sizeof(nkui_transform_command) || framebuffer_width <= 0 ||
            framebuffer_height <= 0)
            return false;
        if (framebuffer_width == last_width && framebuffer_height == last_height)
            return true;
        const float scale = std::min(framebuffer_width / 900.0f, framebuffer_height / 600.0f);
        const float offset_x = (framebuffer_width - 900.0f * scale) * 0.5f;
        const float offset_y = (framebuffer_height - 600.0f * scale) * 0.5f;
        const nkui_transform_command transform{
            header(NKUI_COMMAND_SET_TRANSFORM, sizeof(nkui_transform_command)),
            {scale, 0.0f, 0.0f, scale, offset_x, offset_y}};
        std::memcpy(commands.data(), &transform, sizeof(transform));
        if (nkui_display_list_submit(list, commands.data(), commands.size()) != NKUI_OK)
            return false;
        last_width = framebuffer_width;
        last_height = framebuffer_height;
        return true;
    }

    void destroy() {
        if (renderer.id)
            nkui_renderer_destroy(renderer);
        if (list.id)
            nkui_display_list_destroy(list);
        for (auto resource : resources)
            nkui_resource_destroy(resource);
        if (fonts.id)
            nkui_resource_destroy(fonts);
    }
};

#if defined(__EMSCRIPTEN__)

EM_JS(void, nk_web_example_report, (int result), {
    if (typeof Module !== "undefined" && Module.onNativeKitResult)
        Module.onNativeKitResult(result);
});

struct WebShowcase {
    nk_handle window = NK_INVALID_HANDLE;
    nk_handle surface = NK_INVALID_HANDLE;
    CApiShowcase showcase;
    bool smoke = false;
    bool ready = false;
    bool finished = false;
    bool frame_callback_installed = false;
    bool initialized = false;
    nk_request_id asset_request = NK_INVALID_REQUEST_ID;
    bool asset_loaded = false;
    std::vector<uint8_t> asset_bytes;
    bool surface_ready = false;
    bool text_edit_seen = false;
    bool pointer_seen = false;
    bool touch_seen = false;
    bool resize_seen = false;
    int framebuffer_width = 0;
    int framebuffer_height = 0;
    int result = 0;

    static void NK_CALL draw_frame(nk_handle surface, int32_t width, int32_t height,
                                   void *user_data) {
        auto &app = *static_cast<WebShowcase *>(user_data);
        if (app.finished)
            return;
        if (!app.poll_events()) {
            if (!app.finished)
                app.finish(app.result != 0 ? app.result : 4);
            return;
        }
        if (!app.ready)
            return;
        if (!app.showcase.render_frame(surface, width, height)) {
            app.finish(5);
            return;
        }
        ++app.showcase.rendered_frames;
        if (app.smoke && app.showcase.rendered_frames >= 30 && app.asset_loaded &&
            app.text_edit_seen && app.pointer_seen && app.touch_seen && app.resize_seen)
            app.finish(0);
    }

    bool start(bool smoke_mode) {
        smoke = smoke_mode;
        nk_init_options init{};
        init.struct_size = sizeof(init);
        init.api_version = NK_API_VERSION;
        if (nk_init(&init) != NK_OK)
            return fail(1);
        initialized = true;

        nk_window_options window_options{};
        window_options.struct_size = sizeof(window_options);
        window_options.flags = NK_WINDOW_RESIZABLE;
        window_options.width = 900;
        window_options.height = 600;
        window_options.title = "NativeKit UI C ABI - WebGL2";
        if (nk_window_create(&window_options, &window) != NK_OK)
            return fail(1);

        nk_surface_options surface_options{};
        surface_options.struct_size = sizeof(surface_options);
        surface_options.flags = NK_SURFACE_FORWARD_COMPATIBLE | NK_SURFACE_STENCIL;
        surface_options.api = NK_GRAPHICS_OPENGL_ES;
        surface_options.major_version = 3;
        surface_options.width = window_options.width;
        surface_options.height = window_options.height;
        if (nk_surface_create(window, &surface_options, &surface) != NK_OK)
            return fail(1);

        nk_resource asset{};
        asset.struct_size = sizeof(asset);
        asset.flags = NK_RESOURCE_READABLE;
        asset.uri = "/assets/IBMPlexSans-Regular.ttf";
        if (nk_resource_load_async(&asset, &asset_request) != NK_OK)
            return fail(1);

        if (smoke) {
            const nk_capabilities capabilities = nk_get_capabilities();
            if ((capabilities & (NK_CAP_CLIPBOARD | NK_CAP_CURSOR | NK_CAP_POINTER_CAPTURE)) !=
                (NK_CAP_CLIPBOARD | NK_CAP_CURSOR | NK_CAP_POINTER_CAPTURE))
                return fail(8);
            nk_handle cursor = NK_INVALID_HANDLE;
            nk_cursor_mode cursor_mode = NK_CURSOR_MODE_NORMAL;
            if (nk_cursor_create_standard(NK_CURSOR_HAND, &cursor) != NK_OK ||
                nk_window_set_cursor(window, cursor) != NK_OK ||
                nk_cursor_destroy(cursor) != NK_OK ||
                nk_window_get_cursor_mode(window, &cursor_mode) != NK_OK ||
                cursor_mode != NK_CURSOR_MODE_NORMAL)
                return fail(8);
            nk_text_input_state text_state{};
            text_state.struct_size = sizeof(text_state);
            text_state.text = "";
            text_state.composition_start = NK_TEXT_POSITION_NONE;
            text_state.composition_end = NK_TEXT_POSITION_NONE;
            if (nk_surface_set_text_input_state(surface, &text_state) != NK_OK ||
                nk_surface_set_text_input_active(surface, 1) != NK_OK)
                return fail(8);
        }

        return poll_events() && result == 0;
    }

    bool poll_events() {
        for (;;) {
            nk_event event{};
            event.struct_size = sizeof(event);
            if (nk_poll_event(&event) != NK_OK) {
                result = 3;
                nk_event_release(&event);
                return false;
            }
            if (event.kind == NK_EVENT_NONE) {
                nk_event_release(&event);
                return true;
            }
            handle_event(event);
            nk_event_release(&event);
            if (finished)
                return false;
        }
    }

    void handle_event(const nk_event &event) {
        if (event.kind == NK_EVENT_RESOURCE_DATA_COMPLETE &&
            event.request_id == asset_request) {
            if (event.result != NK_OK || event.data_size == 0 || !event.data ||
                event.data_size > UINT32_MAX)
                result = 7;
            else {
                const auto *bytes = static_cast<const uint8_t *>(event.data);
                asset_bytes.assign(bytes, bytes + event.data_size);
                asset_loaded = true;
                try_start_showcase();
            }
            return;
        }
        if (event.source == surface && event.kind == NK_EVENT_TEXT_EDIT) {
            const char *text = nullptr;
            uint32_t length = 0;
            if (event.data_size < sizeof(nk_text_edit_event) ||
                nk_text_edit_event_text(&event, &text, &length) != NK_OK ||
                event.data_size < sizeof(nk_text_edit_event) || length != 1 || !text ||
                text[0] != 'A') {
                result = 8;
            } else {
                text_edit_seen = true;
                nk_surface_set_text_input_active(surface, 0);
            }
            return;
        }
        if (event.source == window && event.kind == NK_EVENT_WINDOW_RESIZE &&
            event.data_size >= sizeof(nk_window_resize_event)) {
            const auto *resize = static_cast<const nk_window_resize_event *>(event.data);
            if (nk_surface_set_bounds(surface, 0, 0, resize->width, resize->height) != NK_OK)
                result = 6;
            else
                resize_seen = true;
            return;
        }
        if (event.source == window && event.kind == NK_EVENT_POINTER_MOVE &&
            event.data_size >= sizeof(nk_pointer_move_event)) {
            pointer_seen = true;
            return;
        }
        if (event.source == window && event.kind == NK_EVENT_TOUCH &&
            event.data_size >= sizeof(nk_touch_event)) {
            const auto *touch = static_cast<const nk_touch_event *>(event.data);
            if (touch->action == NK_TOUCH_BEGIN || touch->action == NK_TOUCH_END)
                touch_seen = true;
            return;
        }
        if (event.source != surface)
            return;
        if (event.kind == NK_EVENT_SURFACE_READY) {
            int32_t width = 0;
            int32_t height = 0;
            surface_ready = nk_surface_make_current(surface) == NK_OK &&
                            nk_surface_get_framebuffer_size(surface, &width, &height) == NK_OK &&
                            width > 0 && height > 0;
            if (!surface_ready) {
                result = 4;
                return;
            }
            framebuffer_width = width;
            framebuffer_height = height;
            if (!frame_callback_installed) {
                if (nk_surface_set_frame_callback(surface, draw_frame, this) != NK_OK) {
                    result = 4;
                    surface_ready = false;
                } else {
                    frame_callback_installed = true;
                }
            }
            try_start_showcase();
            return;
        }
        if (event.kind == NK_EVENT_SURFACE_RESIZE &&
            event.data_size >= sizeof(nk_surface_resize_event)) {
            const auto *resize = static_cast<const nk_surface_resize_event *>(event.data);
            framebuffer_width = resize->framebuffer_width;
            framebuffer_height = resize->framebuffer_height;
            surface_ready = framebuffer_width > 0 && framebuffer_height > 0;
            ready = surface_ready && showcase.list.id;
            if (ready && showcase.list.id &&
                !showcase.resize(framebuffer_width, framebuffer_height))
                result = 6;
        } else if (event.kind == NK_EVENT_SURFACE_LOST) {
            ready = false;
            surface_ready = false;
        }
    }

    void try_start_showcase() {
        if (finished || ready || !surface_ready || !asset_loaded)
            return;
        if (!showcase.create(framebuffer_width, framebuffer_height, asset_bytes.data(),
                             static_cast<uint32_t>(asset_bytes.size())))
            result = 4;
        else
            ready = true;
    }

    bool fail(int code) {
        result = code;
        finish(result);
        return false;
    }

    void finish(int code) {
        if (finished)
            return;
        finished = true;
        result = code;
        if (surface != NK_INVALID_HANDLE)
            nk_surface_set_frame_callback(surface, nullptr, nullptr);
        showcase.destroy();
        if (surface != NK_INVALID_HANDLE) {
            nk_surface_destroy(surface);
            surface = NK_INVALID_HANDLE;
        }
        if (window != NK_INVALID_HANDLE) {
            nk_window_destroy(window);
            window = NK_INVALID_HANDLE;
        }
        if (initialized)
            nk_shutdown();
        nk_web_example_report(result);
    }
};

#endif

} // namespace

int main(int argc, char **argv) {
    const bool smoke = argc == 2 && std::strcmp(argv[1], "--smoke-test") == 0;
    if (argc > 1 && !smoke)
        return 2;

#if defined(__EMSCRIPTEN__)
    static WebShowcase app;
    return app.start(smoke) ? 0 : app.result;
#else

    nk_init_options init{};
    init.struct_size = sizeof(init);
    init.api_version = NK_API_VERSION;
    if (nk_init(&init) != NK_OK)
        return 1;

    nk_window_options window_options{};
    window_options.struct_size = sizeof(window_options);
    window_options.flags = NK_WINDOW_RESIZABLE;
    window_options.width = 900;
    window_options.height = 600;
    window_options.title = "NativeKit UI C ABI";
    nk_handle window = NK_INVALID_HANDLE;
    if (nk_window_create(&window_options, &window) != NK_OK)
        return 1;

    nk_surface_options surface_options{};
    surface_options.struct_size = sizeof(surface_options);
    surface_options.flags = NK_SURFACE_FORWARD_COMPATIBLE | NK_SURFACE_STENCIL;
#if defined(NK_SOKOL_BACKEND_GLES3)
    surface_options.api = NK_GRAPHICS_OPENGL_ES;
    surface_options.major_version = 3;
#else
    surface_options.api = NK_GRAPHICS_OPENGL;
    surface_options.major_version = 3;
    surface_options.minor_version = 3;
#endif
    surface_options.width = window_options.width;
    surface_options.height = window_options.height;
    nk_handle surface = NK_INVALID_HANDLE;
    if (nk_surface_create(window, &surface_options, &surface) != NK_OK)
        return 1;

    CApiShowcase showcase;
    bool running = true;
    bool ready = false;
    int framebuffer_width = 0;
    int framebuffer_height = 0;
    int frames = 0;
    int result = 0;
    while (running && !result) {
        nk_event event{};
        event.struct_size = sizeof(event);
        if (nk_poll_event(&event) != NK_OK) {
            result = 3;
        } else if (event.kind == NK_EVENT_WINDOW_CLOSE && event.source == window) {
            running = false;
        } else if (event.kind == NK_EVENT_WINDOW_RESIZE && event.source == window &&
                   event.data_size >= sizeof(nk_window_resize_event)) {
            const auto *resize = static_cast<const nk_window_resize_event *>(event.data);
            if (nk_surface_set_bounds(surface, 0, 0, resize->width, resize->height) != NK_OK)
                result = 6;
        } else if (event.kind == NK_EVENT_SURFACE_READY && event.source == surface) {
            ready = nk_surface_make_current(surface) == NK_OK &&
                    nk_surface_get_framebuffer_size(surface, &framebuffer_width,
                                                    &framebuffer_height) == NK_OK &&
                    showcase.create(framebuffer_width, framebuffer_height) &&
                    nk_surface_set_frame_callback(surface, CApiShowcase::draw_frame, &showcase) ==
                        NK_OK;
            if (!ready)
                result = 4;
        } else if (event.kind == NK_EVENT_SURFACE_RESIZE && event.source == surface &&
                   event.data_size >= sizeof(nk_surface_resize_event)) {
            const auto *resize = static_cast<const nk_surface_resize_event *>(event.data);
            framebuffer_width = resize->framebuffer_width;
            framebuffer_height = resize->framebuffer_height;
            if (framebuffer_width <= 0 || framebuffer_height <= 0) {
                ready = false;
            } else if (showcase.list.id) {
                ready = showcase.resize(framebuffer_width, framebuffer_height);
                if (!ready)
                    result = 6;
            }
        } else if (event.kind == NK_EVENT_SURFACE_LOST && event.source == surface) {
            ready = false;
        }
        const bool idle = event.kind == NK_EVENT_NONE;
        nk_event_release(&event);
        if (showcase.frame_failed)
            result = 5;
        if (ready && running) {
            if (nk_surface_present(surface) != NK_OK)
                result = 5;
            if (smoke && ++frames == 30)
                running = false;
        } else if (idle) {
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
        }
    }

    nk_surface_set_frame_callback(surface, nullptr, nullptr);
    if (smoke && showcase.rendered_frames == 0)
        result = 7;
    if (ready)
        nk_surface_make_current(surface);
    showcase.destroy();
    nk_surface_destroy(surface);
    nk_window_destroy(window);
    nk_shutdown();
    return result;
#endif
}
