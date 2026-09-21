#include "nativekit.h"
#include "nativekit_resource.h"
#include "nativekit_graphics.h"
#include "nativekit_input.h"
#include "nativekit_gpu.h"
#include "nativekit_system.h"
#include "nativekit_time.h"
#include "nativekit_ui.h"
#include "nativekit_window.h"

#if defined(__EMSCRIPTEN__)
#include <emscripten/emscripten.h>
#endif

#include <algorithm>
#include <cstring>
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
    bool smoke_sequence = false;
    int rendered_frames = 0;
    int last_width = 0;
    int last_height = 0;

    static void NK_CALL draw_frame(nk_surface surface, int32_t width, int32_t height,
                                   void *user_data) {
        auto &showcase = *static_cast<CApiShowcase *>(user_data);
        const bool rendered = showcase.render_frame(surface, width, height);
        if (!rendered)
            showcase.frame_failed = true;
        else {
            ++showcase.rendered_frames;
            if (showcase.smoke_sequence && showcase.rendered_frames < 30) {
                const auto request = nk_surface_request_frame(surface);
                showcase.frame_failed = request != NK_OK;
            }
        }
    }

    bool render_frame(nk_surface surface, int32_t framebuffer_width, int32_t framebuffer_height) {
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
        } else if (nkui_font_collection_add(fonts, NKUI_TEST_FONT_PATH, NKUI_FONT_FAMILY_DEFAULT) !=
                   NKUI_OK) {
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
        if (!background.id || !panel.id || !badge.id || !navy.id || !panel_paint.id || !green.id ||
            !cyan.id)
            return false;

        nkui_resource title{}, subtitle{}, details{};
        if (nkui_text_layout_create(fonts, "NativeKit C UI", 600.0f, 34.0f, &title) != NKUI_OK ||
            nkui_text_layout_create(fonts, "The intentionally small opaque-handle ABI example",
                                    600.0f, 18.0f, &subtitle) != NKUI_OK ||
            nkui_text_layout_create(
                fonts, "create resources\nencode a transaction\nsubmit and render\ndestroy", 240.0f,
                22.0f, &details) != NKUI_OK)
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
        append(commands, nkui_layer_command{{NKUI_COMMAND_BEGIN_LAYER, NKUI_LAYER_COMMAND_VERSION,
                                             sizeof(nkui_layer_command)},
                                            0.72f,
                                            NKUI_COMPOSITE_SOURCE_OVER});
        set_paint(commands, cyan);
        draw_path(commands, badge);
        append(commands, nkui_draw_rect_command{
                             header(NKUI_COMMAND_DRAW_IMAGE, sizeof(nkui_draw_rect_command)), image,
                             492.0f, 250.0f, 276.0f, 196.0f});
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

// clang-format off
EM_JS(void, nk_web_example_report, (int result), {
    if (typeof Module !== "undefined" && Module.onNativeKitResult)
        Module.onNativeKitResult(result);
});
// clang-format on

struct WebShowcase {
    nk_window window = 0;
    nk_surface surface = 0;
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

    bool gpu_offscreen_smoke() {
        nkgpu_renderer gpu{};
        if (nkgpu_renderer_create(surface, &gpu) != NKGPU_OK)
            return false;

        nkgpu_features features{};
        features.struct_size = sizeof(features);
        if (nkgpu_query_features(gpu, &features) != NKGPU_OK || !features.image_readback) {
            nkgpu_renderer_destroy(gpu);
            return false;
        }

        const nkgpu_image_format formats[] = {
            NKGPU_IMAGEFORMAT_RGBA8,
            NKGPU_IMAGEFORMAT_RGBA16F,
            NKGPU_IMAGEFORMAT_R32F,
            NKGPU_IMAGEFORMAT_R32_UINT,
            NKGPU_IMAGEFORMAT_DEPTH32F,
        };
        bool success = true;
        for (const nkgpu_image_format format : formats) {
            nkgpu_image_format_support support{};
            support.struct_size = sizeof(support);
            if (nkgpu_query_image_format_support(gpu, format, &support) != NKGPU_OK) {
                success = false;
                break;
            }

            const bool depth = format == NKGPU_IMAGEFORMAT_DEPTH32F;
            const bool renderable = depth ? support.depth_stencil != 0
                                          : support.render_target != 0;
            const bool required = format == NKGPU_IMAGEFORMAT_RGBA8;
            if (!renderable || !support.sampled) {
                if (required)
                    success = false;
                continue;
            }

            nkgpu_image_desc image_desc{};
            image_desc.struct_size = sizeof(image_desc);
            image_desc.width = 2;
            image_desc.height = 2;
            image_desc.format = format;
            image_desc.usage = NKGPU_IMAGE_SAMPLED |
                               (depth ? NKGPU_IMAGE_DEPTH_STENCIL
                                      : NKGPU_IMAGE_RENDER_TARGET);
            nkgpu_image source{};
            nkgpu_image destination{};
            nkgpu_readback readback{};
            if (nkgpu_image_create_desc(gpu, &image_desc, &source) != NKGPU_OK)
                success = false;

            nkgpu_render_pass_desc pass{};
            pass.struct_size = sizeof(pass);
            if (success) {
                if (depth) {
                    pass.depth_stencil = source;
                    pass.depth_stencil_action.load_action = NKGPU_LOADACTION_CLEAR;
                    pass.depth_stencil_action.store_action = NKGPU_STOREACTION_STORE;
                    pass.depth_stencil_action.clear_depth = 1.0f;
                } else {
                    pass.color_count = 1;
                    pass.colors[0].image = source;
                    /* Integer attachments cannot consume the float clear value
                       carried by the portable pass descriptor. The smoke only
                       checks transfer/readback for R32_UINT, so preserve its
                       contents as discard instead of issuing an invalid GL
                       clear on WebGL. */
                    pass.colors[0].action.load_action =
                        format == NKGPU_IMAGEFORMAT_R32_UINT ? NKGPU_LOADACTION_DISCARD
                                                              : NKGPU_LOADACTION_CLEAR;
                    pass.colors[0].action.store_action = NKGPU_STOREACTION_STORE;
                    pass.colors[0].action.clear_color = {0.0f, 0.0f, 0.0f, 0.0f};
                }
                success = nkgpu_frame_begin(gpu) == NKGPU_OK &&
                          nkgpu_begin_render_pass(gpu, &pass) == NKGPU_OK &&
                          nkgpu_end_pass(gpu) == NKGPU_OK &&
                          nkgpu_end_frame(gpu) == NKGPU_OK;
            }

            if (success) {
                image_desc.data = nullptr;
                image_desc.data_size = 0;
                success = nkgpu_image_create_desc(gpu, &image_desc, &destination) == NKGPU_OK;
            }

            if (success) {
                nkgpu_image_copy_desc copy{};
                copy.struct_size = sizeof(copy);
                copy.source = source;
                copy.destination = destination;
                copy.width = 2;
                copy.height = 2;
                success = nkgpu_image_copy(gpu, &copy) == NKGPU_OK;
            }

            if (success) {
                nkgpu_image_readback_desc readback_desc{};
                readback_desc.struct_size = sizeof(readback_desc);
                readback_desc.image = destination;
                readback_desc.width = 2;
                readback_desc.height = 2;
                success = nkgpu_readback_begin_image(gpu, &readback_desc, &readback) == NKGPU_OK;
            }

            nkgpu_readback_info readback_info{};
            if (success) {
                readback_info.struct_size = sizeof(readback_info);
                for (int poll = 0; poll != 1000; ++poll) {
                    if (nkgpu_readback_query(gpu, readback, &readback_info) != NKGPU_OK) {
                        success = false;
                        break;
                    }
                    if (readback_info.state != NKGPU_READBACK_PENDING)
                        break;
                }
                success = success && readback_info.state == NKGPU_READBACK_READY &&
                          readback_info.size == 2 * 2 * (depth ? 4u :
                                                        format == NKGPU_IMAGEFORMAT_RGBA16F ? 8u
                                                        : 4u);
            }

            if (success) {
                uint8_t pixels[64]{};
                uint32_t size = 0;
                success = nkgpu_readback_read(gpu, readback, pixels, sizeof(pixels), &size) ==
                              NKGPU_OK &&
                          size == readback_info.size;
                if (success && depth) {
                    uint32_t depth_bits = 0;
                    std::memcpy(&depth_bits, pixels, sizeof(depth_bits));
                    success = depth_bits == 0x3f800000u;
                }
            }

            if (readback.id)
                nkgpu_readback_destroy(gpu, readback);
            if (destination.id)
                nkgpu_image_destroy(gpu, destination);
            if (source.id)
                nkgpu_image_destroy(gpu, source);
            if (!success)
                break;
        }

        nkgpu_renderer_destroy(gpu);
        return success;
    }

    static void NK_CALL draw_frame(nk_surface surface, int32_t width, int32_t height,
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
        const bool input_checks_complete =
            app.text_edit_seen && app.pointer_seen && app.touch_seen && app.resize_seen;
        if (app.smoke && !input_checks_complete)
            return;
        if (!app.showcase.render_frame(surface, width, height)) {
            app.finish(5);
            return;
        }
        ++app.showcase.rendered_frames;
        if (app.smoke && app.showcase.rendered_frames >= 1 && app.asset_loaded &&
            input_checks_complete)
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
        asset.uri = "assets/IBMPlexSans-Regular.ttf";
        if (nk_resource_load_async(&asset, &asset_request) != NK_OK)
            return fail(1);

        if (smoke) {
            const nk_capabilities capabilities = nk_get_capabilities();
            if ((capabilities & (NK_CAP_CLIPBOARD | NK_CAP_CURSOR | NK_CAP_POINTER_CAPTURE)) !=
                (NK_CAP_CLIPBOARD | NK_CAP_CURSOR | NK_CAP_POINTER_CAPTURE))
                return fail(8);

            nk_system_info system_info{};
            system_info.struct_size = sizeof(system_info);
            if (nk_system_get_info(&system_info) != NK_OK ||
                system_info.platform != NK_SYSTEM_PLATFORM_WEB ||
                system_info.endianness != NK_SYSTEM_ENDIAN_LITTLE || system_info.mobile)
                return fail(9);

            uint32_t locale_size = 0;
            if (nk_system_locale(nullptr, &locale_size) != NK_ERROR_BUFFER_TOO_SMALL ||
                locale_size < 2)
                return fail(9);
            std::vector<char> locale(locale_size);
            if (nk_system_locale(locale.data(), &locale_size) != NK_OK || locale.back() != '\0')
                return fail(9);

            if (capabilities & NK_CAP_SYSTEM_APPEARANCE) {
                nk_system_appearance appearance{};
                appearance.struct_size = sizeof(appearance);
                if (nk_system_get_appearance(&appearance) != NK_OK ||
                    (appearance.color_scheme != NK_COLOR_SCHEME_LIGHT &&
                     appearance.color_scheme != NK_COLOR_SCHEME_DARK))
                    return fail(9);
            }

            if (capabilities & NK_CAP_DISPLAY_ORIENTATION) {
                nk_system_orientation orientation{};
                orientation.struct_size = sizeof(orientation);
                if (nk_system_get_orientation(&orientation) != NK_OK ||
                    orientation.display == NK_ORIENTATION_UNKNOWN)
                    return fail(9);
            }

            if (capabilities & NK_CAP_KEEP_AWAKE) {
                nk_keep_awake_options keep_awake_options{};
                keep_awake_options.struct_size = sizeof(keep_awake_options);
                keep_awake_options.flags = NK_KEEP_AWAKE_DISPLAY;
                nk_keep_awake first_lock = 0;
                nk_keep_awake second_lock = 0;
                if (nk_system_keep_awake_acquire(&keep_awake_options, &first_lock) != NK_OK ||
                    nk_system_keep_awake_acquire(&keep_awake_options, &second_lock) != NK_OK ||
                    first_lock == 0 || second_lock == 0 || first_lock == second_lock ||
                    nk_system_keep_awake_release(first_lock) != NK_OK ||
                    nk_system_keep_awake_release(second_lock) != NK_OK)
                    return fail(9);
            }

            nk_cursor cursor = 0;
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
        if (event.kind == NK_EVENT_RESOURCE_DATA_COMPLETE && event.request_id == asset_request) {
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
            if (smoke && !gpu_offscreen_smoke()) {
                result = 10;
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
    nk_window window = 0;
    if (nk_window_create(&window_options, &window) != NK_OK)
        return 1;

    nk_surface_options surface_options{};
    surface_options.struct_size = sizeof(surface_options);
    surface_options.flags = NK_SURFACE_FORWARD_COMPATIBLE | NK_SURFACE_STENCIL;
    surface_options.api = nkgpu_default_graphics_api();
    surface_options.major_version = 3;
    if (surface_options.api == NK_GRAPHICS_OPENGL)
        surface_options.minor_version = 3;
    surface_options.width = window_options.width;
    surface_options.height = window_options.height;
    nk_surface surface = 0;
    if (nk_surface_create(window, &surface_options, &surface) != NK_OK)
        return 1;

    CApiShowcase showcase;
    showcase.smoke_sequence = smoke;
    bool running = true;
    bool ready = false;
    int framebuffer_width = 0;
    int framebuffer_height = 0;
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
                        NK_OK &&
                    nk_surface_request_frame(surface) == NK_OK;
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
        if (smoke && showcase.rendered_frames >= 30)
            running = false;
        if (running && !result && idle && nk_wait_events_timeout(1.0 / 60.0) != NK_OK)
            result = 3;
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
