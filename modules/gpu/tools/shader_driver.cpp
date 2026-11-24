#include "shader_reflection.h"
#include "shader_sha256.h"
#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <cstdio>
#include <fstream>
#include <iostream>
#include <random>
#include <set>
#ifdef _WIN32
#include <process.h>
#include <io.h>
#else
#include <sys/wait.h>
#include <unistd.h>
#include <cerrno>
#endif

namespace shader {
namespace {
const std::vector<std::string> all_targets{"glsl410", "glsl300es", "hlsl5", "metal_macos", "metal_ios"};
const std::string revision = "4f5b2796806875dcf0a5a5dfa9f9dab462726a5e";
const std::map<std::string, std::string> pins{
    {"linux","2554534631a7c7c6c2261dc6d20a603cdabf0cc1390e293f5ca06d345bb48c13"},
    {"linux_arm64","da5e56ec9dc736b9fb7b43c881b2fe5616b2dd4beef7518dcfb5d60deb0eb50c"},
    {"osx","8d8b74e0f612f556a0f57ea5a49c1c64bf990eed518001db626332a9ef887afc"},
    {"osx_arm64","54b98765886f4abd3bb86ffcf8c75c5073073a7fb009e7f693fb2f4d9ce3baa6"},
    {"win32","10bd643a869cbacff606c28aa44f79a55ad545981c907889b6fb28bc8977b380"}
};
std::string environment(const char *key) {
    const auto value = std::getenv(key);
    return value ? value : "";
}
std::string host() {
#if defined(_WIN32) && (defined(_M_X64) || defined(__x86_64__))
    return "win32";
#elif defined(__APPLE__) && defined(__aarch64__)
    return "osx_arm64";
#elif defined(__APPLE__) && defined(__x86_64__)
    return "osx";
#elif defined(__linux__) && defined(__aarch64__)
    return "linux_arm64";
#elif defined(__linux__) && defined(__x86_64__)
    return "linux";
#else
    throw std::runtime_error("no pinned sokol-shdc for this host");
#endif
}
std::string join(const std::vector<std::string> &parts, const std::string &separator) {
    std::string out;
    bool first = true;
    for (const auto &part : parts) {
        if (!first) out += separator;
        first = false;
        out += part;
    }
    return out;
}
std::vector<std::string> split(const std::string &text, const std::string &delimiter) {
    std::vector<std::string> values;
    size_t start = 0;
    for (;;) {
        auto end = text.find(delimiter, start);
        values.push_back(text.substr(start, end == std::string::npos ? end : end - start));
        if (end == std::string::npos) return values;
        start = end + delimiter.size();
    }
}
std::vector<std::string> list(const Json &value) {
    std::vector<std::string> result;
    for (const auto &item : value.array()) result.push_back(item.string());
    return result;
}
std::string trim(const std::string &text) {
    auto first = text.find_first_not_of(" \r\n\t");
    if (first == std::string::npos) return "";
    return text.substr(first, text.find_last_not_of(" \r\n\t") - first + 1);
}
std::string upper(std::string text) {
    for (char &character : text)
        if (character >= 'a' && character <= 'z') character -= 'a' - 'A';
    return text;
}
std::string text_file(const fs::path &path) {
    const auto raw = read_file(path);
    std::string normalized;
    for (size_t i = 0; i < raw.size(); ++i) {
        if (raw[i] == '\r') {
            if (i + 1 < raw.size() && raw[i + 1] == '\n') ++i;
            normalized += '\n';
        } else normalized += raw[i];
    }
    return normalized;
}
void run(const std::vector<std::string> &arguments, const fs::path &stdout_path = {}) {
#ifdef _WIN32
    std::vector<std::wstring> wide;
    for (const auto &argument : arguments) {
        std::wstring encoded = L"\"";
        unsigned slashes = 0;
        for (const auto c : fs::u8path(argument).wstring()) {
            if (c == L'\\') { ++slashes; continue; }
            encoded.append(c == L'"' ? 2 * slashes + 1 : slashes, L'\\');
            slashes = 0;
            encoded += c;
        }
        wide.push_back(encoded + std::wstring(2 * slashes, L'\\') + L'"');
    }
    std::vector<const wchar_t *> argv;
    for (const auto &value : wide) argv.push_back(value.c_str());
    argv.push_back(nullptr);
    int saved = -1;
    if (!stdout_path.empty()) {
        auto *output = _wfopen(stdout_path.c_str(), L"wb");
        if (!output) throw std::runtime_error("could not open process output");
        saved = _dup(1);
        if (saved < 0 || _dup2(_fileno(output), 1) != 0) {
            if (saved >= 0) _close(saved);
            std::fclose(output);
            throw std::runtime_error("could not redirect process output");
        }
        std::fclose(output);
    }
    const auto status = _wspawnvp(_P_WAIT, fs::u8path(arguments.front()).c_str(), argv.data());
    if (saved >= 0) {
        _dup2(saved, 1);
        _close(saved);
    }
    if (status != 0) throw std::runtime_error(arguments.front() + " exited with status " + std::to_string(status));
#else
    std::vector<char *> argv;
    for (const auto &arg : arguments) argv.push_back(const_cast<char *>(arg.c_str()));
    argv.push_back(nullptr);
    auto child = fork();
    if (child == 0) {
        if (!stdout_path.empty()) {
            auto *output = std::fopen(stdout_path.c_str(), "wb");
            if (!output || dup2(fileno(output), STDOUT_FILENO) < 0) _exit(126);
            std::fclose(output);
        }
        execvp(argv.front(), argv.data());
        _exit(127);
    }
    int status;
    if (child < 0) throw std::runtime_error("could not start " + arguments.front());
    while (waitpid(child, &status, 0) < 0)
        if (errno != EINTR) throw std::runtime_error("could not wait for " + arguments.front());
    if (!WIFEXITED(status) || WEXITSTATUS(status) != 0)
        throw std::runtime_error(arguments.front() + " failed");
#endif
}
struct Work {
    fs::path path;
    explicit Work(const fs::path &base) {
        std::random_device random;
        do path = base / ("nkgpu-shaders-" + std::to_string(random()));
        while (!fs::create_directory(path));
    }
    ~Work() { std::error_code error; fs::remove_all(path, error); }
};
void verify(const fs::path &file, const std::string &sha) {
    if (sha.empty() || !fs::is_regular_file(file) || sha256(read_file(file)) != sha)
        throw std::runtime_error("SHA-256 mismatch or missing executable: " + file.string());
}
fs::path executable(const std::string &name) {
    if (fs::is_regular_file(fs::u8path(name))) return fs::absolute(fs::u8path(name));
#ifdef _WIN32
    const char *separator = ";";
    const std::string suffix = fs::u8path(name).extension().empty() ? ".exe" : "";
#else
    const char *separator = ":";
    const std::string suffix;
#endif
    for (const auto &directory : split(environment("PATH"), separator)) {
        auto candidate = fs::u8path(directory) / fs::u8path(name + suffix);
        if (fs::is_regular_file(candidate)) return fs::absolute(candidate);
    }
    return fs::u8path(name);
}
fs::path compiler(const fs::path &tool, const std::string &configured) {
    const auto folder = host();
    const std::string name = folder == "win32" ? "sokol-shdc.exe" : "sokol-shdc";
    auto path = tool.parent_path() / name;
    if (!configured.empty()) path = executable(configured);
    else if (!fs::is_regular_file(path)) {
        auto home = environment(folder == "win32" ? "USERPROFILE" : "HOME");
        if (home.empty()) throw std::runtime_error("no home directory for shader compiler cache");
        path = fs::u8path(home) / ".cache" / "nativekit" / revision / folder / name;
        if (!fs::is_regular_file(path)) {
            fs::create_directories(path.parent_path());
            Work work(path.parent_path());
            auto temporary = work.path / name;
            run({"curl", "-fsSL", "--max-time", "60",
                "https://raw.githubusercontent.com/floooh/sokol-tools-bin/" + revision + "/bin/" + folder + "/" + name,
                "-o", temporary.u8string()});
            verify(temporary, pins.at(folder));
            fs::permissions(temporary, fs::perms::owner_exec | fs::perms::group_exec | fs::perms::others_exec,
                fs::perm_options::add);
            if (!fs::is_regular_file(path)) fs::rename(temporary, path);
        }
    }
    verify(path, pins.at(folder));
    return fs::canonical(path);
}
void write(const fs::path &path, const std::string &text, bool check) {
    if (check) {
        if (!fs::is_regular_file(path) || read_file(path) != text)
            throw std::runtime_error(path.string() + " is stale. Regenerate without --check.");
        return;
    }
    if (!path.parent_path().empty()) fs::create_directories(path.parent_path());
    std::ofstream file(path, std::ios::binary);
    file << text;
    if (!file) throw std::runtime_error("cannot write " + path.string());
}
using Sources = std::map<std::string, std::string>;
std::string tables(const Programs &programs, const Sources &sources, const std::string &header, const std::set<std::string> &existing) {
    std::string out = "// Generated by NativeKit GPU shader tools. Do not edit.\n#pragma once\n#include \"nativekit_gpu_shader_table.h\"\n";
    if (!header.empty()) out += "#include " + quote(header) + "\nnamespace shader_source {\n";
    for (const auto &[symbol, source] : sources)
        if (!existing.count(symbol)) out += "static const char " + symbol + "[] = " + quote(source) + ";\n";
    const auto array = [&](const std::string &symbol, const std::string &kind, const std::vector<std::string> &rows) -> std::string {
        if (rows.empty()) return "NULL, 0";
        out += "static const nkgpu_shader_table_" + kind + " " + symbol + "[] = {\n";
        for (const auto &row : rows) out += "    {" + row + "},\n";
        out += "};\n";
        return symbol + ", " + std::to_string(rows.size());
    };
    const std::map<std::string,std::string> uniforms{{"float","FLOAT"},{"vec2","FLOAT2"},{"vec3","FLOAT3"},{"vec4","FLOAT4"},
        {"int","INT"},{"ivec2","INT2"},{"ivec3","INT3"},{"ivec4","INT4"},{"mat4","MAT4"}};
    const auto stage = [](const std::string &value) { return "NKGPU_SHADERSTAGE_" + upper(value); };
    for (const auto &[name, variants] : programs)
        for (const auto &[variant, program] : variants) {
            const auto symbol = name + "_" + variant;
            const auto &targets = program.targets;
            auto gl = targets.find("glsl410");
            if (gl == targets.end()) gl = targets.find("glsl300es");
            std::vector<std::string> rows;
            if (gl != targets.end())
                for (const auto &a : gl->second.attributes)
                    rows.push_back(join({std::to_string(a.slot), quote(a.name), quote(a.semantic), std::to_string(a.semantic_index)}, ", "));
            const auto attrs = array(symbol + "_attributes", "attribute", rows);
            std::vector<std::string> entries;
            for (const auto &[slang, target] : targets) {
                const auto stem = symbol + "_" + slang;
                std::vector<std::string> blocks, members, textures;
                for (const auto &b : target.blocks) {
                    blocks.push_back(join({std::to_string(b.slot), stage(b.stage), std::to_string(b.size)}, ", "));
                    for (size_t i = 0; i < b.gl_uniforms.size(); ++i) {
                        const auto &u = b.gl_uniforms[i];
                        members.push_back(join({std::to_string(b.slot), std::to_string(i), quote(u.name),
                            "NKGPU_UNIFORMTYPE_" + uniforms.at(u.type), std::to_string(u.count)}, ", "));
                    }
                }
                for (const auto &p : target.pairs) {
                    const auto v = std::find_if(target.images.begin(), target.images.end(), [&](const Image &img) { return img.slot == p.image_slot; });
                    if (v == target.images.end() || v->sample_type != "float" || v->multisampled)
                        throw std::runtime_error(name + ": unsupported GPU table texture type");
                    textures.push_back(join({std::to_string(v->slot), std::to_string(p.sampler_slot), stage(v->stage),
                        "NKGPU_IMAGETYPE_" + upper(v->type), quote(p.name)}, ", "));
                }
                if (!target.storage.empty()) throw std::runtime_error(name + ": unsupported GPU table resource");
                const auto block_ref = array(stem + "_blocks", "block", blocks);
                const auto uniform_ref = array(stem + "_uniforms", "uniform", members);
                const auto texture_ref = array(stem + "_textures", "texture", textures);
                entries.push_back(join({"NKGPU_SHADER_TARGET_" + upper(slang), target.stages.at("vertex").source,
                    target.stages.at("fragment").source, attrs, block_ref, uniform_ref, texture_ref}, ", "));
            }
            array(symbol + "_table", "entry", entries);
        }
    if (!header.empty()) out += "} // namespace shader_source\n";
    return out;
}
void stage_tree(const fs::path &root, const fs::path &destination, std::map<fs::path, fs::path> &files) {
    for (const auto &entry : fs::recursive_directory_iterator(root)) {
        if (!entry.is_regular_file() || entry.path().extension() != ".glsl") continue;
        auto copy = (destination / entry.path().lexically_relative(root)).lexically_normal();
        if (files.count(copy)) throw std::runtime_error("duplicate shader include " + copy.filename().string());
        fs::create_directories(copy.parent_path());
        fs::copy_file(entry.path(), copy);
        files.emplace(copy, fs::canonical(entry.path()));
    }
}
void build(const Json &manifest, const fs::path &directory, const fs::path &compiler_path, const fs::path &validator,
           bool check, const fs::path &dep_output = {}) {
    const auto slangs = list(manifest.get("slangs", strings(all_targets)));
    if (slangs.empty() || std::set<std::string>(slangs.begin(), slangs.end()).size() != slangs.size()
        || std::any_of(slangs.begin(), slangs.end(), [](const auto &s) { return std::find(all_targets.begin(), all_targets.end(), s) == all_targets.end(); }))
        throw std::runtime_error("invalid shader targets");
    const bool supplemental = !dep_output.empty() && std::find(slangs.begin(), slangs.end(), "glsl410") == slangs.end()
        && std::find(slangs.begin(), slangs.end(), "glsl300es") == slangs.end();
    auto compiler_targets = slangs;
    if (supplemental) compiler_targets.push_back("glsl410");
    const auto canonical = std::find(compiler_targets.begin(), compiler_targets.end(), "glsl410") != compiler_targets.end()
        ? std::string("glsl410") : compiler_targets.front();
    Work work(fs::temp_directory_path());
    if (!validator.empty()) {
        auto expected = environment("NATIVEKIT_GLSLANG_SHA256");
        if (expected.empty() && host() == "linux")
            expected = "f0a8e48614b36de3ebc79ab1c3b3f110ff6f5b4ec93d8c7c86cf1612da9953d1";
        verify(validator, expected);
        const auto version = work.path / "glslang-version";
        run({validator.u8string(), "--version"}, version);
        if (text_file(version).find(":15.1.0") == std::string::npos)
            throw std::runtime_error("shader validation requires glslang 15.1.0");
    }
    Programs programs;
    Sources sources;
    std::set<std::string> dependencies;
    size_t index = 0;
    for (const auto &group : items(manifest, "sources")) {
        auto source = fs::canonical(directory / fs::u8path(group.at("file").string()));
        const auto variants = group.get("variants", Json::Object{{"default", Json::Array{}}});
        for (const auto &[variant, define_list] : variants.object()) {
            const auto defines = list(define_list);
            const auto invocation = work.path / std::to_string(index++);
            fs::create_directory(invocation);
            const auto output = invocation / "shader", depfile = invocation / "shader.d";
            std::map<fs::path, fs::path> staged;
            auto input = source;
            if (manifest.has("include_roots")) {
                const auto tree = invocation / "inputs";
                stage_tree(directory, tree, staged);
                const auto relative = source.lexically_relative(directory);
                if (relative.empty() || *relative.begin() == "..") throw std::runtime_error("shader source is outside manifest directory");
                input = tree / relative;
                for (const auto &root : items(manifest, "include_roots")) {
                    const auto prefix = fs::u8path(root.at("prefix").string());
                    if (prefix.is_absolute() || std::find(prefix.begin(), prefix.end(), fs::path("..")) != prefix.end())
                        throw std::runtime_error("shader include prefix must be relative");
                    stage_tree(directory / fs::u8path(root.at("path").string()), input.parent_path() / prefix, staged);
                }
            }
            std::vector<std::string> args{compiler_path.u8string(), "-i", input.u8string(), "-o", output.u8string(),
                "-l", join(compiler_targets, ":"), "-f", "bare_yaml", "--no-log-cmdline", "--save-intermediate-spirv",
                "--dependency-file=" + depfile.u8string()};
            if (!defines.empty()) args.push_back("--defines=" + join(defines, ":"));
            run(args);
            const auto dep_text = text_file(depfile);
            const auto delimiter = dep_text.find(": ");
            if (delimiter == std::string::npos) throw std::runtime_error("invalid compiler dependency file");
            std::vector<std::string> includes;
            for (const auto &row : split(dep_text.substr(delimiter + 2), "\\\n")) {
                if (trim(row).empty()) continue;
                auto path = fs::canonical(fs::u8path(trim(row)));
                if (staged.count(path)) path = staged.at(path);
                dependencies.insert(path.generic_u8string());
                auto relative = path.lexically_relative(directory);
                includes.push_back(relative.empty() ? path.generic_u8string() : relative.generic_u8string());
            }
            std::sort(includes.begin(), includes.end());
            auto backends = read_compiler_reflection(output, canonical);
            for (auto &[slang, entries] : backends) {
                if (group.has("programs")) {
                    auto declared = list(group.at("programs"));
                    std::vector<std::string> actual;
                    for (const auto &[name, unused] : entries) actual.push_back(name);
                    std::sort(declared.begin(), declared.end());
                    if (actual != declared) throw std::runtime_error("manifest programs differ from @program declarations");
                }
                for (auto &[name, target] : entries) {
                    auto &program = programs[name][variant];
                    program.defines = defines;
                    program.dependencies = includes;
                    for (auto &[stage, data] : target.stages) {
                        const auto suffix = variant == "default" ? "" : "_" + variant;
                        data.source = manifest.get("prefix", "shader_").string() + name + suffix + "_" + slang + "_" + stage;
                        if (!validator.empty() && slang.rfind("glsl", 0) == 0)
                            run({validator.u8string(), "-S", stage == "vertex" ? "vert" : stage == "fragment" ? "frag" : "comp", data.path.u8string()});
                        if (!supplemental || slang != "glsl410") sources[data.source] = text_file(data.path);
                    }
                    if (!program.targets.emplace(slang, std::move(target)).second)
                        throw std::runtime_error("duplicate shader " + name + "/" + variant + "/" + slang);
                }
            }
        }
    }
    for (auto &[name, variants] : programs)
        for (auto &[variant, program] : variants) {
            if (program.targets.size() != compiler_targets.size()) throw std::runtime_error("incomplete shader target output");
            std::map<int, Attribute> attributes;
            for (const auto &[slang, target] : program.targets)
                for (const auto &attribute : target.attributes) {
                    auto &merged = attributes.emplace(attribute.slot, attribute).first->second;
                    if (merged.format != attribute.format) throw std::runtime_error("vertex format differs across shader targets");
                    if (!attribute.name.empty()) merged.name = attribute.name;
                    if (!attribute.semantic.empty()) {
                        merged.semantic = attribute.semantic;
                        merged.semantic_index = attribute.semantic_index;
                    }
                }
            for (auto &[slang, target] : program.targets)
                for (auto &attribute : target.attributes) {
                    attribute = attributes.at(attribute.slot);
                    if (attribute.name.empty()) throw std::runtime_error("vertex names require one GLSL reflection target");
                }
            if (supplemental) program.targets.erase("glsl410");
        }
    std::set<std::string> existing;
    std::string header_name;
    if (manifest.has("header")) {
        const auto &header = manifest.at("header");
        const auto targets = list(header.get("slangs", strings(slangs)));
        std::string out = header.at("banner").string() + "\n#pragma once\n\nnamespace " + header.at("namespace").string() + " {\n";
        for (const auto &[symbol, source] : sources)
            for (const auto &target : targets)
                for (const std::string stage : {"vertex", "fragment", "compute"}) {
                    const auto suffix = "_" + target + "_" + stage;
                    if (symbol.size() >= suffix.size() && symbol.compare(symbol.size() - suffix.size(), suffix.size(), suffix) == 0) {
                        existing.insert(symbol);
                        out += "inline constexpr char " + symbol + "[] = " + quote(source) + ";\n";
                    }
                }
        out += "} // namespace " + header.at("namespace").string() + "\n";
        const auto path = fs::u8path(header.at("output").string());
        header_name = path.filename().u8string();
        write(directory / path, out, check);
    }
    if (manifest.has("tables")) write(directory / fs::u8path(manifest.at("tables").string()), tables(programs, sources, header_name, existing), check);
    if (manifest.has("reflection")) {
        Json::Object reflection{{"schema", "nkgpu-shader-reflection/1"}, {"programs", reflect(programs)}};
        if (manifest.get("embed_sources", false).boolean()) {
            Json::Object text;
            for (const auto &[name, content] : sources) text[name] = content;
            reflection["sources"] = std::move(text);
        }
        write(directory / fs::u8path(manifest.at("reflection").string()), json_text(reflection) + "\n", check);
    }
    if (!dep_output.empty()) {
        std::string text;
        for (const auto &path : dependencies) text += path + "\n";
        write(dep_output, text, check);
    }
}
}

int command(const std::vector<std::string> &arguments) {
    std::string manifest, source, output, targets, variants = "{\"default\":[]}", shdc = environment("SOKOL_SHDC"), validator;
    bool check = false, print_compiler = false;
    for (size_t i = 1; i < arguments.size(); ++i) {
        const auto arg = arguments[i];
        auto value = [&]() {
            if (++i >= arguments.size()) throw std::runtime_error("missing value for " + arg);
            return arguments[i];
        };
        if (arg == "--check") check = true;
        else if (arg == "--source") source = value();
        else if (arg == "--output") output = value();
        else if (arg == "--target") targets = value();
        else if (arg == "--variants") variants = value();
        else if (arg == "--shdc") shdc = value();
        else if (arg == "--validator") validator = value();
        else if (arg == "--print-compiler") print_compiler = true;
        else if (arg == "--help") {
            std::cout << "nkgpu-shaders MANIFEST [--check] [--validator PATH] [--shdc PATH]\n"
                      << "nkgpu-shaders --source FILE --output DIR --target TARGET[:TARGET] [--variants JSON]\n"
                      << "nkgpu-shaders --print-compiler [--shdc PATH]\n";
            return 0;
        } else if (!arg.empty() && arg.front() != '-' && manifest.empty()) manifest = arg;
        else throw std::runtime_error("unknown argument " + arg);
    }
    const auto tool = compiler(executable(arguments.front()), shdc);
    if (print_compiler) { std::cout << tool.u8string() << '\n'; return 0; }
    auto verifier = validator.empty() ? fs::path{} : executable(validator);
    if (!source.empty() && !output.empty() && !targets.empty() && manifest.empty()) {
        auto file = fs::canonical(fs::u8path(source));
        auto directory = fs::absolute(fs::u8path(output));
        const Json built = Json::Object{{"embed_sources", true}, {"reflection", (directory / "reflection.json").u8string()},
            {"slangs", strings(split(targets, ":"))}, {"sources", Json::Array{
                Json::Object{{"file", file.u8string()}, {"variants", JsonReader(variants).read()}}}}};
        build(built, file.parent_path(), tool, verifier, check, directory / "dependencies");
    } else if (!manifest.empty() && source.empty() && output.empty() && targets.empty()) {
        auto file = fs::canonical(fs::u8path(manifest));
        build(JsonReader(read_file(file)).read(), file.parent_path(), tool, verifier, check);
    } else throw std::runtime_error("specify a manifest or --source, --output and --target");
    return 0;
}
}

#ifdef _WIN32
int wmain(int argc, wchar_t **argv) {
    std::vector<std::string> args;
    for (int i = 0; i < argc; ++i) args.push_back(std::filesystem::path(argv[i]).u8string());
#else
int main(int argc, char **argv) {
    std::vector<std::string> args(argv, argv + argc);
#endif
    try {
        return shader::command(args);
    } catch (const std::exception &error) {
        std::cerr << "shader generation failed: " << error.what() << "\n";
        return 1;
    }
}
