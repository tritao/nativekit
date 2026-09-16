#include "core/text_edit_transaction.hpp"
#include "core/text_offsets.hpp"

#include <cassert>
#include <string>

int main() {
    using nk::core::TextEditTransaction;
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
    return 0;
}
