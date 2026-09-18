#pragma once

namespace nk::core {

/**
 * Destroys every live plugin instance and forgets its services.
 *
 * nk_shutdown() calls this before resources, handles, and the runtime
 * generation disappear so plugins always observe a complete teardown on the
 * thread that owns event delivery.
 */
void plugins_shutdown() noexcept;

} // namespace nk::core
