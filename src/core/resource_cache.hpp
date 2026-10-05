#pragma once

#include "nativekit_resource.h"

#include <cstddef>
#include <memory>
#include <vector>

namespace nk::core {

using ResourceAssetBytes = std::shared_ptr<const std::vector<std::byte>>;

/** Returns the immutable encoded bytes of a ready cached resource asset. */
nk_result resource_asset_get_bytes(nk_resource_asset asset, ResourceAssetBytes &out_bytes) noexcept;

} // namespace nk::core
