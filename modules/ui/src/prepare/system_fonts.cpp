#include "system_fonts.h"

#include "skribidi/skb_common.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cwctype>
#include <fstream>
#include <iterator>
#include <string>

#if defined(NKUI_HAS_FONTCONFIG)
#include <fontconfig/fontconfig.h>
#endif

#if defined(__APPLE__)
#include <CoreFoundation/CoreFoundation.h>
#include <CoreText/CoreText.h>
#include <limits.h>
#endif

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif

namespace nkui {

namespace {

uint16_t read_u16(const uint8_t *bytes) {
    return static_cast<uint16_t>((uint16_t(bytes[0]) << 8) | uint16_t(bytes[1]));
}

uint32_t read_u32(const uint8_t *bytes) {
    return (uint32_t(bytes[0]) << 24) | (uint32_t(bytes[1]) << 16) | (uint32_t(bytes[2]) << 8) |
           uint32_t(bytes[3]);
}

bool read_bytes(std::ifstream &file, std::streamoff offset, void *destination, std::size_t bytes) {
    file.clear();
    file.seekg(offset, std::ios::beg);
    return file.good() && static_cast<bool>(file.read(static_cast<char *>(destination), bytes));
}

bool has_sfnt_table(const std::string &path, uint32_t wanted_tag) {
    std::ifstream file(path, std::ios::binary);
    if (!file)
        return false;

    file.seekg(0, std::ios::end);
    const std::streamoff file_size = file.tellg();
    if (file_size < 12)
        return false;

    std::array<uint8_t, 12> header{};
    if (!read_bytes(file, 0, header.data(), header.size()))
        return false;

    std::streamoff sfnt_offset = 0;
    if (read_u32(header.data()) == 0x74746366u) { // 'ttcf'
        if (read_u32(header.data() + 8) == 0 || file_size < 16)
            return false;
        std::array<uint8_t, 4> offset_bytes{};
        if (!read_bytes(file, 12, offset_bytes.data(), offset_bytes.size()))
            return false;
        sfnt_offset = read_u32(offset_bytes.data());
    }

    if (sfnt_offset < 0 || sfnt_offset > file_size - 12)
        return false;
    if (!read_bytes(file, sfnt_offset, header.data(), header.size()))
        return false;

    const uint32_t table_count = read_u16(header.data() + 4);
    const std::streamoff directory_offset = sfnt_offset + 12;
    if (table_count > (file_size - directory_offset) / 16)
        return false;

    std::array<uint8_t, 16> record{};
    for (uint32_t index = 0; index < table_count; ++index) {
        if (!read_bytes(file, directory_offset + std::streamoff(index) * 16, record.data(),
                        record.size()))
            return false;
        if (read_u32(record.data()) == wanted_tag)
            return true;
    }
    return false;
}

bool supports_skribidi_color(const std::string &path) {
    // Skribidi's current color rasterizer consumes vector COLR/CPAL glyphs.
    // Do not classify bitmap color fonts (CBDT/CBLC or sbix) as usable color
    // fallbacks: they have FC_COLOR=true but cannot produce Skribidi quads.
    return has_sfnt_table(path, SKB_TAG_STR("COLR")) && has_sfnt_table(path, SKB_TAG_STR("CPAL"));
}

void append_unique(std::vector<SystemFontFallback> &fonts, const std::string &path, bool emoji,
                   uint32_t script_tag) {
    if (path.empty())
        return;
    const auto found = std::find_if(fonts.begin(), fonts.end(), [&](const auto &font) {
        return font.path == path && font.emoji == emoji && font.script_tag == script_tag;
    });
    const bool color = emoji && supports_skribidi_color(path);
    if (found == fonts.end())
        fonts.push_back({path, emoji, script_tag, color});
    else if (color)
        found->color = true;
}

#if defined(NKUI_HAS_FONTCONFIG)
void append_fontconfig_match(std::vector<SystemFontFallback> &fonts, const char *query, bool emoji,
                             uint32_t script_tag) {
    FcPattern *pattern = FcNameParse(reinterpret_cast<const FcChar8 *>(query));
    if (!pattern)
        return;
    FcConfigSubstitute(nullptr, pattern, FcMatchPattern);
    FcDefaultSubstitute(pattern);
    FcResult result = FcResultNoMatch;
    FcPattern *match = FcFontMatch(nullptr, pattern, &result);
    if (match) {
        FcChar8 *file = nullptr;
        if (FcPatternGetString(match, FC_FILE, 0, &file) == FcResultMatch && file)
            append_unique(fonts, reinterpret_cast<const char *>(file), emoji, script_tag);
        FcPatternDestroy(match);
    }
    FcPatternDestroy(pattern);
}

void append_fontconfig_emoji_matches(std::vector<SystemFontFallback> &fonts) {
    // FcFontMatch returns the first preferred font. On Linux that is often
    // Noto Color Emoji, whose CBDT/CBLC bitmap data is not supported by
    // Skribidi. Enumerate the color candidates and retain the first COLR/CPAL
    // font instead of accepting the first FC_COLOR=true result.
    const char *queries[] = {":charset=1f44b:color=true", ":charset=1f44b"};
    for (const char *query : queries) {
        FcPattern *pattern = FcNameParse(reinterpret_cast<const FcChar8 *>(query));
        if (!pattern)
            continue;
        FcConfigSubstitute(nullptr, pattern, FcMatchPattern);
        FcDefaultSubstitute(pattern);
        FcResult result = FcResultNoMatch;
        FcFontSet *matches = FcFontSort(nullptr, pattern, FcTrue, nullptr, &result);
        if (matches) {
            for (int index = 0; index < matches->nfont; ++index) {
                FcChar8 *file = nullptr;
                if (FcPatternGetString(matches->fonts[index], FC_FILE, 0, &file) != FcResultMatch ||
                    !file)
                    continue;
                const std::string path(reinterpret_cast<const char *>(file));
                if (supports_skribidi_color(path)) {
                    append_unique(fonts, path, true, 0);
                    break;
                }
            }
            FcFontSetDestroy(matches);
        }
        FcPatternDestroy(pattern);
        if (std::any_of(fonts.begin(), fonts.end(),
                        [](const auto &font) { return font.emoji && font.color; }))
            break;
    }

    // A monochrome outline fallback is still preferable to an empty glyph
    // when the platform has no COLR/CPAL emoji font.
    if (!std::any_of(fonts.begin(), fonts.end(), [](const auto &font) { return font.emoji; }))
        append_fontconfig_match(fonts, ":charset=1f44b:color=false", true, 0);
}
#endif

#if defined(__APPLE__)
void append_core_text_match(std::vector<SystemFontFallback> &fonts, const char *sample, bool emoji,
                            uint32_t script_tag) {
    CFStringRef text = CFStringCreateWithCString(nullptr, sample, kCFStringEncodingUTF8);
    if (!text)
        return;
    CTFontRef font = CTFontCreateForString(nullptr, text, CFRangeMake(0, CFStringGetLength(text)));
    if (font) {
        CFTypeRef value = CTFontCopyAttribute(font, kCTFontURLAttribute);
        if (value && CFGetTypeID(value) == CFURLGetTypeID()) {
            char path[PATH_MAX] = {};
            if (CFURLGetFileSystemRepresentation(reinterpret_cast<CFURLRef>(value), true,
                                                 reinterpret_cast<UInt8 *>(path), sizeof(path)))
                append_unique(fonts, path, emoji, script_tag);
        }
        if (value)
            CFRelease(value);
        CFRelease(font);
    }
    CFRelease(text);
}
#endif

#if defined(_WIN32)
std::string utf8_from_wide(const std::wstring &value) {
    if (value.empty())
        return {};
    const int size = WideCharToMultiByte(CP_UTF8, 0, value.data(), static_cast<int>(value.size()),
                                         nullptr, 0, nullptr, nullptr);
    if (size <= 0)
        return {};
    std::string result(static_cast<size_t>(size), '\0');
    WideCharToMultiByte(CP_UTF8, 0, value.data(), static_cast<int>(value.size()), result.data(),
                        size, nullptr, nullptr);
    return result;
}

std::wstring lowercase(std::wstring value) {
    std::transform(value.begin(), value.end(), value.begin(), [](wchar_t character) {
        return static_cast<wchar_t>(std::towlower(character));
    });
    return value;
}

void append_windows_registry_fonts(std::vector<SystemFontFallback> &fonts, HKEY root, REGSAM view,
                                   bool user_fonts) {
    HKEY key = nullptr;
    if (RegOpenKeyExW(root, L"SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion\\Fonts", 0,
                      KEY_READ | view, &key) != ERROR_SUCCESS)
        return;

    wchar_t windows_directory[MAX_PATH] = {};
    const UINT directory_size = GetWindowsDirectoryW(windows_directory, MAX_PATH);
    std::wstring font_directory;
    if (user_fonts) {
        wchar_t local_app_data[MAX_PATH] = {};
        const DWORD local_app_data_size =
            GetEnvironmentVariableW(L"LOCALAPPDATA", local_app_data, MAX_PATH);
        font_directory = local_app_data_size ? std::wstring(local_app_data, local_app_data_size)
                                             : std::wstring(windows_directory, directory_size);
        font_directory += L"\\Microsoft\\Windows\\Fonts\\";
    } else {
        font_directory = directory_size ? std::wstring(windows_directory, directory_size)
                                        : std::wstring(L"C:\\Windows");
        font_directory += L"\\Fonts\\";
    }

    for (DWORD index = 0;; ++index) {
        wchar_t name[256] = {};
        wchar_t data[MAX_PATH] = {};
        DWORD name_size = static_cast<DWORD>(std::size(name));
        DWORD data_size = static_cast<DWORD>(sizeof(data));
        DWORD type = 0;
        const LONG result = RegEnumValueW(key, index, name, &name_size, nullptr, &type,
                                          reinterpret_cast<LPBYTE>(data), &data_size);
        if (result == ERROR_NO_MORE_ITEMS)
            break;
        if (result != ERROR_SUCCESS || (type != REG_SZ && type != REG_EXPAND_SZ))
            continue;

        std::wstring family = lowercase(std::wstring(name, name_size));
        std::wstring path(data, data_size / sizeof(wchar_t));
        while (!path.empty() && path.back() == L'\0')
            path.pop_back();
        if (type == REG_EXPAND_SZ) {
            wchar_t expanded[MAX_PATH] = {};
            const DWORD expanded_size = ExpandEnvironmentStringsW(
                path.c_str(), expanded, static_cast<DWORD>(std::size(expanded)));
            if (expanded_size > 0 && expanded_size <= std::size(expanded))
                path.assign(expanded, expanded_size - 1);
        }
        if (path.empty())
            continue;
        if (path.find(L'\\') == std::wstring::npos && path.find(L'/') == std::wstring::npos)
            path = font_directory + path;

        const bool emoji = family.find(L"emoji") != std::wstring::npos;
        const bool useful = emoji || family.find(L"segoe") != std::wstring::npos ||
                            family.find(L"arial") != std::wstring::npos ||
                            family.find(L"tahoma") != std::wstring::npos ||
                            family.find(L"verdana") != std::wstring::npos ||
                            family.find(L"calibri") != std::wstring::npos ||
                            family.find(L"gadugi") != std::wstring::npos ||
                            family.find(L"ebrima") != std::wstring::npos ||
                            family.find(L"nirmala") != std::wstring::npos ||
                            family.find(L"leelawadee") != std::wstring::npos ||
                            family.find(L"arab") != std::wstring::npos ||
                            family.find(L"hebrew") != std::wstring::npos ||
                            family.find(L"japan") != std::wstring::npos ||
                            family.find(L"meiryo") != std::wstring::npos ||
                            family.find(L"gothic") != std::wstring::npos ||
                            family.find(L"malgun") != std::wstring::npos ||
                            family.find(L"korean") != std::wstring::npos ||
                            family.find(L"noto") != std::wstring::npos ||
                            family.find(L"simsun") != std::wstring::npos ||
                            family.find(L"yahei") != std::wstring::npos ||
                            family.find(L"cjk") != std::wstring::npos ||
                            family.find(L"unicode") != std::wstring::npos;
        if (useful)
            append_unique(fonts, utf8_from_wide(path), emoji, 0);
    }
    RegCloseKey(key);
}

void append_windows_fonts(std::vector<SystemFontFallback> &fonts) {
    const REGSAM views[] = {KEY_WOW64_64KEY, KEY_WOW64_32KEY};
    const HKEY roots[] = {HKEY_LOCAL_MACHINE, HKEY_CURRENT_USER};
    for (HKEY root : roots)
        for (REGSAM view : views)
            append_windows_registry_fonts(fonts, root, view, root == HKEY_CURRENT_USER);
}
#endif

} // namespace

std::vector<SystemFontFallback> discover_system_font_fallbacks() {
    std::vector<SystemFontFallback> fonts;

#if defined(NKUI_HAS_FONTCONFIG)
    if (FcInit()) {
        append_fontconfig_match(fonts, ":lang=en", false, SKB_TAG_STR("Latn"));
        append_fontconfig_match(fonts, ":lang=ar", false, SKB_TAG_STR("Arab"));
        append_fontconfig_match(fonts, ":lang=he", false, SKB_TAG_STR("Hebr"));
        append_fontconfig_match(fonts, ":lang=ja", false, SKB_TAG_STR("Hani"));
        append_fontconfig_match(fonts, ":lang=ja", false, SKB_TAG_STR("Hira"));
        append_fontconfig_match(fonts, ":lang=ja", false, SKB_TAG_STR("Kana"));
        append_fontconfig_match(fonts, ":lang=ko", false, SKB_TAG_STR("Hang"));
        append_fontconfig_match(fonts, ":lang=zh", false, SKB_TAG_STR("Hani"));
        append_fontconfig_match(fonts, ":lang=hi", false, SKB_TAG_STR("Deva"));
        append_fontconfig_match(fonts, ":lang=th", false, SKB_TAG_STR("Thai"));
        // Prefer a system font selected by actual emoji coverage.  Some
        // Linux installations expose Noto Color Emoji as CBDT/CBLC bitmap
        // data, while Skribidi's portable rasterizer consumes outline/COLR
        // glyphs.  The coverage query lets Fontconfig choose a compatible
        // installed fallback (for example Symbola) instead of producing an
        // empty glyph quad.
        append_fontconfig_emoji_matches(fonts);
    }
#elif defined(__APPLE__)
    append_core_text_match(fonts, "مرحبا", false, SKB_TAG_STR("Arab"));
    append_core_text_match(fonts, "שלום", false, SKB_TAG_STR("Hebr"));
    append_core_text_match(fonts, "こんにちは", false, SKB_TAG_STR("Hira"));
    append_core_text_match(fonts, "こんにちは", false, SKB_TAG_STR("Kana"));
    append_core_text_match(fonts, "你好", false, SKB_TAG_STR("Hani"));
    append_core_text_match(fonts, "안녕하세요", false, SKB_TAG_STR("Hang"));
    append_core_text_match(fonts, "👋", true, 0);
#elif defined(_WIN32)
    append_windows_fonts(fonts);
#endif

    return fonts;
}

const std::vector<SystemFontFallback> &system_font_fallbacks() {
    static const std::vector<SystemFontFallback> fonts = discover_system_font_fallbacks();
    return fonts;
}

} // namespace nkui
