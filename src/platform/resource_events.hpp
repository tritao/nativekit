#pragma once

#include "nativekit_resource.h"

#include "core/error.hpp"

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace nk::platform {

struct ResourceValue {
    nk_resource_flags flags = 0;
    std::string uri;
    std::string mime_type;
    std::string display_name;
};

inline bool valid_utf8(std::string_view value) {
    for (std::size_t index = 0; index < value.size();) {
        const auto first = static_cast<std::uint8_t>(value[index]);
        std::size_t count = 0;
        std::uint32_t codepoint = 0;
        if (first <= 0x7f) {
            count = 1;
            codepoint = first;
        } else if (first >= 0xc2 && first <= 0xdf) {
            count = 2;
            codepoint = first & 0x1fu;
        } else if (first >= 0xe0 && first <= 0xef) {
            count = 3;
            codepoint = first & 0x0fu;
        } else if (first >= 0xf0 && first <= 0xf4) {
            count = 4;
            codepoint = first & 0x07u;
        } else {
            return false;
        }
        if (index + count > value.size())
            return false;
        for (std::size_t offset = 1; offset < count; ++offset) {
            const auto continuation = static_cast<std::uint8_t>(value[index + offset]);
            if ((continuation & 0xc0u) != 0x80u)
                return false;
            codepoint = (codepoint << 6) | (continuation & 0x3fu);
        }
        if ((count == 2 && codepoint < 0x80u) ||
            (count == 3 && codepoint < 0x800u) ||
            (count == 4 && codepoint < 0x10000u) || codepoint > 0x10ffffu ||
            (codepoint >= 0xd800u && codepoint <= 0xdfffu))
            return false;
        index += count;
    }
    return true;
}

inline bool valid_utf8(const char *value) {
    return !value || valid_utf8(std::string_view(value));
}

inline nk_result validate_resources(const nk_resource *resources, std::uint32_t count,
                                    bool allow_empty) {
    if ((!allow_empty && count == 0) || (count != 0 && !resources)) {
        nk::core::set_error("resource list is missing or empty");
        return NK_ERROR_INVALID_ARGUMENT;
    }
    for (std::uint32_t index = 0; index < count; ++index) {
        const auto &resource = resources[index];
        if (resource.struct_size < sizeof(nk_resource) || !resource.uri || !*resource.uri ||
            !std::strchr(resource.uri, ':') || !valid_utf8(resource.uri) ||
            !valid_utf8(resource.mime_type) || !valid_utf8(resource.display_name)) {
            nk::core::set_error("resource descriptor or URI is invalid");
            return NK_ERROR_INVALID_ARGUMENT;
        }
    }
    return NK_OK;
}

inline std::string percent_encode_path(std::string_view path) {
    constexpr char hex[] = "0123456789ABCDEF";
    std::string result;
    result.reserve(path.size());
    for (const auto byte : path) {
        const auto value = static_cast<unsigned char>(byte);
        const bool unreserved = (value >= 'a' && value <= 'z') ||
                                (value >= 'A' && value <= 'Z') ||
                                (value >= '0' && value <= '9') || value == '-' || value == '_' ||
                                value == '.' || value == '~' || value == '/' || value == ':';
        if (unreserved)
            result.push_back(static_cast<char>(value));
        else {
            result.push_back('%');
            result.push_back(hex[value >> 4]);
            result.push_back(hex[value & 0xf]);
        }
    }
    return result;
}

inline std::string file_uri_from_path(std::string path) {
#if defined(_WIN32)
    for (char &value : path)
        if (value == '\\')
            value = '/';
#endif
    if (path.empty())
        return {};
    if (path.front() != '/')
        path.insert(path.begin(), '/');
    return "file://" + percent_encode_path(path);
}

inline bool file_path_from_uri(std::string_view uri, std::string &path) {
    if (uri.size() < 7 || uri.substr(0, 7) != "file://")
        return false;
    const auto encoded = uri.substr(7);
    if (encoded.empty() || encoded.front() != '/')
        return false;
    path.clear();
    constexpr auto hex_value = [](char value) -> int {
        if (value >= '0' && value <= '9')
            return value - '0';
        if (value >= 'a' && value <= 'f')
            return value - 'a' + 10;
        if (value >= 'A' && value <= 'F')
            return value - 'A' + 10;
        return -1;
    };
    for (std::size_t index = 0; index < encoded.size(); ++index) {
        if (encoded[index] != '%') {
            path.push_back(encoded[index]);
            continue;
        }
        if (index + 2 >= encoded.size())
            return false;
        const auto high = hex_value(encoded[index + 1]);
        const auto low = hex_value(encoded[index + 2]);
        if (high < 0 || low < 0 || (high == 0 && low == 0))
            return false;
        path.push_back(static_cast<char>((high << 4) | low));
        index += 2;
    }
#if defined(_WIN32)
    if (path.size() >= 3 && path[0] == '/' && path[2] == ':')
        path.erase(path.begin());
    for (char &value : path)
        if (value == '/')
            value = '\\';
#endif
    return !path.empty();
}

inline std::string uri_display_name(std::string_view uri) {
    const auto slash = uri.find_last_of("/\\");
    const auto start = slash == std::string_view::npos ? 0 : slash + 1;
    if (start >= uri.size())
        return {};
    return std::string(uri.substr(start));
}

inline ResourceValue resource_from_uri(std::string uri, nk_resource_flags flags,
                                       std::string mime_type = {},
                                       std::string display_name = {}) {
    ResourceValue result;
    result.flags = flags;
    result.uri = std::move(uri);
    result.mime_type = std::move(mime_type);
    result.display_name = display_name.empty() ? uri_display_name(result.uri)
                                                : std::move(display_name);
    return result;
}

inline ResourceValue resource_from_file_path(std::string path, nk_resource_flags flags) {
    return resource_from_uri(file_uri_from_path(path), flags, {}, uri_display_name(path));
}

inline std::vector<ResourceValue> resources_from_uri_list(std::string_view value,
                                                           nk_resource_flags flags) {
    std::vector<ResourceValue> resources;
    std::size_t start = 0;
    while (start < value.size()) {
        const auto end = value.find('\n', start);
        const auto line_end = end == std::string_view::npos ? value.size() : end;
        std::string uri(value.substr(start, line_end - start));
        if (!uri.empty() && uri.back() == '\r')
            uri.pop_back();
        if (!uri.empty() && uri.front() != '#' && valid_utf8(uri) &&
            uri.find(':') != std::string::npos)
            resources.push_back(resource_from_uri(std::move(uri), flags));
        start = end == std::string_view::npos ? value.size() : end + 1;
    }
    return resources;
}

inline std::vector<std::byte> resource_payload(bool accepted,
                                                const std::vector<ResourceValue> &resources) {
    const auto items_offset = sizeof(nk_resource_list);
    const auto strings_offset = items_offset + resources.size() * sizeof(nk_resource_item);
    std::size_t total = strings_offset;
    for (const auto &resource : resources) {
        total += resource.uri.size() + 1;
        if (!resource.mime_type.empty())
            total += resource.mime_type.size() + 1;
        if (!resource.display_name.empty())
            total += resource.display_name.size() + 1;
    }
    std::vector<std::byte> result(total);
    const nk_resource_list header{accepted ? 1u : 0u, static_cast<std::uint32_t>(resources.size()),
                                  static_cast<std::uint32_t>(items_offset),
                                  static_cast<std::uint32_t>(strings_offset)};
    std::memcpy(result.data(), &header, sizeof(header));
    std::size_t cursor = strings_offset;
    for (std::size_t index = 0; index < resources.size(); ++index) {
        const auto &resource = resources[index];
        nk_resource_item item{};
        const auto append = [&](const std::string &value, std::uint32_t &offset) {
            if (value.empty())
                return;
            offset = static_cast<std::uint32_t>(cursor);
            std::memcpy(result.data() + cursor, value.c_str(), value.size() + 1);
            cursor += value.size() + 1;
        };
        item.flags = resource.flags;
        append(resource.uri, item.uri_offset);
        append(resource.mime_type, item.mime_type_offset);
        append(resource.display_name, item.display_name_offset);
        std::memcpy(result.data() + items_offset + index * sizeof(item), &item, sizeof(item));
    }
    return result;
}

inline std::vector<std::byte> resource_drop_payload(float x, float y, const std::string &text,
                                                    const std::vector<ResourceValue> &resources) {
    auto packed = resource_payload(false, resources);
    const auto prefix = sizeof(nk_resource_drop);
    nk_resource_list list{};
    std::memcpy(&list, packed.data(), sizeof(list));
    list.items_offset += static_cast<std::uint32_t>(prefix);
    list.strings_offset += static_cast<std::uint32_t>(prefix);
    std::memcpy(packed.data(), &list, sizeof(list));
    for (std::uint32_t index = 0; index < list.item_count; ++index) {
        nk_resource_item item{};
        const auto offset = sizeof(nk_resource_list) + index * sizeof(item);
        std::memcpy(&item, packed.data() + offset, sizeof(item));
        item.uri_offset += static_cast<std::uint32_t>(prefix);
        if (item.mime_type_offset)
            item.mime_type_offset += static_cast<std::uint32_t>(prefix);
        if (item.display_name_offset)
            item.display_name_offset += static_cast<std::uint32_t>(prefix);
        std::memcpy(packed.data() + offset, &item, sizeof(item));
    }
    const auto text_size = text.empty() ? 0u : static_cast<std::uint32_t>(text.size() + 1);
    std::vector<std::byte> result(prefix + packed.size() + text_size);
    nk_resource_drop drop{};
    drop.resources_offset = static_cast<std::uint32_t>(prefix);
    drop.x = x;
    drop.y = y;
    std::memcpy(result.data() + prefix, packed.data(), packed.size());
    if (!text.empty()) {
        drop.text_offset = static_cast<std::uint32_t>(prefix + packed.size());
        std::memcpy(result.data() + drop.text_offset, text.c_str(), text.size() + 1);
    }
    std::memcpy(result.data(), &drop, sizeof(drop));
    return result;
}

} // namespace nk::platform
