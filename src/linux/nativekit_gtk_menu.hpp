#pragma once

#include <gtk/gtk.h>

namespace nk::backend {

/** Initializes the GTK application object used for native menu export. */
bool menu_backend_initialize() noexcept;

/** Returns the registered GTK application, or nullptr when unavailable. */
GtkApplication *menu_backend_application() noexcept;

/** Releases the GTK application and exported menu models. */
void menu_backend_shutdown() noexcept;

} // namespace nk::backend
