#include "nativekit.h"
#include "nativekit_gamepad.h"
#include "nativekit_joystick.h"

#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <linux/input.h>
#include <linux/uinput.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/ioctl.h>
#include <time.h>
#include <unistd.h>

static const char *device_name = "NativeKit uinput joystick";

static void pause_briefly(void) {
    const struct timespec delay = {0, 10000000};
    nanosleep(&delay, NULL);
}

static int open_uinput(void) {
    int fd = open("/dev/uinput", O_WRONLY | O_NONBLOCK | O_CLOEXEC);
    if (fd < 0)
        fd = open("/dev/input/uinput", O_WRONLY | O_NONBLOCK | O_CLOEXEC);
    return fd;
}

static int create_device(int fd) {
    if (ioctl(fd, UI_SET_EVBIT, EV_KEY) < 0 || ioctl(fd, UI_SET_KEYBIT, BTN_GAMEPAD) < 0 ||
        ioctl(fd, UI_SET_EVBIT, EV_ABS) < 0 || ioctl(fd, UI_SET_ABSBIT, ABS_X) < 0 ||
        ioctl(fd, UI_SET_ABSBIT, ABS_HAT0X) < 0 || ioctl(fd, UI_SET_ABSBIT, ABS_HAT0Y) < 0)
        return -1;

    struct uinput_user_dev setup;
    memset(&setup, 0, sizeof(setup));
    snprintf(setup.name, sizeof(setup.name), "%s", device_name);
    setup.id.bustype = BUS_USB;
    setup.id.vendor = 0x1209;
    setup.id.product = 0x4e4b;
    setup.id.version = 1;
    setup.absmin[ABS_X] = -32768;
    setup.absmax[ABS_X] = 32767;
    setup.absmin[ABS_HAT0X] = -1;
    setup.absmax[ABS_HAT0X] = 1;
    setup.absmin[ABS_HAT0Y] = -1;
    setup.absmax[ABS_HAT0Y] = 1;
    if (write(fd, &setup, sizeof(setup)) != (ssize_t)sizeof(setup))
        return -1;
    return ioctl(fd, UI_DEV_CREATE);
}

static void send_event(int fd, uint16_t type, uint16_t code, int32_t value) {
    struct input_event event;
    memset(&event, 0, sizeof(event));
    event.type = type;
    event.code = code;
    event.value = value;
    assert(write(fd, &event, sizeof(event)) == (ssize_t)sizeof(event));
}

static nk_handle wait_for_device(void) {
    for (int attempt = 0; attempt < 300; ++attempt) {
        nk_handle handles[32];
        uint32_t count = 32;
        const nk_result result = nk_joystick_list(handles, &count);
        assert(result == NK_OK || result == NK_ERROR_BUFFER_TOO_SMALL);
        if (result == NK_OK) {
            for (uint32_t index = 0; index < count; ++index) {
                char name[256];
                uint32_t size = sizeof(name);
                if (nk_joystick_get_name(handles[index], name, &size) == NK_OK &&
                    strcmp(name, device_name) == 0)
                    return handles[index];
            }
        }
        pause_briefly();
    }
    return NK_INVALID_HANDLE;
}

static void verify_state(nk_handle joystick, float expected_axis, uint8_t expected_button,
                         uint8_t expected_hat) {
    float axes[8];
    uint8_t buttons[32];
    uint8_t hats[4];
    uint32_t axis_count = 8;
    uint32_t button_count = 32;
    uint32_t hat_count = 4;
    assert(nk_joystick_get_axes(joystick, axes, &axis_count) == NK_OK);
    assert(nk_joystick_get_buttons(joystick, buttons, &button_count) == NK_OK);
    assert(nk_joystick_get_hats(joystick, hats, &hat_count) == NK_OK);
    assert(axis_count == 1);
    assert(button_count == 1);
    assert(hat_count == 1);
    assert(fabsf(axes[0] - expected_axis) < 0.001f);
    assert(buttons[0] == expected_button);
    assert(hats[0] == expected_hat);
}

static void verify_change_events(nk_handle joystick) {
    int raw_axis = 0;
    int raw_button = 0;
    int raw_hat = 0;
    int mapped_axis = 0;
    int mapped_button = 0;
    for (;;) {
        nk_event event;
        memset(&event, 0, sizeof(event));
        event.struct_size = sizeof(event);
        assert(nk_poll_event(&event) == NK_OK);
        if (event.kind == NK_EVENT_NONE) {
            nk_event_release(&event);
            break;
        }
        if (event.source == joystick) {
            raw_axis += event.kind == NK_EVENT_JOYSTICK_AXIS;
            raw_button += event.kind == NK_EVENT_JOYSTICK_BUTTON;
            raw_hat += event.kind == NK_EVENT_JOYSTICK_HAT;
            mapped_axis += event.kind == NK_EVENT_GAMEPAD_AXIS;
            mapped_button += event.kind == NK_EVENT_GAMEPAD_BUTTON;
        }
        nk_event_release(&event);
    }
    if (joystick == NK_INVALID_HANDLE)
        return;
    assert(raw_axis == 1);
    assert(raw_button == 1);
    assert(raw_hat == 1);
    assert(mapped_axis >= 1);
    assert(mapped_button >= 2);
}

int main(void) {
    const int fd = open_uinput();
    if (fd < 0)
        return 77;
    if (create_device(fd) < 0) {
        close(fd);
        return 77;
    }

    nk_init_options options;
    memset(&options, 0, sizeof(options));
    options.struct_size = sizeof(options);
    options.api_version = NK_API_VERSION;
    assert(nk_init(&options) == NK_OK);

    const nk_handle joystick = wait_for_device();
    assert(joystick != NK_INVALID_HANDLE);
    char guid[33];
    uint32_t guid_size = sizeof(guid);
    assert(nk_joystick_get_guid(joystick, guid, &guid_size) == NK_OK);
    assert(strcmp(guid, "03000000091200004b4e000001000000") == 0);
    assert(nk_gamepad_add_mapping("03000000091200004b4e000001000000,NativeKit Virtual Gamepad,a:b0,"
                                  "dpup:h0.1,dpright:h0.2,leftx:a0,platform:Linux,") == NK_OK);
    send_event(fd, EV_SYN, SYN_REPORT, 0);
    float baseline[1];
    uint32_t baseline_count = 1;
    assert(nk_joystick_get_axes(joystick, baseline, &baseline_count) == NK_OK);
    verify_change_events(NK_INVALID_HANDLE);

    send_event(fd, EV_ABS, ABS_X, 32767);
    send_event(fd, EV_KEY, BTN_GAMEPAD, 1);
    send_event(fd, EV_ABS, ABS_HAT0Y, -1);
    send_event(fd, EV_SYN, SYN_REPORT, 0);
    verify_state(joystick, 1.f, 1, NK_JOYSTICK_HAT_UP);
    verify_change_events(joystick);

    send_event(fd, EV_SYN, SYN_DROPPED, 0);
    send_event(fd, EV_ABS, ABS_X, -32768);
    send_event(fd, EV_KEY, BTN_GAMEPAD, 0);
    send_event(fd, EV_ABS, ABS_HAT0X, 1);
    send_event(fd, EV_ABS, ABS_HAT0Y, 0);
    send_event(fd, EV_SYN, SYN_REPORT, 0);
    verify_state(joystick, -1.f, 0, NK_JOYSTICK_HAT_RIGHT);

    assert(ioctl(fd, UI_DEV_DESTROY) == 0);
    close(fd);
    int disconnected = 0;
    for (int attempt = 0; attempt < 300 && !disconnected; ++attempt) {
        nk_event event;
        memset(&event, 0, sizeof(event));
        event.struct_size = sizeof(event);
        assert(nk_poll_event(&event) == NK_OK);
        disconnected = event.kind == NK_EVENT_JOYSTICK_DISCONNECTED && event.source == joystick;
        nk_event_release(&event);
        if (!disconnected)
            pause_briefly();
    }
    assert(disconnected);
    uint32_t count = 0;
    assert(nk_joystick_get_axes(joystick, NULL, &count) == NK_ERROR_INVALID_HANDLE);
    nk_shutdown();
    return 0;
}
