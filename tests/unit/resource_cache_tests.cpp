#include "nativekit_resource.h"

#include <cassert>
#include <cstdio>
#include <fstream>
#include <string>
#include <thread>

namespace {

constexpr char test_path[] = "/tmp/nativekit-resource-cache-test.bin";
constexpr char test_uri[] = "file:///tmp/nativekit-resource-cache-test.bin";
constexpr char test_data[] = "cache-data";

} // namespace

int main() {
    std::ofstream file(test_path, std::ios::binary | std::ios::trunc);
    if (!file)
        return 77;
    file.write(test_data, sizeof(test_data) - 1);
    file.close();

    nk_init_options init{};
    init.struct_size = sizeof(init);
    init.api_version = NK_API_VERSION;
    assert(nk_init(&init) == NK_OK);

    nk_resource_cache cache = NK_INVALID_HANDLE;
    assert(nk_resource_cache_create(&cache) == NK_OK);
    uint32_t count = 0;
    assert(nk_resource_cache_get_count(cache, &count) == NK_OK && count == 0);

    nk_resource resource{};
    resource.struct_size = sizeof(resource);
    resource.flags = NK_RESOURCE_READABLE;
    resource.uri = test_uri;
    resource.mime_type = "application/octet-stream";
    resource.display_name = "cache-data";

    nk_resource_asset first = NK_INVALID_HANDLE;
    assert(nk_resource_cache_load(cache, &resource, &first) == NK_OK);
    assert(first != NK_INVALID_HANDLE);
    nk_resource_asset_load_state state = NK_RESOURCE_ASSET_LOADING;
    assert(nk_resource_asset_get_load_state(first, &state) == NK_OK);
    assert(state == NK_RESOURCE_ASSET_READY);
    nk_result load_result = NK_ERROR_UNKNOWN;
    assert(nk_resource_asset_get_result(first, &load_result) == NK_OK && load_result == NK_OK);
    uint64_t size = 0;
    assert(nk_resource_asset_get_size(first, &size) == NK_OK &&
           size == sizeof(test_data) - 1);

    uint64_t required = 0;
    assert(nk_resource_asset_copy_data(first, nullptr, &required) == NK_ERROR_BUFFER_TOO_SMALL);
    assert(required == sizeof(test_data) - 1);
    char data[sizeof(test_data)]{};
    uint64_t capacity = sizeof(data);
    assert(nk_resource_asset_copy_data(first, data, &capacity) == NK_OK);
    assert(capacity == sizeof(test_data) - 1);
    assert(std::string(data, capacity) == test_data);

    uint32_t uri_size = 0;
    assert(nk_resource_asset_get_uri(first, nullptr, &uri_size) == NK_ERROR_BUFFER_TOO_SMALL);
    assert(uri_size == sizeof(test_uri));
    char uri[sizeof(test_uri)]{};
    assert(nk_resource_asset_get_uri(first, uri, &uri_size) == NK_OK);
    assert(std::string(uri) == test_uri);

    bool worker_ok = false;
    std::thread worker([&] {
        nk_resource_asset_load_state worker_state = NK_RESOURCE_ASSET_LOADING;
        nk_result worker_result = NK_ERROR_UNKNOWN;
        uint64_t worker_size = 0;
        char worker_data[sizeof(test_data)]{};
        uint64_t worker_capacity = sizeof(worker_data);
        worker_ok = nk_resource_asset_get_load_state(first, &worker_state) == NK_OK &&
                    worker_state == NK_RESOURCE_ASSET_READY &&
                    nk_resource_asset_get_result(first, &worker_result) == NK_OK &&
                    worker_result == NK_OK &&
                    nk_resource_asset_get_size(first, &worker_size) == NK_OK &&
                    worker_size == sizeof(test_data) - 1 &&
                    nk_resource_asset_copy_data(first, worker_data, &worker_capacity) == NK_OK &&
                    worker_capacity == sizeof(test_data) - 1 &&
                    std::string(worker_data, worker_capacity) == test_data;
    });
    worker.join();
    assert(worker_ok);

    assert(nk_resource_cache_get_count(cache, &count) == NK_OK && count == 1);
    nk_resource_asset second = NK_INVALID_HANDLE;
    assert(nk_resource_cache_load(cache, &resource, &second) == NK_OK);
    assert(second != NK_INVALID_HANDLE && second != first);
    assert(nk_resource_cache_get_count(cache, &count) == NK_OK && count == 1);

    nk_resource_asset found = NK_INVALID_HANDLE;
    assert(nk_resource_cache_find(cache, test_uri, &found) == NK_OK);
    assert(found != NK_INVALID_HANDLE && found != first && found != second);
    const auto found_asset = found;
    assert(nk_resource_cache_remove(cache, test_uri) == NK_OK);
    assert(nk_resource_cache_get_count(cache, &count) == NK_OK && count == 0);
    assert(nk_resource_cache_find(cache, test_uri, &found) == NK_ERROR_INVALID_REQUEST);

    capacity = sizeof(data);
    assert(nk_resource_asset_copy_data(first, data, &capacity) == NK_OK);
    assert(std::string(data, capacity) == test_data);
    assert(nk_resource_asset_destroy(found_asset) == NK_OK);
    assert(nk_resource_asset_destroy(second) == NK_OK);
    assert(nk_resource_asset_destroy(first) == NK_OK);
    assert(nk_resource_cache_destroy(cache) == NK_OK);
    nk_shutdown();
    std::remove(test_path);
    return 0;
}
