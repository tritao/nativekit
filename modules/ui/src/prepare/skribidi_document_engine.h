#ifndef NATIVEKIT_UI_SKRIBIDI_DOCUMENT_ENGINE_H
#define NATIVEKIT_UI_SKRIBIDI_DOCUMENT_ENGINE_H

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "skribidi_adapter.h"

typedef struct skb_editor_t skb_editor_t;
typedef struct skb_temp_alloc_t skb_temp_alloc_t;

namespace nkui {

struct SkribidiEditTransaction {
    int32_t replacement_start = 0;
    int32_t replacement_end = 0;
    const char *replacement_text = nullptr;
    int32_t selection_start = 0;
    int32_t selection_end = 0;
    uint8_t selection_affinity = 0;
    bool has_composition = false;
    int32_t composition_start = -1;
    int32_t composition_end = -1;
    uint8_t history_kind = 0;
};

/**
 * NativeKit's first document-engine adapter backed by Skribidi's editor core.
 *
 * The public surface deliberately speaks in code-point offsets, matching the
 * TextDocumentEngine contract. Exact byte/UTF-16 conversion helpers are
 * available for platform adapters, but are not part of the generic editor
 * interface.
 */
class SkribidiDocumentEngine {
  public:
    SkribidiDocumentEngine(std::shared_ptr<SkribidiFontCollection> fonts, const char *initial_text,
                           float width, const TextLayoutOptions &options);
    ~SkribidiDocumentEngine();
    SkribidiDocumentEngine(const SkribidiDocumentEngine &) = delete;
    SkribidiDocumentEngine &operator=(const SkribidiDocumentEngine &) = delete;

    bool valid() const;
    bool apply_edit(const SkribidiEditTransaction &transaction);

    std::string text_utf8() const;
    int32_t document_length() const;
    bool codepoint_to_utf8_byte_offset(int32_t codepoint_offset, int32_t *utf8_byte_offset) const;
    bool utf8_byte_offset_to_codepoint(int32_t utf8_byte_offset, int32_t *codepoint_offset) const;
    bool codepoint_to_utf16_unit_offset(int32_t codepoint_offset, int32_t *utf16_unit_offset) const;
    bool utf16_unit_offset_to_codepoint(int32_t utf16_unit_offset, int32_t *codepoint_offset) const;
    TextRange selection() const;
    uint8_t selection_affinity() const;
    TextRange composition() const;
    bool has_composition() const;
    bool commit_composition();
    bool cancel_composition();

    bool undo();
    bool redo();
    TextPosition hit_test(float x, float y) const;
    TextCaret caret(TextPosition position) const;
    std::vector<TextRect> selection_rects(TextPosition start, TextPosition end) const;

  private:
    bool valid_range(int32_t start, int32_t end) const;

    std::shared_ptr<SkribidiFontCollection> fonts_;
    skb_temp_alloc_t *temporary_ = nullptr;
    skb_editor_t *editor_ = nullptr;
};

} // namespace nkui

#endif
