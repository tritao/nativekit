#include "nativekit_joystick.h"

#include "core/error.hpp"
#include "core/gamepad_events.hpp"
#include "core/runtime.hpp"
#include "linux/joystick.hpp"

#include <linux/input.h>
#include <sys/inotify.h>
#include <sys/ioctl.h>

#include <algorithm>
#include <array>
#include <cerrno>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <dirent.h>
#include <fcntl.h>
#include <memory>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>
#include <unistd.h>

namespace {

template <std::size_t N> bool bit_set(const std::array<unsigned long, N> &bits, int bit) {
    constexpr int word_bits = static_cast<int>(sizeof(unsigned long) * 8);
    return (bits[static_cast<std::size_t>(bit / word_bits)] &
            (1UL << static_cast<unsigned int>(bit % word_bits))) != 0;
}

constexpr std::size_t bit_words(int count) {
    return (static_cast<std::size_t>(count) + sizeof(unsigned long) * 8 - 1) /
           (sizeof(unsigned long) * 8);
}

struct Axis {
    input_absinfo info{};
    float value = 0.f;
};

struct Joystick final : nk::core::Resource {
    ~Joystick() override {
        if (fd >= 0)
            close(fd);
    }
    int fd = -1;
    nk_handle handle = NK_INVALID_HANDLE;
    std::string path;
    std::string name;
    std::string guid;
    std::array<int, ABS_CNT> axis_map{};
    std::array<int, KEY_CNT> button_map{};
    std::vector<Axis> axes;
    std::vector<std::uint8_t> buttons;
    std::array<std::array<int, 2>, 4> hat_values{};
    std::vector<std::uint8_t> hats;
    bool dropped = false;
};

int notify_fd = -1;
int notify_watch = -1;
bool initialized = false;
bool pumping = false;
std::unordered_map<std::string, std::shared_ptr<Joystick>> devices;
std::string transport_diagnostic;

void record_diagnostic(const std::string &message) {
    if (transport_diagnostic.empty())
        transport_diagnostic = message;
}

void record_system_diagnostic(const std::string &operation, int error) {
    record_diagnostic(operation + ": " + std::strerror(error));
}

bool event_name(const char *name) {
    if (!name || std::strncmp(name, "event", 5) != 0 || name[5] == '\0')
        return false;
    for (const char *p = name + 5; *p; ++p)
        if (*p < '0' || *p > '9')
            return false;
    return true;
}

float normalize_axis(int value, const input_absinfo &info) {
    if (info.maximum <= info.minimum)
        return 0.f;
    const double normalized = (static_cast<double>(value - info.minimum) * 2.0 /
                               static_cast<double>(info.maximum - info.minimum)) -
                              1.0;
    return static_cast<float>(std::clamp(normalized, -1.0, 1.0));
}

std::uint8_t compose_hat(const std::array<int, 2> &values) {
    std::uint8_t result = NK_JOYSTICK_HAT_CENTERED;
    if (values[0] < 0)
        result |= NK_JOYSTICK_HAT_LEFT;
    else if (values[0] > 0)
        result |= NK_JOYSTICK_HAT_RIGHT;
    if (values[1] < 0)
        result |= NK_JOYSTICK_HAT_UP;
    else if (values[1] > 0)
        result |= NK_JOYSTICK_HAT_DOWN;
    return result;
}

std::string make_guid(const input_id &id, const std::string &name) {
    std::array<unsigned char, 16> bytes{};
    bytes[0] = static_cast<unsigned char>(id.bustype & 0xff);
    bytes[1] = static_cast<unsigned char>((id.bustype >> 8) & 0xff);
    if (id.vendor || id.product || id.version) {
        bytes[4] = static_cast<unsigned char>(id.vendor & 0xff);
        bytes[5] = static_cast<unsigned char>((id.vendor >> 8) & 0xff);
        bytes[8] = static_cast<unsigned char>(id.product & 0xff);
        bytes[9] = static_cast<unsigned char>((id.product >> 8) & 0xff);
        bytes[12] = static_cast<unsigned char>(id.version & 0xff);
        bytes[13] = static_cast<unsigned char>((id.version >> 8) & 0xff);
    } else {
        const auto count = std::min<std::size_t>(name.size(), 11);
        std::memcpy(bytes.data() + 4, name.data(), count);
    }
    char text[33]{};
    for (std::size_t i = 0; i < bytes.size(); ++i)
        std::snprintf(text + i * 2, 3, "%02x", bytes[i]);
    return text;
}

void emit(nk_event_kind kind, nk_handle handle) {
    nk::core::QueuedEvent event;
    event.kind = kind;
    event.source = handle;
    nk::core::push_event(std::move(event));
}

template <typename Payload>
void emit_input(nk_event_kind kind, nk_handle handle, const Payload &payload) {
    nk::core::QueuedEvent event;
    event.kind = kind;
    event.source = handle;
    const auto *begin = reinterpret_cast<const std::byte *>(&payload);
    event.data.assign(begin, begin + sizeof(payload));
    nk::core::push_event(std::move(event));
}

void remove_device(const std::string &path) {
    const auto found = devices.find(path);
    if (found == devices.end())
        return;
    const auto handle = found->second->handle;
    emit(NK_EVENT_JOYSTICK_DISCONNECTED, handle);
    nk::core::gamepad_events::disconnect(handle);
    nk::core::handles().erase(handle, nk::core::ResourceType::joystick);
    devices.erase(found);
}

void update_hat(Joystick &device, int code, int value, bool emit_change = false) {
    const int offset = code - ABS_HAT0X;
    const auto hat = static_cast<std::size_t>(offset / 2);
    device.hat_values[hat][static_cast<std::size_t>(offset % 2)] = value;
    const auto previous = device.hats[hat];
    device.hats[hat] = compose_hat(device.hat_values[hat]);
    if (emit_change && previous != device.hats[hat]) {
        const nk_joystick_hat_event payload{static_cast<std::uint32_t>(hat), device.hats[hat]};
        emit_input(NK_EVENT_JOYSTICK_HAT, device.handle, payload);
    }
}

void update_axis(Joystick &device, std::size_t axis, int value, bool emit_change = true) {
    auto &state = device.axes[axis];
    const float normalized = normalize_axis(value, state.info);
    if (state.value == normalized)
        return;
    state.value = normalized;
    if (emit_change) {
        const nk_joystick_axis_event payload{static_cast<std::uint32_t>(axis), normalized};
        emit_input(NK_EVENT_JOYSTICK_AXIS, device.handle, payload);
    }
}

void update_button(Joystick &device, std::size_t button, bool pressed, bool emit_change = true) {
    const std::uint8_t value = pressed ? 1 : 0;
    if (device.buttons[button] == value)
        return;
    device.buttons[button] = value;
    if (emit_change) {
        const nk_joystick_button_event payload{static_cast<std::uint32_t>(button), value};
        emit_input(NK_EVENT_JOYSTICK_BUTTON, device.handle, payload);
    }
}

void add_device(const std::string &path) {
    if (devices.find(path) != devices.end())
        return;
    const int fd = open(path.c_str(), O_RDONLY | O_NONBLOCK | O_CLOEXEC);
    if (fd < 0) {
        if (errno == EACCES || errno == EPERM)
            record_system_diagnostic("cannot read joystick " + path, errno);
        return;
    }

    std::array<unsigned long, bit_words(EV_CNT)> ev_bits{};
    std::array<unsigned long, bit_words(KEY_CNT)> key_bits{};
    std::array<unsigned long, bit_words(ABS_CNT)> abs_bits{};
    if (ioctl(fd, EVIOCGBIT(0, sizeof(ev_bits)), ev_bits.data()) < 0 || !bit_set(ev_bits, EV_KEY) ||
        !bit_set(ev_bits, EV_ABS) ||
        ioctl(fd, EVIOCGBIT(EV_KEY, sizeof(key_bits)), key_bits.data()) < 0 ||
        ioctl(fd, EVIOCGBIT(EV_ABS, sizeof(abs_bits)), abs_bits.data()) < 0) {
        close(fd);
        return;
    }
    bool joystick_button = false;
    for (int code = BTN_JOYSTICK; code < BTN_DIGI; ++code)
        joystick_button = joystick_button || bit_set(key_bits, code);
    if (!joystick_button) {
        close(fd);
        return;
    }

    auto device = std::make_shared<Joystick>();
    device->fd = fd;
    device->path = path;
    device->axis_map.fill(-1);
    device->button_map.fill(-1);
    char name[256]{};
    if (ioctl(fd, EVIOCGNAME(sizeof(name)), name) < 0 || name[0] == '\0')
        std::strcpy(name, "Unknown joystick");
    device->name = name;
    input_id id{};
    ioctl(fd, EVIOCGID, &id);
    device->guid = make_guid(id, device->name);

    for (int code = 0; code < ABS_CNT; ++code) {
        if (!bit_set(abs_bits, code))
            continue;
        input_absinfo info{};
        if (ioctl(fd, EVIOCGABS(code), &info) < 0)
            continue;
        if (code >= ABS_HAT0X && code <= ABS_HAT3Y) {
            if (device->hats.size() <= static_cast<std::size_t>((code - ABS_HAT0X) / 2))
                device->hats.resize(static_cast<std::size_t>((code - ABS_HAT0X) / 2 + 1));
            update_hat(*device, code, info.value);
        } else {
            device->axis_map[static_cast<std::size_t>(code)] =
                static_cast<int>(device->axes.size());
            device->axes.push_back({info, normalize_axis(info.value, info)});
        }
    }
    for (int code = BTN_MISC; code < KEY_CNT; ++code) {
        if (!bit_set(key_bits, code))
            continue;
        device->button_map[static_cast<std::size_t>(code)] =
            static_cast<int>(device->buttons.size());
        device->buttons.push_back(0);
    }
    std::array<unsigned long, bit_words(KEY_CNT)> key_state{};
    if (ioctl(fd, EVIOCGKEY(sizeof(key_state)), key_state.data()) >= 0) {
        for (int code = BTN_MISC; code < KEY_CNT; ++code) {
            const int button = device->button_map[static_cast<std::size_t>(code)];
            if (button >= 0)
                device->buttons[static_cast<std::size_t>(button)] =
                    bit_set(key_state, code) ? 1 : 0;
        }
    }
    device->handle = nk::core::handles().insert(nk::core::ResourceType::joystick, device);
    devices.emplace(path, device);
    emit(NK_EVENT_JOYSTICK_CONNECTED, device->handle);
    nk::core::gamepad_events::update(device->handle, false);
}

void scan_devices() {
    DIR *directory = opendir("/dev/input");
    if (!directory) {
        record_system_diagnostic("cannot scan /dev/input", errno);
        return;
    }
    std::unordered_set<std::string> present;
    while (const auto *entry = readdir(directory)) {
        if (!event_name(entry->d_name))
            continue;
        std::string path = std::string("/dev/input/") + entry->d_name;
        present.insert(path);
        add_device(path);
    }
    closedir(directory);
    std::vector<std::string> removed;
    for (const auto &[path, device] : devices) {
        (void)device;
        if (present.find(path) == present.end())
            removed.push_back(path);
    }
    for (const auto &path : removed)
        remove_device(path);
}

void initialize() {
    if (initialized)
        return;
    initialized = true;
    transport_diagnostic.clear();
    scan_devices();
    notify_fd = inotify_init1(IN_NONBLOCK | IN_CLOEXEC);
    if (notify_fd >= 0) {
        notify_watch =
            inotify_add_watch(notify_fd, "/dev/input",
                              IN_CREATE | IN_ATTRIB | IN_DELETE | IN_MOVED_TO | IN_MOVED_FROM);
        if (notify_watch < 0)
            record_system_diagnostic("cannot monitor /dev/input", errno);
    } else {
        record_system_diagnostic("cannot initialize joystick hotplug monitoring", errno);
    }
}

void poll_hotplug() {
    if (notify_fd < 0)
        return;
    if (notify_watch < 0) {
        notify_watch =
            inotify_add_watch(notify_fd, "/dev/input",
                              IN_CREATE | IN_ATTRIB | IN_DELETE | IN_MOVED_TO | IN_MOVED_FROM);
        if (notify_watch < 0)
            return;
        scan_devices();
    }
    alignas(inotify_event) std::array<char, 4096> buffer{};
    for (;;) {
        const auto count = read(notify_fd, buffer.data(), buffer.size());
        if (count <= 0)
            break;
        for (std::size_t offset = 0; offset < static_cast<std::size_t>(count);) {
            const auto *event = reinterpret_cast<const inotify_event *>(buffer.data() + offset);
            if (event->mask & IN_Q_OVERFLOW) {
                record_diagnostic("joystick hotplug queue overflowed; rescanning devices");
                scan_devices();
            } else if (event->mask & IN_IGNORED) {
                record_diagnostic("joystick hotplug monitoring stopped");
                notify_watch = -1;
            } else if (event->len && event_name(event->name)) {
                const std::string path = std::string("/dev/input/") + event->name;
                if (event->mask & (IN_DELETE | IN_MOVED_FROM))
                    remove_device(path);
                else
                    add_device(path);
            }
            offset += sizeof(inotify_event) + event->len;
        }
    }
}

bool resynchronize(Joystick &device) {
    const auto previous_hats = device.hats;
    for (int code = 0; code < ABS_CNT; ++code) {
        const bool mapped_axis = device.axis_map[static_cast<std::size_t>(code)] >= 0;
        const bool mapped_hat =
            code >= ABS_HAT0X && code <= ABS_HAT3Y &&
            static_cast<std::size_t>((code - ABS_HAT0X) / 2) < device.hats.size();
        if (!mapped_axis && !mapped_hat)
            continue;
        input_absinfo info{};
        if (ioctl(device.fd, EVIOCGABS(code), &info) < 0)
            return false;
        if (mapped_hat) {
            update_hat(device, code, info.value);
        } else {
            const auto axis = static_cast<std::size_t>(device.axis_map[code]);
            device.axes[axis].info = info;
            update_axis(device, axis, info.value);
        }
    }
    for (std::size_t hat = 0; hat < device.hats.size(); ++hat) {
        if (previous_hats[hat] == device.hats[hat])
            continue;
        const nk_joystick_hat_event payload{static_cast<std::uint32_t>(hat), device.hats[hat]};
        emit_input(NK_EVENT_JOYSTICK_HAT, device.handle, payload);
    }

    std::array<unsigned long, bit_words(KEY_CNT)> key_state{};
    if (ioctl(device.fd, EVIOCGKEY(sizeof(key_state)), key_state.data()) < 0)
        return false;
    for (int code = BTN_MISC; code < KEY_CNT; ++code) {
        const int button = device.button_map[static_cast<std::size_t>(code)];
        if (button >= 0)
            update_button(device, static_cast<std::size_t>(button), bit_set(key_state, code));
    }
    return true;
}

void poll_device(const std::shared_ptr<Joystick> &device) {
    input_event events[32];
    for (;;) {
        const auto bytes = read(device->fd, events, sizeof(events));
        if (bytes < 0) {
            if (errno == ENODEV)
                remove_device(device->path);
            return;
        }
        if (bytes == 0)
            return;
        const auto count = static_cast<std::size_t>(bytes) / sizeof(input_event);
        for (std::size_t i = 0; i < count; ++i) {
            const auto &event = events[i];
            if (event.type == EV_SYN && event.code == SYN_DROPPED) {
                device->dropped = true;
                continue;
            }
            if (device->dropped) {
                if (event.type == EV_SYN && event.code == SYN_REPORT) {
                    device->dropped = false;
                    if (!resynchronize(*device)) {
                        const int error = errno;
                        if (error == ENODEV)
                            remove_device(device->path);
                        else
                            record_system_diagnostic("cannot resynchronize " + device->path, error);
                        return;
                    }
                    nk::core::gamepad_events::update(device->handle, true);
                }
                continue;
            }
            if (event.type == EV_SYN && event.code == SYN_REPORT) {
                nk::core::gamepad_events::update(device->handle, true);
                continue;
            }
            if (event.type == EV_ABS && event.code < ABS_CNT) {
                if (event.code >= ABS_HAT0X && event.code <= ABS_HAT3Y &&
                    static_cast<std::size_t>((event.code - ABS_HAT0X) / 2) < device->hats.size()) {
                    update_hat(*device, event.code, event.value, true);
                } else {
                    const int axis = device->axis_map[event.code];
                    if (axis >= 0)
                        update_axis(*device, static_cast<std::size_t>(axis), event.value);
                }
            } else if (event.type == EV_KEY && event.code < KEY_CNT) {
                const int button = device->button_map[event.code];
                if (button >= 0)
                    update_button(*device, static_cast<std::size_t>(button), event.value != 0);
            }
        }
        if (static_cast<std::size_t>(bytes) < sizeof(events))
            return;
    }
}

std::shared_ptr<Joystick> lookup(nk_handle handle) {
    return std::static_pointer_cast<Joystick>(
        nk::core::handles().get(handle, nk::core::ResourceType::joystick));
}

nk_result validate_call() {
    const auto result = nk::core::require_ui_thread();
    if (result == NK_OK)
        nk::linux_joystick::pump();
    return result;
}

template <typename T>
nk_result copy_array(const std::vector<T> &source, T *output, std::uint32_t *inout_count) {
    if (!inout_count) {
        nk::core::set_error("inout_count is required");
        return NK_ERROR_INVALID_ARGUMENT;
    }
    const auto required = static_cast<std::uint32_t>(source.size());
    if (!output || *inout_count < required) {
        *inout_count = required;
        return required == 0 && !output ? NK_OK : NK_ERROR_BUFFER_TOO_SMALL;
    }
    std::copy(source.begin(), source.end(), output);
    *inout_count = required;
    return NK_OK;
}

nk_result copy_string(const std::string &source, char *output, std::uint32_t *inout_size) {
    if (!inout_size) {
        nk::core::set_error("inout_size is required");
        return NK_ERROR_INVALID_ARGUMENT;
    }
    const auto required = static_cast<std::uint32_t>(source.size() + 1);
    if (!output || *inout_size < required) {
        *inout_size = required;
        return NK_ERROR_BUFFER_TOO_SMALL;
    }
    std::memcpy(output, source.c_str(), required);
    *inout_size = required;
    return NK_OK;
}

template <typename Function> nk_result boundary(Function &&function) noexcept {
    try {
        nk::core::clear_error();
        return function();
    } catch (const std::bad_alloc &) {
        nk::core::set_error("out of memory while accessing joysticks");
        return NK_ERROR_OUT_OF_MEMORY;
    } catch (...) {
        nk::core::set_error("unexpected error while accessing joysticks");
        return NK_ERROR_UNKNOWN;
    }
}

} // namespace

namespace nk::linux_joystick {
void pump() noexcept {
    if (pumping)
        return;
    pumping = true;
    try {
        initialize();
        poll_hotplug();
        std::vector<std::shared_ptr<Joystick>> snapshot;
        snapshot.reserve(devices.size());
        for (const auto &[path, device] : devices) {
            (void)path;
            snapshot.push_back(device);
        }
        for (const auto &device : snapshot)
            if (devices.find(device->path) != devices.end())
                poll_device(device);
    } catch (...) {
    }
    pumping = false;
}

void shutdown() noexcept {
    try {
        for (const auto &[path, device] : devices) {
            (void)path;
            nk::core::handles().erase(device->handle, nk::core::ResourceType::joystick);
        }
        devices.clear();
        nk::core::gamepad_events::reset();
        if (notify_watch >= 0 && notify_fd >= 0)
            inotify_rm_watch(notify_fd, notify_watch);
        if (notify_fd >= 0)
            close(notify_fd);
        notify_fd = -1;
        notify_watch = -1;
        initialized = false;
        transport_diagnostic.clear();
    } catch (...) {
    }
}
} // namespace nk::linux_joystick

extern "C" {
nk_result NK_CALL nk_joystick_list(nk_handle *joysticks, uint32_t *inout_count) {
    return boundary([&]() -> nk_result {
        if (const auto result = validate_call(); result != NK_OK)
            return result;
        std::vector<nk_handle> handles;
        handles.reserve(devices.size());
        for (const auto &[path, device] : devices) {
            (void)path;
            handles.push_back(device->handle);
        }
        std::sort(handles.begin(), handles.end());
        return copy_array(handles, joysticks, inout_count);
    });
}

nk_result NK_CALL nk_joystick_get_name(nk_handle handle, char *buffer, uint32_t *inout_size) {
    return boundary([&]() -> nk_result {
        if (const auto result = validate_call(); result != NK_OK)
            return result;
        const auto device = lookup(handle);
        if (!device) {
            nk::core::set_error("invalid joystick handle");
            return NK_ERROR_INVALID_HANDLE;
        }
        return copy_string(device->name, buffer, inout_size);
    });
}

nk_result NK_CALL nk_joystick_get_guid(nk_handle handle, char *buffer, uint32_t *inout_size) {
    return boundary([&]() -> nk_result {
        if (const auto result = validate_call(); result != NK_OK)
            return result;
        const auto device = lookup(handle);
        if (!device) {
            nk::core::set_error("invalid joystick handle");
            return NK_ERROR_INVALID_HANDLE;
        }
        return copy_string(device->guid, buffer, inout_size);
    });
}

nk_result NK_CALL nk_joystick_get_axes(nk_handle handle, float *axes, uint32_t *count) {
    return boundary([&]() -> nk_result {
        if (const auto result = validate_call(); result != NK_OK)
            return result;
        const auto device = lookup(handle);
        if (!device) {
            nk::core::set_error("invalid joystick handle");
            return NK_ERROR_INVALID_HANDLE;
        }
        std::vector<float> values;
        values.reserve(device->axes.size());
        for (const auto &axis : device->axes)
            values.push_back(axis.value);
        return copy_array(values, axes, count);
    });
}

nk_result NK_CALL nk_joystick_get_buttons(nk_handle handle, uint8_t *buttons, uint32_t *count) {
    return boundary([&]() -> nk_result {
        if (const auto result = validate_call(); result != NK_OK)
            return result;
        const auto device = lookup(handle);
        if (!device) {
            nk::core::set_error("invalid joystick handle");
            return NK_ERROR_INVALID_HANDLE;
        }
        return copy_array(device->buttons, buttons, count);
    });
}

nk_result NK_CALL nk_joystick_get_hats(nk_handle handle, uint8_t *hats, uint32_t *count) {
    return boundary([&]() -> nk_result {
        if (const auto result = validate_call(); result != NK_OK)
            return result;
        const auto device = lookup(handle);
        if (!device) {
            nk::core::set_error("invalid joystick handle");
            return NK_ERROR_INVALID_HANDLE;
        }
        return copy_array(device->hats, hats, count);
    });
}

nk_result NK_CALL nk_joystick_get_diagnostics(char *buffer, uint32_t *inout_size) {
    return boundary([&]() -> nk_result {
        if (const auto result = validate_call(); result != NK_OK)
            return result;
        return copy_string(transport_diagnostic, buffer, inout_size);
    });
}
}
