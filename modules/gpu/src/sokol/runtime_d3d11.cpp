#include "nativekit_sokol_runtime.h"

#if !defined(SOKOL_D3D11)
#error "runtime_d3d11.cpp requires the Sokol D3D11 backend"
#endif

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <d3d11.h>

#include <algorithm>
#include <cstdint>
#include <cstring>

namespace {

constexpr uint32_t kReadbackCapacity = 128;
constexpr uint32_t kReadbackPending = 1;
constexpr uint32_t kReadbackReady = 2;
constexpr uint32_t kReadbackFailed = 3;

struct ImageInfo {
    ID3D11Texture2D *texture = nullptr;
    D3D11_TEXTURE2D_DESC desc{};
    uint32_t width = 0;
    uint32_t height = 0;
    uint32_t bytes = 0;
    DXGI_FORMAT format = DXGI_FORMAT_UNKNOWN;
};

struct ReadbackSlot {
    uint32_t generation = 0;
    ID3D11Texture2D *texture = nullptr;
    ID3D11Buffer *buffer = nullptr;
    ID3D11Query *query = nullptr;
    uint32_t x = 0;
    uint32_t y = 0;
    uint32_t size = 0;
    uint32_t row_pitch = 0;
    uint32_t width = 0;
    uint32_t height = 0;
    bool active = false;
};

ReadbackSlot readbacks[kReadbackCapacity];

struct TimestampSlot {
    uint32_t generation = 0;
    ID3D11Query *begin = nullptr;
    ID3D11Query *end = nullptr;
    ID3D11Query *disjoint = nullptr;
    uint64_t nanoseconds = 0;
    bool active = false;
    bool ended = false;
};

TimestampSlot timestamps[kReadbackCapacity];

ID3D11Device *device() {
    return const_cast<ID3D11Device *>(static_cast<const ID3D11Device *>(sg_d3d11_device()));
}

ID3D11DeviceContext *context() {
    return const_cast<ID3D11DeviceContext *>(
        static_cast<const ID3D11DeviceContext *>(sg_d3d11_device_context()));
}

bool format_info(sg_pixel_format format, DXGI_FORMAT &out_format, uint32_t &out_bytes) {
    switch (format) {
    case SG_PIXELFORMAT_R8:
        out_format = DXGI_FORMAT_R8_UNORM;
        out_bytes = 1;
        return true;
    case SG_PIXELFORMAT_RG8:
        out_format = DXGI_FORMAT_R8G8_UNORM;
        out_bytes = 2;
        return true;
    case SG_PIXELFORMAT_RGBA8:
        out_format = DXGI_FORMAT_R8G8B8A8_UNORM;
        out_bytes = 4;
        return true;
    case SG_PIXELFORMAT_BGRA8:
        out_format = DXGI_FORMAT_B8G8R8A8_UNORM;
        out_bytes = 4;
        return true;
    case SG_PIXELFORMAT_R16F:
        out_format = DXGI_FORMAT_R16_FLOAT;
        out_bytes = 2;
        return true;
    case SG_PIXELFORMAT_RG16F:
        out_format = DXGI_FORMAT_R16G16_FLOAT;
        out_bytes = 4;
        return true;
    case SG_PIXELFORMAT_RGBA16F:
        out_format = DXGI_FORMAT_R16G16B16A16_FLOAT;
        out_bytes = 8;
        return true;
    case SG_PIXELFORMAT_R32F:
        out_format = DXGI_FORMAT_R32_FLOAT;
        out_bytes = 4;
        return true;
    case SG_PIXELFORMAT_RGBA32F:
        out_format = DXGI_FORMAT_R32G32B32A32_FLOAT;
        out_bytes = 16;
        return true;
    case SG_PIXELFORMAT_R32UI:
        out_format = DXGI_FORMAT_R32_UINT;
        out_bytes = 4;
        return true;
    case SG_PIXELFORMAT_DEPTH:
        out_format = DXGI_FORMAT_R32_TYPELESS;
        out_bytes = 4;
        return true;
    case SG_PIXELFORMAT_DEPTH_STENCIL:
        return false;
    default:
        return false;
    }
}

bool image_info(sg_image image, uint32_t mip_level, uint32_t layer, uint32_t x, uint32_t y,
                uint32_t width, uint32_t height, ImageInfo &out) {
    if (!image.id || sg_query_image_state(image) != SG_RESOURCESTATE_VALID ||
        sg_query_image_type(image) != SG_IMAGETYPE_2D &&
            sg_query_image_type(image) != SG_IMAGETYPE_ARRAY ||
        mip_level >= static_cast<uint32_t>(sg_query_image_num_mipmaps(image)) ||
        layer >= static_cast<uint32_t>(sg_query_image_num_slices(image)) ||
        sg_query_image_sample_count(image) != 1 || !width || !height)
        return false;
    const sg_d3d11_image_info native = sg_d3d11_query_image_info(image);
    if (!native.tex2d || !native.res)
        return false;
    DXGI_FORMAT expected_format = DXGI_FORMAT_UNKNOWN;
    uint32_t bytes = 0;
    if (!format_info(sg_query_image_pixelformat(image), expected_format, bytes))
        return false;
    auto *texture =
        const_cast<ID3D11Texture2D *>(static_cast<const ID3D11Texture2D *>(native.tex2d));
    texture->GetDesc(&out.desc);
    const bool depth_format = expected_format == DXGI_FORMAT_R32_TYPELESS;
    const bool format_matches = out.desc.Format == expected_format ||
                                (depth_format && out.desc.Format == DXGI_FORMAT_D32_FLOAT);
    if (!format_matches || out.desc.SampleDesc.Count != 1 || out.desc.ArraySize <= layer)
        return false;
    out.texture = texture;
    out.format = expected_format;
    out.bytes = bytes;
    out.width = std::max(1u, static_cast<uint32_t>(sg_query_image_width(image)) >> mip_level);
    out.height = std::max(1u, static_cast<uint32_t>(sg_query_image_height(image)) >> mip_level);
    if (x > out.width || y > out.height || width > out.width - x || height > out.height - y)
        return false;
    return true;
}

bool buffer_info(sg_buffer buffer, ID3D11Buffer *&out, uint32_t &out_size) {
    if (!buffer.id || sg_query_buffer_state(buffer) != SG_RESOURCESTATE_VALID)
        return false;
    const sg_d3d11_buffer_info native = sg_d3d11_query_buffer_info(buffer);
    if (!native.buf || sg_query_buffer_size(buffer) > UINT32_MAX)
        return false;
    out = const_cast<ID3D11Buffer *>(static_cast<const ID3D11Buffer *>(native.buf));
    out_size = static_cast<uint32_t>(sg_query_buffer_size(buffer));
    return true;
}

uint32_t subresource(const ImageInfo &image, uint32_t mip_level, uint32_t layer) {
    return D3D11CalcSubresource(mip_level, layer, image.desc.MipLevels);
}

bool create_staging_buffer(uint32_t size, UINT cpu_access, ID3D11Buffer **out) {
    if (!out || !size || !device())
        return false;
    D3D11_BUFFER_DESC desc{};
    desc.ByteWidth = size;
    desc.Usage = D3D11_USAGE_STAGING;
    desc.CPUAccessFlags = cpu_access;
    return SUCCEEDED(device()->CreateBuffer(&desc, nullptr, out));
}

bool create_staging_texture(const ImageInfo &source, uint32_t width, uint32_t height,
                            ID3D11Texture2D **out) {
    if (!out || !device())
        return false;
    D3D11_TEXTURE2D_DESC desc = source.desc;
    desc.Width = width;
    desc.Height = height;
    desc.MipLevels = 1;
    desc.ArraySize = 1;
    desc.SampleDesc.Count = 1;
    desc.SampleDesc.Quality = 0;
    desc.Usage = D3D11_USAGE_STAGING;
    desc.BindFlags = 0;
    desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    desc.MiscFlags = 0;
    /* Preserve typeless depth resources for CopySubresourceRegion. The depth
       view remains D32_FLOAT, while the staging copy must use the same
       resource format to preserve the underlying R32 depth bits. */
    if (source.format == DXGI_FORMAT_R32_TYPELESS)
        desc.Format = source.desc.Format;
    return SUCCEEDED(device()->CreateTexture2D(&desc, nullptr, out));
}

bool copy_buffer_region(ID3D11Buffer *source, uint32_t source_offset, ID3D11Buffer *destination,
                        uint32_t destination_offset, uint32_t size) {
    if (!source || !destination || !context() || !size)
        return false;
    D3D11_BOX box{};
    box.left = source_offset;
    box.right = source_offset + size;
    box.top = 0;
    box.bottom = 1;
    box.front = 0;
    box.back = 1;
    context()->CopySubresourceRegion(destination, 0, destination_offset, 0, 0, source, 0, &box);
    return true;
}

uint32_t readback_token(uint32_t index, uint32_t generation) {
    return (generation << 16) | (index + 1u);
}

ReadbackSlot *readback_slot(uint32_t token) {
    const uint32_t encoded_index = token & 0xFFFFu;
    const uint32_t generation = token >> 16;
    if (!encoded_index || encoded_index > kReadbackCapacity || !generation)
        return nullptr;
    ReadbackSlot &slot = readbacks[encoded_index - 1u];
    return slot.active && slot.generation == generation ? &slot : nullptr;
}

void release_readback(ReadbackSlot &slot) {
    if (slot.texture)
        slot.texture->Release();
    if (slot.buffer)
        slot.buffer->Release();
    if (slot.query)
        slot.query->Release();
    slot.texture = nullptr;
    slot.buffer = nullptr;
    slot.query = nullptr;
    slot.x = 0;
    slot.y = 0;
    slot.size = 0;
    slot.row_pitch = 0;
    slot.width = 0;
    slot.height = 0;
    slot.active = false;
    slot.generation = (slot.generation % 0xFFFFu) + 1u;
    if (!slot.generation)
        slot.generation = 1;
}

uint32_t timestamp_token(uint32_t index, uint32_t generation) {
    return (generation << 16) | (index + 1u);
}

TimestampSlot *timestamp_slot(uint32_t token) {
    const uint32_t encoded_index = token & 0xFFFFu;
    const uint32_t generation = token >> 16;
    if (!encoded_index || encoded_index > kReadbackCapacity || !generation)
        return nullptr;
    TimestampSlot &slot = timestamps[encoded_index - 1u];
    return slot.active && slot.generation == generation ? &slot : nullptr;
}

void release_timestamp(TimestampSlot &slot) {
    if (slot.begin)
        slot.begin->Release();
    if (slot.end)
        slot.end->Release();
    if (slot.disjoint)
        slot.disjoint->Release();
    slot.begin = nullptr;
    slot.end = nullptr;
    slot.disjoint = nullptr;
    slot.nanoseconds = 0;
    slot.active = false;
    slot.ended = false;
    slot.generation = (slot.generation % 0xFFFFu) + 1u;
    if (!slot.generation)
        slot.generation = 1;
}

uint32_t d3d11_buffer_copy(sg_buffer source, uint32_t source_offset, sg_buffer destination,
                           uint32_t destination_offset, uint32_t size) {
    ID3D11Buffer *source_buffer = nullptr;
    ID3D11Buffer *destination_buffer = nullptr;
    uint32_t source_size = 0;
    uint32_t destination_size = 0;
    if (!buffer_info(source, source_buffer, source_size) ||
        !buffer_info(destination, destination_buffer, destination_size) || !size ||
        source_offset > source_size || size > source_size - source_offset ||
        destination_offset > destination_size || size > destination_size - destination_offset)
        return 0;
    return copy_buffer_region(source_buffer, source_offset, destination_buffer, destination_offset,
                              size);
}

uint32_t d3d11_image_copy(sg_image source, uint32_t source_mip, uint32_t source_layer,
                          uint32_t source_x, uint32_t source_y, sg_image destination,
                          uint32_t destination_mip, uint32_t destination_layer,
                          uint32_t destination_x, uint32_t destination_y, uint32_t width,
                          uint32_t height) {
    ImageInfo source_info;
    ImageInfo destination_info;
    if (!image_info(source, source_mip, source_layer, source_x, source_y, width, height,
                    source_info) ||
        !image_info(destination, destination_mip, destination_layer, destination_x, destination_y,
                    width, height, destination_info) ||
        source_info.format != destination_info.format || !context())
        return 0;
    if (source_info.format == DXGI_FORMAT_R32_TYPELESS) {
        /* D3D11 requires whole-subresource copies for depth-stencil resources. */
        if (source_x || source_y || destination_x || destination_y || width != source_info.width ||
            height != source_info.height || width != destination_info.width ||
            height != destination_info.height)
            return 0;
        context()->CopySubresourceRegion(
            destination_info.texture,
            subresource(destination_info, destination_mip, destination_layer), 0, 0, 0,
            source_info.texture,
            subresource(source_info, source_mip, source_layer), nullptr);
        return 1;
    }
    D3D11_BOX box{};
    box.left = source_x;
    box.right = source_x + width;
    box.top = source_y;
    box.bottom = source_y + height;
    box.front = 0;
    box.back = 1;
    context()->CopySubresourceRegion(
        destination_info.texture, subresource(destination_info, destination_mip, destination_layer),
        destination_x, destination_y, 0, source_info.texture,
        subresource(source_info, source_mip, source_layer), &box);
    return 1;
}

uint32_t d3d11_buffer_to_image(sg_buffer source, uint32_t source_offset, uint32_t row_pitch,
                               sg_image destination, uint32_t mip_level, uint32_t layer, uint32_t x,
                               uint32_t y, uint32_t width, uint32_t height) {
    ImageInfo destination_info;
    ID3D11Buffer *source_buffer = nullptr;
    uint32_t source_size = 0;
    if (!image_info(destination, mip_level, layer, x, y, width, height, destination_info) ||
        !buffer_info(source, source_buffer, source_size) || !context() ||
        width > UINT32_MAX / destination_info.bytes || row_pitch < width * destination_info.bytes ||
        row_pitch % destination_info.bytes != 0)
        return 0;
    const uint64_t transfer_size = static_cast<uint64_t>(row_pitch) * height;
    if (!transfer_size || transfer_size > UINT32_MAX || source_offset > source_size ||
        transfer_size > source_size - source_offset)
        return 0;
    ID3D11Buffer *staging = nullptr;
    if (!create_staging_buffer(static_cast<uint32_t>(transfer_size), D3D11_CPU_ACCESS_READ,
                               &staging))
        return 0;
    const bool copied = copy_buffer_region(source_buffer, source_offset, staging, 0,
                                           static_cast<uint32_t>(transfer_size));
    if (!copied) {
        staging->Release();
        return 0;
    }
    context()->Flush();
    D3D11_MAPPED_SUBRESOURCE mapped{};
    const HRESULT map_result = context()->Map(staging, 0, D3D11_MAP_READ, 0, &mapped);
    if (FAILED(map_result)) {
        staging->Release();
        return 0;
    }
    D3D11_BOX box{};
    box.left = x;
    box.right = x + width;
    box.top = y;
    box.bottom = y + height;
    box.front = 0;
    box.back = 1;
    context()->UpdateSubresource(destination_info.texture,
                                 subresource(destination_info, mip_level, layer), &box,
                                 mapped.pData, row_pitch, 0);
    context()->Unmap(staging, 0);
    staging->Release();
    return 1;
}

uint32_t d3d11_image_to_buffer(sg_image source, uint32_t mip_level, uint32_t layer, uint32_t x,
                               uint32_t y, uint32_t width, uint32_t height, sg_buffer destination,
                               uint32_t destination_offset, uint32_t row_pitch) {
    ImageInfo source_info;
    ID3D11Buffer *destination_buffer = nullptr;
    uint32_t destination_size = 0;
    if (!image_info(source, mip_level, layer, x, y, width, height, source_info) ||
        !buffer_info(destination, destination_buffer, destination_size) || !context() ||
        width > UINT32_MAX / source_info.bytes || row_pitch < width * source_info.bytes ||
        row_pitch % source_info.bytes != 0)
        return 0;
    const uint64_t transfer_size = static_cast<uint64_t>(row_pitch) * height;
    if (!transfer_size || transfer_size > UINT32_MAX || destination_offset > destination_size ||
        transfer_size > destination_size - destination_offset)
        return 0;
    const bool depth = source_info.format == DXGI_FORMAT_R32_TYPELESS;
    const uint32_t staging_width = depth ? source_info.width : width;
    const uint32_t staging_height = depth ? source_info.height : height;
    ID3D11Texture2D *staging_texture = nullptr;
    if (!create_staging_texture(source_info, staging_width, staging_height, &staging_texture))
        return 0;
    if (depth) {
        /* D3D11 requires whole-subresource copies for depth-stencil resources. */
        context()->CopySubresourceRegion(staging_texture, 0, 0, 0, 0, source_info.texture,
                                         subresource(source_info, mip_level, layer), nullptr);
    } else {
        D3D11_BOX source_box{};
        source_box.left = x;
        source_box.right = x + width;
        source_box.top = y;
        source_box.bottom = y + height;
        source_box.front = 0;
        source_box.back = 1;
        context()->CopySubresourceRegion(staging_texture, 0, 0, 0, 0, source_info.texture,
                                         subresource(source_info, mip_level, layer), &source_box);
    }
    context()->Flush();
    D3D11_MAPPED_SUBRESOURCE mapped_texture{};
    if (FAILED(context()->Map(staging_texture, 0, D3D11_MAP_READ, 0, &mapped_texture))) {
        staging_texture->Release();
        return 0;
    }
    ID3D11Buffer *upload = nullptr;
    if (!create_staging_buffer(static_cast<uint32_t>(transfer_size), D3D11_CPU_ACCESS_WRITE,
                               &upload)) {
        context()->Unmap(staging_texture, 0);
        staging_texture->Release();
        return 0;
    }
    D3D11_MAPPED_SUBRESOURCE mapped_buffer{};
    if (FAILED(context()->Map(upload, 0, D3D11_MAP_WRITE, 0, &mapped_buffer))) {
        upload->Release();
        context()->Unmap(staging_texture, 0);
        staging_texture->Release();
        return 0;
    }
    const uint32_t tight_pitch = width * source_info.bytes;
    for (uint32_t row = 0; row < height; ++row) {
        auto *destination_row =
            static_cast<uint8_t *>(mapped_buffer.pData) + static_cast<size_t>(row) * row_pitch;
        const auto *source_row = static_cast<const uint8_t *>(mapped_texture.pData) +
                                 static_cast<size_t>(row + (depth ? y : 0)) *
                                     mapped_texture.RowPitch +
                                 static_cast<size_t>(depth ? x : 0) * source_info.bytes;
        std::memcpy(destination_row, source_row, tight_pitch);
        if (row_pitch > tight_pitch)
            std::memset(destination_row + tight_pitch, 0, row_pitch - tight_pitch);
    }
    context()->Unmap(upload, 0);
    context()->Unmap(staging_texture, 0);
    const bool copied = copy_buffer_region(upload, 0, destination_buffer, destination_offset,
                                           static_cast<uint32_t>(transfer_size));
    context()->Flush();
    upload->Release();
    staging_texture->Release();
    return copied;
}

uint32_t d3d11_readback_begin(sg_image source, uint32_t mip_level, uint32_t layer, uint32_t x,
                              uint32_t y, uint32_t width, uint32_t height) {
    ImageInfo source_info;
    if (!image_info(source, mip_level, layer, x, y, width, height, source_info) || !context() ||
        width > UINT32_MAX / source_info.bytes || height > UINT32_MAX / (width * source_info.bytes))
        return 0;
    uint32_t index = kReadbackCapacity;
    for (uint32_t i = 0; i < kReadbackCapacity; ++i) {
        if (!readbacks[i].active) {
            index = i;
            break;
        }
    }
    if (index == kReadbackCapacity)
        return 0;
    const bool depth = source_info.format == DXGI_FORMAT_R32_TYPELESS;
    const uint32_t staging_width = depth ? source_info.width : width;
    const uint32_t staging_height = depth ? source_info.height : height;
    ID3D11Texture2D *staging_texture = nullptr;
    if (!create_staging_texture(source_info, staging_width, staging_height, &staging_texture))
        return 0;
    if (depth) {
        /* D3D11 requires whole-subresource copies for depth-stencil resources. */
        context()->CopySubresourceRegion(staging_texture, 0, 0, 0, 0, source_info.texture,
                                         subresource(source_info, mip_level, layer), nullptr);
    } else {
        D3D11_BOX source_box{};
        source_box.left = x;
        source_box.right = x + width;
        source_box.top = y;
        source_box.bottom = y + height;
        source_box.front = 0;
        source_box.back = 1;
        context()->CopySubresourceRegion(staging_texture, 0, 0, 0, 0, source_info.texture,
                                         subresource(source_info, mip_level, layer), &source_box);
    }
    D3D11_QUERY_DESC query_desc{};
    query_desc.Query = D3D11_QUERY_EVENT;
    ID3D11Query *query = nullptr;
    if (!device() || FAILED(device()->CreateQuery(&query_desc, &query))) {
        staging_texture->Release();
        return 0;
    }
    context()->End(query);
    context()->Flush();
    ReadbackSlot &slot = readbacks[index];
    if (!slot.generation)
        slot.generation = 1;
    slot.texture = staging_texture;
    slot.query = query;
    slot.x = depth ? x : 0;
    slot.y = depth ? y : 0;
    slot.size = width * source_info.bytes * height;
    slot.row_pitch = width * source_info.bytes;
    slot.width = width;
    slot.height = height;
    slot.active = true;
    return readback_token(index, slot.generation);
}

uint32_t d3d11_readback_begin_buffer(sg_buffer source, uint32_t offset, uint32_t size) {
    ID3D11Buffer *source_buffer = nullptr;
    uint32_t source_size = 0;
    if (!buffer_info(source, source_buffer, source_size) || !context() || !size ||
        offset > source_size || size > source_size - offset)
        return 0;
    uint32_t index = kReadbackCapacity;
    for (uint32_t i = 0; i < kReadbackCapacity; ++i) {
        if (!readbacks[i].active) {
            index = i;
            break;
        }
    }
    if (index == kReadbackCapacity)
        return 0;
    ID3D11Buffer *staging_buffer = nullptr;
    if (!create_staging_buffer(size, D3D11_CPU_ACCESS_READ, &staging_buffer))
        return 0;
    D3D11_BOX source_box{};
    source_box.left = offset;
    source_box.right = offset + size;
    source_box.top = 0;
    source_box.bottom = 1;
    source_box.front = 0;
    source_box.back = 1;
    context()->CopySubresourceRegion(staging_buffer, 0, 0, 0, 0, source_buffer, 0, &source_box);
    D3D11_QUERY_DESC query_desc{};
    query_desc.Query = D3D11_QUERY_EVENT;
    ID3D11Query *query = nullptr;
    if (!device() || FAILED(device()->CreateQuery(&query_desc, &query))) {
        staging_buffer->Release();
        return 0;
    }
    context()->End(query);
    context()->Flush();
    ReadbackSlot &slot = readbacks[index];
    if (!slot.generation)
        slot.generation = 1;
    slot.buffer = staging_buffer;
    slot.query = query;
    slot.size = size;
    slot.row_pitch = size;
    slot.width = size;
    slot.height = 1;
    slot.active = true;
    return readback_token(index, slot.generation);
}

uint32_t d3d11_timestamp_begin() {
    if (!device() || !context())
        return 0;
    uint32_t index = kReadbackCapacity;
    for (uint32_t i = 0; i < kReadbackCapacity; ++i) {
        if (!timestamps[i].active) {
            index = i;
            break;
        }
    }
    if (index == kReadbackCapacity)
        return 0;

    D3D11_QUERY_DESC timestamp_desc{};
    timestamp_desc.Query = D3D11_QUERY_TIMESTAMP;
    D3D11_QUERY_DESC disjoint_desc{};
    disjoint_desc.Query = D3D11_QUERY_TIMESTAMP_DISJOINT;
    ID3D11Query *begin = nullptr;
    ID3D11Query *end = nullptr;
    ID3D11Query *disjoint = nullptr;
    if (FAILED(device()->CreateQuery(&timestamp_desc, &begin)) ||
        FAILED(device()->CreateQuery(&timestamp_desc, &end)) ||
        FAILED(device()->CreateQuery(&disjoint_desc, &disjoint))) {
        if (begin)
            begin->Release();
        if (end)
            end->Release();
        if (disjoint)
            disjoint->Release();
        return 0;
    }
    context()->Begin(disjoint);
    context()->End(begin);
    TimestampSlot &slot = timestamps[index];
    if (!slot.generation)
        slot.generation = 1;
    slot.begin = begin;
    slot.end = end;
    slot.disjoint = disjoint;
    slot.active = true;
    slot.ended = false;
    return timestamp_token(index, slot.generation);
}

int d3d11_timestamp_end(uint32_t token) {
    TimestampSlot *slot = timestamp_slot(token);
    if (!slot || slot->ended || !context())
        return 0;
    context()->End(slot->end);
    context()->End(slot->disjoint);
    context()->Flush();
    slot->ended = true;
    return 1;
}

uint32_t d3d11_timestamp_status(uint32_t token) {
    TimestampSlot *slot = timestamp_slot(token);
    if (!slot || !slot->ended || !context())
        return kReadbackPending;
    D3D11_QUERY_DATA_TIMESTAMP_DISJOINT disjoint_data{};
    const HRESULT disjoint_result = context()->GetData(
        slot->disjoint, &disjoint_data, sizeof(disjoint_data), D3D11_ASYNC_GETDATA_DONOTFLUSH);
    if (disjoint_result == S_FALSE)
        return kReadbackPending;
    if (FAILED(disjoint_result) || disjoint_data.Disjoint || !disjoint_data.Frequency)
        return kReadbackFailed;
    UINT64 begin = 0;
    UINT64 end = 0;
    const HRESULT begin_result =
        context()->GetData(slot->begin, &begin, sizeof(begin), D3D11_ASYNC_GETDATA_DONOTFLUSH);
    const HRESULT end_result =
        context()->GetData(slot->end, &end, sizeof(end), D3D11_ASYNC_GETDATA_DONOTFLUSH);
    if (begin_result == S_FALSE || end_result == S_FALSE)
        return kReadbackPending;
    if (FAILED(begin_result) || FAILED(end_result) || end < begin)
        return kReadbackFailed;
    const long double elapsed = static_cast<long double>(end - begin) * 1000000000.0L /
                                static_cast<long double>(disjoint_data.Frequency);
    slot->nanoseconds = elapsed >= static_cast<long double>(UINT64_MAX)
                            ? UINT64_MAX
                            : static_cast<uint64_t>(elapsed);
    return kReadbackReady;
}

uint64_t d3d11_timestamp_elapsed_ns(uint32_t token) {
    TimestampSlot *slot = timestamp_slot(token);
    return slot ? slot->nanoseconds : 0;
}

void d3d11_timestamp_destroy(uint32_t token) {
    if (TimestampSlot *slot = timestamp_slot(token))
        release_timestamp(*slot);
}

int d3d11_timestamp_supported() {
    return device() && context() ? 1 : 0;
}

uint32_t d3d11_readback_status(uint32_t token) {
    ReadbackSlot *slot = readback_slot(token);
    if (!slot || !slot->query || !context())
        return kReadbackFailed;
    const HRESULT result =
        context()->GetData(slot->query, nullptr, 0, D3D11_ASYNC_GETDATA_DONOTFLUSH);
    if (result == S_OK)
        return kReadbackReady;
    if (result == S_FALSE)
        return kReadbackPending;
    return kReadbackFailed;
}

uint32_t d3d11_readback_size(uint32_t token) {
    ReadbackSlot *slot = readback_slot(token);
    return slot ? slot->size : 0;
}

uint32_t d3d11_readback_row_pitch(uint32_t token) {
    ReadbackSlot *slot = readback_slot(token);
    return slot ? slot->row_pitch : 0;
}

int d3d11_readback_read(uint32_t token, void *destination, uint32_t size) {
    ReadbackSlot *slot = readback_slot(token);
    if (!slot || !destination || size < slot->size ||
        d3d11_readback_status(token) != kReadbackReady)
        return 0;
    if (slot->buffer) {
        D3D11_MAPPED_SUBRESOURCE mapped{};
        if (FAILED(context()->Map(slot->buffer, 0, D3D11_MAP_READ, 0, &mapped)))
            return 0;
        std::memcpy(destination, mapped.pData, slot->size);
        context()->Unmap(slot->buffer, 0);
        return 1;
    }
    D3D11_MAPPED_SUBRESOURCE mapped{};
    if (FAILED(context()->Map(slot->texture, 0, D3D11_MAP_READ, 0, &mapped)))
        return 0;
    const uint32_t tight_pitch = slot->row_pitch;
    for (uint32_t row = 0; row < slot->height; ++row) {
        std::memcpy(static_cast<uint8_t *>(destination) + static_cast<size_t>(row) * tight_pitch,
                    static_cast<const uint8_t *>(mapped.pData) +
                        static_cast<size_t>(row + slot->y) * mapped.RowPitch +
                        static_cast<size_t>(slot->x) * tight_pitch / slot->width,
                    tight_pitch);
    }
    context()->Unmap(slot->texture, 0);
    return 1;
}

void d3d11_readback_destroy(uint32_t token) {
    if (ReadbackSlot *slot = readback_slot(token))
        release_readback(*slot);
}

int d3d11_begin_pass() {
    return 1;
}

int d3d11_end_pass() {
    if (context())
        context()->Flush();
    return 1;
}

const nk_sokol_transfer_api transfer_api = {
    d3d11_buffer_copy,
    d3d11_image_copy,
    d3d11_buffer_to_image,
    d3d11_image_to_buffer,
    d3d11_readback_begin,
    d3d11_readback_begin_buffer,
    d3d11_readback_status,
    d3d11_readback_size,
    d3d11_readback_row_pitch,
    d3d11_readback_read,
    d3d11_readback_destroy,
    d3d11_begin_pass,
    d3d11_end_pass,
    d3d11_timestamp_begin,
    d3d11_timestamp_end,
    d3d11_timestamp_status,
    d3d11_timestamp_elapsed_ns,
    d3d11_timestamp_destroy,
    d3d11_timestamp_supported,
};

} // namespace

extern "C" const nk_sokol_transfer_api *nk_sokol_d3d11_transfer_get_api(void) {
    return &transfer_api;
}

extern "C" int nk_sokol_d3d11_query_max_samples(void) {
    ID3D11Device *native_device = device();
    if (!native_device)
        return 1;
    int max_samples = 1;
    for (UINT samples = 2; samples <= 32; samples *= 2) {
        UINT quality_levels = 0;
        const HRESULT result = native_device->CheckMultisampleQualityLevels(
            DXGI_FORMAT_R8G8B8A8_UNORM, samples, &quality_levels);
        if (FAILED(result) || !quality_levels)
            break;
        max_samples = static_cast<int>(samples);
    }
    return max_samples;
}

extern "C" void nk_sokol_d3d11_transfer_shutdown(void) {
    if (context())
        context()->Flush();
    for (auto &slot : readbacks)
        if (slot.active)
            release_readback(slot);
    for (auto &slot : timestamps)
        if (slot.active)
            release_timestamp(slot);
}
