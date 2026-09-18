#include "core/text_edit_transaction.hpp"
#include "core/text_input_contract.hpp"
#include "core/text_offsets.hpp"
#include "core/text_input_geometry.hpp"

#include <cassert>
#include <cmath>
#include <cstddef>
#include <cstring>
#include <string>
#include <vector>

int main() {
    using nk::core::TextEditTransaction;
    using nk::core::TextInputStateValidation;
    using nk::core::utf16_to_codepoint_offset;

    static_assert(sizeof(nk_text_edit_event) == 48);
    static_assert(offsetof(nk_text_edit_event, history_kind) == 40);
    assert(nk::core::infer_text_edit_history_kind(NK_TEXT_EDIT_COMPOSE, 2, 2, 2, 2) ==
           NK_TEXT_EDIT_HISTORY_COMPOSITION);
    assert(nk::core::infer_text_edit_history_kind(NK_TEXT_EDIT_COMPOSE, NK_TEXT_POSITION_NONE,
                                                  NK_TEXT_POSITION_NONE, 2,
                                                  2) == NK_TEXT_EDIT_HISTORY_COMPOSITION);
    assert(nk::core::infer_text_edit_history_kind(NK_TEXT_EDIT_COMMIT, 2, 2, 2, 2) ==
           NK_TEXT_EDIT_HISTORY_TYPING);
    assert(nk::core::infer_text_edit_history_kind(NK_TEXT_EDIT_COMMIT, 2, 2, 1, 2) ==
           NK_TEXT_EDIT_HISTORY_GENERIC);
    assert(nk::core::infer_text_edit_history_kind(NK_TEXT_EDIT_DELETE, 2, 2, 1, 2) ==
           NK_TEXT_EDIT_HISTORY_DELETE_BACKWARD);
    assert(nk::core::infer_text_edit_history_kind(NK_TEXT_EDIT_DELETE, 2, 2, 2, 3) ==
           NK_TEXT_EDIT_HISTORY_DELETE_FORWARD);

    const std::u16string unicode = u"A\U0001f600B";
    uint32_t codepoint_offset = 99;
    assert(utf16_to_codepoint_offset(std::u16string_view(unicode), 0, &codepoint_offset) &&
           codepoint_offset == 0);
    assert(utf16_to_codepoint_offset(std::u16string_view(unicode), 1, &codepoint_offset) &&
           codepoint_offset == 1);
    assert(!utf16_to_codepoint_offset(std::u16string_view(unicode), 2, &codepoint_offset));
    assert(utf16_to_codepoint_offset(std::u16string_view(unicode), 3, &codepoint_offset) &&
           codepoint_offset == 2);
    assert(utf16_to_codepoint_offset(std::u16string_view(unicode), 4, &codepoint_offset) &&
           codepoint_offset == 3);

    const std::u16string unpaired_high(1, static_cast<char16_t>(0xd800));
    const std::u16string unpaired_low(1, static_cast<char16_t>(0xdc00));
    assert(!utf16_to_codepoint_offset(std::u16string_view(unpaired_high), 1, &codepoint_offset));
    assert(!utf16_to_codepoint_offset(std::u16string_view(unpaired_low), 1, &codepoint_offset));

    nk::core::TextOffsetMap offset_map;
    assert(offset_map.assign("a\U0001f600e\xcc\x81"));
    std::size_t byte_offset = 0;
    std::size_t unit_offset = 0;
    assert(offset_map.utf8ByteOffset(2, &byte_offset) && byte_offset == 5);
    assert(offset_map.utf16CodeUnitOffset(2, &unit_offset) && unit_offset == 3);
    assert(offset_map.codepointOffsetForUtf16(3, &codepoint_offset) && codepoint_offset == 2);
    assert(!offset_map.codepointOffsetForUtf16(2, &codepoint_offset));
    assert(!offset_map.assign(std::string_view("\xf0\x9f\x98", 3)));

    // The mapping cache is code-point based, but must remain exact across the
    // scalar boundaries used by IME APIs even when the text contains combining
    // marks, ZWJ sequences, flags, modifiers, and RTL text.
    const std::string complex_unicode = u8"e\u0301👨‍👩‍👧‍👦🇵🇹👍🏽אב";
    assert(offset_map.assign(complex_unicode));
    for (uint32_t position = 0; position <= offset_map.codepointCount(); ++position) {
        assert(offset_map.utf8ByteOffset(position, &byte_offset));
        assert(offset_map.utf16CodeUnitOffset(position, &unit_offset));
        uint32_t round_trip = 0;
        assert(offset_map.codepointOffsetForUtf16(unit_offset, &round_trip));
        assert(round_trip == position);
    }
    assert(offset_map.utf16CodeUnitOffset(2, &unit_offset) && unit_offset == 2);
    assert(!offset_map.codepointOffsetForUtf16(3, &codepoint_offset));

    nk_text_input_state anchor_state{};
    anchor_state.cursor_x = 4.0f;
    anchor_state.cursor_y = 8.0f;
    anchor_state.cursor_width = 1.0f;
    anchor_state.cursor_height = 18.0f;
    anchor_state.selection_start = 3;
    anchor_state.selection_end = 5;
    std::vector<nk_text_input_rect> anchor_selection{
        {sizeof(nk_text_input_rect), 20.0f, 30.0f, 40.0f, 18.0f}};
    std::vector<nk_text_input_rect> anchor_composition{
        {sizeof(nk_text_input_rect), 60.0f, 70.0f, 20.0f, 18.0f}};
    auto anchor = nk::core::text_input_anchor_rect(anchor_state, anchor_selection, {});
    assert(anchor.x == 20.0f && anchor.y == 30.0f);
    anchor_state.composition_start = 4;
    anchor_state.composition_end = 5;
    anchor = nk::core::text_input_anchor_rect(anchor_state, anchor_selection, anchor_composition);
    assert(anchor.x == 60.0f && anchor.y == 70.0f);
    anchor_state.composition_start = NK_TEXT_POSITION_NONE;
    anchor_state.composition_end = NK_TEXT_POSITION_NONE;
    anchor_state.selection_start = 5;
    anchor_state.selection_end = 5;
    anchor = nk::core::text_input_anchor_rect(anchor_state, anchor_selection, anchor_composition);
    assert(anchor.x == 4.0f && anchor.y == 8.0f);
    anchor_state.selection_start = 3;
    anchor_state.selection_end = 5;
    auto hit =
        nk::core::text_input_hit_test_range(anchor_state, anchor_selection, {}, 21.0f, 35.0f);
    assert(hit.matched && hit.position == 3);
    hit = nk::core::text_input_hit_test_range(anchor_state, anchor_selection, {}, 59.0f, 35.0f);
    assert(hit.matched && hit.position == 5);
    hit = nk::core::text_input_hit_test_range(anchor_state, anchor_selection, {}, 20.0f, 55.0f);
    assert(!hit.matched);
    anchor_state.composition_start = 4;
    anchor_state.composition_end = 5;
    hit = nk::core::text_input_hit_test_range(anchor_state, anchor_selection, anchor_composition,
                                              61.0f, 75.0f);
    assert(hit.matched && hit.position == 4);

    std::vector<nk_text_input_range_rect> range_selection{
        {sizeof(nk_text_input_range_rect), 20.0f, 30.0f, 12.0f, 18.0f, 3, 4},
        {sizeof(nk_text_input_range_rect), 34.0f, 30.0f, 16.0f, 18.0f, 4, 6}};
    std::vector<nk_text_input_range_rect> range_composition{
        {sizeof(nk_text_input_range_rect), 60.0f, 70.0f, 20.0f, 18.0f, 8, 10}};
    const auto *range_hit =
        nk::core::text_input_range_rect_at_point(range_selection, range_composition, 22.0f, 35.0f);
    assert(range_hit && range_hit->range_start == 3 && range_hit->range_end == 4);
    range_hit =
        nk::core::text_input_range_rect_at_point(range_selection, range_composition, 0.0f, 0.0f);
    assert(!range_hit);
    hit =
        nk::core::text_input_hit_test_range_rects(range_selection, range_composition, 45.0f, 35.0f);
    assert(hit.matched && hit.position == 6);
    hit =
        nk::core::text_input_hit_test_range_rects(range_selection, range_composition, 65.0f, 75.0f);
    assert(hit.matched && hit.position == 8);
    const auto *first_range = nk::core::text_input_first_range_rect(range_selection, 3, 6);
    assert(first_range && first_range->range_start == 3);
    assert(!nk::core::text_input_first_range_rect(range_selection, 4, 4));

    nk_text_input_state state{};
    state.struct_size = sizeof(state);
    state.text_start = 10;
    state.document_length = 20;
    state.selection_start = 11;
    state.selection_end = 12;
    state.composition_start = 11;
    state.composition_end = 12;
    state.input_type = NK_TEXT_INPUT_TEXT;
    state.action = NK_TEXT_INPUT_ACTION_DEFAULT;
    state.cursor_width = 1.0f;
    state.cursor_height = 18.0f;
    TextInputStateValidation validation;
    assert(nk::core::validate_text_input_state(state, "x\U0001f600y", &validation));
    assert(validation.text_codepoints == 3 && validation.text_end == 13);
    state.selection_start = 9;
    assert(!nk::core::validate_text_input_state(state, "x\U0001f600y"));
    state.selection_start = 11;
    state.composition_end = 14;
    assert(!nk::core::validate_text_input_state(state, "x\U0001f600y"));
    state.composition_end = 12;
    state.composition_start = NK_TEXT_POSITION_NONE;
    assert(!nk::core::validate_text_input_state(state, "x\U0001f600y"));
    state.composition_end = NK_TEXT_POSITION_NONE;
    assert(nk::core::validate_text_input_state(state, "x\U0001f600y"));
    assert(!nk::core::validate_text_input_state(state, std::string_view("\xc0\x80", 2)));
    assert(!nk::core::validate_text_input_state(state, std::string_view("\xf0\x9f\x98", 3)));
    state.cursor_width = -1.0f;
    assert(!nk::core::validate_text_input_state(state, "x\U0001f600y"));
    state.cursor_width = 1.0f;
    state.flags = 0x80000000u;
    assert(!nk::core::validate_text_input_state(state, "x\U0001f600y"));
    state.flags = 0;

    const TextEditTransaction compose{NK_TEXT_EDIT_COMPOSE, 2, 2, "かな", 4, 4, 2, 4};
    assert(compose.valid());
    const TextEditTransaction commit{
        NK_TEXT_EDIT_COMMIT, 2, 4, "漢字", 4, 4, NK_TEXT_POSITION_NONE, NK_TEXT_POSITION_NONE};
    assert(commit.valid());
    const TextEditTransaction selection{
        NK_TEXT_EDIT_SET_SELECTION, NK_TEXT_POSITION_NONE, NK_TEXT_POSITION_NONE, {}, 1, 3, 2, 4};
    assert(selection.valid());
    const TextEditTransaction finish{
        NK_TEXT_EDIT_FINISH_COMPOSITION, NK_TEXT_POSITION_NONE, NK_TEXT_POSITION_NONE, {}, 4, 4,
        NK_TEXT_POSITION_NONE,           NK_TEXT_POSITION_NONE};
    assert(finish.valid());
    const TextEditTransaction finish_with_replacement{NK_TEXT_EDIT_FINISH_COMPOSITION,
                                                      4,
                                                      4,
                                                      {},
                                                      4,
                                                      4,
                                                      NK_TEXT_POSITION_NONE,
                                                      NK_TEXT_POSITION_NONE};
    assert(!finish_with_replacement.valid());
    const TextEditTransaction malformed{
        NK_TEXT_EDIT_COMMIT, 4, 2, "x", 4, 4, NK_TEXT_POSITION_NONE, NK_TEXT_POSITION_NONE};
    assert(!malformed.valid());
    const TextEditTransaction missing_composition{
        NK_TEXT_EDIT_COMPOSE, 2, 2, "x", 3, 3, NK_TEXT_POSITION_NONE, NK_TEXT_POSITION_NONE};
    assert(!missing_composition.valid());
    const TextEditTransaction delete_with_text{
        NK_TEXT_EDIT_DELETE, 2, 3, "x", 2, 2, NK_TEXT_POSITION_NONE, NK_TEXT_POSITION_NONE};
    assert(!delete_with_text.valid());
    const TextEditTransaction selection_with_replacement{
        NK_TEXT_EDIT_SET_SELECTION, 2, 2, "x", 2, 2, NK_TEXT_POSITION_NONE, NK_TEXT_POSITION_NONE};
    assert(!selection_with_replacement.valid());
    const TextEditTransaction invalid_affinity{
        NK_TEXT_EDIT_COMMIT, 1, 1, "x", 2, 2, NK_TEXT_POSITION_NONE, NK_TEXT_POSITION_NONE, 5};
    assert(!invalid_affinity.valid());
    TextEditTransaction invalid_history = commit;
    invalid_history.history_kind = 7;
    assert(!invalid_history.valid());

    nk_text_input_rect selection_rect{sizeof(nk_text_input_rect), 4.0f, 8.0f, 32.0f, 18.0f};
    std::vector<uint8_t> packed(sizeof(selection_rect));
    std::memcpy(packed.data(), &selection_rect, sizeof(selection_rect));
    nk::core::TextInputGeometry geometry;
    assert(nk::core::decode_text_input_geometry(1, 3, NK_TEXT_POSITION_NONE, NK_TEXT_POSITION_NONE,
                                                packed.data(), packed.size(), nullptr, 0,
                                                &geometry));
    assert(geometry.selection_rects.size() == 1);
    assert(geometry.selection_rects.front().x == 4.0f);
    nk_text_input_range_rect range_rect{
        sizeof(nk_text_input_range_rect), 4.0f, 8.0f, 12.0f, 18.0f, 1, 3};
    std::vector<uint8_t> range_packed(sizeof(range_rect));
    std::memcpy(range_packed.data(), &range_rect, sizeof(range_rect));
    assert(nk::core::decode_text_input_geometry(1, 3, NK_TEXT_POSITION_NONE, NK_TEXT_POSITION_NONE,
                                                range_packed.data(), range_packed.size(), nullptr,
                                                0, &geometry));
    assert(geometry.selection_range_rects.size() == 1);
    assert(geometry.selection_range_rects.front().range_start == 1);
    assert(geometry.selection_range_rects.front().range_end == 3);
    assert(!nk::core::decode_text_input_geometry(3, 1, NK_TEXT_POSITION_NONE, NK_TEXT_POSITION_NONE,
                                                 packed.data(), packed.size(), nullptr, 0,
                                                 &geometry));
    assert(!nk::core::decode_text_input_geometry(1, 3, 2, NK_TEXT_POSITION_NONE, packed.data(),
                                                 packed.size(), nullptr, 0, &geometry));
    selection_rect.struct_size = sizeof(selection_rect) - 1;
    std::memcpy(packed.data(), &selection_rect, sizeof(selection_rect));
    assert(!nk::core::decode_text_input_geometry(1, 3, NK_TEXT_POSITION_NONE, NK_TEXT_POSITION_NONE,
                                                 packed.data(), packed.size(), nullptr, 0,
                                                 &geometry));

    state.selection_start = 11;
    state.selection_end = 12;
    state.composition_start = NK_TEXT_POSITION_NONE;
    state.composition_end = NK_TEXT_POSITION_NONE;
    nk_text_input_rect composition_rect{sizeof(nk_text_input_rect), 7.0f, 26.0f, 18.0f, 18.0f};
    std::vector<uint8_t> composition_packed(sizeof(composition_rect));
    std::memcpy(composition_packed.data(), &composition_rect, sizeof(composition_rect));
    selection_rect.struct_size = sizeof(selection_rect);
    std::memcpy(packed.data(), &selection_rect, sizeof(selection_rect));
    assert(nk::core::decode_text_input_geometry(
        state.selection_start, state.selection_end, NK_TEXT_POSITION_NONE, NK_TEXT_POSITION_NONE,
        packed.data(), packed.size(), nullptr, 0, &geometry));
    assert(nk::core::text_input_geometry_matches_state(geometry, state));
    assert(!nk::core::text_input_geometry_matches_state(
        geometry, nk_text_input_state{sizeof(nk_text_input_state),
                                      0,
                                      nullptr,
                                      10,
                                      20,
                                      10,
                                      11,
                                      11,
                                      12,
                                      NK_TEXT_INPUT_TEXT,
                                      NK_TEXT_INPUT_ACTION_DEFAULT,
                                      0,
                                      0,
                                      1,
                                      18,
                                      {0, 0}}));
    assert(nk::core::decode_text_input_geometry(
        state.selection_start, state.selection_end, 11, 12, packed.data(), packed.size(),
        composition_packed.data(), composition_packed.size(), &geometry));
    assert(geometry.composition_rects.size() == 1);
    assert(geometry.composition_rects.front().y == 26.0f);

    nk_text_input_range_rect state_selection_range{
        sizeof(nk_text_input_range_rect), 4.0f, 8.0f, 32.0f, 18.0f, 11, 12};
    nk_text_input_range_rect state_composition_range{
        sizeof(nk_text_input_range_rect), 7.0f, 26.0f, 18.0f, 18.0f, 11, 12};
    std::vector<uint8_t> state_selection_packed(sizeof(state_selection_range));
    std::vector<uint8_t> state_composition_packed(sizeof(state_composition_range));
    std::memcpy(state_selection_packed.data(), &state_selection_range,
                sizeof(state_selection_range));
    std::memcpy(state_composition_packed.data(), &state_composition_range,
                sizeof(state_composition_range));
    state.composition_start = 11;
    state.composition_end = 12;
    assert(nk::core::decode_text_input_geometry(
        state.selection_start, state.selection_end, state.composition_start, state.composition_end,
        state_selection_packed.data(), state_selection_packed.size(),
        state_composition_packed.data(), state_composition_packed.size(), &geometry));
    assert(geometry.selection_range_rects.size() == 1);
    assert(geometry.composition_range_rects.size() == 1);
    assert(nk::core::text_input_geometry_matches_state(geometry, state));

    state_selection_range.range_start = 10;
    std::memcpy(state_selection_packed.data(), &state_selection_range,
                sizeof(state_selection_range));
    assert(!nk::core::decode_text_input_geometry(
        state.selection_start, state.selection_end, state.composition_start, state.composition_end,
        state_selection_packed.data(), state_selection_packed.size(),
        state_composition_packed.data(), state_composition_packed.size(), &geometry));
    return 0;
}
