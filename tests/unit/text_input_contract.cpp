#include "core/text_edit_transaction.hpp"
#include "core/text_input_contract.hpp"
#include "core/text_offsets.hpp"
#include "core/text_input_geometry.hpp"

#include <cassert>
#include <cmath>
#include <cstring>
#include <string>
#include <vector>

int main() {
    using nk::core::TextEditTransaction;
    using nk::core::TextInputStateValidation;
    using nk::core::utf16_to_codepoint_offset;

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
    assert(!utf16_to_codepoint_offset(std::u16string_view(unpaired_high), 1,
                                      &codepoint_offset));
    assert(!utf16_to_codepoint_offset(std::u16string_view(unpaired_low), 1,
                                      &codepoint_offset));

    nk::core::TextOffsetMap offset_map;
    assert(offset_map.assign("a\U0001f600e\xcc\x81"));
    std::size_t byte_offset = 0;
    std::size_t unit_offset = 0;
    assert(offset_map.utf8ByteOffset(2, &byte_offset) && byte_offset == 5);
    assert(offset_map.utf16CodeUnitOffset(2, &unit_offset) && unit_offset == 3);
    assert(offset_map.codepointOffsetForUtf16(3, &codepoint_offset) && codepoint_offset == 2);
    assert(!offset_map.codepointOffsetForUtf16(2, &codepoint_offset));
    assert(!offset_map.assign(std::string_view("\xf0\x9f\x98", 3)));

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
    const TextEditTransaction commit{NK_TEXT_EDIT_COMMIT, 2, 4, "漢字", 4, 4,
                                     NK_TEXT_POSITION_NONE, NK_TEXT_POSITION_NONE};
    assert(commit.valid());
    const TextEditTransaction selection{NK_TEXT_EDIT_SET_SELECTION,
                                        NK_TEXT_POSITION_NONE,
                                        NK_TEXT_POSITION_NONE,
                                        {},
                                        1,
                                        3,
                                        2,
                                        4};
    assert(selection.valid());
    const TextEditTransaction malformed{NK_TEXT_EDIT_COMMIT, 4, 2, "x", 4, 4,
                                        NK_TEXT_POSITION_NONE, NK_TEXT_POSITION_NONE};
    assert(!malformed.valid());
    const TextEditTransaction missing_composition{NK_TEXT_EDIT_COMPOSE, 2, 2, "x", 3, 3,
                                                  NK_TEXT_POSITION_NONE,
                                                  NK_TEXT_POSITION_NONE};
    assert(!missing_composition.valid());
    const TextEditTransaction delete_with_text{NK_TEXT_EDIT_DELETE, 2, 3, "x", 2, 2,
                                               NK_TEXT_POSITION_NONE,
                                               NK_TEXT_POSITION_NONE};
    assert(!delete_with_text.valid());
    const TextEditTransaction selection_with_replacement{NK_TEXT_EDIT_SET_SELECTION,
                                                          2,
                                                          2,
                                                          "x",
                                                          2,
                                                          2,
                                                          NK_TEXT_POSITION_NONE,
                                                          NK_TEXT_POSITION_NONE};
    assert(!selection_with_replacement.valid());

    nk_text_input_rect selection_rect{sizeof(nk_text_input_rect), 4.0f, 8.0f, 32.0f, 18.0f};
    std::vector<uint8_t> packed(sizeof(selection_rect));
    std::memcpy(packed.data(), &selection_rect, sizeof(selection_rect));
    nk::core::TextInputGeometry geometry;
    assert(nk::core::decode_text_input_geometry(
        1, 3, NK_TEXT_POSITION_NONE, NK_TEXT_POSITION_NONE, packed.data(), packed.size(),
        nullptr, 0, &geometry));
    assert(geometry.selection_rects.size() == 1);
    assert(geometry.selection_rects.front().x == 4.0f);
    assert(!nk::core::decode_text_input_geometry(
        3, 1, NK_TEXT_POSITION_NONE, NK_TEXT_POSITION_NONE, packed.data(), packed.size(),
        nullptr, 0, &geometry));
    assert(!nk::core::decode_text_input_geometry(
        1, 3, 2, NK_TEXT_POSITION_NONE, packed.data(), packed.size(), nullptr, 0, &geometry));
    selection_rect.struct_size = sizeof(selection_rect) - 1;
    std::memcpy(packed.data(), &selection_rect, sizeof(selection_rect));
    assert(!nk::core::decode_text_input_geometry(
        1, 3, NK_TEXT_POSITION_NONE, NK_TEXT_POSITION_NONE, packed.data(), packed.size(),
        nullptr, 0, &geometry));

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
        state.selection_start, state.selection_end, NK_TEXT_POSITION_NONE,
        NK_TEXT_POSITION_NONE, packed.data(), packed.size(), nullptr, 0, &geometry));
    assert(nk::core::text_input_geometry_matches_state(geometry, state));
    assert(!nk::core::text_input_geometry_matches_state(
        geometry, nk_text_input_state{sizeof(nk_text_input_state), 0, nullptr, 10, 20, 10, 11,
                                       11, 12, NK_TEXT_INPUT_TEXT, NK_TEXT_INPUT_ACTION_DEFAULT,
                                       0, 0, 1, 18, {0, 0}}));
    assert(nk::core::decode_text_input_geometry(
        state.selection_start, state.selection_end, 11, 12, packed.data(), packed.size(),
        composition_packed.data(), composition_packed.size(), &geometry));
    assert(geometry.composition_rects.size() == 1);
    assert(geometry.composition_rects.front().y == 26.0f);
    return 0;
}
