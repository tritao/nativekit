#include "nativekit.h"
#include "nativekit_resource.h"

#include <assert.h>
#include <string.h>
#include <unistd.h>

static nk_event wait_for_event(nk_event_kind kind, nk_request_id request) {
    for (int attempt = 0; attempt < 500; ++attempt) {
        nk_event event = {0};
        event.struct_size = sizeof(event);
        assert(nk_poll_event(&event) == NK_OK);
        if (event.kind == kind && event.request_id == request)
            return event;
        nk_event_release(&event);
        usleep(10000);
    }
    assert(!"timed out waiting for resource event");
    nk_event unreachable = {0};
    return unreachable;
}

int main(void) {
    nk_init_options init = {0};
    init.struct_size = sizeof(init);
    init.api_version = NK_API_VERSION;
    assert(nk_init(&init) == NK_OK);

    nk_resource resources[2] = {0};
    resources[0].struct_size = sizeof(resources[0]);
    resources[0].flags = NK_RESOURCE_READABLE;
    resources[0].uri = "file:///tmp/nativekit-resource-workflow.txt";
    resources[0].mime_type = "text/plain";
    resources[0].display_name = "workflow.txt";
    resources[1].struct_size = sizeof(resources[1]);
    resources[1].flags = NK_RESOURCE_READABLE;
    resources[1].uri = "https://example.test/nativekit-resource";
    resources[1].display_name = "remote-resource";

    assert(nk_clipboard_set_resources(resources, 2) == NK_OK);
    nk_request_id request = NK_INVALID_REQUEST_ID;
    assert(nk_clipboard_read_resources(&request) == NK_OK);
    nk_event event = wait_for_event(NK_EVENT_CLIPBOARD_RESOURCES_COMPLETE, request);
    assert(event.data_count == 2);
    nk_resource_view view = {0};
    view.struct_size = sizeof(view);
    assert(nk_resource_event_item(&event, 0, &view) == NK_OK);
    assert(view.flags == NK_RESOURCE_READABLE);
    assert(view.uri_length == strlen(resources[0].uri));
    assert(memcmp(view.uri, resources[0].uri, view.uri_length) == 0);
    assert(nk_resource_event_item(&event, 1, &view) == NK_OK);
    assert(view.uri_length == strlen(resources[1].uri));
    assert(memcmp(view.uri, resources[1].uri, view.uri_length) == 0);
    nk_event_release(&event);

    nk_share_options share = {0};
    share.struct_size = sizeof(share);
    share.title = "NativeKit resource workflow";
    share.text = "shared resource text";
    share.resources = resources;
    share.resource_count = 2;
    assert(nk_share(&share) == NK_OK);
    assert(nk_clipboard_read_resources(&request) == NK_OK);
    event = wait_for_event(NK_EVENT_CLIPBOARD_RESOURCES_COMPLETE, request);
    assert(event.data_count == 2);
    nk_event_release(&event);

    nk_file_dialog_options options = {0};
    options.struct_size = sizeof(options);
    options.title = "NativeKit resource workflow";
    assert(nk_dialog_open_resource(NK_INVALID_HANDLE, &options, &request) == NK_OK);
    assert(nk_dialog_cancel(request) == NK_OK);
    event = wait_for_event(NK_EVENT_DIALOG_RESOURCES_COMPLETE, request);
    assert(event.data_count == 0);
    nk_resource_list list = {0};
    assert(event.data_size >= sizeof(list));
    memcpy(&list, event.data, sizeof(list));
    assert(list.accepted == 0);
    assert(list.item_count == 0);
    nk_event_release(&event);

    nk_shutdown();
    return 0;
}
