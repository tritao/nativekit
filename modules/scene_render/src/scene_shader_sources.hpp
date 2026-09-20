#ifndef NATIVEKIT_SCENE_SHADER_SOURCES_HPP
#define NATIVEKIT_SCENE_SHADER_SOURCES_HPP

#include "nativekit_gpu.h"

namespace nkscene::render_internal {

struct SceneShaderSources {
    const char *vertex = nullptr;
    const char *fragment = nullptr;
    nkgpu_shader_language language = 0;
};

inline constexpr char scene_vertex_glsl[] = R"(
#version 330
layout(location = 0) in vec3 position;
layout(location = 1) in vec4 transform0;
layout(location = 2) in vec4 transform1;
layout(location = 3) in vec4 transform2;
layout(location = 4) in vec4 transform3;
void main(){ mat4 transform=mat4(transform0,transform1,transform2,transform3); gl_Position=transform*vec4(position,1.0); }
)";

inline constexpr char scene_vertex_gles[] = R"(
#version 300 es
precision highp float;
layout(location = 0) in vec3 position;
layout(location = 1) in vec4 transform0;
layout(location = 2) in vec4 transform1;
layout(location = 3) in vec4 transform2;
layout(location = 4) in vec4 transform3;
void main(){ mat4 transform=mat4(transform0,transform1,transform2,transform3); gl_Position=transform*vec4(position,1.0); }
)";

inline constexpr char scene_fragment_glsl[] = R"(
#version 330
uniform vec4 material_color;
out vec4 fragment_color;
void main(){ fragment_color=material_color; }
)";

inline constexpr char scene_fragment_gles[] = R"(
#version 300 es
precision mediump float;
uniform vec4 material_color;
out vec4 fragment_color;
void main(){ fragment_color=material_color; }
)";

inline constexpr char scene_vertex_hlsl[] = R"(
struct SceneVertexInput
{
    float3 position : POSITION0;
    float4 transform0 : TEXCOORD1;
    float4 transform1 : TEXCOORD2;
    float4 transform2 : TEXCOORD3;
    float4 transform3 : TEXCOORD4;
};

struct SceneVertexOutput
{
    float4 position : SV_Position;
};

SceneVertexOutput main(SceneVertexInput input)
{
    SceneVertexOutput output;
    output.position = mul(float4(input.position, 1.0f),
                          float4x4(input.transform0, input.transform1,
                                   input.transform2, input.transform3));
    return output;
}
)";

inline constexpr char scene_fragment_hlsl[] = R"(
cbuffer material_color : register(b0)
{
    float4 value : packoffset(c0);
};

float4 main() : SV_Target0
{
    return value;
}
)";

inline constexpr char scene_vertex_msl[] = R"(
#include <metal_stdlib>
#include <simd/simd.h>

using namespace metal;

struct SceneVertexOutput
{
    float4 position [[position]];
};

struct SceneVertexInput
{
    float3 position [[attribute(0)]];
    float4 transform0 [[attribute(1)]];
    float4 transform1 [[attribute(2)]];
    float4 transform2 [[attribute(3)]];
    float4 transform3 [[attribute(4)]];
};

vertex SceneVertexOutput main0(SceneVertexInput input [[stage_in]])
{
    SceneVertexOutput output = {};
    output.position = float4x4(input.transform0, input.transform1,
                               input.transform2, input.transform3) *
        float4(input.position, 1.0);
    return output;
}
)";

inline constexpr char scene_fragment_msl[] = R"(
#include <metal_stdlib>

using namespace metal;

struct SceneMaterialParams
{
    float4 value;
};

fragment float4 main0(constant SceneMaterialParams &params [[buffer(0)]])
{
    return params.value;
}
)";

inline constexpr char scene_pick_vertex_glsl[] = R"(
#version 330
layout(location = 0) in vec3 position;
layout(location = 1) in vec4 transform0;
layout(location = 2) in vec4 transform1;
layout(location = 3) in vec4 transform2;
layout(location = 4) in vec4 transform3;
layout(location = 5) in vec4 pick_color;
out vec4 vertex_pick_color;
void main(){ mat4 transform=mat4(transform0,transform1,transform2,transform3); vertex_pick_color=pick_color; gl_Position=transform*vec4(position,1.0); }
)";

inline constexpr char scene_pick_vertex_gles[] = R"(
#version 300 es
precision highp float;
layout(location = 0) in vec3 position;
layout(location = 1) in vec4 transform0;
layout(location = 2) in vec4 transform1;
layout(location = 3) in vec4 transform2;
layout(location = 4) in vec4 transform3;
layout(location = 5) in vec4 pick_color;
out vec4 vertex_pick_color;
void main(){ mat4 transform=mat4(transform0,transform1,transform2,transform3); vertex_pick_color=pick_color; gl_Position=transform*vec4(position,1.0); }
)";

inline constexpr char scene_pick_fragment_glsl[] = R"(
#version 330
in vec4 vertex_pick_color;
out vec4 fragment_color;
layout(location=1) out vec4 fragment_subelement;
void main(){ uint id=uint(gl_PrimitiveID)+1u; fragment_color=vertex_pick_color; fragment_subelement=vec4(float(id & 0xffu)/255.0,float((id >> 8u) & 0xffu)/255.0,float((id >> 16u) & 0xffu)/255.0,1.0); }
)";

inline constexpr char scene_pick_fragment_gles[] = R"(
#version 300 es
precision mediump float;
precision highp int;
in vec4 vertex_pick_color;
out vec4 fragment_color;
layout(location=1) out vec4 fragment_subelement;
void main(){ uint id=uint(gl_PrimitiveID)+1u; fragment_color=vertex_pick_color; fragment_subelement=vec4(float(id & 0xffu)/255.0,float((id >> 8u) & 0xffu)/255.0,float((id >> 16u) & 0xffu)/255.0,1.0); }
)";

inline constexpr char scene_pick_vertex_hlsl[] = R"(
struct ScenePickVertexInput
{
    float3 position : POSITION0;
    float4 transform0 : TEXCOORD1;
    float4 transform1 : TEXCOORD2;
    float4 transform2 : TEXCOORD3;
    float4 transform3 : TEXCOORD4;
    float4 pick_color : TEXCOORD5;
};

struct ScenePickVertexOutput
{
    float4 position : SV_Position;
    float4 pick_color : TEXCOORD0;
};

ScenePickVertexOutput main(ScenePickVertexInput input)
{
    ScenePickVertexOutput output;
    output.position = mul(float4(input.position, 1.0f),
                          float4x4(input.transform0, input.transform1,
                                   input.transform2, input.transform3));
    output.pick_color = input.pick_color;
    return output;
}
)";

inline constexpr char scene_pick_fragment_hlsl[] = R"(
struct ScenePickFragmentInput
{
    float4 pick_color : TEXCOORD0;
    uint primitive_id : SV_PrimitiveID;
};

struct ScenePickFragmentOutput
{
    float4 color : SV_Target0;
    float4 subelement : SV_Target1;
};

ScenePickFragmentOutput main(ScenePickFragmentInput input)
{
    ScenePickFragmentOutput output;
    uint id = input.primitive_id + 1u;
    output.color = input.pick_color;
    output.subelement = float4(float(id & 0xffu) / 255.0f,
                               float((id >> 8u) & 0xffu) / 255.0f,
                               float((id >> 16u) & 0xffu) / 255.0f, 1.0f);
    return output;
}
)";

inline constexpr char scene_pick_vertex_msl[] = R"(
#include <metal_stdlib>
#include <simd/simd.h>

using namespace metal;

struct ScenePickVertexOutput
{
    float4 position [[position]];
    float4 pick_color [[user(locn0)]];
};

struct ScenePickVertexInput
{
    float3 position [[attribute(0)]];
    float4 transform0 [[attribute(1)]];
    float4 transform1 [[attribute(2)]];
    float4 transform2 [[attribute(3)]];
    float4 transform3 [[attribute(4)]];
    float4 pick_color [[attribute(5)]];
};

vertex ScenePickVertexOutput main0(ScenePickVertexInput input [[stage_in]])
{
    ScenePickVertexOutput output = {};
    output.position = float4x4(input.transform0, input.transform1,
                               input.transform2, input.transform3) *
        float4(input.position, 1.0);
    output.pick_color = input.pick_color;
    return output;
}
)";

inline constexpr char scene_pick_fragment_msl[] = R"(
#include <metal_stdlib>

using namespace metal;

struct ScenePickFragmentInput
{
    float4 pick_color [[user(locn0)]];
    uint primitive_id [[primitive_id]];
};

struct ScenePickFragmentOutput
{
    float4 color [[color(0)]];
    float4 subelement [[color(1)]];
};

fragment ScenePickFragmentOutput main0(ScenePickFragmentInput input [[stage_in]])
{
    ScenePickFragmentOutput output = {};
    uint id = input.primitive_id + 1u;
    output.color = input.pick_color;
    output.subelement = float4(float(id & 0xffu) / 255.0,
                               float((id >> 8u) & 0xffu) / 255.0,
                               float((id >> 16u) & 0xffu) / 255.0, 1.0);
    return output;
}
)";

inline SceneShaderSources scene_shader_sources(nkgpu_backend backend, bool picking) {
    switch (backend) {
    case NKGPU_BACKEND_GLCORE:
        return picking ? SceneShaderSources{scene_pick_vertex_glsl, scene_pick_fragment_glsl,
                                            NKGPU_SHADERLANGUAGE_GLSL}
                       : SceneShaderSources{scene_vertex_glsl, scene_fragment_glsl,
                                            NKGPU_SHADERLANGUAGE_GLSL};
    case NKGPU_BACKEND_GLES3:
        return picking ? SceneShaderSources{scene_pick_vertex_gles, scene_pick_fragment_gles,
                                            NKGPU_SHADERLANGUAGE_GLSL}
                       : SceneShaderSources{scene_vertex_gles, scene_fragment_gles,
                                            NKGPU_SHADERLANGUAGE_GLSL};
    case NKGPU_BACKEND_D3D11:
        return picking ? SceneShaderSources{scene_pick_vertex_hlsl, scene_pick_fragment_hlsl,
                                            NKGPU_SHADERLANGUAGE_HLSL5}
                       : SceneShaderSources{scene_vertex_hlsl, scene_fragment_hlsl,
                                            NKGPU_SHADERLANGUAGE_HLSL5};
    case NKGPU_BACKEND_METAL:
        return picking ? SceneShaderSources{scene_pick_vertex_msl, scene_pick_fragment_msl,
                                            NKGPU_SHADERLANGUAGE_MSL}
                       : SceneShaderSources{scene_vertex_msl, scene_fragment_msl,
                                            NKGPU_SHADERLANGUAGE_MSL};
    default:
        return {};
    }
}

} // namespace nkscene::render_internal

#endif
