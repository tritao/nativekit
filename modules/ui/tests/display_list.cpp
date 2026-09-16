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

    list.reset();
    if (!list.begin_layer(1.0f, LayerBounds{4.0f, 8.0f, 32.0f, 24.0f}) ||
        !list.end_layer() || !validate_display_list(list.data(), list.size(), &error))
        return false;

    list.reset();
    EffectDescriptor effect{};
    effect.kind = EffectKind::ColorMatrix;
    effect.color_matrix[0] = 1.0f;
    effect.color_matrix[6] = 1.0f;
    effect.color_matrix[12] = 1.0f;
    effect.color_matrix[18] = 1.0f;
    if (!list.begin_layer(1.0f, LayerBounds{4.0f, 8.0f, 32.0f, 24.0f}, effect) ||
        !list.end_layer() || !validate_display_list(list.data(), list.size(), &error))
        return false;

    list.reset();
    MaskDescriptor mask{};
    mask.kind = MaskKind::RoundedRect;
    mask.values[0] = 6.0f;
    if (!list.begin_layer(1.0f, LayerBounds{4.0f, 8.0f, 32.0f, 24.0f}, mask) ||
        !list.end_layer() || !validate_display_list(list.data(), list.size(), &error))
        return false;

    list.reset();
    EffectDescriptor backdrop_effect{};
    backdrop_effect.kind = EffectKind::ColorMatrix;
    backdrop_effect.color_matrix[0] = 1.0f;
    backdrop_effect.color_matrix[6] = 1.0f;
    backdrop_effect.color_matrix[12] = 1.0f;
    backdrop_effect.color_matrix[18] = 1.0f;
    if (!list.begin_layer(1.0f, LayerBounds{4.0f, 8.0f, 32.0f, 24.0f}, EffectDescriptor{},
                          MaskDescriptor{}, backdrop_effect) ||
        !list.end_layer() || !validate_display_list(list.data(), list.size(), &error) ||
        !list.has_backdrop_effects())
        return false;

    list.reset();
    mask.kind = MaskKind::LinearGradient;
    mask.values[0] = 0.0f;
    mask.values[1] = 0.0f;
    mask.values[2] = 1.0f;
    mask.values[3] = 0.0f;
    mask.values[4] = 0.0f;
    mask.values[5] = 1.0f;
    if (!list.begin_layer(1.0f, mask) || !list.end_layer() ||
        !validate_display_list(list.data(), list.size(), &error))
        return false;

    list.reset();
    mask.kind = MaskKind::Image;
    mask.image = make_resource_id(ResourceKind::Image, 1, 2);
    if (!list.begin_layer(1.0f, mask) || !list.end_layer() ||
        !validate_display_list(list.data(), list.size(), &error))
        return false;

    list.reset();
    mask.image = make_resource_id(ResourceKind::Path, 1, 2);
    if (!list.begin_layer(1.0f, mask) || validate_display_list(list.data(), list.size(), &error))
        return false;

    list.reset();
    effect.kind = static_cast<EffectKind>(99);
    if (!list.begin_layer(1.0f, effect) || validate_display_list(list.data(), list.size(), &error))
        return false;

    list.reset();
    effect.kind = EffectKind::ColorMatrix;
    effect.color_matrix[0] = NAN;
    if (!list.begin_layer(1.0f, effect) || validate_display_list(list.data(), list.size(), &error))
        return false;

    list.reset();
    effect = {};
    effect.kind = EffectKind::Blur;
    effect.color_matrix[0] = 4.0f;
    if (!list.begin_layer(1.0f, LayerBounds{4.0f, 8.0f, 32.0f, 24.0f}, effect) ||
        !list.end_layer() || !validate_display_list(list.data(), list.size(), &error))
        return false;

    list.reset();
    effect.color_matrix[0] = -1.0f;
    if (!list.begin_layer(1.0f, effect) || validate_display_list(list.data(), list.size(), &error))
        return false;

    list.reset();
    effect = {};
    effect.kind = EffectKind::DropShadow;
    effect.color_matrix[0] = 4.0f;
    effect.color_matrix[2] = 1.0f;
    effect.color_matrix[3] = 6.0f;
    effect.color_matrix[7] = 0.35f;
    if (!list.begin_layer(1.0f, LayerBounds{4.0f, 8.0f, 32.0f, 24.0f}, effect) ||
        !list.end_layer() || !validate_display_list(list.data(), list.size(), &error))
        return false;

    list.reset();
    effect.color_matrix[7] = 1.1f;
    if (!list.begin_layer(1.0f, effect) || validate_display_list(list.data(), list.size(), &error))
        return false;

    list.reset();
    if (!list.begin_layer(1.0f, LayerBounds{4.0f, 8.0f, 0.0f, 24.0f}) ||
        validate_display_list(list.data(), list.size(), &error))
        return false;
    return true;
}

int main() {
    return valid_list_and_growth() && rejects_bad_streams() ? 0 : 1;
}
