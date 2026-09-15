#include "nativekit_audio.h"

#include "core/boundary.hpp"
#include "core/error.hpp"
#include "core/event_queue.hpp"
#include "core/handle_registry.hpp"
#include "core/runtime.hpp"

#include "miniaudio.h"

#include <atomic>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
#include <memory>
#include <mutex>
#include <new>
#include <string>
#include <vector>

namespace {

constexpr uint32_t supported_voice_flags = NK_AUDIO_VOICE_LOOPING | NK_AUDIO_VOICE_STREAM |
                                           NK_AUDIO_VOICE_ASYNC;

struct AudioEngineResource final : nk::core::Resource {
    ma_engine engine{};
    bool initialized = false;

    ~AudioEngineResource() override {
        if (initialized)
            ma_engine_uninit(&engine);
    }
};

struct AudioClipResource final : nk::core::Resource {
    std::shared_ptr<AudioEngineResource> engine;
    std::string path;
    std::string resource_uri;
    std::vector<std::byte> encoded_data;
    std::atomic<nk_audio_clip> handle{NK_INVALID_HANDLE};
    std::atomic<nk_audio_clip_load_state> load_state{NK_AUDIO_CLIP_READY};
    std::atomic<nk_result> load_result{NK_OK};
    std::atomic<bool> load_event_emitted{false};
    nk_request_id load_request = NK_INVALID_REQUEST_ID;
    bool from_memory = false;
    bool from_resource = false;

    ~AudioClipResource() override {
        handle.store(NK_INVALID_HANDLE, std::memory_order_release);
    }
};

struct AudioBusResource final : nk::core::Resource {
    std::shared_ptr<AudioEngineResource> engine;
    ma_sound_group group{};
    bool initialized = false;
    float volume = 1.0f;
    bool muted = false;

    ~AudioBusResource() override {
        if (initialized)
            ma_sound_group_uninit(&group);
    }
};

struct AudioVoiceResource;
struct AudioResourceReader {
    nk_resource_stream stream = NK_INVALID_HANDLE;
};

struct AudioVoiceLoadNotification {
    ma_async_notification_callbacks callbacks{};
    AudioVoiceResource *voice = nullptr;
};

struct AudioResourceLoadContext {
    std::shared_ptr<AudioClipResource> clip;
};

void audio_voice_end_callback(void *user_data, ma_sound *sound) noexcept;
void audio_voice_load_callback(ma_async_notification *notification) noexcept;
void audio_voice_update_load_state(AudioVoiceResource &voice) noexcept;
void audio_voice_publish_load_event(AudioVoiceResource &voice) noexcept;
void audio_clip_publish_load_event(AudioClipResource &clip) noexcept;
void audio_resource_load_callback(nk_request_id request, nk_result result, const void *data,
                                  uint64_t data_size, void *user_data) noexcept;
void audio_resource_load_cleanup(void *user_data) noexcept;

struct AudioVoiceResource final : nk::core::Resource {
    std::shared_ptr<AudioEngineResource> engine;
    std::shared_ptr<AudioClipResource> clip;
    std::shared_ptr<AudioBusResource> bus;
    std::atomic<nk_audio_voice> handle{NK_INVALID_HANDLE};
    std::atomic<nk_audio_voice_load_state> load_state{NK_AUDIO_VOICE_READY};
    std::atomic<nk_result> load_result{NK_OK};
    std::atomic<bool> asynchronous{false};
    std::atomic<bool> load_notification_signaled{false};
    std::atomic<bool> load_status_query_ready{false};
    std::atomic<bool> load_event_emitted{false};
    AudioVoiceLoadNotification load_notification{};
    ma_decoder decoder{};
    bool decoder_initialized = false;
    AudioResourceReader resource_reader{};
    ma_sound sound{};
    bool sound_initialized = false;

    ~AudioVoiceResource() override {
        handle.store(NK_INVALID_HANDLE, std::memory_order_release);
        if (sound_initialized)
            ma_sound_uninit(&sound);
        if (decoder_initialized)
            ma_decoder_uninit(&decoder);
        if (resource_reader.stream != NK_INVALID_HANDLE)
            nk_resource_close(resource_reader.stream);
    }
};

ma_result resource_result(nk_result result) {
    if (result == NK_OK)
        return MA_SUCCESS;
    if (result == NK_ERROR_INVALID_ARGUMENT)
        return MA_INVALID_ARGS;
    if (result == NK_ERROR_OUT_OF_MEMORY)
        return MA_OUT_OF_MEMORY;
    if (result == NK_ERROR_UNSUPPORTED)
        return MA_NOT_IMPLEMENTED;
    if (result == NK_ERROR_INVALID_REQUEST)
        return MA_INVALID_OPERATION;
    return MA_IO_ERROR;
}

ma_result audio_resource_read(ma_decoder *decoder, void *buffer, size_t bytes_to_read,
                              size_t *bytes_read) {
    if (bytes_read)
        *bytes_read = 0;
    if (!decoder || !decoder->pUserData)
        return MA_INVALID_ARGS;
    auto *reader = static_cast<AudioResourceReader *>(decoder->pUserData);
    uint64_t read = 0;
    const auto result = nk_resource_read(reader->stream, buffer, bytes_to_read, &read);
    if (bytes_read)
        *bytes_read = static_cast<size_t>(read);
    if (result != NK_OK)
        return resource_result(result);
    return read == 0 ? MA_AT_END : MA_SUCCESS;
}

ma_result audio_resource_seek(ma_decoder *decoder, ma_int64 byte_offset, ma_seek_origin origin) {
    if (!decoder || !decoder->pUserData)
        return MA_INVALID_ARGS;
    auto *reader = static_cast<AudioResourceReader *>(decoder->pUserData);
    const auto resource_origin = origin == ma_seek_origin_start
                                     ? NK_SEEK_START
                                     : origin == ma_seek_origin_current ? NK_SEEK_CURRENT
                                                                         : NK_SEEK_END;
    uint64_t position = 0;
    return resource_result(nk_resource_seek(reader->stream, byte_offset, resource_origin,
                                            &position));
}

void audio_voice_end_callback(void *user_data, ma_sound *) noexcept {
    auto *voice = static_cast<AudioVoiceResource *>(user_data);
    if (!voice)
        return;
    const auto handle = voice->handle.load(std::memory_order_acquire);
    if (handle == NK_INVALID_HANDLE)
        return;
    nk::core::QueuedEvent event;
    event.kind = NK_EVENT_AUDIO_VOICE_COMPLETE;
    event.source = handle;
    nk::core::push_event(std::move(event));
}

std::mutex engine_mutex;
std::weak_ptr<AudioEngineResource> engine_resource;

nk_result invalid_argument(const char *message) {
    nk::core::set_error(message);
    return NK_ERROR_INVALID_ARGUMENT;
}

nk_result invalid_handle(const char *message) {
    nk::core::set_error(message);
    return NK_ERROR_INVALID_HANDLE;
}

nk_result invalid_request(const char *message) {
    nk::core::set_error(message);
    return NK_ERROR_INVALID_REQUEST;
}

bool valid_fade_volume(float volume, bool allow_current) {
    if (!std::isfinite(volume))
        return false;
    if (allow_current && volume == NK_AUDIO_VOLUME_CURRENT)
        return true;
    return volume >= 0.0f;
}

nk_result miniaudio_result_code(ma_result result) {
    if (result == MA_SUCCESS)
        return NK_OK;
    if (result == MA_INVALID_ARGS)
        return NK_ERROR_INVALID_ARGUMENT;
    if (result == MA_OUT_OF_MEMORY)
        return NK_ERROR_OUT_OF_MEMORY;
    if (result == MA_NO_BACKEND || result == MA_NO_DEVICE || result == MA_NOT_IMPLEMENTED)
        return NK_ERROR_UNSUPPORTED;
    return NK_ERROR_UNKNOWN;
}

nk_result map_miniaudio_result(ma_result result, const char *message) {
    const auto mapped = miniaudio_result_code(result);
    if (mapped == NK_OK)
        return NK_OK;
    nk::core::set_error(message);
    return mapped;
}

void audio_clip_publish_load_event(AudioClipResource &clip) noexcept {
    const auto state = clip.load_state.load(std::memory_order_acquire);
    if (state == NK_AUDIO_CLIP_LOADING || clip.load_request == NK_INVALID_REQUEST_ID)
        return;
    const auto handle = clip.handle.load(std::memory_order_acquire);
    if (handle == NK_INVALID_HANDLE)
        return;

    bool expected = false;
    if (!clip.load_event_emitted.compare_exchange_strong(expected, true,
                                                         std::memory_order_acq_rel))
        return;

    nk::core::QueuedEvent event;
    event.kind = state == NK_AUDIO_CLIP_READY ? NK_EVENT_AUDIO_CLIP_READY
                                              : NK_EVENT_AUDIO_CLIP_LOAD_FAILED;
    event.source = handle;
    event.request_id = clip.load_request;
    event.result = clip.load_result.load(std::memory_order_acquire);
    if (nk::core::push_event(std::move(event)) != NK_OK)
        clip.load_event_emitted.store(false, std::memory_order_release);
}

void audio_voice_publish_load_event(AudioVoiceResource &voice) noexcept {
    const auto state = voice.load_state.load(std::memory_order_acquire);
    if (state == NK_AUDIO_VOICE_LOADING)
        return;
    const auto handle = voice.handle.load(std::memory_order_acquire);
    if (handle == NK_INVALID_HANDLE)
        return;

    bool expected = false;
    if (!voice.load_event_emitted.compare_exchange_strong(expected, true,
                                                           std::memory_order_acq_rel))
        return;

    nk::core::QueuedEvent event;
    event.kind = state == NK_AUDIO_VOICE_READY ? NK_EVENT_AUDIO_VOICE_READY
                                               : NK_EVENT_AUDIO_VOICE_LOAD_FAILED;
    event.source = handle;
    event.result = voice.load_result.load(std::memory_order_acquire);
    if (nk::core::push_event(std::move(event)) != NK_OK)
        voice.load_event_emitted.store(false, std::memory_order_release);
}

void audio_voice_update_load_state(AudioVoiceResource &voice) noexcept {
    if (!voice.asynchronous.load(std::memory_order_acquire) ||
        !voice.load_notification_signaled.load(std::memory_order_acquire) ||
        !voice.load_status_query_ready.load(std::memory_order_acquire))
        return;

    const auto *data_source = ma_sound_get_data_source(&voice.sound);
    if (!data_source)
        return;
    const auto result = ma_resource_manager_data_source_result(
        reinterpret_cast<const ma_resource_manager_data_source *>(data_source));
    if (result == MA_BUSY)
        return;

    const auto mapped = miniaudio_result_code(result);
    voice.load_result.store(mapped, std::memory_order_release);
    std::uint32_t expected = static_cast<std::uint32_t>(NK_AUDIO_VOICE_LOADING);
    voice.load_state.compare_exchange_strong(expected,
                                             mapped == NK_OK
                                                 ? static_cast<std::uint32_t>(NK_AUDIO_VOICE_READY)
                                                 : static_cast<std::uint32_t>(
                                                       NK_AUDIO_VOICE_LOAD_FAILED),
                                             std::memory_order_acq_rel);
    audio_voice_publish_load_event(voice);
}

void audio_voice_load_callback(ma_async_notification *notification) noexcept {
    auto *load_notification = reinterpret_cast<AudioVoiceLoadNotification *>(notification);
    if (!load_notification || !load_notification->voice)
        return;
    auto &voice = *load_notification->voice;
    voice.load_notification_signaled.store(true, std::memory_order_release);
    audio_voice_update_load_state(voice);
}

nk_result enter_audio_ui() {
    nk::core::clear_error();
    return nk::core::require_ui_thread();
}

nk_result voice_options(const nk_audio_voice_options *options, uint32_t &flags,
                        nk_audio_bus &bus) {
    flags = 0;
    bus = NK_INVALID_HANDLE;
    if (!options)
        return NK_OK;
    if (options->struct_size < sizeof(nk_audio_voice_options))
        return invalid_argument("audio voice options are missing or too small");
    if (options->flags & ~supported_voice_flags)
        return invalid_argument("audio voice options contain unsupported flags");
    flags = options->flags;
    bus = options->bus;
    return NK_OK;
}

std::shared_ptr<AudioEngineResource> ensure_engine(nk_result &out_result) {
    out_result = NK_OK;
    std::lock_guard lock(engine_mutex);
    if (auto current = engine_resource.lock())
        return current;

    auto next = std::make_shared<AudioEngineResource>();
    const auto result = ma_engine_init(nullptr, &next->engine);
    if (result != MA_SUCCESS) {
        out_result = map_miniaudio_result(result, "could not initialize the miniaudio audio device");
        return {};
    }
    next->initialized = true;
    const auto handle = nk::core::handles().insert(nk::core::ResourceType::audio_engine, next);
    if (handle == NK_INVALID_HANDLE) {
        nk::core::set_error("could not allocate the miniaudio engine handle");
        out_result = NK_ERROR_OUT_OF_MEMORY;
        return {};
    }
    engine_resource = next;
    return next;
}

std::shared_ptr<AudioClipResource> get_clip(nk_audio_clip handle) {
    auto resource = nk::core::handles().get(handle, nk::core::ResourceType::audio_clip);
    if (!resource) {
        nk::core::set_error("invalid audio clip handle");
        return {};
    }
    auto clip = std::dynamic_pointer_cast<AudioClipResource>(std::move(resource));
    if (!clip)
        nk::core::set_error("invalid audio clip resource");
    return clip;
}

nk_result insert_clip(std::shared_ptr<AudioClipResource> clip, nk_audio_clip *out_clip) {
    const auto handle = nk::core::handles().insert(nk::core::ResourceType::audio_clip, clip);
    if (handle == NK_INVALID_HANDLE) {
        nk::core::set_error("could not allocate an audio clip handle");
        return NK_ERROR_OUT_OF_MEMORY;
    }
    clip->handle.store(handle, std::memory_order_release);
    *out_clip = handle;
    return NK_OK;
}

std::shared_ptr<AudioClipResource> create_clip_from_file(const char *path,
                                                         nk_result &out_result) {
    out_result = NK_OK;
    nk_result engine_result = NK_OK;
    auto engine = ensure_engine(engine_result);
    if (!engine) {
        out_result = engine_result;
        return {};
    }

    ma_decoder decoder{};
    const auto result = ma_decoder_init_file(path, nullptr, &decoder);
    if (result != MA_SUCCESS) {
        out_result = map_miniaudio_result(result, "could not validate audio clip file");
        return {};
    }
    ma_decoder_uninit(&decoder);

    auto clip = std::make_shared<AudioClipResource>();
    clip->engine = std::move(engine);
    clip->path = path;
    return clip;
}

std::shared_ptr<AudioClipResource> create_clip_from_resource(const nk_resource *resource,
                                                             nk_result &out_result) {
    out_result = NK_OK;
    nk_result engine_result = NK_OK;
    auto engine = ensure_engine(engine_result);
    if (!engine) {
        out_result = engine_result;
        return {};
    }

    AudioResourceReader reader{};
    const auto open_result = nk_resource_open(resource, NK_RESOURCE_OPEN_READ, &reader.stream);
    if (open_result != NK_OK) {
        out_result = open_result;
        return {};
    }

    ma_decoder decoder{};
    const auto result = ma_decoder_init(audio_resource_read, audio_resource_seek, &reader,
                                        nullptr, &decoder);
    if (result != MA_SUCCESS) {
        nk_resource_close(reader.stream);
        out_result = map_miniaudio_result(result, "could not validate audio resource");
        return {};
    }
    ma_decoder_uninit(&decoder);
    if (const auto close_result = nk_resource_close(reader.stream); close_result != NK_OK) {
        out_result = close_result;
        return {};
    }

    auto clip = std::make_shared<AudioClipResource>();
    clip->engine = std::move(engine);
    clip->resource_uri = resource->uri;
    clip->from_resource = true;
    return clip;
}

nk_result create_clip_from_resource_async(const nk_resource *resource, nk_audio_clip *out_clip,
                                          nk_request_id *out_request) {
    nk_result engine_result = NK_OK;
    auto engine = ensure_engine(engine_result);
    if (!engine)
        return engine_result;

    auto clip = std::make_shared<AudioClipResource>();
    clip->engine = std::move(engine);
    clip->resource_uri = resource->uri;
    clip->from_resource = true;
    clip->load_state.store(NK_AUDIO_CLIP_LOADING, std::memory_order_relaxed);

    auto context = std::make_unique<AudioResourceLoadContext>();
    context->clip = clip;
    const auto insert_result = insert_clip(clip, out_clip);
    if (insert_result != NK_OK)
        return insert_result;

    const auto load_result = nk::core::start_resource_load(
        resource, out_request, audio_resource_load_callback, context.release(),
        audio_resource_load_cleanup);
    if (load_result != NK_OK) {
        clip->handle.store(NK_INVALID_HANDLE, std::memory_order_release);
        nk::core::handles().erase(*out_clip, nk::core::ResourceType::audio_clip);
        *out_clip = NK_INVALID_HANDLE;
        *out_request = NK_INVALID_REQUEST_ID;
        return load_result;
    }
    clip->load_request = *out_request;
    return NK_OK;
}

nk_result initialize_clip_from_memory(AudioClipResource &clip, const void *data,
                                      uint64_t data_size) {
    if (!data || data_size == 0 ||
        data_size > static_cast<uint64_t>(std::numeric_limits<std::size_t>::max()))
        return invalid_argument("audio clip memory data is invalid");
    clip.encoded_data.resize(static_cast<std::size_t>(data_size));
    std::memcpy(clip.encoded_data.data(), data, clip.encoded_data.size());

    ma_decoder decoder{};
    const auto result = ma_decoder_init_memory(clip.encoded_data.data(), clip.encoded_data.size(),
                                               nullptr, &decoder);
    if (result != MA_SUCCESS)
        return map_miniaudio_result(result, "could not validate audio clip memory");
    ma_decoder_uninit(&decoder);
    clip.from_memory = true;
    clip.from_resource = false;
    return NK_OK;
}

std::shared_ptr<AudioClipResource> create_clip_from_memory(const void *data, uint64_t data_size,
                                                           nk_result &out_result) {
    out_result = NK_OK;
    nk_result engine_result = NK_OK;
    auto engine = ensure_engine(engine_result);
    if (!engine) {
        out_result = engine_result;
        return {};
    }

    auto clip = std::make_shared<AudioClipResource>();
    clip->engine = std::move(engine);
    out_result = initialize_clip_from_memory(*clip, data, data_size);
    if (out_result != NK_OK)
        return {};
    return clip;
}

void audio_resource_load_callback(nk_request_id request, nk_result result, const void *data,
                                  uint64_t data_size, void *user_data) noexcept {
    auto *context = static_cast<AudioResourceLoadContext *>(user_data);
    if (!context || !context->clip)
        return;
    auto &clip = *context->clip;
    if (clip.handle.load(std::memory_order_acquire) == NK_INVALID_HANDLE)
        return;
    clip.load_request = request;
    auto load_result = result;
    if (load_result == NK_OK) {
        try {
            load_result = initialize_clip_from_memory(clip, data, data_size);
        } catch (const std::bad_alloc &) {
            nk::core::set_error("out of memory while storing audio resource data");
            load_result = NK_ERROR_OUT_OF_MEMORY;
        } catch (...) {
            nk::core::set_error("unexpected error while storing audio resource data");
            load_result = NK_ERROR_UNKNOWN;
        }
    }
    clip.load_result.store(load_result, std::memory_order_release);
    clip.load_state.store(load_result == NK_OK ? NK_AUDIO_CLIP_READY
                                               : NK_AUDIO_CLIP_LOAD_FAILED,
                          std::memory_order_release);
    audio_clip_publish_load_event(clip);
}

void audio_resource_load_cleanup(void *user_data) noexcept {
    delete static_cast<AudioResourceLoadContext *>(user_data);
}

std::shared_ptr<AudioBusResource> get_bus(nk_audio_bus handle) {
    auto resource = nk::core::handles().get(handle, nk::core::ResourceType::audio_bus);
    if (!resource) {
        nk::core::set_error("invalid audio bus handle");
        return {};
    }
    auto bus = std::dynamic_pointer_cast<AudioBusResource>(std::move(resource));
    if (!bus)
        nk::core::set_error("invalid audio bus resource");
    return bus;
}

std::shared_ptr<AudioVoiceResource> get_voice(nk_audio_voice handle) {
    auto resource = nk::core::handles().get(handle, nk::core::ResourceType::audio_voice);
    if (!resource) {
        nk::core::set_error("invalid audio voice handle");
        return {};
    }
    auto voice = std::dynamic_pointer_cast<AudioVoiceResource>(std::move(resource));
    if (!voice)
        nk::core::set_error("invalid audio voice resource");
    return voice;
}

nk_result insert_bus(std::shared_ptr<AudioBusResource> bus, nk_audio_bus *out_bus) {
    const auto handle = nk::core::handles().insert(nk::core::ResourceType::audio_bus, bus);
    if (handle == NK_INVALID_HANDLE) {
        nk::core::set_error("could not allocate an audio bus handle");
        return NK_ERROR_OUT_OF_MEMORY;
    }
    *out_bus = handle;
    return NK_OK;
}

uint32_t miniaudio_voice_flags(uint32_t flags) {
    uint32_t result = 0;
    if (flags & NK_AUDIO_VOICE_LOOPING)
        result |= MA_SOUND_FLAG_LOOPING;
    if (flags & NK_AUDIO_VOICE_STREAM)
        result |= MA_SOUND_FLAG_STREAM;
    else
        result |= MA_SOUND_FLAG_DECODE;
    if (flags & NK_AUDIO_VOICE_ASYNC)
        result |= MA_SOUND_FLAG_ASYNC;
    return result;
}

std::shared_ptr<AudioVoiceResource> create_voice_from_clip(
    std::shared_ptr<AudioClipResource> clip, const nk_audio_voice_options *options,
    nk_result &out_result) {
    out_result = NK_OK;
    uint32_t flags = 0;
    nk_audio_bus bus_handle = NK_INVALID_HANDLE;
    if (const auto result = voice_options(options, flags, bus_handle); result != NK_OK) {
        out_result = result;
        return {};
    }

    if (clip->load_state.load(std::memory_order_acquire) != NK_AUDIO_CLIP_READY) {
        out_result = invalid_request("audio clip is not ready for voice creation");
        return {};
    }

    std::shared_ptr<AudioBusResource> bus;
    if (bus_handle != NK_INVALID_HANDLE) {
        bus = get_bus(bus_handle);
        if (!bus) {
            out_result = NK_ERROR_INVALID_HANDLE;
            return {};
        }
    }

    if ((clip->from_memory || clip->from_resource) && (flags & NK_AUDIO_VOICE_ASYNC)) {
        out_result = invalid_argument(
            "asynchronous audio loading requires a file-backed clip");
        return {};
    }

    auto voice = std::make_shared<AudioVoiceResource>();
    voice->engine = clip->engine;
    voice->clip = std::move(clip);
    voice->bus = std::move(bus);
    const bool asynchronous = (flags & NK_AUDIO_VOICE_ASYNC) != 0;
    voice->asynchronous.store(asynchronous, std::memory_order_relaxed);
    if (asynchronous)
        voice->load_state.store(NK_AUDIO_VOICE_LOADING, std::memory_order_relaxed);

    ma_result result = MA_SUCCESS;
    if (voice->clip->from_memory) {
        result = ma_decoder_init_memory(voice->clip->encoded_data.data(),
                                        voice->clip->encoded_data.size(), nullptr,
                                        &voice->decoder);
        if (result != MA_SUCCESS) {
            out_result = map_miniaudio_result(result, "could not initialize audio clip decoder");
            return {};
        }
        voice->decoder_initialized = true;
        const uint32_t sound_flags =
            (flags & NK_AUDIO_VOICE_LOOPING) ? MA_SOUND_FLAG_LOOPING : 0;
        result = ma_sound_init_from_data_source(
            &voice->engine->engine, &voice->decoder, sound_flags,
            voice->bus ? &voice->bus->group : nullptr, &voice->sound);
    } else if (voice->clip->from_resource) {
        nk_resource resource{};
        resource.struct_size = sizeof(resource);
        resource.flags = NK_RESOURCE_READABLE;
        resource.uri = voice->clip->resource_uri.c_str();
        const auto open_result =
            nk_resource_open(&resource, NK_RESOURCE_OPEN_READ, &voice->resource_reader.stream);
        if (open_result != NK_OK) {
            out_result = open_result;
            return {};
        }
        result = ma_decoder_init(audio_resource_read, audio_resource_seek,
                                 &voice->resource_reader, nullptr, &voice->decoder);
        if (result != MA_SUCCESS) {
            out_result = map_miniaudio_result(result, "could not initialize audio resource decoder");
            return {};
        }
        voice->decoder_initialized = true;
        const uint32_t sound_flags =
            (flags & NK_AUDIO_VOICE_LOOPING) ? MA_SOUND_FLAG_LOOPING : 0;
        result = ma_sound_init_from_data_source(
            &voice->engine->engine, &voice->decoder, sound_flags,
            voice->bus ? &voice->bus->group : nullptr, &voice->sound);
    } else if (asynchronous) {
        voice->load_notification.voice = voice.get();
        voice->load_notification.callbacks.onSignal = audio_voice_load_callback;
        auto config = ma_sound_config_init_2(&voice->engine->engine);
        config.pFilePath = voice->clip->path.c_str();
        config.flags = miniaudio_voice_flags(flags);
        config.pInitialAttachment = voice->bus ? &voice->bus->group : nullptr;
        auto *notification = reinterpret_cast<ma_async_notification *>(
            &voice->load_notification.callbacks);
        if (flags & NK_AUDIO_VOICE_STREAM)
            config.initNotifications.init.pNotification = notification;
        else
            config.initNotifications.done.pNotification = notification;
        result = ma_sound_init_ex(&voice->engine->engine, &config, &voice->sound);
    } else {
        result = ma_sound_init_from_file(
            &voice->engine->engine, voice->clip->path.c_str(), miniaudio_voice_flags(flags),
            voice->bus ? &voice->bus->group : nullptr, nullptr, &voice->sound);
    }
    if (result != MA_SUCCESS) {
        out_result = map_miniaudio_result(result, "could not create audio clip voice");
        return {};
    }
    voice->sound_initialized = true;
    if (asynchronous)
        voice->load_status_query_ready.store(true, std::memory_order_release);
    const auto callback_result =
        ma_sound_set_end_callback(&voice->sound, audio_voice_end_callback, voice.get());
    if (callback_result != MA_SUCCESS) {
        out_result = map_miniaudio_result(callback_result,
                                          "could not configure audio voice completion");
        return {};
    }
    if (asynchronous)
        audio_voice_update_load_state(*voice);
    return voice;
}

nk_result insert_voice(std::shared_ptr<AudioVoiceResource> voice, nk_audio_voice *out_voice) {
    const auto handle = nk::core::handles().insert(nk::core::ResourceType::audio_voice, voice);
    if (handle == NK_INVALID_HANDLE) {
        nk::core::set_error("could not allocate an audio voice handle");
        return NK_ERROR_OUT_OF_MEMORY;
    }
    voice->handle.store(handle, std::memory_order_release);
    *out_voice = handle;
    audio_voice_update_load_state(*voice);
    audio_voice_publish_load_event(*voice);
    return NK_OK;
}

template <typename Function>
nk_result with_voice(nk_audio_voice handle, const char *message, Function &&function) {
    auto voice = get_voice(handle);
    if (!voice)
        return NK_ERROR_INVALID_HANDLE;
    return function(*voice, message);
}

template <typename Function>
nk_result with_bus(nk_audio_bus handle, const char *message, Function &&function) {
    auto bus = get_bus(handle);
    if (!bus)
        return NK_ERROR_INVALID_HANDLE;
    return function(*bus, message);
}

} // namespace

extern "C" {

nk_result NK_CALL nk_audio_bus_create(nk_audio_bus *out_bus) {
    return nk::core::result_boundary(
        "unexpected error while creating an audio bus", [&]() -> nk_result {
            if (const auto result = enter_audio_ui(); result != NK_OK)
                return result;
            if (!out_bus)
                return invalid_argument("audio bus output is missing");
            *out_bus = NK_INVALID_HANDLE;
            nk_result engine_result = NK_OK;
            auto engine = ensure_engine(engine_result);
            if (!engine)
                return engine_result;
            auto bus = std::make_shared<AudioBusResource>();
            bus->engine = std::move(engine);
            const auto result = ma_sound_group_init(&bus->engine->engine, 0, nullptr,
                                                    &bus->group);
            if (result != MA_SUCCESS)
                return map_miniaudio_result(result, "could not create audio mixer bus");
            bus->initialized = true;
            return insert_bus(std::move(bus), out_bus);
        });
}

nk_result NK_CALL nk_audio_bus_destroy(nk_audio_bus bus) {
    return nk::core::result_boundary("unexpected error while destroying an audio bus",
                                     [&]() -> nk_result {
        if (const auto result = enter_audio_ui(); result != NK_OK)
            return result;
        if (!nk::core::handles().erase(bus, nk::core::ResourceType::audio_bus))
            return invalid_handle("invalid audio bus handle");
        return NK_OK;
    });
}

nk_result NK_CALL nk_audio_bus_start(nk_audio_bus bus) {
    return nk::core::result_boundary("unexpected error while starting an audio bus", [&]() {
        if (const auto result = enter_audio_ui(); result != NK_OK)
            return result;
        return with_bus(bus, "could not start audio bus", [](AudioBusResource &value,
                                                              const char *message) {
            return map_miniaudio_result(ma_sound_group_start(&value.group), message);
        });
    });
}

nk_result NK_CALL nk_audio_bus_stop(nk_audio_bus bus) {
    return nk::core::result_boundary("unexpected error while stopping an audio bus", [&]() {
        if (const auto result = enter_audio_ui(); result != NK_OK)
            return result;
        return with_bus(bus, "could not stop audio bus", [](AudioBusResource &value,
                                                             const char *message) {
            return map_miniaudio_result(ma_sound_group_stop(&value.group), message);
        });
    });
}

nk_result NK_CALL nk_audio_bus_schedule_start(nk_audio_bus bus,
                                              uint64_t absolute_time_pcm_frames) {
    return nk::core::result_boundary(
        "unexpected error while scheduling an audio bus start", [&]() {
            if (const auto result = enter_audio_ui(); result != NK_OK)
                return result;
            return with_bus(bus, "could not schedule audio bus start",
                            [&](AudioBusResource &value, const char *) {
                                ma_sound_group_set_start_time_in_pcm_frames(
                                    &value.group, absolute_time_pcm_frames);
                                return NK_OK;
                            });
        });
}

nk_result NK_CALL nk_audio_bus_schedule_stop(nk_audio_bus bus,
                                             uint64_t absolute_time_pcm_frames) {
    return nk::core::result_boundary(
        "unexpected error while scheduling an audio bus stop", [&]() {
            if (const auto result = enter_audio_ui(); result != NK_OK)
                return result;
            return with_bus(bus, "could not schedule audio bus stop",
                            [&](AudioBusResource &value, const char *) {
                                ma_sound_group_set_stop_time_in_pcm_frames(
                                    &value.group, absolute_time_pcm_frames);
                                return NK_OK;
                            });
        });
}

nk_result NK_CALL nk_audio_bus_clear_schedule(nk_audio_bus bus) {
    return nk::core::result_boundary(
        "unexpected error while clearing an audio bus schedule", [&]() {
            if (const auto result = enter_audio_ui(); result != NK_OK)
                return result;
            return with_bus(bus, "could not clear audio bus schedule",
                            [&](AudioBusResource &value, const char *) {
                                ma_sound_reset_start_time(&value.group);
                                ma_sound_reset_stop_time_and_fade(&value.group);
                                return NK_OK;
                            });
        });
}

nk_result NK_CALL nk_audio_bus_is_playing(nk_audio_bus bus, nk_bool *out_playing) {
    return nk::core::result_boundary(
        "unexpected error while querying an audio bus", [&]() -> nk_result {
            if (const auto result = enter_audio_ui(); result != NK_OK)
                return result;
            if (!out_playing)
                return invalid_argument("audio bus playing output is missing");
            return with_bus(bus, "could not query audio bus", [&](AudioBusResource &value,
                                                                   const char *) {
                *out_playing = ma_sound_group_is_playing(&value.group) ? 1u : 0u;
                return NK_OK;
            });
        });
}

nk_result NK_CALL nk_audio_bus_set_volume(nk_audio_bus bus, float volume) {
    return nk::core::result_boundary("unexpected error while setting audio bus volume", [&]() {
        if (const auto result = enter_audio_ui(); result != NK_OK)
            return result;
        if (!std::isfinite(volume) || volume < 0)
            return invalid_argument("audio bus volume must be finite and non-negative");
        return with_bus(bus, "could not set audio bus volume", [&](AudioBusResource &value,
                                                                    const char *) {
            value.volume = volume;
            if (!value.muted)
                ma_sound_group_set_volume(&value.group, volume);
            return NK_OK;
        });
    });
}

nk_result NK_CALL nk_audio_bus_get_volume(nk_audio_bus bus, float *out_volume) {
    return nk::core::result_boundary("unexpected error while getting audio bus volume", [&]() {
        if (const auto result = enter_audio_ui(); result != NK_OK)
            return result;
        if (!out_volume)
            return invalid_argument("audio bus volume output is missing");
        return with_bus(bus, "could not get audio bus volume", [&](AudioBusResource &value,
                                                                   const char *) {
            *out_volume = value.volume;
            return NK_OK;
        });
    });
}

nk_result NK_CALL nk_audio_bus_fade(nk_audio_bus bus, float volume_begin, float volume_end,
                                    uint64_t duration_pcm_frames) {
    return nk::core::result_boundary("unexpected error while fading an audio bus", [&]() {
        if (const auto result = enter_audio_ui(); result != NK_OK)
            return result;
        if (!valid_fade_volume(volume_begin, true) || !valid_fade_volume(volume_end, false))
            return invalid_argument(
                "audio bus fade volumes must be finite and non-negative; the start may be "
                "NK_AUDIO_VOLUME_CURRENT");
        return with_bus(bus, "could not fade audio bus", [&](AudioBusResource &value,
                                                               const char *) {
            ma_sound_group_set_fade_in_pcm_frames(&value.group, volume_begin, volume_end,
                                                  duration_pcm_frames);
            return NK_OK;
        });
    });
}

nk_result NK_CALL nk_audio_bus_fade_at(nk_audio_bus bus, float volume_begin, float volume_end,
                                       uint64_t duration_pcm_frames,
                                       uint64_t absolute_start_time_pcm_frames) {
    return nk::core::result_boundary(
        "unexpected error while scheduling an audio bus fade", [&]() {
            if (const auto result = enter_audio_ui(); result != NK_OK)
                return result;
            if (!valid_fade_volume(volume_begin, true) || !valid_fade_volume(volume_end, false))
                return invalid_argument(
                    "audio bus fade volumes must be finite and non-negative; the start may be "
                    "NK_AUDIO_VOLUME_CURRENT");
            return with_bus(bus, "could not schedule audio bus fade",
                            [&](AudioBusResource &value, const char *) {
                                ma_sound_set_fade_start_in_pcm_frames(
                                    &value.group, volume_begin, volume_end, duration_pcm_frames,
                                    absolute_start_time_pcm_frames);
                                return NK_OK;
                            });
        });
}

nk_result NK_CALL nk_audio_bus_set_muted(nk_audio_bus bus, nk_bool muted) {
    return nk::core::result_boundary("unexpected error while setting audio bus mute", [&]() {
        if (const auto result = enter_audio_ui(); result != NK_OK)
            return result;
        if (muted > 1)
            return invalid_argument("audio bus mute must be zero or one");
        return with_bus(bus, "could not set audio bus mute", [&](AudioBusResource &value,
                                                                  const char *) {
            value.muted = muted != 0;
            ma_sound_group_set_volume(&value.group, value.muted ? 0.0f : value.volume);
            return NK_OK;
        });
    });
}

nk_result NK_CALL nk_audio_bus_is_muted(nk_audio_bus bus, nk_bool *out_muted) {
    return nk::core::result_boundary(
        "unexpected error while querying audio bus mute", [&]() -> nk_result {
            if (const auto result = enter_audio_ui(); result != NK_OK)
                return result;
            if (!out_muted)
                return invalid_argument("audio bus mute output is missing");
            return with_bus(bus, "could not query audio bus mute", [&](AudioBusResource &value,
                                                                       const char *) {
                *out_muted = value.muted ? 1u : 0u;
                return NK_OK;
            });
        });
}

nk_result NK_CALL nk_audio_clip_create_from_file(const char *path, nk_audio_clip *out_clip) {
    return nk::core::result_boundary(
        "unexpected error while creating an audio clip from a file", [&]() -> nk_result {
            if (const auto result = enter_audio_ui(); result != NK_OK)
                return result;
            if (!path || !*path || !out_clip)
                return invalid_argument("audio clip file path or output is missing");
            *out_clip = NK_INVALID_HANDLE;
            nk_result clip_result = NK_OK;
            auto clip = create_clip_from_file(path, clip_result);
            if (!clip)
                return clip_result;
            return insert_clip(std::move(clip), out_clip);
        });
}

nk_result NK_CALL nk_audio_clip_create_from_resource(const nk_resource *resource,
                                                     nk_audio_clip *out_clip) {
    return nk::core::result_boundary(
        "unexpected error while creating an audio clip from a resource", [&]() -> nk_result {
            if (const auto result = enter_audio_ui(); result != NK_OK)
                return result;
            if (!resource || resource->struct_size < sizeof(nk_resource) ||
                (resource->flags & NK_RESOURCE_READABLE) == 0 || !resource->uri ||
                !*resource->uri || !out_clip)
                return invalid_argument("audio clip resource or output is invalid");
            *out_clip = NK_INVALID_HANDLE;
            nk_result clip_result = NK_OK;
            auto clip = create_clip_from_resource(resource, clip_result);
            if (!clip)
                return clip_result;
            return insert_clip(std::move(clip), out_clip);
        });
}

nk_result NK_CALL nk_audio_clip_create_from_resource_async(const nk_resource *resource,
                                                           nk_audio_clip *out_clip,
                                                           nk_request_id *out_request) {
    return nk::core::result_boundary(
        "unexpected error while creating an asynchronous audio clip from a resource",
        [&]() -> nk_result {
            if (const auto result = enter_audio_ui(); result != NK_OK)
                return result;
            if (!resource || resource->struct_size < sizeof(nk_resource) ||
                (resource->flags & NK_RESOURCE_READABLE) == 0 || !resource->uri ||
                !*resource->uri || !out_clip || !out_request)
                return invalid_argument("audio resource or output is invalid");
            *out_clip = NK_INVALID_HANDLE;
            *out_request = NK_INVALID_REQUEST_ID;
            return create_clip_from_resource_async(resource, out_clip, out_request);
        });
}

nk_result NK_CALL nk_audio_clip_create_from_memory(const void *data, uint64_t data_size,
                                                   nk_audio_clip *out_clip) {
    return nk::core::result_boundary(
        "unexpected error while creating an audio clip from memory", [&]() -> nk_result {
            if (const auto result = enter_audio_ui(); result != NK_OK)
                return result;
            if (!data || data_size == 0 || !out_clip ||
                data_size > static_cast<uint64_t>(std::numeric_limits<std::size_t>::max()))
                return invalid_argument("audio clip memory data or output is invalid");
            *out_clip = NK_INVALID_HANDLE;
            nk_result clip_result = NK_OK;
            auto clip = create_clip_from_memory(data, data_size, clip_result);
            if (!clip)
                return clip_result;
            return insert_clip(std::move(clip), out_clip);
        });
}

nk_result NK_CALL nk_audio_clip_destroy(nk_audio_clip clip) {
    return nk::core::result_boundary("unexpected error while destroying an audio clip",
                                     [&]() -> nk_result {
        if (const auto result = enter_audio_ui(); result != NK_OK)
            return result;
        auto value = get_clip(clip);
        if (!value)
            return NK_ERROR_INVALID_HANDLE;
        if (value->load_state.load(std::memory_order_acquire) == NK_AUDIO_CLIP_LOADING &&
            value->load_request != NK_INVALID_REQUEST_ID) {
            const auto cancel_result = nk::core::cancel_resource_load(value->load_request);
            if (cancel_result != NK_OK && cancel_result != NK_ERROR_UNSUPPORTED)
                return cancel_result;
        }
        value->handle.store(NK_INVALID_HANDLE, std::memory_order_release);
        if (!nk::core::handles().erase(clip, nk::core::ResourceType::audio_clip))
            return invalid_handle("invalid audio clip handle");
        return NK_OK;
    });
}

nk_result NK_CALL nk_audio_clip_get_load_state(nk_audio_clip clip,
                                               nk_audio_clip_load_state *out_state) {
    return nk::core::result_boundary(
        "unexpected error while querying an audio clip load state", [&]() -> nk_result {
            if (const auto result = enter_audio_ui(); result != NK_OK)
                return result;
            if (!out_state)
                return invalid_argument("audio clip load state output is missing");
            auto value = get_clip(clip);
            if (!value)
                return NK_ERROR_INVALID_HANDLE;
            audio_clip_publish_load_event(*value);
            *out_state = value->load_state.load(std::memory_order_acquire);
            return NK_OK;
        });
}

nk_result NK_CALL nk_audio_voice_create(nk_audio_clip clip, const nk_audio_voice_options *options,
                                        nk_audio_voice *out_voice) {
    return nk::core::result_boundary(
        "unexpected error while creating an audio clip voice", [&]() -> nk_result {
            if (const auto result = enter_audio_ui(); result != NK_OK)
                return result;
            if (!out_voice)
                return invalid_argument("audio voice output is missing");
            *out_voice = NK_INVALID_HANDLE;
            auto source = get_clip(clip);
            if (!source)
                return NK_ERROR_INVALID_HANDLE;
            nk_result voice_result = NK_OK;
            auto voice = create_voice_from_clip(std::move(source), options, voice_result);
            if (!voice)
                return voice_result;
            return insert_voice(std::move(voice), out_voice);
        });
}

nk_result NK_CALL nk_audio_voice_destroy(nk_audio_voice sound) {
    return nk::core::result_boundary("unexpected error while destroying an audio voice",
                                     [&]() -> nk_result {
        if (const auto result = enter_audio_ui(); result != NK_OK)
            return result;
        auto voice = get_voice(sound);
        if (!voice)
            return NK_ERROR_INVALID_HANDLE;
        voice->handle.store(NK_INVALID_HANDLE, std::memory_order_release);
        if (!nk::core::handles().erase(sound, nk::core::ResourceType::audio_voice))
            return invalid_handle("invalid audio voice handle");
        return NK_OK;
    });
}

nk_result NK_CALL nk_audio_voice_start(nk_audio_voice sound) {
    return nk::core::result_boundary("unexpected error while starting an audio voice", [&]() {
        if (const auto result = enter_audio_ui(); result != NK_OK)
            return result;
        return with_voice(sound, "could not start audio voice", [](AudioVoiceResource &value,
                                                                  const char *message) {
            return map_miniaudio_result(ma_sound_start(&value.sound), message);
        });
    });
}

nk_result NK_CALL nk_audio_voice_stop(nk_audio_voice sound) {
    return nk::core::result_boundary("unexpected error while stopping an audio voice", [&]() {
        if (const auto result = enter_audio_ui(); result != NK_OK)
            return result;
        return with_voice(sound, "could not stop audio voice", [](AudioVoiceResource &value,
                                                                 const char *message) {
            return map_miniaudio_result(ma_sound_stop(&value.sound), message);
        });
    });
}

nk_result NK_CALL nk_audio_voice_rewind(nk_audio_voice sound) {
    return nk::core::result_boundary("unexpected error while rewinding an audio voice", [&]() {
        if (const auto result = enter_audio_ui(); result != NK_OK)
            return result;
        return with_voice(sound, "could not rewind audio voice", [](AudioVoiceResource &value,
                                                                    const char *message) {
            return map_miniaudio_result(ma_sound_seek_to_pcm_frame(&value.sound, 0), message);
        });
    });
}

nk_result NK_CALL nk_audio_voice_schedule_start(nk_audio_voice sound,
                                                 uint64_t absolute_time_pcm_frames) {
    return nk::core::result_boundary(
        "unexpected error while scheduling an audio voice start", [&]() {
            if (const auto result = enter_audio_ui(); result != NK_OK)
                return result;
            return with_voice(sound, "could not schedule audio voice start",
                              [&](AudioVoiceResource &value, const char *) {
                                  ma_sound_set_start_time_in_pcm_frames(
                                      &value.sound, absolute_time_pcm_frames);
                                  return NK_OK;
                              });
        });
}

nk_result NK_CALL nk_audio_voice_schedule_stop(nk_audio_voice sound,
                                                uint64_t absolute_time_pcm_frames) {
    return nk::core::result_boundary(
        "unexpected error while scheduling an audio voice stop", [&]() {
            if (const auto result = enter_audio_ui(); result != NK_OK)
                return result;
            return with_voice(sound, "could not schedule audio voice stop",
                              [&](AudioVoiceResource &value, const char *) {
                                  ma_sound_set_stop_time_in_pcm_frames(
                                      &value.sound, absolute_time_pcm_frames);
                                  return NK_OK;
                              });
        });
}

nk_result NK_CALL nk_audio_voice_clear_schedule(nk_audio_voice sound) {
    return nk::core::result_boundary(
        "unexpected error while clearing an audio voice schedule", [&]() {
            if (const auto result = enter_audio_ui(); result != NK_OK)
                return result;
            return with_voice(sound, "could not clear audio voice schedule",
                              [&](AudioVoiceResource &value, const char *) {
                                  ma_sound_reset_start_time(&value.sound);
                                  ma_sound_reset_stop_time_and_fade(&value.sound);
                                  return NK_OK;
                              });
        });
}

nk_result NK_CALL nk_audio_voice_fade(nk_audio_voice sound, float volume_begin,
                                      float volume_end, uint64_t duration_pcm_frames) {
    return nk::core::result_boundary("unexpected error while fading an audio voice", [&]() {
        if (const auto result = enter_audio_ui(); result != NK_OK)
            return result;
        if (!valid_fade_volume(volume_begin, true) || !valid_fade_volume(volume_end, false))
            return invalid_argument(
                "audio voice fade volumes must be finite and non-negative; the start may be "
                "NK_AUDIO_VOLUME_CURRENT");
        return with_voice(sound, "could not fade audio voice", [&](AudioVoiceResource &value,
                                                                     const char *) {
            ma_sound_set_fade_in_pcm_frames(&value.sound, volume_begin, volume_end,
                                            duration_pcm_frames);
            return NK_OK;
        });
    });
}

nk_result NK_CALL nk_audio_voice_fade_at(nk_audio_voice sound, float volume_begin,
                                         float volume_end, uint64_t duration_pcm_frames,
                                         uint64_t absolute_start_time_pcm_frames) {
    return nk::core::result_boundary(
        "unexpected error while scheduling an audio voice fade", [&]() {
            if (const auto result = enter_audio_ui(); result != NK_OK)
                return result;
            if (!valid_fade_volume(volume_begin, true) || !valid_fade_volume(volume_end, false))
                return invalid_argument(
                    "audio voice fade volumes must be finite and non-negative; the start may be "
                    "NK_AUDIO_VOLUME_CURRENT");
            return with_voice(sound, "could not schedule audio voice fade",
                              [&](AudioVoiceResource &value, const char *) {
                                  ma_sound_set_fade_start_in_pcm_frames(
                                      &value.sound, volume_begin, volume_end, duration_pcm_frames,
                                      absolute_start_time_pcm_frames);
                                  return NK_OK;
                              });
        });
}

nk_result NK_CALL nk_audio_voice_is_playing(nk_audio_voice sound, nk_bool *out_playing) {
    return nk::core::result_boundary(
        "unexpected error while querying an audio voice", [&]() -> nk_result {
            if (const auto result = enter_audio_ui(); result != NK_OK)
                return result;
            if (!out_playing)
                return invalid_argument("audio playing output is missing");
            return with_voice(sound, "could not query audio voice", [&](AudioVoiceResource &value,
                                                                         const char *) {
                *out_playing = ma_sound_is_playing(&value.sound) ? 1u : 0u;
                return NK_OK;
            });
        });
}

nk_result NK_CALL nk_audio_voice_get_load_state(nk_audio_voice sound,
                                                nk_audio_voice_load_state *out_state) {
    return nk::core::result_boundary(
        "unexpected error while querying an audio voice load state", [&]() -> nk_result {
            if (const auto result = enter_audio_ui(); result != NK_OK)
                return result;
            if (!out_state)
                return invalid_argument("audio voice load state output is missing");
            return with_voice(sound, "could not query audio voice load state",
                              [&](AudioVoiceResource &value, const char *) {
                                  audio_voice_update_load_state(value);
                                  *out_state = value.load_state.load(std::memory_order_acquire);
                                  return NK_OK;
                              });
        });
}

nk_result NK_CALL nk_audio_voice_at_end(nk_audio_voice sound, nk_bool *out_at_end) {
    return nk::core::result_boundary(
        "unexpected error while querying an audio voice end state", [&]() -> nk_result {
            if (const auto result = enter_audio_ui(); result != NK_OK)
                return result;
            if (!out_at_end)
                return invalid_argument("audio end-state output is missing");
            return with_voice(sound, "could not query audio voice end state",
                              [&](AudioVoiceResource &value, const char *) {
                                  *out_at_end = ma_sound_at_end(&value.sound) ? 1u : 0u;
                                  return NK_OK;
                              });
        });
}

nk_result NK_CALL nk_audio_voice_set_volume(nk_audio_voice sound, float volume) {
    return nk::core::result_boundary("unexpected error while setting audio voice volume", [&]() {
        if (const auto result = enter_audio_ui(); result != NK_OK)
            return result;
        if (!std::isfinite(volume) || volume < 0)
            return invalid_argument("audio voice volume must be finite and non-negative");
        return with_voice(sound, "could not set audio voice volume", [&](AudioVoiceResource &value,
                                                                          const char *) {
            ma_sound_set_volume(&value.sound, volume);
            return NK_OK;
        });
    });
}

nk_result NK_CALL nk_audio_voice_get_volume(nk_audio_voice sound, float *out_volume) {
    return nk::core::result_boundary("unexpected error while getting audio voice volume", [&]() {
        if (const auto result = enter_audio_ui(); result != NK_OK)
            return result;
        if (!out_volume)
            return invalid_argument("audio voice volume output is missing");
        return with_voice(sound, "could not get audio voice volume", [&](AudioVoiceResource &value,
                                                                          const char *) {
            *out_volume = ma_sound_get_volume(&value.sound);
            return NK_OK;
        });
    });
}

nk_result NK_CALL nk_audio_voice_set_pan(nk_audio_voice sound, float pan) {
    return nk::core::result_boundary("unexpected error while setting audio voice pan", [&]() {
        if (const auto result = enter_audio_ui(); result != NK_OK)
            return result;
        if (!std::isfinite(pan) || pan < -1.0f || pan > 1.0f)
            return invalid_argument("audio voice pan must be finite and in the range [-1, 1]");
        return with_voice(sound, "could not set audio voice pan", [&](AudioVoiceResource &value,
                                                                       const char *) {
            ma_sound_set_pan(&value.sound, pan);
            return NK_OK;
        });
    });
}

nk_result NK_CALL nk_audio_voice_get_pan(nk_audio_voice sound, float *out_pan) {
    return nk::core::result_boundary("unexpected error while getting audio voice pan", [&]() {
        if (const auto result = enter_audio_ui(); result != NK_OK)
            return result;
        if (!out_pan)
            return invalid_argument("audio voice pan output is missing");
        return with_voice(sound, "could not get audio voice pan", [&](AudioVoiceResource &value,
                                                                       const char *) {
            *out_pan = ma_sound_get_pan(&value.sound);
            return NK_OK;
        });
    });
}

nk_result NK_CALL nk_audio_voice_set_pitch(nk_audio_voice sound, float pitch) {
    return nk::core::result_boundary("unexpected error while setting audio voice pitch", [&]() {
        if (const auto result = enter_audio_ui(); result != NK_OK)
            return result;
        if (!std::isfinite(pitch) || pitch <= 0)
            return invalid_argument("audio voice pitch must be finite and positive");
        return with_voice(sound, "could not set audio voice pitch", [&](AudioVoiceResource &value,
                                                                        const char *) {
            ma_sound_set_pitch(&value.sound, pitch);
            return NK_OK;
        });
    });
}

nk_result NK_CALL nk_audio_voice_get_pitch(nk_audio_voice sound, float *out_pitch) {
    return nk::core::result_boundary("unexpected error while getting audio voice pitch", [&]() {
        if (const auto result = enter_audio_ui(); result != NK_OK)
            return result;
        if (!out_pitch)
            return invalid_argument("audio voice pitch output is missing");
        return with_voice(sound, "could not get audio voice pitch", [&](AudioVoiceResource &value,
                                                                         const char *) {
            *out_pitch = ma_sound_get_pitch(&value.sound);
            return NK_OK;
        });
    });
}

nk_result NK_CALL nk_audio_voice_set_looping(nk_audio_voice sound, nk_bool looping) {
    return nk::core::result_boundary("unexpected error while setting audio voice looping", [&]() {
        if (const auto result = enter_audio_ui(); result != NK_OK)
            return result;
        if (looping > 1)
            return invalid_argument("audio voice looping must be zero or one");
        return with_voice(sound, "could not set audio voice looping", [&](AudioVoiceResource &value,
                                                                           const char *) {
            ma_sound_set_looping(&value.sound, looping != 0 ? MA_TRUE : MA_FALSE);
            return NK_OK;
        });
    });
}

nk_result NK_CALL nk_audio_voice_is_looping(nk_audio_voice sound, nk_bool *out_looping) {
    return nk::core::result_boundary(
        "unexpected error while querying audio voice looping", [&]() -> nk_result {
            if (const auto result = enter_audio_ui(); result != NK_OK)
                return result;
            if (!out_looping)
                return invalid_argument("audio looping output is missing");
            return with_voice(sound, "could not query audio voice looping",
                              [&](AudioVoiceResource &value, const char *) {
                                  *out_looping = ma_sound_is_looping(&value.sound) ? 1u : 0u;
                                  return NK_OK;
                              });
        });
}

nk_result NK_CALL nk_audio_voice_get_time_seconds(nk_audio_voice sound, float *out_seconds) {
    return nk::core::result_boundary(
        "unexpected error while getting audio voice time", [&]() -> nk_result {
            if (const auto result = enter_audio_ui(); result != NK_OK)
                return result;
            if (!out_seconds)
                return invalid_argument("audio time output is missing");
            return with_voice(sound, "could not get audio voice time",
                              [&](AudioVoiceResource &value, const char *message) {
                                  return map_miniaudio_result(
                                      ma_sound_get_cursor_in_seconds(&value.sound, out_seconds),
                                      message);
                              });
        });
}

nk_result NK_CALL nk_audio_voice_get_length_seconds(nk_audio_voice sound, float *out_seconds) {
    return nk::core::result_boundary(
        "unexpected error while getting audio voice length", [&]() -> nk_result {
            if (const auto result = enter_audio_ui(); result != NK_OK)
                return result;
            if (!out_seconds)
                return invalid_argument("audio length output is missing");
            return with_voice(sound, "could not get audio voice length",
                              [&](AudioVoiceResource &value, const char *message) {
                                  return map_miniaudio_result(
                                      ma_sound_get_length_in_seconds(&value.sound, out_seconds),
                                      message);
                              });
    });
}

nk_result NK_CALL nk_audio_get_time_pcm_frames(uint64_t *out_time_pcm_frames) {
    return nk::core::result_boundary(
        "unexpected error while getting the audio engine clock", [&]() -> nk_result {
            if (const auto result = enter_audio_ui(); result != NK_OK)
                return result;
            if (!out_time_pcm_frames)
                return invalid_argument("audio engine clock output is missing");
            nk_result engine_result = NK_OK;
            auto engine = ensure_engine(engine_result);
            if (!engine)
                return engine_result;
            *out_time_pcm_frames = ma_engine_get_time_in_pcm_frames(&engine->engine);
            return NK_OK;
        });
}

nk_result NK_CALL nk_audio_get_sample_rate(uint32_t *out_sample_rate) {
    return nk::core::result_boundary(
        "unexpected error while getting the audio engine sample rate", [&]() -> nk_result {
            if (const auto result = enter_audio_ui(); result != NK_OK)
                return result;
            if (!out_sample_rate)
                return invalid_argument("audio engine sample rate output is missing");
            nk_result engine_result = NK_OK;
            auto engine = ensure_engine(engine_result);
            if (!engine)
                return engine_result;
            *out_sample_rate = ma_engine_get_sample_rate(&engine->engine);
            return NK_OK;
        });
}

nk_result NK_CALL nk_audio_set_master_volume(float volume) {
    return nk::core::result_boundary(
        "unexpected error while setting master audio volume", [&]() -> nk_result {
        if (const auto result = enter_audio_ui(); result != NK_OK)
            return result;
        if (!std::isfinite(volume) || volume < 0)
            return invalid_argument("master audio volume must be finite and non-negative");
        nk_result engine_result = NK_OK;
        auto engine = ensure_engine(engine_result);
        if (!engine)
            return engine_result;
        return map_miniaudio_result(ma_engine_set_volume(&engine->engine, volume),
                                     "could not set master audio volume");
    });
}

nk_result NK_CALL nk_audio_get_master_volume(float *out_volume) {
    return nk::core::result_boundary(
        "unexpected error while getting master audio volume", [&]() -> nk_result {
        if (const auto result = enter_audio_ui(); result != NK_OK)
            return result;
        if (!out_volume)
            return invalid_argument("master audio volume output is missing");
        nk_result engine_result = NK_OK;
        auto engine = ensure_engine(engine_result);
        if (!engine)
            return engine_result;
        *out_volume = ma_engine_get_volume(&engine->engine);
        return NK_OK;
    });
}

} // extern "C"
