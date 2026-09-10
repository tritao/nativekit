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
    std::vector<nkui_resource> content_resources;
    std::vector<uint8_t> commands;
    nkui_resource navy{};
    nkui_resource rail_paint{};
    nkui_resource card_paint{};
    nkui_resource green{};
    nkui_resource violet{};
    nkui_resource cyan{};
    nkui_resource image{};
    bool frame_failed = false;
    int rendered_frames = 0;
    int last_width = 0;
    int last_height = 0;

    static void NK_CALL draw_frame(nk_handle surface, int32_t width, int32_t height,
                                   void *user_data) {
        auto &showcase = *static_cast<Showcase *>(user_data);
        if (!showcase.resize(width, height) ||
            nkui_renderer_render(showcase.renderer, showcase.list, surface) != NKUI_OK)
            showcase.frame_failed = true;
        else
            ++showcase.rendered_frames;
    }

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
        navy = retain(make_paint(0.025f, 0.04f, 0.09f));
        rail_paint = retain(make_paint(0.055f, 0.085f, 0.16f));
        card_paint = retain(make_paint(0.09f, 0.13f, 0.23f));
        green = retain(make_paint(0.12f, 0.72f, 0.48f));
        violet = retain(make_paint(0.49f, 0.32f, 0.92f));
        cyan = retain(make_paint(0.12f, 0.67f, 0.9f));
        if (!navy.id || !rail_paint.id || !card_paint.id || !green.id || !violet.id || !cyan.id)
            return false;

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
        if (nkui_image_create(16, 16, NKUI_IMAGE_RGBA8, checker.data(), checker.size(), &image) !=
            NKUI_OK)
            return false;
        resources.push_back(image);

        return resize(framebuffer_width, framebuffer_height);
    }

    bool resize(int framebuffer_width, int framebuffer_height) {
        if (framebuffer_width <= 0 || framebuffer_height <= 0)
            return false;
        if (framebuffer_width == last_width && framebuffer_height == last_height)
            return true;
        const float scale = std::min(framebuffer_width / 900.0f, framebuffer_height / 600.0f);
        const float offset_x = (framebuffer_width - 900.0f * scale) * 0.5f;
        const float offset_y = (framebuffer_height - 600.0f * scale) * 0.5f;

        const auto x = [&](float value) { return offset_x + value * scale; };
        const auto y = [&](float value) { return offset_y + value * scale; };
        const auto s = [&](float value) { return value * scale; };
        std::vector<nkui_resource> next_resources;
        const auto retain = [&](nkui_resource resource) {
            if (resource.id)
                next_resources.push_back(resource);
            return resource;
        };
        const auto rect = [&](float left, float top, float width, float height) {
            return retain(make_rect(x(left), y(top), s(width), s(height)));
        };

        const nkui_resource background =
            retain(make_rect(0.0f, 0.0f, static_cast<float>(framebuffer_width),
                             static_cast<float>(framebuffer_height)));
        const nkui_resource rail = rect(0.0f, 0.0f, 250.0f, 600.0f);
        const nkui_resource card = rect(292.0f, 96.0f, 552.0f, 220.0f);
        const nkui_resource lower_card = rect(292.0f, 350.0f, 264.0f, 174.0f);
        const nkui_resource accent = rect(584.0f, 350.0f, 260.0f, 174.0f);
        const nkui_resource badge = rect(55.0f, 67.0f, 140.0f, 44.0f);
        const nkui_resource floating_badge = rect(83.0f, 457.0f, 140.0f, 44.0f);
        nkui_resource title{}, subtitle{}, section{}, details{};
        const bool valid = background.id && rail.id && card.id && lower_card.id && accent.id &&
                           badge.id && floating_badge.id &&
                           nkui_text_layout_create(fonts, "NativeKit Graphics", s(560.0f),
                                                   s(38.0f), &title) == NKUI_OK &&
                           nkui_text_layout_create(
                               fonts, "One display list · one compositor · one GPU owner",
                               s(560.0f), s(19.0f), &subtitle) == NKUI_OK &&
                           nkui_text_layout_create(fonts, "Skribidi text + NanoVG paths",
                                                   s(500.0f), s(25.0f), &section) == NKUI_OK &&
                           nkui_text_layout_create(
                               fonts, "Retained resources\nExact ordering\nIsolated opacity layers",
                               s(220.0f), s(18.0f), &details) == NKUI_OK;
        for (nkui_resource text : {title, subtitle, section, details})
            if (text.id)
                next_resources.push_back(text);
        if (!valid) {
            for (auto resource : next_resources)
                nkui_resource_destroy(resource);
            return false;
        }

        std::vector<uint8_t> next_commands;
        set_paint(next_commands, navy);
        draw_path(next_commands, background);
        set_paint(next_commands, rail_paint);
        draw_path(next_commands, rail);
        set_paint(next_commands, green);
        draw_path(next_commands, badge);
        draw_text(next_commands, title, x(292.0f), y(38.0f));
        draw_text(next_commands, subtitle, x(294.0f), y(76.0f));
        set_paint(next_commands, card_paint);
        draw_path(next_commands, card);
        draw_path(next_commands, lower_card);
        draw_text(next_commands, section, x(326.0f), y(138.0f));
        draw_text(next_commands, details, x(326.0f), y(382.0f));
        append(next_commands,
               nkui_command_header{header(NKUI_COMMAND_PUSH_STATE, sizeof(nkui_command_header))});
        append(next_commands,
               nkui_rect_command{header(NKUI_COMMAND_CLIP_RECT, sizeof(nkui_rect_command)),
                                 x(610.0f), y(374.0f), s(208.0f), s(126.0f)});
        append(next_commands,
               nkui_layer_command{header(NKUI_COMMAND_BEGIN_LAYER, sizeof(nkui_layer_command)),
                                  0.72f, NKUI_COMPOSITE_SOURCE_OVER});
        set_paint(next_commands, violet);
        draw_path(next_commands, accent);
        append(next_commands,
               nkui_draw_rect_command{header(NKUI_COMMAND_DRAW_IMAGE,
                                             sizeof(nkui_draw_rect_command)),
                                      image, x(615.0f), y(375.0f), s(200.0f), s(124.0f)});
        append(next_commands,
               nkui_command_header{header(NKUI_COMMAND_END_LAYER, sizeof(nkui_command_header))});
        append(next_commands,
               nkui_command_header{header(NKUI_COMMAND_POP_STATE, sizeof(nkui_command_header))});
        set_paint(next_commands, cyan);
        draw_path(next_commands, floating_badge);

        if (nkui_display_list_submit(list, next_commands.data(), next_commands.size()) != NKUI_OK) {
            for (auto resource : next_resources)
                nkui_resource_destroy(resource);
            return false;
        }
        for (auto resource : content_resources)
            nkui_resource_destroy(resource);
        content_resources = std::move(next_resources);
        commands = std::move(next_commands);
        last_width = framebuffer_width;
        last_height = framebuffer_height;
        return true;
    }

    void destroy() {
        if (renderer.id)
            nkui_renderer_destroy(renderer);
        if (list.id)
            nkui_display_list_destroy(list);
        for (auto resource : content_resources)
            nkui_resource_destroy(resource);
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
            ready =
                nk_surface_make_current(surface) == NK_OK &&
                nk_surface_get_framebuffer_size(surface, &framebuffer_width, &framebuffer_height) ==
                    NK_OK &&
                showcase.create(framebuffer_width, framebuffer_height) &&
                nk_surface_set_frame_callback(surface, Showcase::draw_frame, &showcase) == NK_OK;
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
}
