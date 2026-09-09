#include "core/event_queue.hpp"
#include "core/handle_registry.hpp"

#include <cassert>
#include <cstddef>
#include <cstring>
#include <memory>

namespace {
struct Dummy final : nk::core::Resource {};
}

int main() {
    nk::core::HandleRegistry handles;
    const auto first = handles.insert(nk::core::ResourceType::window, std::make_shared<Dummy>());
    assert(first != NK_INVALID_HANDLE);
    assert(handles.get(first, nk::core::ResourceType::window));
    assert(!handles.get(first, nk::core::ResourceType::webview));
    assert(handles.erase(first, nk::core::ResourceType::window));
    assert(!handles.get(first, nk::core::ResourceType::window));
    const auto second = handles.insert(nk::core::ResourceType::window, std::make_shared<Dummy>());
    assert(second != first);

    nk::core::EventQueue queue(1);
    nk::core::QueuedEvent queued;
    queued.kind = NK_EVENT_WEBVIEW_MESSAGE;
    const char payload[] = "hello";
    const auto* begin = reinterpret_cast<const std::byte*>(payload);
    queued.data.assign(begin, begin + sizeof(payload) - 1);
    assert(queue.push(std::move(queued)) == NK_OK);
    assert(queue.push({}) == NK_ERROR_QUEUE_FULL);

    nk_event event{};
    event.struct_size = sizeof(event);
    assert(queue.poll(event) == NK_OK);
    assert(event.kind == NK_EVENT_WEBVIEW_MESSAGE);
    assert(event.data_size == 5);
    assert(std::memcmp(event.data, "hello", 5) == 0);
    nk_event_release(&event);
    return 0;
}
