#include "display_list/display_list.h"

#include <cmath>
#include <cstring>
#include <vector>

using namespace nkui;

static bool valid_list_and_growth() {
    DisplayList list(8);
    const float transform[] = {1.0f, 0.0f, 0.0f, 1.0f, 10.0f, 20.0f};
    const auto path = make_resource_id(ResourceKind::Path, 1, 1);
    const auto image = make_resource_id(ResourceKind::Image, 1, 2);
    const auto text = make_resource_id(ResourceKind::TextLayout, 2, 3);
    if (!list.set_transform(transform) || !list.set_global_alpha(0.75f) ||
        !list.set_composite_mode(CompositeMode::SourceOver) || !list.push_state() ||
        !list.clip_rect(0.0f, 0.0f, 100.0f, 50.0f) || !list.draw_path(path) ||
        !list.stroke_path(path, 4.0f, 1, 4, 10.0f) ||
        !list.begin_layer(0.5f) || !list.draw_image(image, 1.0f, 2.0f, 30.0f, 40.0f) ||
        !list.draw_text_layout(text, 4.0f, 5.0f) || !list.end_layer() || !list.pop_state())
        return false;
    ValidationError error{};
    if (!validate_display_list(list.data(), list.size(), &error) || !list.growth_count() ||
        list.command_count() != 12)
        return false;
    const auto capacity = list.capacity();
    const auto growth = list.growth_count();
    list.reset();
    return list.size() == 0 && list.command_count() == 0 && list.capacity() == capacity &&
           list.growth_count() == growth;
}

static bool rejects_bad_streams() {
    ValidationError error{};
    const uint8_t truncated[4] = {};
    if (validate_display_list(truncated, sizeof(truncated), &error) || error.offset != 0)
        return false;

    DisplayList list;
    if (!list.pop_state() || validate_display_list(list.data(), list.size(), &error) ||
        error.command_index != 0)
        return false;

    list.reset();
    if (!list.begin_layer(0.5f) || validate_display_list(list.data(), list.size(), &error) ||
        error.offset != list.size())
        return false;

    list.reset();
    if (!list.draw_path(make_resource_id(ResourceKind::Image, 1, 1)) ||
        validate_display_list(list.data(), list.size(), &error))
        return false;

    list.reset();
    const float transform[] = {1.0f, 0.0f, 0.0f, 1.0f, NAN, 0.0f};
    if (!list.set_transform(transform) || validate_display_list(list.data(), list.size(), &error))
        return false;

    list.reset();
    if (!list.draw_path(make_resource_id(ResourceKind::Path, 1, 1)))
        return false;
    std::vector<uint8_t> corrupt(list.data(), list.data() + list.size());
    CommandHeader header{};
    std::memcpy(&header, corrupt.data(), sizeof(header));
    header.size = UINT32_MAX;
    std::memcpy(corrupt.data(), &header, sizeof(header));
    if (validate_display_list(corrupt.data(), corrupt.size(), &error))
        return false;

    list.reset();
    if (!list.stroke_path(make_resource_id(ResourceKind::Path, 1, 1), 0.0f, 1, 4, 10.0f) ||
        validate_display_list(list.data(), list.size(), &error))
        return false;
    return true;
}

int main() {
    return valid_list_and_growth() && rejects_bad_streams() ? 0 : 1;
}
