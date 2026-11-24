#include "shader_reflection.h"
#include <algorithm>
#include <fstream>
#include <functional>
#include <set>
#include <sstream>

namespace shader {
std::string read_file(const fs::path &path) {
    std::ifstream file(path, std::ios::binary);
    if (!file) throw std::runtime_error("cannot read " + path.string());
    return {std::istreambuf_iterator<char>(file), {}};
}

namespace {
// The pinned compiler writes nested mappings and sequences of mappings only.
Json read_yaml(const std::string &text) {
    std::istringstream input(text);
    std::vector<std::pair<size_t, std::string>> rows;
    for (std::string line; std::getline(input, line);) {
        const auto first = line.find_first_not_of(" \t\r"), last = line.find_last_not_of(" \t\r");
        if (first != std::string::npos) rows.emplace_back(first, line.substr(first, last - first + 1));
    }
    size_t cursor = 0;
    std::function<Json(size_t)> node = [&](size_t indent) -> Json {
        const bool sequence = rows.at(cursor).second == "-";
        Json::Array list;
        Json::Object object;
        while (cursor < rows.size() && rows[cursor].first == indent) {
            const auto &line = rows[cursor++].second;
            if (sequence) {
                if (line != "-" || cursor == rows.size() || rows[cursor].first <= indent)
                    throw std::runtime_error("invalid compiler reflection sequence");
                list.push_back(node(rows[cursor].first));
                continue;
            }
            const auto colon = line.find(':');
            if (colon == std::string::npos) throw std::runtime_error("invalid compiler reflection field");
            const auto start = line.find_first_not_of(' ', colon + 1);
            const auto key = line.substr(0, colon);
            if (start == std::string::npos && cursor < rows.size() && rows[cursor].first > indent)
                object[key] = node(rows[cursor].first);
            else {
                const auto value = start == std::string::npos ? "" : line.substr(start);
                if (value == "true" || value == "false") object[key] = value == "true";
                else if (!value.empty() && value.find_first_not_of("0123456789") == std::string::npos)
                    object[key] = std::stoi(value);
                else object[key] = value;
            }
        }
        return sequence ? Json(std::move(list)) : Json(std::move(object));
    };
    if (rows.empty()) throw std::runtime_error("empty compiler reflection");
    auto result = node(rows.front().first);
    if (cursor != rows.size()) throw std::runtime_error("invalid compiler reflection indentation");
    return result;
}

using Members = std::map<std::string, std::vector<Member>>;
Members uniform_members(const fs::path &path) {
    const auto bytes = read_file(path);
    if (bytes.size() < 20 || bytes.size() % 4) throw std::runtime_error("invalid SPIR-V: " + path.string());
    const auto byte = [&](size_t i) { return uint32_t(static_cast<unsigned char>(bytes[i])); };
    std::vector<uint32_t> words;
    for (size_t i = 0; i < bytes.size(); i += 4)
        words.push_back(byte(i) | byte(i + 1) << 8 | byte(i + 2) << 16 | byte(i + 3) << 24);
    if (words.front() != 0x07230203) throw std::runtime_error("invalid SPIR-V magic");
    using Key = std::pair<uint32_t, uint32_t>;
    std::map<uint32_t, std::string> names;
    std::map<Key, std::string> member_names;
    std::map<uint32_t, std::vector<uint32_t>> types;
    std::map<uint32_t, uint32_t> constants, strides;
    std::map<Key, uint32_t> offsets;
    std::set<uint32_t> blocks;
    for (size_t i = 5; i < words.size();) {
        const auto count = words[i] >> 16, op = words[i] & 0xffff;
        if (!count || i + count > words.size()) throw std::runtime_error("invalid SPIR-V instruction");
        const auto a = [&](size_t n) {
            if (n >= count) throw std::runtime_error("incomplete SPIR-V instruction");
            return words[i + n];
        };
        const auto name = [&](size_t n) {
            a(n);
            auto text = bytes.substr((i + n) * 4, (count - n) * 4);
            return text.substr(0, text.find('\0'));
        };
        switch (op) {
        case 5: names[a(1)] = name(2); break;
        case 6: member_names[{a(1), a(2)}] = name(3); break;
        case 21: case 22: case 23: case 24: case 28: case 30: {
            auto &type = types[a(1)];
            type.push_back(op);
            for (size_t n = 2; n < count; ++n) type.push_back(a(n));
            break;
        }
        case 43: constants[a(2)] = a(3); break;
        case 71: if (a(2) == 2) blocks.insert(a(1)); else if (a(2) == 6) strides[a(1)] = a(3); break;
        case 72: if (a(3) == 35) offsets[{a(1), a(2)}] = a(4); break;
        }
        i += count;
    }
    std::function<std::string(uint32_t)> kind = [&](uint32_t id) -> std::string {
        const auto &type = types.at(id);
        if (type == std::vector<uint32_t>{21, 32, 1}) return "int";
        if (type == std::vector<uint32_t>{22, 32}) return "float";
        if (type[0] == 23 && type.at(2) >= 2 && type.at(2) <= 4)
            return (kind(type[1]) == "int" ? "ivec" : "vec") + std::to_string(type[2]);
        if (type[0] == 24 && type.at(2) == 4 && kind(type[1]) == "vec4") return "mat4";
        throw std::runtime_error("unsupported std140 member in " + path.string());
    };
    Members result;
    for (auto block : blocks) {
        if (!names.count(block) || !types.count(block) || types.at(block)[0] != 30) continue;
        const auto &type = types.at(block);
        std::vector<Member> members;
        for (uint32_t i = 0; i + 1 < type.size(); ++i) {
            const Key key{block, i};
            if (!offsets.count(key)) break;
            auto id = type[i + 1];
            const auto &field = types.at(id);
            int stride = 0, count = 1;
            if (field[0] == 28) {
                stride = strides.at(id);
                count = constants.at(field.at(2));
                id = field.at(1);
            }
            members.push_back({member_names.at(key), kind(id), count, int(offsets.at(key)), stride});
        }
        if (!members.empty() && members.size() + 1 == type.size())
            result[names.at(block)] = std::move(members);
    }
    return result;
}

int slot(const Json &row, const std::string &field) {
    const auto value = row.at(field).integer();
    if (value < 0) throw std::runtime_error("invalid shader slot " + field);
    return value;
}

std::string stage(const Json &row) {
    const auto &value = row.at("stage").string();
    if (value != "vertex" && value != "fragment" && value != "compute")
        throw std::runtime_error("invalid shader stage " + value);
    return value;
}

Json member_json(const Member &member, bool layout) {
    Json::Object value{{"name", member.name}, {"type", member.type}, {"array_count", member.count}};
    if (layout) {
        value["offset"] = member.offset;
        if (member.stride) value["array_stride"] = member.stride;
    }
    return value;
}
template<class T, class F>
Json mapped(const std::vector<T> &list, F emit) {
    Json::Array output;
    for (const auto &entry : list) output.emplace_back(emit(entry));
    return output;
}
}

Targets read_compiler_reflection(const fs::path &prefix, const std::string &canonical) {
    Members members;
    const auto stem = prefix.filename().string() + "_" + canonical + "_";
    for (const auto &entry : fs::directory_iterator(prefix.parent_path())) {
        if (entry.path().extension() != ".spv" || entry.path().filename().string().rfind(stem, 0) != 0) continue;
        for (auto &[name, fields] : uniform_members(entry.path())) {
            auto inserted = members.emplace(name, fields);
            if (!inserted.second && json_text(mapped(inserted.first->second, [](const Member &f) { return member_json(f, true); }))
                                 != json_text(mapped(fields, [](const Member &f) { return member_json(f, true); })))
                throw std::runtime_error("ambiguous std140 block " + name);
        }
    }
    const auto yaml = read_yaml(read_file(prefix.string() + "_reflection.yaml"));
    Targets result;
    const std::map<std::string, std::string> formats{{"float","float"},{"vec2","float2"},{"vec3","float3"},{"vec4","float4"}};
    for (const auto &backend : items(yaml, "shaders")) {
        auto &programs = result[backend.at("slang").string()];
        for (const auto &raw : items(backend, "programs")) {
            auto &program = programs[raw.at("name").string()];
            for (const auto &row : items(raw, "attrs"))
                program.attributes.push_back({slot(row, "slot"), row.get("glsl_name", "").string(),
                    formats.at(row.at("type").string()), row.get("hlsl_sem_name", "").string(), row.get("hlsl_sem_index", 0).integer()});
            for (const auto &row : items(raw, "uniform_blocks")) {
                auto name = row.at("struct_name").string();
                Block block{slot(row, "slot"), row.at("size").integer(), stage(row), name, members.at(name), {}};
                if (block.size <= 0) throw std::runtime_error("invalid std140 block size for " + name);
                for (const auto &field : block.members)
                    if (field.count < 1 || field.offset < 0 || field.offset >= block.size)
                        throw std::runtime_error("invalid std140 layout for " + name);
                for (const auto &field : items(row, "glsl_uniforms"))
                    block.gl_uniforms.push_back({field.at("glsl_name").string(), field.at("type").string(), field.at("array_count").integer()});
                program.blocks.push_back(std::move(block));
            }
            for (const auto &view : items(raw, "views")) {
                if (view.has("texture")) {
                    const auto &row = view.at("texture");
                    program.images.push_back({slot(row, "slot"), stage(row), row.at("name").string(),
                        row.at("type").string(), row.at("sample_type").string(), row.at("multisampled").boolean()});
                } else if (view.has("storage_buffer")) {
                    const auto &row = view.at("storage_buffer");
                    const auto view_slot = slot(row, "slot");
                    // NativeKit binds storage view n at GLSL binding n, HLSL register n and Metal buffer 8 + n.
                    for (const auto &[field, expected] : std::map<std::string, int>{{"glsl_binding_n", view_slot},
                             {"hlsl_register_t_n", view_slot}, {"hlsl_register_u_n", view_slot}, {"msl_buffer_n", 8 + view_slot}})
                        if (row.has(field) && row.at(field).integer() != expected)
                            throw std::runtime_error("storage buffer " + row.at("struct_name").string() + " has " + field + " "
                                + std::to_string(row.at(field).integer()) + ", expected " + std::to_string(expected));
                    program.storage.push_back({view_slot, stage(row), row.at("struct_name").string(), row.at("readonly").boolean()});
                } else throw std::runtime_error("unsupported compiler shader resource");
            }
            for (const auto &row : items(raw, "samplers"))
                program.samplers.push_back({slot(row, "slot"), stage(row), row.at("name").string(), row.at("sampler_type").string()});
            for (const auto &row : items(raw, "texture_sampler_pairs"))
                program.pairs.push_back({slot(row, "view_slot"), slot(row, "sampler_slot"), row.get("glsl_name", row.at("name")).string()});
            if (raw.has("mtl_threads_per_threadgroup")) {
                const auto &threads = raw.at("mtl_threads_per_threadgroup");
                program.threads[0] = slot(threads, "x");
                program.threads[1] = slot(threads, "y");
                program.threads[2] = slot(threads, "z");
            }
            for (const std::string stage : {"vertex", "fragment", "compute"}) {
                if (!raw.has(stage + "_func")) continue;
                const auto &func = raw.at(stage + "_func");
                if (func.at("is_binary").boolean()) throw std::runtime_error("expected shader source output");
                program.stages.emplace(stage, Stage{fs::u8path(func.at("path").string()), func.at("entry_point").string(), {}});
            }
        }
    }
    return result;
}

Json reflect(const Programs &programs) {
    Json::Object out;
    for (const auto &[name, variants] : programs) {
        Json::Object by_variant;
        for (const auto &[variant, program] : variants) {
            Json::Object targets;
            for (const auto &[slang, target] : program.targets) {
                Json::Object stages;
                for (const auto &[key, stage] : target.stages)
                    stages[key] = Json::Object{{"source", stage.source}, {"entry_point", stage.entry_point}};
                Json::Object threads;
                if (target.threads[0])
                    threads = Json::Object{{"x", target.threads[0]}, {"y", target.threads[1]}, {"z", target.threads[2]}};
                targets[slang] = Json::Object{
                    {"threads", std::move(threads)},
                    {"attributes", mapped(target.attributes, [](const Attribute &a) {
                        return Json::Object{{"slot", a.slot}, {"name", a.name}, {"format", a.format},
                            {"semantic", Json::Object{{"name", a.semantic}, {"index", a.semantic_index}}}};
                    })},
                    {"uniform_blocks", mapped(target.blocks, [](const Block &b) {
                        return Json::Object{{"slot", b.slot}, {"stage", b.stage}, {"name", b.name}, {"size", b.size},
                            {"members", mapped(b.members, [](const Member &m) { return member_json(m, true); })},
                            {"gl_uniforms", mapped(b.gl_uniforms, [](const Member &m) { return member_json(m, false); })}};
                    })},
                    {"images", mapped(target.images, [](const Image &v) {
                        return Json::Object{{"slot", v.slot}, {"stage", v.stage}, {"name", v.name},
                            {"type", v.type}, {"sample_type", v.sample_type}, {"multisampled", v.multisampled}};
                    })},
                    {"samplers", mapped(target.samplers, [](const Sampler &v) {
                        return Json::Object{{"slot", v.slot}, {"stage", v.stage}, {"name", v.name}, {"type", v.type}};
                    })},
                    {"storage_buffers", mapped(target.storage, [](const Storage &v) {
                        return Json::Object{{"slot", v.slot}, {"stage", v.stage}, {"name", v.name}, {"readonly", v.readonly}};
                    })},
                    {"image_sampler_pairs", mapped(target.pairs, [](const Pair &v) {
                        return Json::Object{{"image_slot", v.image_slot}, {"sampler_slot", v.sampler_slot}, {"name", v.name}};
                    })},
                    {"stages", std::move(stages)}
                };
            }
            by_variant[variant] = Json::Object{{"defines", strings(program.defines)},
                {"dependencies", strings(program.dependencies)}, {"targets", std::move(targets)}};
        }
        out[name] = std::move(by_variant);
    }
    return out;
}
}
