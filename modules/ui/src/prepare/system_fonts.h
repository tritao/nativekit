#ifndef NATIVEKIT_UI_SYSTEM_FONTS_H
#define NATIVEKIT_UI_SYSTEM_FONTS_H

#include <cstdint>
#include <string>
#include <vector>

namespace nkui {

struct SystemFontFallback {
    std::string path;
    bool emoji = false;
    uint32_t script_tag = 0;
    // True when the font uses the COLR/CPAL color format supported by
    // Skribidi's color-glyph rasterizer. Fontconfig's generic color flag also
    // includes bitmap-only formats such as CBDT/CBLC.
    bool color = false;
};

const std::vector<SystemFontFallback> &system_font_fallbacks();

} // namespace nkui

#endif
