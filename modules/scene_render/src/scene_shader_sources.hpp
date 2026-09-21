#ifndef NATIVEKIT_SCENE_SHADER_SOURCES_HPP
#define NATIVEKIT_SCENE_SHADER_SOURCES_HPP

#include "nativekit_gpu.h"
#include "scene_shader_sources.h"

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
cbuffer view_projection : register(b1)
{
    float4x4 value : packoffset(c0);
};

struct SceneVertexInput
{
    float3 position : POSITION0;
    float4 transform0 : TEXCOORD1;
    float4 transform1 : TEXCOORD2;
    float4 transform2 : TEXCOORD3;
    float4 transform3 : TEXCOORD4;
    float3 normal : TEXCOORD5;
    float2 texcoord0 : TEXCOORD6;
    float4 color0 : TEXCOORD7;
};

struct SceneVertexOutput
{
    float4 position : SV_Position;
    float3 world_position : TEXCOORD0;
    float3 normal : TEXCOORD1;
    float2 texcoord0 : TEXCOORD2;
    float4 color0 : TEXCOORD3;
};

SceneVertexOutput main(SceneVertexInput input)
{
    SceneVertexOutput output;
    float3 world_position = mul(float4(input.position, 1.0f),
                                float4x4(input.transform0, input.transform1,
                                         input.transform2, input.transform3)).xyz;
    output.position = mul(float4(world_position, 1.0f), value);
    output.world_position = world_position;
    output.normal = normalize(mul(float4(input.normal, 0.0f),
                                  float4x4(input.transform0, input.transform1,
                                           input.transform2, input.transform3)).xyz);
    output.texcoord0 = input.texcoord0;
    output.color0 = input.color0;
    return output;
}
)";

inline constexpr char scene_fragment_hlsl[] = R"(
cbuffer material_params : register(b0)
{
    float4 base_color : packoffset(c0);
    float4 surface_params : packoffset(c1);
    float4 emissive : packoffset(c2);
};

Texture2D base_color_texture : register(t0);
SamplerState base_color_sampler : register(s0);

cbuffer clip_params : register(b2)
{
    float4 clip_planes[32] : packoffset(c0);
    float4 clip_plane_count : packoffset(c32);
};

struct SceneFragmentInput
{
    float3 world_position : TEXCOORD0;
    float3 normal : TEXCOORD1;
    float2 texcoord0 : TEXCOORD2;
    float4 color0 : TEXCOORD3;
};

float4 main(SceneFragmentInput input) : SV_Target0
{
    for (int index = 0; index < 32; ++index) {
        if (index >= int(clip_plane_count.x))
            break;
        if (dot(clip_planes[index].xyz, input.world_position) + clip_planes[index].w < 0.0f)
            discard;
    }
    float4 texture_color = base_color_texture.Sample(base_color_sampler, input.texcoord0);
    float3 light_direction = normalize(float3(0.35f, 0.45f, 0.82f));
    float diffuse = 0.35f + 0.65f * max(dot(normalize(input.normal), light_direction), 0.0f);
    float3 color = base_color.rgb * texture_color.rgb * input.color0.rgb * diffuse + emissive.rgb;
    return float4(color, base_color.a * texture_color.a * input.color0.a);
}
)";

inline constexpr char scene_vertex_msl[] = R"(
#include <metal_stdlib>
#include <simd/simd.h>

using namespace metal;

struct SceneViewParams
{
    float4x4 value;
};

struct SceneVertexOutput
{
    float4 position [[position]];
    float3 world_position [[user(locn0)]];
    float3 normal [[user(locn1)]];
    float2 texcoord0 [[user(locn2)]];
    float4 color0 [[user(locn3)]];
};

struct SceneVertexInput
{
    float3 position [[attribute(0)]];
    float4 transform0 [[attribute(1)]];
    float4 transform1 [[attribute(2)]];
    float4 transform2 [[attribute(3)]];
    float4 transform3 [[attribute(4)]];
    float3 normal [[attribute(5)]];
    float2 texcoord0 [[attribute(6)]];
    float4 color0 [[attribute(7)]];
};

vertex SceneVertexOutput main0(SceneVertexInput input [[stage_in]],
                              constant SceneViewParams &view [[buffer(1)]])
{
    SceneVertexOutput output = {};
    output.world_position = (float4x4(input.transform0, input.transform1,
                                      input.transform2, input.transform3) *
                             float4(input.position, 1.0)).xyz;
    output.position = view.value * float4(output.world_position, 1.0);
    output.normal = normalize((float4x4(input.transform0, input.transform1,
                                        input.transform2, input.transform3) *
                               float4(input.normal, 0.0)).xyz);
    output.texcoord0 = input.texcoord0;
    output.color0 = input.color0;
    return output;
}
)";

inline constexpr char scene_fragment_msl[] = R"(
#include <metal_stdlib>

using namespace metal;

struct SceneMaterialParams
{
    float4 base_color;
    float4 surface_params;
    float4 emissive;
};

struct SceneClipParams
{
    float4 planes[32];
    float4 count;
};

struct SceneFragmentInput
{
    float3 world_position [[user(locn0)]];
    float3 normal [[user(locn1)]];
    float2 texcoord0 [[user(locn2)]];
    float4 color0 [[user(locn3)]];
};

fragment float4 main0(SceneFragmentInput input [[stage_in]],
                      constant SceneMaterialParams &params [[buffer(0)]],
                      constant SceneClipParams &clip [[buffer(2)]],
                      texture2d<float> base_color_texture [[texture(0)]],
                      sampler base_color_sampler [[sampler(0)]])
{
    for (int index = 0; index < 32; ++index) {
        if (index >= int(clip.count.x))
            break;
        if (dot(clip.planes[index].xyz, input.world_position) + clip.planes[index].w < 0.0)
            discard_fragment();
    }
    float4 texture_color = base_color_texture.sample(base_color_sampler, input.texcoord0);
    float3 light_direction = normalize(float3(0.35, 0.45, 0.82));
    float diffuse = 0.35 + 0.65 * max(dot(normalize(input.normal), light_direction), 0.0);
    float3 color = params.base_color.rgb * texture_color.rgb * input.color0.rgb * diffuse +
                   params.emissive.rgb;
    return float4(color, params.base_color.a * texture_color.a * input.color0.a);
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
cbuffer view_projection : register(b1)
{
    float4x4 value : packoffset(c0);
};

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
    float3 world_position : TEXCOORD1;
};

ScenePickVertexOutput main(ScenePickVertexInput input)
{
    ScenePickVertexOutput output;
    output.world_position = mul(float4(input.position, 1.0f),
                                float4x4(input.transform0, input.transform1,
                                         input.transform2, input.transform3)).xyz;
    output.position = mul(float4(output.world_position, 1.0f), value);
    output.pick_color = input.pick_color;
    return output;
}
)";

inline constexpr char scene_pick_fragment_hlsl[] = R"(
cbuffer clip_params : register(b2)
{
    float4 clip_planes[32] : packoffset(c0);
    float4 clip_plane_count : packoffset(c32);
};

struct ScenePickFragmentInput
{
    float4 pick_color : TEXCOORD0;
    float3 world_position : TEXCOORD1;
    float4 position : SV_Position;
    uint primitive_id : SV_PrimitiveID;
};

struct ScenePickFragmentOutput
{
    float4 color : SV_Target0;
    float4 subelement : SV_Target1;
    float depth : SV_Target2;
};

ScenePickFragmentOutput main(ScenePickFragmentInput input)
{
    for (int index = 0; index < 32; ++index) {
        if (index >= int(clip_plane_count.x))
            break;
        if (dot(clip_planes[index].xyz, input.world_position) + clip_planes[index].w < 0.0f)
            discard;
    }
    ScenePickFragmentOutput output;
    uint id = input.primitive_id + 1u;
    output.color = input.pick_color;
    output.subelement = float4(float(id & 0xffu) / 255.0f,
                               float((id >> 8u) & 0xffu) / 255.0f,
                               float((id >> 16u) & 0xffu) / 255.0f, 1.0f);
    output.depth = input.position.z;
    return output;
}
)";

inline constexpr char scene_pick_vertex_msl[] = R"(
#include <metal_stdlib>
#include <simd/simd.h>

using namespace metal;

struct SceneViewParams
{
    float4x4 value;
};

struct ScenePickVertexOutput
{
    float4 position [[position]];
    float4 pick_color [[user(locn0)]];
    float3 world_position [[user(locn1)]];
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

vertex ScenePickVertexOutput main0(ScenePickVertexInput input [[stage_in]],
                                  constant SceneViewParams &view [[buffer(1)]])
{
    ScenePickVertexOutput output = {};
    output.world_position = (float4x4(input.transform0, input.transform1,
                                      input.transform2, input.transform3) *
                             float4(input.position, 1.0)).xyz;
    output.position = view.value * float4(output.world_position, 1.0);
    output.pick_color = input.pick_color;
    return output;
}
)";

inline constexpr char scene_pick_fragment_msl[] = R"(
#include <metal_stdlib>

using namespace metal;

struct SceneClipParams
{
    float4 planes[32];
    float4 count;
};

struct ScenePickFragmentInput
{
    float4 pick_color [[user(locn0)]];
    float3 world_position [[user(locn1)]];
    float4 position [[position]];
    uint primitive_id [[primitive_id]];
};

struct ScenePickFragmentOutput
{
    float4 color [[color(0)]];
    float4 subelement [[color(1)]];
    float depth [[color(2)]];
};

fragment ScenePickFragmentOutput main0(ScenePickFragmentInput input [[stage_in]],
                                      constant SceneClipParams &clip [[buffer(2)]])
{
    for (int index = 0; index < 32; ++index) {
        if (index >= int(clip.count.x))
            break;
        if (dot(clip.planes[index].xyz, input.world_position) + clip.planes[index].w < 0.0)
            discard_fragment();
    }
    ScenePickFragmentOutput output = {};
    uint id = input.primitive_id + 1u;
    output.color = input.pick_color;
    output.subelement = float4(float(id & 0xffu) / 255.0,
                               float((id >> 8u) & 0xffu) / 255.0,
                               float((id >> 16u) & 0xffu) / 255.0, 1.0);
    output.depth = input.position.z;
    return output;
}
)";

inline SceneShaderSources scene_shader_sources(nkgpu_backend backend, bool picking) {
    switch (backend) {
    case NKGPU_BACKEND_GLCORE:
        return picking ? SceneShaderSources{shader_source::pick_vertex_gl,
                                            shader_source::pick_fragment_gl,
                                            NKGPU_SHADERLANGUAGE_GLSL}
                       : SceneShaderSources{shader_source::scene_vertex_gl,
                                            shader_source::scene_fragment_gl,
                                            NKGPU_SHADERLANGUAGE_GLSL};
    case NKGPU_BACKEND_GLES3:
        return picking ? SceneShaderSources{shader_source::pick_vertex_gles,
                                            shader_source::pick_fragment_gles,
                                            NKGPU_SHADERLANGUAGE_GLSL}
                       : SceneShaderSources{shader_source::scene_vertex_gles,
                                            shader_source::scene_fragment_gles,
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
