#pragma once
#include <cctype>
#include <cstdint>
#include <map>
#include <stdexcept>
#include <string>
#include <variant>
#include <vector>

namespace shader {
struct Json {
    using Object = std::map<std::string, Json>;
    using Array = std::vector<Json>;
    std::variant<std::nullptr_t, bool, int64_t, std::string, Array, Object> value;
    Json() : value(nullptr) {}
    Json(bool item) : value(item) {}
    Json(int item) : value(int64_t(item)) {}
    Json(int64_t item) : value(item) {}
    Json(const char *item) : value(std::string(item)) {}
    Json(std::string item) : value(std::move(item)) {}
    Json(Array item) : value(std::move(item)) {}
    Json(Object item) : value(std::move(item)) {}
    bool has(const std::string &key) const { return object().count(key) != 0; }
    const Json &at(const std::string &key) const { return object().at(key); }
    Json get(const std::string &key, Json fallback = {}) const { return has(key) ? at(key) : fallback; }
    const Object &object() const { return std::get<Object>(value); }
    const Array &array() const { return std::get<Array>(value); }
    const std::string &string() const { return std::get<std::string>(value); }
    int integer() const { return static_cast<int>(std::get<int64_t>(value)); }
    bool boolean() const { return std::get<bool>(value); }
};
inline Json::Array items(const Json &value, const std::string &name) {
    return value.has(name) ? value.at(name).array() : Json::Array{};
}
inline Json strings(const std::vector<std::string> &values) {
    return Json::Array(values.begin(), values.end());
}
inline std::string quote(const std::string &text) {
    std::string result = "\"";
    const char *hex = "0123456789abcdef";
    for (unsigned char c : text) {
        switch (c) {
        case '"': result += "\\\""; break;
        case '\\': result += "\\\\"; break;
        case '\n': result += "\\n"; break;
        case '\r': result += "\\r"; break;
        case '\t': result += "\\t"; break;
        case '\b': result += "\\b"; break;
        case '\f': result += "\\f"; break;
        default:
            if (c < 32) {
                result += "\\u00";
                result += hex[c >> 4];
                result += hex[c & 15];
            } else result += char(c);
        }
    }
    return result + '"';
}
inline std::string json_text(const Json &value, size_t depth = 0) {
    const auto indent = [](size_t n) { return std::string(n * 2, ' '); };
    switch (value.value.index()) {
    case 0: return "null";
    case 1: return value.boolean() ? "true" : "false";
    case 2: return std::to_string(std::get<int64_t>(value.value));
    case 3: return quote(value.string());
    case 4: {
        std::string result = "[";
        for (const auto &item : value.array()) {
            result += result.size() == 1 ? "\n" : ",\n";
            result += indent(depth + 1) + json_text(item, depth + 1);
        }
        return result + (result.size() == 1 ? "" : "\n" + indent(depth)) + "]";
    }
    default: {
        std::string result = "{";
        for (const auto &[key, item] : value.object()) {
            result += result.size() == 1 ? "\n" : ",\n";
            result += indent(depth + 1) + quote(key) + ": " + json_text(item, depth + 1);
        }
        return result + (result.size() == 1 ? "" : "\n" + indent(depth)) + "}";
    }
    }
}
class JsonReader {
    const std::string &text;
    size_t pos = 0;
    void space() { while (pos < text.size() && std::isspace(static_cast<unsigned char>(text[pos]))) ++pos; }
    bool eat(char c) { space(); if (pos == text.size() || text[pos] != c) return false; ++pos; return true; }
    [[noreturn]] void fail() const { throw std::runtime_error("invalid JSON at byte " + std::to_string(pos)); }
    void require(char c) { if (!eat(c)) fail(); }
    unsigned unicode() {
        unsigned code = 0;
        for (int i = 0; i < 4; ++i) {
            if (pos == text.size()) fail();
            char c = text[pos++];
            unsigned digit = c >= '0' && c <= '9' ? c - '0' : c >= 'a' && c <= 'f' ? c - 'a' + 10 : c >= 'A' && c <= 'F' ? c - 'A' + 10 : 16;
            if (digit > 15) fail();
            code = code * 16 + digit;
        }
        return code;
    }
    std::string string() {
        require('"');
        std::string result;
        while (pos < text.size()) {
            char c = text[pos++];
            if (c == '"') return result;
            if (static_cast<unsigned char>(c) < 32) fail();
            if (c != '\\') { result += c; continue; }
            if (pos == text.size()) fail();
            switch (c = text[pos++]) {
            case '"': case '\\': case '/': result += c; break;
            case 'n': result += '\n'; break;
            case 'r': result += '\r'; break;
            case 't': result += '\t'; break;
            case 'b': result += '\b'; break;
            case 'f': result += '\f'; break;
            case 'u': {
                auto code = unicode();
                if (code >= 0xd800 && code <= 0xdbff) {
                    if (text.compare(pos, 2, "\\u") != 0) fail();
                    pos += 2;
                    auto low = unicode();
                    if (low < 0xdc00 || low > 0xdfff) fail();
                    code = 0x10000 + ((code - 0xd800) << 10) + low - 0xdc00;
                } else if (code >= 0xdc00 && code <= 0xdfff) fail();
                if (code < 0x80) result += char(code);
                else {
                    const int count = code < 0x800 ? 2 : code < 0x10000 ? 3 : 4;
                    result += char((0xff << (8 - count)) | (code >> (6 * (count - 1))));
                    for (int i = count - 2; i >= 0; --i) result += char(0x80 | ((code >> (i * 6)) & 63));
                }
                break;
            }
            default: fail();
            }
        }
        fail();
    }
    Json item(size_t depth) {
        if (depth > 128) fail();
        space();
        if (pos == text.size()) fail();
        if (eat('{')) {
            Json::Object object;
            if (!eat('}')) {
                do {
                    auto key = string();
                    require(':');
                    if (!object.emplace(key, item(depth + 1)).second) fail();
                } while (eat(','));
                require('}');
            }
            return object;
        }
        if (eat('[')) {
            Json::Array array;
            if (!eat(']')) {
                do array.push_back(item(depth + 1)); while (eat(','));
                require(']');
            }
            return array;
        }
        if (text[pos] == '"') return string();
        for (const auto &literal : {std::string("true"), std::string("false"), std::string("null")})
            if (text.compare(pos, literal.size(), literal) == 0) {
                pos += literal.size();
                return literal == "null" ? Json() : Json(literal == "true");
            }
        size_t start = pos;
        if (text[pos] == '-') ++pos;
        size_t digits = pos;
        while (pos < text.size() && text[pos] >= '0' && text[pos] <= '9') ++pos;
        if (pos == digits) fail();
        return int64_t(std::stoll(text.substr(start, pos - start)));
    }
public:
    explicit JsonReader(const std::string &value) : text(value) {}
    Json read() { auto value = item(0); space(); if (pos != text.size()) fail(); return value; }
};
}
