#!/usr/bin/env python3
"""Build checked-in shader sources and reflection from a NativeKit shader manifest."""

import argparse
import hashlib
import itertools
import json
import os
from pathlib import Path
import platform
import shlex
import shutil
import struct
import subprocess
import sys
import tempfile
import urllib.request

import yaml

SHDC_REVISION = "4f5b2796806875dcf0a5a5dfa9f9dab462726a5e"
SLANGS = ("glsl410", "glsl300es", "hlsl5", "metal_macos", "metal_ios")
STAGES = ("vertex", "fragment", "compute")



UNIFORM_TYPES = {
    "float": "FLOAT", "vec2": "FLOAT2", "vec3": "FLOAT3", "vec4": "FLOAT4",
    "int": "INT", "ivec2": "INT2", "ivec3": "INT3", "ivec4": "INT4", "mat4": "MAT4",
}
IMAGE_TYPES = {"2d": "2D", "array": "ARRAY", "cube": "CUBE", "cube_array": "CUBE_ARRAY"}
VERTEX_FORMATS = {"float": "float", "vec2": "float2", "vec3": "float3", "vec4": "float4"}
SHDC_SHA256 = {
    "linux": "2554534631a7c7c6c2261dc6d20a603cdabf0cc1390e293f5ca06d345bb48c13",
    "linux_arm64": "da5e56ec9dc736b9fb7b43c881b2fe5616b2dd4beef7518dcfb5d60deb0eb50c",
    "osx": "8d8b74e0f612f556a0f57ea5a49c1c64bf990eed518001db626332a9ef887afc",
    "osx_arm64": "54b98765886f4abd3bb86ffcf8c75c5073073a7fb009e7f693fb2f4d9ce3baa6",
    "win32": "10bd643a869cbacff606c28aa44f79a55ad545981c907889b6fb28bc8977b380",
}
VALIDATOR_SHA256 = {
    ("Linux", "x86_64"): "f0a8e48614b36de3ebc79ab1c3b3f110ff6f5b4ec93d8c7c86cf1612da9953d1",
}


def normalize(shader, members):
    """Translate compiler fields into NativeKit's reflection vocabulary."""
    def binding(row, **fields):
        stage, slot = row["stage"], row["slot"]
        if stage not in STAGES or type(slot) is not int or slot < 0:
            raise ValueError(f"invalid {stage} resource slot {slot}")
        return dict(stage=stage, slot=slot, **fields)

    attributes = []
    for row in shader.get("attrs", []):
        slot, format_type = row["slot"], row["type"]
        if type(slot) is not int or slot < 0 or format_type not in VERTEX_FORMATS:
            raise ValueError(f"unsupported vertex attribute {format_type} at slot {slot}")
        attributes.append(dict(slot=slot, name=row.get("glsl_name", ""), format=VERTEX_FORMATS[format_type],
                               semantic=dict(name=row.get("hlsl_sem_name", ""), index=row.get("hlsl_sem_index", 0))))
    blocks = []
    for row in shader.get("uniform_blocks", []):
        name, size = row["struct_name"], row["size"]
        fields = members[name]
        if type(size) is not int or size <= 0 or any(
                field["array_count"] < 1 or not 0 <= field["offset"] < size for field in fields):
            raise ValueError(f"invalid std140 layout for {name}")
        uniforms = [dict(name=field["glsl_name"], type=field["type"], array_count=field["array_count"])
                    for field in row.get("glsl_uniforms", [])]
        blocks.append(binding(row, name=name, size=size, members=fields, gl_uniforms=uniforms))
    images, storage_buffers = [], []
    for resource in shader.get("views", []):
        if "texture" in resource:
            row = resource["texture"]
            images.append(binding(row, name=row["name"], type=row["type"],
                                  sample_type=row["sample_type"], multisampled=row["multisampled"]))
        elif "storage_buffer" in resource:
            row = resource["storage_buffer"]
            storage_buffers.append(binding(row, name=row["name"], readonly=row["readonly"]))
        else:
            raise ValueError(f"unsupported shader resource {list(resource)}")
    return dict(attributes=attributes, uniform_blocks=blocks, images=images, storage_buffers=storage_buffers,
                samplers=[binding(row, name=row["name"], type=row["sampler_type"])
                          for row in shader.get("samplers", [])],
                image_sampler_pairs=[dict(image_slot=row["view_slot"], sampler_slot=row["sampler_slot"],
                                          name=row.get("glsl_name", row["name"]))
                                     for row in shader.get("texture_sampler_pairs", [])])


def uniform_members(path):
    """Recover original std140 members, including blocks flattened by SPIRV-Cross."""
    blob = path.read_bytes()
    words = struct.unpack(f"<{len(blob) // 4}I", blob)
    names, members, types, constants = {}, {}, {}, {}
    offsets, strides, blocks = {}, {}, set()

    def literal(words):
        return struct.pack(f"<{len(words)}I", *words).split(b"\0", 1)[0].decode("utf-8")

    cursor = 5
    while cursor < len(words):
        count, opcode = divmod(words[cursor], 65536)
        if not count:
            raise ValueError(f"{path}: invalid SPIR-V instruction")
        args = words[cursor + 1:cursor + count]
        cursor += count
        if opcode == 5:  # OpName
            names[args[0]] = literal(args[1:])
        elif opcode == 6:  # OpMemberName
            members[args[:2]] = literal(args[2:])
        elif opcode in (21, 22, 23, 24, 28, 30):  # OpType*
            types[args[0]] = (opcode, args[1:])
        elif opcode == 43:  # OpConstant
            constants[args[1]] = args[2]
        elif opcode == 71 and args[1] == 2:  # Block
            blocks.add(args[0])
        elif opcode == 71 and args[1] == 6:  # ArrayStride
            strides[args[0]] = args[2]
        elif opcode == 72 and args[2] == 35:  # Offset
            offsets[args[:2]] = args[3]

    def kind(type_id):
        opcode, operands = types[type_id]
        if opcode == 21 and operands == (32, 1):
            return "int"
        if opcode == 22 and operands == (32,):
            return "float"
        if opcode == 23 and operands[1] in (2, 3, 4):
            return ("i" if kind(operands[0]) == "int" else "") + f"vec{operands[1]}"
        if opcode == 24 and operands[1] == 4 and kind(operands[0]) == "vec4":
            return "mat4"
        raise ValueError(f"{path}: unsupported std140 uniform type (SPIR-V opcode {opcode})")

    result = {}
    for block in blocks:
        if block not in names or not any(owner == block for owner, _ in offsets):
            continue
        block_type = types.get(block)
        if not block_type or block_type[0] != 30:
            continue
        # Built-in interface blocks do not carry std140 member offsets.
        if any((block, i) not in offsets for i in range(len(block_type[1]))):
            continue
        fields = []
        for index, type_id in enumerate(block_type[1]):
            opcode, operands = types[type_id]
            stride, count = None, 1
            if opcode == 28:
                stride, count = strides[type_id], constants[operands[1]]
                type_id = operands[0]
            field = {"name": members[block, index], "type": kind(type_id),
                     "offset": offsets[block, index], "array_count": count}
            if stride is not None:
                field["array_stride"] = stride
            fields.append(field)
        result[names[block]] = fields
    return result


def c_tables(programs, sources, header_name, header_symbols):
    lines = ["// Generated by NativeKit GPU shader tools. Do not edit.", "#pragma once",
             '#include "nativekit_gpu_shader_table.h"']
    if header_name:
        lines.extend((f'#include "{header_name}"', "namespace shader_source {"))
    lines.extend(f"static const char {symbol}[] = {json.dumps(source)};"
                 for symbol, source in sorted(sources.items()) if symbol not in header_symbols)

    def array(symbol, kind, rows):
        if not rows:
            return "NULL, 0"
        lines.append(f"static const nkgpu_shader_table_{kind} {symbol}[] = {{")
        lines.extend("    {" + ", ".join(map(str, row)) + "}," for row in rows)
        lines.append("};")
        return f"{symbol}, {len(rows)}"

    def stage(name):
        return "NKGPU_SHADERSTAGE_" + name.upper()

    for name, variants in sorted(programs.items()):
        for variant, program in sorted(variants.items()):
            backends = program["targets"]
            symbol = f"{name}_{variant}"
            gl = backends.get("glsl410", backends.get("glsl300es", {}))
            dx = backends.get("hlsl5", {})
            semantic = {a["slot"]: a for a in dx.get("attributes", [])}
            attributes = array(symbol + "_attributes", "attribute", [
                (a["slot"], json.dumps(a["name"]),
                 json.dumps(semantic.get(a["slot"], {}).get("semantic", {}).get("name", "")),
                 semantic.get(a["slot"], {}).get("semantic", {}).get("index", 0))
                for a in gl.get("attributes", [])])
            entries = []
            for slang, backend in sorted(backends.items()):
                stem = f"{symbol}_{slang}"
                blocks = backend.get("uniform_blocks", [])
                block_ref = array(stem + "_blocks", "block", [
                    (b["slot"], stage(b["stage"]), b["size"]) for b in blocks])
                uniforms = array(stem + "_uniforms", "uniform", [
                    (b["slot"], i, json.dumps(u["name"]), "NKGPU_UNIFORMTYPE_" + UNIFORM_TYPES[u["type"]],
                     u["array_count"]) for b in blocks for i, u in enumerate(b["gl_uniforms"])])
                views = {v["slot"]: v for v in backend["images"]}
                textures = []
                for pair in backend["image_sampler_pairs"]:
                    view = views[pair["image_slot"]]
                    if view["sample_type"] != "float" or view["multisampled"]:
                        raise ValueError(f"{name}: unsupported GPU table texture type")
                    textures.append((view["slot"], pair["sampler_slot"], stage(view["stage"]),
                                     "NKGPU_IMAGETYPE_" + IMAGE_TYPES[view["type"]],
                                     json.dumps(pair["name"])))
                if backend["storage_buffers"]:
                    raise ValueError(f"{name}: unsupported GPU table resource")
                texture_ref = array(stem + "_textures", "texture", textures)
                funcs = backend["stages"]
                entries.append((f"NKGPU_SHADER_TARGET_{slang.upper()}", funcs["vertex"]["source"],
                                funcs["fragment"]["source"], attributes, block_ref, uniforms, texture_ref))
            array(symbol + "_table", "entry", entries)
    if header_name:
        lines.append("} // namespace shader_source")
    return "\n".join(lines) + "\n"


def verify_digest(data, expected, label):
    if not expected or hashlib.sha256(data).hexdigest() != expected:
        raise ValueError(f"SHA-256 mismatch for {label}")


def pinned_shdc(override):
    os_name, machine = platform.system(), platform.machine().lower()
    architecture = {"arm64": "arm64", "aarch64": "arm64", "amd64": "x86_64", "x86_64": "x86_64"}
    folders = {
        ("Linux", "x86_64"): "linux", ("Linux", "arm64"): "linux_arm64",
        ("Darwin", "x86_64"): "osx", ("Darwin", "arm64"): "osx_arm64",
        ("Windows", "x86_64"): "win32",
    }
    folder = folders.get((os_name, architecture.get(machine)))
    if folder is None:
        raise ValueError(f"no pinned sokol-shdc binary for {os_name}/{machine}")
    executable = "sokol-shdc.exe" if os_name == "Windows" else "sokol-shdc"
    path = Path.home() / ".cache" / "nativekit" / SHDC_REVISION / folder / executable
    if override:
        path = Path(shutil.which(override) or override)
    if not path.is_file() and not override:
        path.parent.mkdir(parents=True, exist_ok=True)
        url = f"https://raw.githubusercontent.com/floooh/sokol-tools-bin/{SHDC_REVISION}/bin/{folder}/{executable}"
        with urllib.request.urlopen(url, timeout=60) as response:
            binary = response.read()
        verify_digest(binary, SHDC_SHA256[folder], url)
        with tempfile.NamedTemporaryFile(dir=path.parent, delete=False) as output:
            output.write(binary)
            temporary = Path(output.name)
        temporary.chmod(0o755)
        temporary.replace(path)
    verify_digest(path.read_bytes(), SHDC_SHA256[folder], path)
    return str(path.resolve())


def write_or_check(path, data, check):
    data = data.encode("utf-8")
    if check:
        if not path.is_file() or path.read_bytes() != data:
            raise ValueError(f"{path} is stale; regenerate without --check")
    else:
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_bytes(data)


def shader_source(path):
    return Path(path).read_text(encoding="utf-8")


def build(manifest_path, shdc, validator, check):
    manifest = json.loads(manifest_path.read_text(encoding="utf-8"))
    directory = manifest_path.parent
    slangs = manifest.get("slangs", SLANGS)
    if not slangs or len(set(slangs)) != len(slangs) or any(s not in SLANGS for s in slangs):
        raise ValueError("invalid shader target list")
    sources, programs = {}, {}
    if validator:
        validator = shutil.which(validator) or validator
        host = (platform.system(), platform.machine().lower())
        digest = os.environ.get("NATIVEKIT_GLSLANG_SHA256", VALIDATOR_SHA256.get(host))
        verify_digest(Path(validator).read_bytes(), digest, validator)
        version = subprocess.check_output([validator, "--version"], text=True)
        if ":15.1.0" not in version:
            raise ValueError("shader validation requires glslang 15.1.0")
    with tempfile.TemporaryDirectory(prefix="nativekit-shaders-") as temporary:
        work = Path(temporary)
        invocation_ids = itertools.count()
        for group in manifest["sources"]:
            source = (directory / group["file"]).resolve()
            for variant, defines in group.get("variants", {"default": []}).items():
                invocation = work / str(next(invocation_ids))
                invocation.mkdir()
                output = invocation / "shader"
                dependencies = invocation / "shader.d"
                staged = {}
                if manifest.get("include_roots"):
                    tree = invocation / "inputs"
                    for original in directory.rglob("*.glsl"):
                        copied = tree / original.relative_to(directory)
                        copied.parent.mkdir(parents=True, exist_ok=True)
                        shutil.copyfile(original, copied)
                        staged[copied] = original
                    input_path = tree / source.relative_to(directory)
                    for root in manifest["include_roots"]:
                        include_root = (directory / root["path"]).resolve()
                        prefix = Path(root["prefix"])
                        if prefix.is_absolute() or ".." in prefix.parts:
                            raise ValueError("shader include prefix must be relative")
                        for original in include_root.rglob("*.glsl"):
                            copied = input_path.parent / prefix / original.relative_to(include_root)
                            if copied in staged:
                                raise ValueError(f"duplicate shader include {copied.name}")
                            copied.parent.mkdir(parents=True, exist_ok=True)
                            shutil.copyfile(original, copied)
                            staged[copied] = original
                else:
                    input_path = source
                command = [shdc, "-i", str(input_path), "-o", str(output), "-l", ":".join(slangs),
                           "-f", "bare_yaml", "--no-log-cmdline", "--save-intermediate-spirv", f"--dependency-file={dependencies}"]
                if defines:
                    command.append("--defines=" + ":".join(defines))
                subprocess.run(command, check=True)
                dependency_text = dependencies.read_text(encoding="utf-8").partition(": ")[2]
                paths = (Path(p).resolve() for p in shlex.split(dependency_text.replace("\\\n", " ")))
                includes = sorted(Path(os.path.relpath(staged.get(path, path), directory)).as_posix()
                                  for path in paths)
                reflected = yaml.safe_load((invocation / "shader_reflection.yaml").read_text(encoding="utf-8"))
                canonical = "glsl410" if "glsl410" in slangs else slangs[0]
                members = {}
                for bytecode in sorted(invocation.glob(f"shader_{canonical}_*.spv")):
                    for block, fields in uniform_members(bytecode).items():
                        if block in members and members[block] != fields:
                            raise ValueError(f"{source}: ambiguous std140 block {block}")
                        members[block] = fields
                for target in reflected["shaders"]:
                    slang = target["slang"]
                    if {p["name"] for p in target["programs"]} != set(group["programs"]):
                        raise ValueError(f"{source}: manifest programs differ from @program declarations")
                    for shader in target["programs"]:
                        name = shader["name"]
                        program = programs.setdefault(name, {}).setdefault(variant, {
                            "defines": defines, "dependencies": includes, "targets": {}
                        })
                        if slang in program["targets"]:
                            raise ValueError(f"duplicate shader {name}/{variant}/{slang}")
                        reflection = normalize(shader, members)
                        reflection["stages"] = {}
                        for stage in STAGES:
                            function = shader.get(f"{stage}_func")
                            if function is None:
                                continue
                            if function["is_binary"]:
                                raise ValueError("shader manifest expects source output")
                            suffix = "" if variant == "default" else "_" + variant
                            symbol = f"{manifest.get('prefix', 'shader_')}{name}{suffix}_{slang}_{stage}"
                            destination = Path(function["path"])
                            if slang.startswith("glsl") and validator:
                                subprocess.run([validator, "-S", {"vertex": "vert", "fragment": "frag",
                                                "compute": "comp"}[stage], str(destination)], check=True)
                            sources[symbol] = shader_source(destination)
                            reflection["stages"][stage] = {"source": symbol, "entry_point": function["entry_point"]}
                        program["targets"][slang] = reflection
    for variants in programs.values():
        for program in variants.values():
            targets = program["targets"]
            if set(targets) != set(slangs):
                raise ValueError("incomplete backend output")
            attributes = {}
            for target in targets.values():
                for attribute in target["attributes"]:
                    canonical = attributes.setdefault(attribute["slot"], dict(attribute))
                    if attribute["format"] != canonical["format"]:
                        raise ValueError("vertex format differs across shader targets")
                    if attribute["name"]:
                        canonical["name"] = attribute["name"]
                    if attribute["semantic"]["name"]:
                        canonical["semantic"] = attribute["semantic"]
            for target in targets.values():
                target["attributes"] = [attributes[a["slot"]] for a in target["attributes"]]
                if any(not a["name"] for a in target["attributes"]):
                    raise ValueError("vertex names require one GLSL reflection target")
    header_symbols = {}
    if "header" in manifest:
        header = manifest["header"]
        lines = [header["banner"], "#pragma once", "", f"namespace {header['namespace']} {{"]
        header_targets = header.get("slangs", slangs)
        header_symbols = {name: source for name, source in sources.items()
                          if any(name.endswith(f"_{target}_{stage}")
                                 for target in header_targets for stage in STAGES)}
        lines.extend(f"inline constexpr char {name}[] = {json.dumps(source)};"
                     for name, source in sorted(header_symbols.items()))
        lines.extend((f"}} // namespace {header['namespace']}", ""))
        write_or_check(directory / header["output"], "\n".join(lines), check)
    if "tables" in manifest:
        output = directory / manifest["tables"]
        source_header = manifest.get("header", {}).get("output")
        header_name = Path(source_header).name if source_header else None
        write_or_check(output, c_tables(programs, sources, header_name, header_symbols), check)
    if "reflection" in manifest:
        reflection_path = directory / manifest["reflection"]
        reflection = {"schema": "nkgpu-shader-reflection/1", "programs": programs}
        if manifest.get("embed_sources", False):
            reflection["sources"] = sources
        write_or_check(reflection_path, json.dumps(reflection, indent=2, sort_keys=True) + "\n", check)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("manifest", type=Path)
    parser.add_argument("--shdc", default=os.environ.get("SOKOL_SHDC"))
    parser.add_argument("--validator", default="glslangValidator")
    parser.add_argument("--check", action="store_true")
    args = parser.parse_args()
    try:
        build(args.manifest.resolve(), pinned_shdc(args.shdc), args.validator, args.check)
    except (OSError, ValueError, KeyError, subprocess.CalledProcessError, yaml.YAMLError) as error:
        print(f"shader generation failed: {error}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
