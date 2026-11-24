#pragma once
#include <filesystem>
#include <map>
#include <string>
#include <vector>
#include "shader_json.h"

namespace shader {
namespace fs = std::filesystem;
struct Attribute { int slot; std::string name, format, semantic; int semantic_index; };
struct Member { std::string name, type; int count, offset = 0, stride = 0; };
struct Block { int slot, size; std::string stage, name; std::vector<Member> members, gl_uniforms; };
struct Image { int slot; std::string stage, name, type, sample_type; bool multisampled; };
struct Storage { int slot; std::string stage, name; bool readonly; };
struct Sampler { int slot; std::string stage, name, type; };
struct Pair { int image_slot, sampler_slot; std::string name; };
struct Stage { fs::path path; std::string entry_point, source; };
struct Target {
    std::vector<Attribute> attributes;
    std::vector<Block> blocks;
    std::vector<Image> images;
    std::vector<Storage> storage;
    std::vector<Sampler> samplers;
    std::vector<Pair> pairs;
    std::map<std::string, Stage> stages;
};
struct Program {
    std::vector<std::string> defines, dependencies;
    std::map<std::string, Target> targets;
};
using Programs = std::map<std::string, std::map<std::string, Program>>;
using Targets = std::map<std::string, std::map<std::string, Target>>;
std::string read_file(const fs::path &path);
Targets read_compiler_reflection(const fs::path &prefix, const std::string &canonical);
Json reflect(const Programs &programs);
}
