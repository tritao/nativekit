#pragma once

#include "nativekit_system.h"

#include <string>

namespace nk::core {

/** Stores the application identity for the active runtime generation. */
void system_initialize(const nk_init_options *options);
/** Releases application identity and all outstanding keep-awake leases. */
void system_shutdown() noexcept;

/** Returns copies so backend path builders do not depend on internal locks. */
std::string system_application_id();
std::string system_application_name();
bool system_keep_awake_held();

/** Platform hooks used by the shared lease and orientation implementation. */
namespace system_backend {
bool keep_awake_supported() noexcept;
nk_result keep_awake_apply(bool enabled) noexcept;
nk_result get_orientation(nk_system_orientation &out_orientation) noexcept;
nk_result request_device_orientation(nk_request_id request) noexcept;
nk_result get_string(nk_system_string_kind kind, std::string &out_value);
} // namespace system_backend

} // namespace nk::core
