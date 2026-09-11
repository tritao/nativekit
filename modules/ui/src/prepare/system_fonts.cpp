#include "system_fonts.h"

#include "skribidi/skb_common.h"

#include <algorithm>
#include <cctype>
#include <cwctype>
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

void append_unique(std::vector<SystemFontFallback> &fonts, const std::string &path, bool emoji,
                   uint32_t script_tag) {
    if (path.empty())
        return;
    const auto found = std::find_if(fonts.begin(), fonts.end(), [&](const auto &font) {
        return font.path == path && font.emoji == emoji && font.script_tag == script_tag;
    });
    if (found == fonts.end())
        fonts.push_back({path, emoji, script_tag});
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
#endif

#if defined(__APPLE__)
void append_core_text_match(std::vector<SystemFontFallback> &fonts, const char *sample,
                            bool emoji, uint32_t script_tag) {
    CFStringRef text = CFStringCreateWithCString(nullptr, sample, kCFStringEncodingUTF8);
    if (!text)
        return;
    CTFontRef font = CTFontCreateForString(nullptr, text, CFRangeMake(0, CFStringGetLength(text)));
    if (font) {
        CFTypeRef value = CTFontCopyAttribute(font, kCTFontURLAttribute);
        if (value && CFGetTypeID(value) == CFURLGetTypeID()) {
            char path[PATH_MAX] = {};
            if (CFURLGetFileSystemRepresentation(static_cast<CFURLRef>(value), true,
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

void append_windows_fonts(std::vector<SystemFontFallback> &fonts) {
    HKEY key = nullptr;
    if (RegOpenKeyExW(HKEY_LOCAL_MACHINE,
                     L"SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion\\Fonts", 0,
                     KEY_READ | KEY_WOW64_64KEY, &key) != ERROR_SUCCESS)
        return;

    wchar_t windows_directory[MAX_PATH] = {};
    const UINT directory_size = GetWindowsDirectoryW(windows_directory, MAX_PATH);
    std::wstring font_directory = directory_size ? std::wstring(windows_directory, directory_size)
                                                 : std::wstring(L"C:\\Windows");
    font_directory += L"\\Fonts\\";

    for (DWORD index = 0;; ++index) {
        wchar_t name[256] = {};
        wchar_t data[MAX_PATH] = {};
        DWORD name_size = static_cast<DWORD>(std::size(name));
        DWORD data_size = static_cast<DWORD>(std::size(data));
        DWORD type = 0;
        const LONG result = RegEnumValueW(key, index, name, &name_size, nullptr, &type,
                                          reinterpret_cast<LPBYTE>(data), &data_size);
        if (result == ERROR_NO_MORE_ITEMS)
            break;
        if (result != ERROR_SUCCESS || (type != REG_SZ && type != REG_EXPAND_SZ))
            continue;

        std::wstring family = lowercase(std::wstring(name, name_size));
        std::wstring path(data, data_size ? data_size - 1 : 0);
        if (path.empty())
            continue;
        if (path.find(L'\\') == std::wstring::npos && path.find(L'/') == std::wstring::npos)
            path = font_directory + path;

        const bool emoji = family.find(L"emoji") != std::wstring::npos;
        const bool useful = emoji || family.find(L"arab") != std::wstring::npos ||
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
#endif

} // namespace

std::vector<SystemFontFallback> discover_system_font_fallbacks() {
    std::vector<SystemFontFallback> fonts;

#if defined(NKUI_HAS_FONTCONFIG)
    if (FcInit()) {
        append_fontconfig_match(fonts, ":lang=ar", false, SKB_TAG_STR("Arab"));
        append_fontconfig_match(fonts, ":lang=he", false, SKB_TAG_STR("Hebr"));
        append_fontconfig_match(fonts, ":lang=ja", false, SKB_TAG_STR("Hani"));
        append_fontconfig_match(fonts, ":lang=ja", false, SKB_TAG_STR("Hira"));
        append_fontconfig_match(fonts, ":lang=ja", false, SKB_TAG_STR("Kana"));
        append_fontconfig_match(fonts, ":lang=ko", false, SKB_TAG_STR("Hang"));
        append_fontconfig_match(fonts, ":lang=zh", false, SKB_TAG_STR("Hani"));
        append_fontconfig_match(fonts, ":lang=hi", false, SKB_TAG_STR("Deva"));
        append_fontconfig_match(fonts, ":lang=th", false, SKB_TAG_STR("Thai"));
        append_fontconfig_match(fonts, "Noto Color Emoji", true, 0);
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
