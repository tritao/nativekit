#include "nativekit.h"
#include "nativekit_graphics.h"
#include "nativekit_ui.h"
#include "nativekit_window.h"

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

struct Showcase {
    nkui_renderer renderer{};
    nkui_display_list list{};
    nkui_resource fonts{};
    std::vector<nkui_resource> resources;
    std::vector<uint8_t> commands;
    size_t accent_transform_offset = 0;

    bool create(int framebuffer_width, int framebuffer_height) {
        if (nkui_renderer_create(&renderer) != NKUI_OK ||
            nkui_display_list_create(&list) != NKUI_OK ||
            nkui_font_collection_create(&fonts) != NKUI_OK ||
            nkui_font_collection_add(fonts, NKUI_TEST_FONT_PATH, NKUI_FONT_FAMILY_DEFAULT) !=
                NKUI_OK)
            return false;

        const auto retain = [this](nkui_resource resource) {
            if (resource.id)
                resources.push_back(resource);
            return resource;
        };
        const nkui_resource background = retain(make_rect(0.0f, 0.0f, 900.0f, 600.0f));
        const nkui_resource rail = retain(make_rect(0.0f, 0.0f, 250.0f, 600.0f));
        const nkui_resource card = retain(make_rect(292.0f, 96.0f, 552.0f, 220.0f));
        const nkui_resource lower_card = retain(make_rect(292.0f, 350.0f, 264.0f, 174.0f));
        const nkui_resource accent = retain(make_rect(584.0f, 350.0f, 260.0f, 174.0f));
        const nkui_resource badge = retain(make_rect(55.0f, 67.0f, 140.0f, 44.0f));
        const nkui_resource navy = retain(make_paint(0.025f, 0.04f, 0.09f));
        const nkui_resource rail_paint = retain(make_paint(0.055f, 0.085f, 0.16f));
        const nkui_resource card_paint = retain(make_paint(0.09f, 0.13f, 0.23f));
        const nkui_resource green = retain(make_paint(0.12f, 0.72f, 0.48f));
        const nkui_resource violet = retain(make_paint(0.49f, 0.32f, 0.92f));
        const nkui_resource cyan = retain(make_paint(0.12f, 0.67f, 0.9f));
        if (!background.id || !rail.id || !card.id || !lower_card.id || !accent.id || !badge.id ||
            !navy.id || !rail_paint.id || !card_paint.id || !green.id || !violet.id || !cyan.id)
            return false;

        nkui_resource title{}, subtitle{}, section{}, details{};
        if (nkui_text_layout_create(fonts, "NativeKit Graphics", 560.0f, 38.0f, &title) !=
                NKUI_OK ||
            nkui_text_layout_create(fonts, "One display list · one compositor · one GPU owner",
                                    560.0f, 19.0f, &subtitle) != NKUI_OK ||
            nkui_text_layout_create(fonts, "Skribidi text + NanoVG paths", 500.0f, 25.0f,
                                    &section) != NKUI_OK ||
            nkui_text_layout_create(fonts,
                                    "Retained resources\nExact ordering\nIsolated opacity layers",
                                    220.0f, 18.0f, &details) != NKUI_OK)
            return false;
        resources.push_back(title);
        resources.push_back(subtitle);
        resources.push_back(section);
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
        set_paint(commands, rail_paint);
        draw_path(commands, rail);
        set_paint(commands, green);
        draw_path(commands, badge);
        draw_text(commands, title, 292.0f, 38.0f);
        draw_text(commands, subtitle, 294.0f, 76.0f);
        set_paint(commands, card_paint);
        draw_path(commands, card);
        draw_path(commands, lower_card);
        draw_text(commands, section, 326.0f, 138.0f);
        draw_text(commands, details, 326.0f, 382.0f);

        append(commands,
               nkui_command_header{header(NKUI_COMMAND_PUSH_STATE, sizeof(nkui_command_header))});
        append(commands,
               nkui_rect_command{header(NKUI_COMMAND_CLIP_RECT, sizeof(nkui_rect_command)), 610.0f,
                                 374.0f, 208.0f, 126.0f});
        append(commands,
               nkui_layer_command{header(NKUI_COMMAND_BEGIN_LAYER, sizeof(nkui_layer_command)),
                                  0.72f, NKUI_COMPOSITE_SOURCE_OVER});
        set_paint(commands, violet);
        draw_path(commands, accent);
        append(commands, nkui_draw_rect_command{
                             header(NKUI_COMMAND_DRAW_IMAGE, sizeof(nkui_draw_rect_command)), image,
                             615.0f, 375.0f, 200.0f, 124.0f});
        append(commands,
               nkui_command_header{header(NKUI_COMMAND_END_LAYER, sizeof(nkui_command_header))});
        append(commands,
               nkui_command_header{header(NKUI_COMMAND_POP_STATE, sizeof(nkui_command_header))});

        append(commands,
               nkui_command_header{header(NKUI_COMMAND_PUSH_STATE, sizeof(nkui_command_header))});
        accent_transform_offset = commands.size();
        append(commands, nkui_transform_command{
                             header(NKUI_COMMAND_SET_TRANSFORM, sizeof(nkui_transform_command)),
                             {1.0f, 0.0f, 0.0f, 1.0f, 28.0f, 390.0f}});
        set_paint(commands, cyan);
        draw_path(commands, badge);
        append(commands,
               nkui_command_header{header(NKUI_COMMAND_POP_STATE, sizeof(nkui_command_header))});

        return resize(framebuffer_width, framebuffer_height);
    }

    bool resize(int framebuffer_width, int framebuffer_height) {
        if (commands.size() < sizeof(nkui_transform_command) || framebuffer_width <= 0 ||
            framebuffer_height <= 0)
            return false;
        const float scale = std::min(framebuffer_width / 900.0f, framebuffer_height / 600.0f);
        const float offset_x = (framebuffer_width - 900.0f * scale) * 0.5f;
        const float offset_y = (framebuffer_height - 600.0f * scale) * 0.5f;
        const nkui_transform_command transform{
            header(NKUI_COMMAND_SET_TRANSFORM, sizeof(nkui_transform_command)),
            {scale, 0.0f, 0.0f, scale, offset_x, offset_y}};
        std::memcpy(commands.data(), &transform, sizeof(transform));
        const nkui_transform_command accent_transform{
            header(NKUI_COMMAND_SET_TRANSFORM, sizeof(nkui_transform_command)),
            {scale, 0.0f, 0.0f, scale, offset_x + 28.0f * scale, offset_y + 390.0f * scale}};
        if (accent_transform_offset > commands.size() - sizeof(accent_transform))
            return false;
        std::memcpy(commands.data() + accent_transform_offset, &accent_transform,
                    sizeof(accent_transform));
        return nkui_display_list_submit(list, commands.data(), commands.size()) == NKUI_OK;
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

} // namespace

int main(int argc, char **argv) {
    const bool smoke = argc == 2 && std::strcmp(argv[1], "--smoke-test") == 0;
    if (argc > 1 && !smoke)
        return 2;
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
    window_options.title = "NativeKit UI Showcase";
    nk_handle window = NK_INVALID_HANDLE;
    if (nk_window_create(&window_options, &window) != NK_OK)
        return 1;
    nk_surface_options surface_options{};
    surface_options.struct_size = sizeof(surface_options);
    surface_options.flags = NK_SURFACE_FORWARD_COMPATIBLE | NK_SURFACE_STENCIL;
    surface_options.api = NK_GRAPHICS_OPENGL;
    surface_options.major_version = 3;
    surface_options.minor_version = 3;
    surface_options.width = window_options.width;
    surface_options.height = window_options.height;
    nk_handle surface = NK_INVALID_HANDLE;
    if (nk_surface_create(window, &surface_options, &surface) != NK_OK)
        return 1;

    Showcase showcase;
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
                    showcase.create(framebuffer_width, framebuffer_height);
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
        if (ready && running) {
            if (nkui_renderer_render(showcase.renderer, showcase.list, surface) != NKUI_OK ||
                nk_surface_present(surface) != NK_OK)
                result = 5;
            if (smoke && ++frames == 30)
                running = false;
        } else if (idle) {
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
        }
    }
    if (ready)
        nk_surface_make_current(surface);
    showcase.destroy();
    nk_surface_destroy(surface);
    nk_window_destroy(window);
    nk_shutdown();
    return result;
}
