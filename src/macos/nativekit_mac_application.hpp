#pragma once

namespace nk::macos {

/**
 * Finishes launching NSApplication when no one has, for hosts that pump Cocoa themselves instead
 * of entering NSApplication.run. Returns true when NativeKit launched the application and so owns
 * its lifecycle (delegate, default menu); false inside an embedding Cocoa app that launched itself.
 */
bool launch_application() noexcept;

/** Whether NativeKit launches (or launched) the application, before or after launch_application. */
bool owns_application() noexcept;

} // namespace nk::macos
