#pragma once

#include "nativekit_file_watch.h"

#include "core/event_queue.hpp"

#include <cstddef>
#include <string_view>
#include <vector>

namespace nk::core {
/* Encodes an nk_file_changed_event header followed by NUL-terminated UTF-8
 * paths. An empty path or old path is omitted and its offset left at zero. */
std::vector<std::byte> file_changed_payload(nk_file_change_type change, nk_file_item_kind kind,
                                            nk_file_event_flags flags, std::string_view path,
                                            std::string_view old_path = {});

/* An overflow from `source`: changes were missed, so its directories need a
 * rescan. The event queue always accepts these and keeps one per source. */
QueuedEvent file_watch_overflow_event(nk_file_watch source);
/** NK_EVENT_FILE_WATCH_READY for `path`; `registration` tells repeated registrations of one path
 * apart. */
QueuedEvent file_watch_ready_event(nk_file_watch source, std::string_view path,
                                   nk_request_id registration);
} // namespace nk::core
