#include "nativekit.h"
#include "nativekit_graphics.h"
#include "nativekit_gpu.h"
#include "nativekit_ui.h"
#include "nativekit_ui_layout.h"
#include "nativekit_window.h"

#if defined(__EMSCRIPTEN__) || defined(__ANDROID__)
#include <GLES3/gl3.h>
#elif defined(__APPLE__)
#define GL_SILENCE_DEPRECATION
#include <OpenGL/gl3.h>
#elif defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <GL/gl.h>
#else
#include <GL/gl.h>
#endif

#include <array>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <thread>
#include <vector>

#ifndef NKUI_TEST_FONT_PATH
#error NKUI_TEST_FONT_PATH is required
#endif

namespace {

void write_u32(std::vector<uint8_t> &bytes, std::size_t offset, uint32_t value) {
    std::memcpy(bytes.data() + offset, &value, sizeof(value));
}

void write_i32(std::vector<uint8_t> &bytes, std::size_t offset, int32_t value) {
    std::memcpy(bytes.data() + offset, &value, sizeof(value));
}

void write_float(std::vector<uint8_t> &bytes, std::size_t offset, float value) {
    std::memcpy(bytes.data() + offset, &value, sizeof(value));
}

void write_color(std::vector<uint8_t> &bytes, std::size_t offset, float red, float green,
                 float blue, float alpha) {
    write_float(bytes, offset, red);
    write_float(bytes, offset + 4, green);
    write_float(bytes, offset + 8, blue);
    write_float(bytes, offset + 12, alpha);
}

template <class T> void append_bytes(std::vector<uint8_t> &bytes, const T &value) {
    const std::size_t offset = bytes.size();
    bytes.resize(offset + sizeof(value));
    std::memcpy(bytes.data() + offset, &value, sizeof(value));
}

std::vector<uint8_t> transaction(float width, float height) {
    constexpr uint32_t node_count = 3;
    constexpr std::size_t string_offset =
        NKUI_LAYOUT_TRANSACTION_HEADER_BYTES + NKUI_LAYOUT_NODE_RECORD_BYTES * node_count;
    const char text[] = "Press";
    std::vector<uint8_t> bytes(string_offset + sizeof(text) - 1);
    write_u32(bytes, 0, NKUI_LAYOUT_TRANSACTION_VERSION);
    write_u32(bytes, 4, node_count);
    write_u32(bytes, 8, NKUI_LAYOUT_NODE_RECORD_BYTES);
    write_u32(bytes, 12, static_cast<uint32_t>(string_offset));

    for (uint32_t index = 0; index < node_count; ++index) {
        const std::size_t offset = NKUI_LAYOUT_TRANSACTION_HEADER_BYTES +
                                   static_cast<std::size_t>(index) * NKUI_LAYOUT_NODE_RECORD_BYTES;
        write_float(bytes, offset + NKUI_LAYOUT_NODE_TRANSFORM_A_OFFSET, 1.0f);
        write_float(bytes, offset + NKUI_LAYOUT_NODE_TRANSFORM_D_OFFSET, 1.0f);
        write_float(bytes, offset + NKUI_LAYOUT_NODE_WIDTH_GROW_WEIGHT_OFFSET, 1.0f);
        write_float(bytes, offset + NKUI_LAYOUT_NODE_HEIGHT_GROW_WEIGHT_OFFSET, 1.0f);
        write_u32(bytes, offset + NKUI_LAYOUT_NODE_FLAGS_OFFSET, NKUI_LAYOUT_NODE_VISIBLE);
    }

    const auto record = [&](uint32_t index) {
        return NKUI_LAYOUT_TRANSACTION_HEADER_BYTES +
               static_cast<std::size_t>(index) * NKUI_LAYOUT_NODE_RECORD_BYTES;
    };

    // Root box painted with a solid background.
    write_u32(bytes, record(0) + NKUI_LAYOUT_NODE_ID_OFFSET, 1);
    write_i32(bytes, record(0) + NKUI_LAYOUT_NODE_PARENT_OFFSET, -1);
    write_u32(bytes, record(0) + NKUI_LAYOUT_NODE_VISUAL_KIND_OFFSET, NKUI_LAYOUT_VISUAL_BOX);
    write_u32(bytes, record(0) + NKUI_LAYOUT_NODE_WIDTH_SIZING_OFFSET, NKUI_LAYOUT_SIZING_FIXED);
    write_float(bytes, record(0) + NKUI_LAYOUT_NODE_WIDTH_VALUE_OFFSET, width);
    write_u32(bytes, record(0) + NKUI_LAYOUT_NODE_HEIGHT_SIZING_OFFSET, NKUI_LAYOUT_SIZING_FIXED);
    write_float(bytes, record(0) + NKUI_LAYOUT_NODE_HEIGHT_VALUE_OFFSET, height);
    write_u32(bytes, record(0) + NKUI_LAYOUT_NODE_DIRECTION_OFFSET,
              NKUI_LAYOUT_DIRECTION_TOP_TO_BOTTOM);
    write_color(bytes, record(0) + NKUI_LAYOUT_NODE_BACKGROUND_OFFSET, 0.1f, 0.1f, 0.1f, 1.0f);
    write_float(bytes, record(0) + NKUI_LAYOUT_NODE_FONT_SIZE_OFFSET, 16.0f);
    write_u32(bytes, record(0) + NKUI_LAYOUT_NODE_TEXT_OFFSET_OFFSET,
              static_cast<uint32_t>(string_offset));

    // Button child with an offset box and background.
    write_u32(bytes, record(1) + NKUI_LAYOUT_NODE_ID_OFFSET, 2);
    write_i32(bytes, record(1) + NKUI_LAYOUT_NODE_PARENT_OFFSET, 0);
    write_u32(bytes, record(1) + NKUI_LAYOUT_NODE_VISUAL_KIND_OFFSET, NKUI_LAYOUT_VISUAL_BOX);
    write_u32(bytes, record(1) + NKUI_LAYOUT_NODE_WIDTH_SIZING_OFFSET, NKUI_LAYOUT_SIZING_FIXED);
    write_float(bytes, record(1) + NKUI_LAYOUT_NODE_WIDTH_VALUE_OFFSET, 160.0f);
    write_u32(bytes, record(1) + NKUI_LAYOUT_NODE_HEIGHT_SIZING_OFFSET, NKUI_LAYOUT_SIZING_FIXED);
    write_float(bytes, record(1) + NKUI_LAYOUT_NODE_HEIGHT_VALUE_OFFSET, 64.0f);
    write_u32(bytes, record(1) + NKUI_LAYOUT_NODE_DIRECTION_OFFSET,
              NKUI_LAYOUT_DIRECTION_TOP_TO_BOTTOM);
    write_float(bytes, record(1) + NKUI_LAYOUT_NODE_TRANSFORM_TX_OFFSET, 12.0f);
    write_float(bytes, record(1) + NKUI_LAYOUT_NODE_TRANSFORM_TY_OFFSET, 20.0f);
    write_color(bytes, record(1) + NKUI_LAYOUT_NODE_BACKGROUND_OFFSET, 0.2f, 0.5f, 0.9f, 1.0f);
    write_float(bytes, record(1) + NKUI_LAYOUT_NODE_FONT_SIZE_OFFSET, 16.0f);
    write_u32(bytes, record(1) + NKUI_LAYOUT_NODE_TEXT_OFFSET_OFFSET,
              static_cast<uint32_t>(string_offset));

    // Text child.
    write_u32(bytes, record(2) + NKUI_LAYOUT_NODE_ID_OFFSET, 3);
    write_i32(bytes, record(2) + NKUI_LAYOUT_NODE_PARENT_OFFSET, 1);
    write_u32(bytes, record(2) + NKUI_LAYOUT_NODE_VISUAL_KIND_OFFSET, NKUI_LAYOUT_VISUAL_TEXT);
    write_u32(bytes, record(2) + NKUI_LAYOUT_NODE_WIDTH_SIZING_OFFSET, NKUI_LAYOUT_SIZING_FIT);
    write_u32(bytes, record(2) + NKUI_LAYOUT_NODE_HEIGHT_SIZING_OFFSET, NKUI_LAYOUT_SIZING_FIT);
    write_u32(bytes, record(2) + NKUI_LAYOUT_NODE_DIRECTION_OFFSET,
              NKUI_LAYOUT_DIRECTION_TOP_TO_BOTTOM);
    write_u32(bytes, record(2) + NKUI_LAYOUT_NODE_TEXT_OFFSET_OFFSET,
              static_cast<uint32_t>(string_offset));
    write_u32(bytes, record(2) + NKUI_LAYOUT_NODE_TEXT_LENGTH_OFFSET, sizeof(text) - 1);
    write_color(bytes, record(2) + NKUI_LAYOUT_NODE_TEXT_COLOR_OFFSET, 1.0f, 1.0f, 1.0f, 1.0f);
    write_float(bytes, record(2) + NKUI_LAYOUT_NODE_FONT_SIZE_OFFSET, 18.0f);

    std::memcpy(bytes.data() + string_offset, text, sizeof(text) - 1);
    return bytes;
}

} // namespace

int main() {
    nk_init_options init{};
    init.struct_size = sizeof(init);
    init.api_version = NK_API_VERSION;
    if (nk_init(&init) != NK_OK) {
        std::fprintf(stderr, "nk_init failed\n");
        return 1;
    }

    constexpr int width = 256;
    constexpr int height = 192;
    nk_window_options window_options{};
    window_options.struct_size = sizeof(window_options);
    window_options.width = width;
    window_options.height = height;
    window_options.title = "NativeKit layout session render";
    nk_window window = 0;
    if (nk_window_create(&window_options, &window) != NK_OK) {
        std::fprintf(stderr, "window create failed\n");
        return 2;
    }

    nk_surface_options surface_options{};
    surface_options.struct_size = sizeof(surface_options);
    surface_options.api = nkgpu_default_graphics_api();
    surface_options.flags = NK_SURFACE_STENCIL;
    if (surface_options.api == NK_GRAPHICS_OPENGL)
        surface_options.flags |= NK_SURFACE_FORWARD_COMPATIBLE;
    surface_options.major_version = 3;
    if (surface_options.api == NK_GRAPHICS_OPENGL)
        surface_options.minor_version = 3;
    surface_options.width = width;
    surface_options.height = height;
    nk_surface surface = 0;
    if (nk_surface_create(window, &surface_options, &surface) != NK_OK) {
        std::fprintf(stderr, "surface create failed\n");
        return 3;
    }

    nkui_resource fonts{};
    nkui_renderer renderer{};
    nkui_display_list list{};
    nkui_layout_session session{};
    if (nkui_font_collection_create(&fonts) != NKUI_OK ||
        nkui_font_collection_add(fonts, NKUI_TEST_FONT_PATH, NKUI_FONT_FAMILY_DEFAULT) != NKUI_OK ||
        nkui_renderer_create(&renderer) != NKUI_OK || nkui_display_list_create(&list) != NKUI_OK ||
        nkui_layout_session_create(&session) != NKUI_OK ||
        nkui_layout_session_set_font_collection(session, fonts) != NKUI_OK) {
        std::fprintf(stderr, "resource setup failed\n");
        return 4;
    }

    bool ready = false;
    int framebuffer_width = 0;
    int framebuffer_height = 0;
    for (int attempt = 0; attempt < 5000 && !ready; ++attempt) {
        nk_event event{};
        event.struct_size = sizeof(event);
        if (nk_poll_event(&event) == NK_OK && event.kind == NK_EVENT_SURFACE_READY &&
            event.source == surface) {
            ready = nk_surface_make_current(surface) == NK_OK &&
                    nk_surface_get_framebuffer_size(surface, &framebuffer_width,
                                                    &framebuffer_height) == NK_OK;
        }
        nk_event_release(&event);
        if (!ready)
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    if (!ready) {
        std::fprintf(stderr, "surface never became ready\n");
        return 5;
    }

    const float pixel_scale = 1.0f;
    const nkui_frame_info frame_info{sizeof(frame_info),
                                     framebuffer_width / pixel_scale,
                                     framebuffer_height / pixel_scale,
                                     framebuffer_width,
                                     framebuffer_height,
                                     pixel_scale};

    int result = 0;

    /*
     * A session that has never been successfully submitted has no layout to
     * render; the render entry point must reject it as an invalid handle
     * rather than rendering an empty frame. This is the failure mode that a
     * malformed transaction produces: submit fails, the session stays
     * unsubmitted, and the render call then reports NKUI_ERROR_INVALID_HANDLE.
     */
    const nkui_result unsubmitted_render =
        nkui_layout_session_render_frame(renderer, session, surface, &frame_info, 0);
    if (unsubmitted_render != NKUI_ERROR_INVALID_HANDLE) {
        std::fprintf(stderr, "unsubmitted session render returned %d, expected %d\n",
                     static_cast<int>(unsubmitted_render),
                     static_cast<int>(NKUI_ERROR_INVALID_HANDLE));
        result = 13;
    }

    const nkui_result display_list_result =
        nkui_renderer_render_frame(renderer, list, surface, &frame_info);
    if (display_list_result != NKUI_OK)
        result = 10;

    const auto bytes =
        transaction(framebuffer_width / pixel_scale, framebuffer_height / pixel_scale);
    nkui_layout_frame_input frame{sizeof(frame), framebuffer_width / pixel_scale,
                                  framebuffer_height / pixel_scale, 1.0f / 60.0f};
    const nkui_result submit_result = nkui_layout_session_submit(
        session, bytes.data(), static_cast<uint32_t>(bytes.size()), &frame);
    if (submit_result != NKUI_OK) {
        std::fprintf(stderr, "session submit returned %d\n", static_cast<int>(submit_result));
        result = 11;
    }

    if (!result) {
        const nkui_result session_result =
            nkui_layout_session_render_frame(renderer, session, surface, &frame_info, 0);
        if (session_result != NKUI_OK) {
            std::fprintf(stderr, "session render returned %d\n", static_cast<int>(session_result));
            result = 12;
        }
    }

    /*
     * The same session renders across pixel scales. Each frame clears first,
     * so reading a corner pixel outside the button proves the layout actually
     * reached the framebuffer instead of the call merely returning OK.
     */
    if (!result) {
        constexpr float scales[] = {1.0f, 1.25f, 2.0f};
        for (float scale : scales) {
            const nkui_frame_info scaled_info{sizeof(scaled_info),        framebuffer_width / scale,
                                              framebuffer_height / scale, framebuffer_width,
                                              framebuffer_height,         scale};
            const nkui_result scaled_result =
                nkui_layout_session_render_frame(renderer, session, surface, &scaled_info, 0);
            if (scaled_result != NKUI_OK) {
                std::fprintf(stderr, "session render at scale %g returned %d\n",
                             static_cast<double>(scale), static_cast<int>(scaled_result));
                result = 14;
                break;
            }
            uint8_t corner[4]{};
            glReadPixels(4, 4, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, corner);
            if (corner[0] != corner[1] || corner[1] != corner[2] || corner[3] != 255 ||
                corner[0] < 10 || corner[0] > 60) {
                std::fprintf(stderr,
                             "session render at scale %g left corner pixel "
                             "(%u,%u,%u,%u)\n",
                             static_cast<double>(scale), corner[0], corner[1], corner[2],
                             corner[3]);
                result = 15;
                break;
            }
        }
    }

    /* Compositing over the existing frame keeps the path out of the clear-first
     * branch and must still succeed. */
    if (!result &&
        nkui_layout_session_render_frame(renderer, session, surface, &frame_info, 1) != NKUI_OK)
        result = 16;

    /*
     * Custom-visual nodes render through the compiler's embedded-plan path
     * instead of plain primitives, so drive one custom paint and confirm its
     * content reaches the node's bounds.
     */
    if (!result) {
        constexpr std::size_t second_record =
            NKUI_LAYOUT_TRANSACTION_HEADER_BYTES + NKUI_LAYOUT_NODE_RECORD_BYTES;
        auto custom_tree =
            transaction(framebuffer_width / pixel_scale, framebuffer_height / pixel_scale);
        write_u32(custom_tree, second_record + NKUI_LAYOUT_NODE_VISUAL_KIND_OFFSET,
                  NKUI_LAYOUT_VISUAL_CUSTOM);

        const nkui_path_element rectangle[] = {
            {NKUI_PATH_MOVE_TO, {0.0f, 0.0f}},
            {NKUI_PATH_LINE_TO, {160.0f, 0.0f}},
            {NKUI_PATH_LINE_TO, {160.0f, 64.0f}},
            {NKUI_PATH_LINE_TO, {0.0f, 64.0f}},
            {NKUI_PATH_CLOSE, {}},
        };
        nkui_resource custom_paint{};
        nkui_resource custom_path{};
        nkui_display_list custom_list{};
        std::vector<uint8_t> custom_commands;
        if (nkui_paint_create_solid({1.0f, 0.0f, 0.0f, 1.0f}, &custom_paint) != NKUI_OK ||
            nkui_path_create(rectangle, 5, &custom_path) != NKUI_OK ||
            nkui_display_list_create(&custom_list) != NKUI_OK)
            result = 18;
        if (!result) {
            append_bytes(custom_commands,
                         nkui_resource_command{{NKUI_COMMAND_SET_PAINT, NKUI_COMMAND_VERSION,
                                                sizeof(nkui_resource_command)},
                                               custom_paint});
            /*
             * Custom paint commands carry their own placement: the layout
             * session does not re-apply the node transform, so the list is
             * responsible for putting content where the node sits.
             */
            append_bytes(custom_commands,
                         nkui_transform_command{{NKUI_COMMAND_SET_TRANSFORM, NKUI_COMMAND_VERSION,
                                                 sizeof(nkui_transform_command)},
                                                {1.0f, 0.0f, 0.0f, 1.0f, 12.0f, 20.0f}});
            append_bytes(custom_commands,
                         nkui_resource_command{{NKUI_COMMAND_DRAW_PATH, NKUI_COMMAND_VERSION,
                                                sizeof(nkui_resource_command)},
                                               custom_path});
            if (nkui_display_list_submit(custom_list, custom_commands.data(),
                                         static_cast<uint32_t>(custom_commands.size())) !=
                    NKUI_OK ||
                nkui_layout_session_submit(session, custom_tree.data(),
                                           static_cast<uint32_t>(custom_tree.size()),
                                           &frame) != NKUI_OK ||
                nkui_layout_session_set_custom_paint(session, 2, custom_list) != NKUI_OK ||
                nkui_layout_session_render_frame(renderer, session, surface, &frame_info, 0) !=
                    NKUI_OK)
                result = 18;
        }
        if (!result) {
            /*
             * Node 2 sits at (12, 20) with a 160x64 box, so its custom paint
             * covers framebuffer x 12..172, y (bottom-up) 108..172.
             */
            std::array<uint8_t, 20 * 20 * 4> block{};
            glReadPixels(40, 120, 20, 20, GL_RGBA, GL_UNSIGNED_BYTE, block.data());
            int red = 0;
            for (std::size_t pixel = 0; pixel < 20 * 20; ++pixel) {
                const uint8_t *rgba = block.data() + pixel * 4;
                if (rgba[0] > 200 && rgba[1] < 60 && rgba[2] < 60 && rgba[3] == 255)
                    ++red;
            }
            if (red < 20 * 20) {
                std::fprintf(stderr, "custom paint covered %d of %d sampled pixels\n", red,
                             20 * 20);
                result = 19;
            }
        }
        nkui_layout_session_clear_custom_paints(session);
        nkui_display_list_destroy(custom_list);
        nkui_resource_destroy(custom_path);
        nkui_resource_destroy(custom_paint);
    }

    if (nkui_layout_session_destroy(session) != NKUI_OK ||
        nkui_layout_session_destroy(session) != NKUI_ERROR_INVALID_HANDLE)
        result = 17;
    nkui_display_list_destroy(list);
    nkui_renderer_destroy(renderer);
    nkui_resource_destroy(fonts);
    nk_surface_destroy(surface);
    nk_window_destroy(window);
    nk_shutdown();
    if (!result)
        std::printf("PASS: layout-session render path renders primitives, scales, and custom "
                    "paints\n");
    return result;
}
