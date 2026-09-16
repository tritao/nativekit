#include "nativekit_audio_graph.h"

#include "core/boundary.hpp"
#include "core/error.hpp"
#include "core/event_queue.hpp"
#include "core/handle_registry.hpp"
#include "core/resource_cache.hpp"
#include "core/runtime.hpp"
#include "core/worker_pool.hpp"

#include "miniaudio.h"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <condition_variable>
#include <limits>
#include <memory>
#include <mutex>
#include <new>
#include <string>
#include <vector>

namespace {

constexpr uint32_t supported_voice_flags = NK_AUDIO_VOICE_LOOPING | NK_AUDIO_VOICE_STREAM |
                                           NK_AUDIO_VOICE_ASYNC;

struct AudioEngineResource;
std::atomic<AudioEngineResource *> active_engine_resource{nullptr};
std::mutex engine_mutex;

struct PendingAudioDeviceConfig {
    uint32_t playback_device_index = NK_AUDIO_DEVICE_DEFAULT;
    uint32_t sample_rate = 0;
    uint32_t channels = 0;
    uint32_t period_size_in_frames = 0;
    uint32_t period_size_in_milliseconds = 0;
    bool no_auto_start = false;
    bool has_playback_device_id = false;
    ma_device_id playback_device_id{};
};

PendingAudioDeviceConfig pending_device_config;

void audio_device_notification_callback(const ma_device_notification *notification) noexcept;

struct AudioEngineResource final : nk::core::Resource {
    ma_engine engine{};
    bool initialized = false;
    std::atomic<bool> interrupted{false};
    std::vector<std::weak_ptr<nk::core::Resource>> voices;

    ~AudioEngineResource() override {
        auto *expected = this;
        active_engine_resource.compare_exchange_strong(expected, nullptr,
                                                        std::memory_order_acq_rel);
        if (initialized)
            ma_engine_uninit(&engine);
    }
};

struct AudioBusResource;
struct AudioEffectResource;

void audio_effect_uninitialize(AudioEffectResource &effect) noexcept;

struct AudioClipResource final : nk::core::Resource {
    std::shared_ptr<AudioEngineResource> engine;
    std::string path;
    std::string resource_uri;
    nk_resource_flags resource_flags = NK_RESOURCE_READABLE;
    nk::core::ResourceAssetBytes encoded_data;
    bool resource_stream = false;
    std::atomic<nk_audio_clip> handle{NK_INVALID_HANDLE};

    ~AudioClipResource() override {
        handle.store(NK_INVALID_HANDLE, std::memory_order_release);
    }
};

struct AudioBusResource final : nk::core::Resource {
    std::shared_ptr<AudioEngineResource> engine;
    std::shared_ptr<AudioBusResource> parent;
    ma_sound_group group{};
    bool initialized = false;
    float volume = 1.0f;
    bool muted = false;
    uint32_t max_voices = 0;
    nk_audio_voice_steal_policy steal_policy = NK_AUDIO_VOICE_STEAL_NONE;
    bool virtualize = false;
    std::vector<std::shared_ptr<AudioEffectResource>> effects;
    std::atomic<nk_audio_bus> handle{NK_INVALID_HANDLE};

    ~AudioBusResource() override;
};

struct AudioMixSnapshotTarget {
    std::weak_ptr<AudioBusResource> bus;
    float volume = 1.0f;
    bool muted = false;
};

struct AudioMixSnapshotResource final : nk::core::Resource {
    std::shared_ptr<AudioEngineResource> engine;
    std::vector<AudioMixSnapshotTarget> targets;
    std::atomic<nk_audio_mix_snapshot> handle{NK_INVALID_HANDLE};
};

struct AudioEffectResource final : nk::core::Resource {
    std::shared_ptr<AudioEngineResource> engine;
    std::weak_ptr<AudioBusResource> bus;
    nk_audio_bus_effect handle = NK_INVALID_HANDLE;
    nk_audio_effect_type type = NK_AUDIO_EFFECT_LOW_PASS;
    bool initialized = false;
    bool enabled = true;
    std::unique_ptr<ma_lpf_node> low_pass;
    std::unique_ptr<ma_hpf_node> high_pass;
    std::unique_ptr<ma_delay_node> delay;
    float cutoff_frequency_hz = 0.0f;
    uint32_t order = 0;
    uint32_t delay_pcm_frames = 0;
    float decay = 0.0f;

    ~AudioEffectResource() override {
        audio_effect_uninitialize(*this);
    }
};

struct AudioVoiceResource;
struct AudioResourceReader {
    nk_resource_stream stream = NK_INVALID_HANDLE;
    AudioVoiceResource *voice = nullptr;
};

struct AudioStreamingSource;

struct AudioVoiceLoadNotification {
    ma_async_notification_callbacks callbacks{};
    AudioVoiceResource *voice = nullptr;
};

void audio_voice_end_callback(void *user_data, ma_sound *sound) noexcept;
void audio_voice_load_callback(ma_async_notification *notification) noexcept;
void audio_voice_update_load_state(AudioVoiceResource &voice) noexcept;
void audio_voice_publish_load_event(AudioVoiceResource &voice) noexcept;
void publish_voice_event(AudioVoiceResource &voice, nk_event_kind kind) noexcept;
void audio_voice_report_stream_error(AudioVoiceResource &voice, nk_result result) noexcept;
nk_result miniaudio_result_code(ma_result result);

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
    std::atomic<nk_result> stream_error{NK_OK};
    std::atomic<bool> stream_error_emitted{false};
    std::atomic<bool> logically_playing{false};
    std::atomic<bool> virtualized{false};
    uint32_t priority = 0;
    uint64_t start_order = 0;
    uint64_t virtual_cursor_frames = 0;
    uint64_t virtual_start_time_frames = 0;
    AudioVoiceLoadNotification load_notification{};
    ma_decoder decoder{};
    bool decoder_initialized = false;
    std::unique_ptr<AudioStreamingSource> streaming_source;
    AudioResourceReader resource_reader{};
    ma_sound sound{};
    bool sound_initialized = false;

    ~AudioVoiceResource() override;
};

constexpr uint64_t no_stream_seek = std::numeric_limits<uint64_t>::max();

struct AudioStreamingSource final {
    ma_pcm_rb pcm{};
    ma_decoder *decoder = nullptr;
    AudioVoiceResource *voice = nullptr;
    ma_format format = ma_format_unknown;
    uint32_t channels = 0;
    uint32_t sample_rate = 0;
    uint64_t length = 0;
    bool length_known = false;
    bool initialized = false;
    std::atomic<bool> stop_requested{false};
    std::atomic<bool> seek_pending{false};
    std::atomic<uint64_t> requested_seek{no_stream_seek};
    std::atomic<bool> ended{false};
    std::atomic<bool> ready{false};
    std::atomic<ma_result> load_result{MA_BUSY};
    std::atomic<ma_result> read_result{MA_SUCCESS};
    std::atomic<uint64_t> cursor{0};
    std::atomic<uint32_t> pending_jobs{0};
    std::atomic<bool> job_scheduled{false};
    nk::core::WorkerTaskState decode_task;
    std::mutex worker_mutex;
    std::condition_variable worker_condition;

    ~AudioStreamingSource();

    ma_result initialize(ma_decoder *source_decoder, AudioVoiceResource *source_voice,
                         ma_format source_format, uint32_t source_channels,
                         uint32_t source_sample_rate, uint64_t source_length,
                         bool source_length_known);
    ma_result start();
    void stop() noexcept;
    ma_result schedule_decode() noexcept;
    void decode_job() noexcept;
    void job_finished() noexcept;
    void fail(ma_result result) noexcept;
};

ma_result streaming_source_read(ma_data_source *data_source, void *frames_out,
                                ma_uint64 frame_count, ma_uint64 *frames_read);
ma_result streaming_source_seek(ma_data_source *data_source, ma_uint64 frame_index);
ma_result streaming_source_get_data_format(ma_data_source *data_source, ma_format *format,
                                           ma_uint32 *channels, ma_uint32 *sample_rate,
                                           ma_channel *channel_map, size_t channel_map_capacity);
ma_result streaming_source_get_cursor(ma_data_source *data_source, ma_uint64 *cursor);
ma_result streaming_source_get_length(ma_data_source *data_source, ma_uint64 *length);
ma_result streaming_source_set_looping(ma_data_source *, ma_bool32) {
    return MA_SUCCESS;
}

const ma_data_source_vtable streaming_source_vtable = {
    streaming_source_read,
    streaming_source_seek,
    streaming_source_get_data_format,
    streaming_source_get_cursor,
    streaming_source_get_length,
    streaming_source_set_looping,
    0};

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
    if (result != NK_OK) {
        if (reader->voice)
            audio_voice_report_stream_error(*reader->voice, result);
        return resource_result(result);
    }
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
    const auto result = nk_resource_seek(reader->stream, byte_offset, resource_origin, &position);
    if (result != NK_OK && reader->voice)
        audio_voice_report_stream_error(*reader->voice, result);
    return resource_result(result);
}

AudioStreamingSource *streaming_source(ma_data_source *data_source) {
    return reinterpret_cast<AudioStreamingSource *>(data_source);
}

ma_result streaming_source_read(ma_data_source *data_source, void *frames_out,
                                ma_uint64 frame_count, ma_uint64 *frames_read) {
    auto *source = streaming_source(data_source);
    if (!source || frame_count == 0)
        return MA_INVALID_ARGS;
    if (frames_read)
        *frames_read = 0;
    if (source->stop_requested.load(std::memory_order_acquire))
        return MA_INVALID_OPERATION;
    if (source->seek_pending.load(std::memory_order_acquire) ||
        source->requested_seek.load(std::memory_order_acquire) != no_stream_seek)
        return MA_BUSY;

    const auto failure = source->read_result.load(std::memory_order_acquire);
    if (failure != MA_SUCCESS)
        return failure;

    ma_uint64 total_read = 0;
    while (total_read < frame_count) {
        const auto remaining = frame_count - total_read;
        const auto requested = static_cast<ma_uint32>(
            std::min<ma_uint64>(remaining, std::numeric_limits<ma_uint32>::max()));
        auto available = requested;
        void *mapped = nullptr;
        const auto acquire_result = ma_pcm_rb_acquire_read(&source->pcm, &available, &mapped);
        if (acquire_result != MA_SUCCESS)
            return acquire_result;
        if (available == 0)
            break;

        if (frames_out) {
            auto *output = ma_offset_pcm_frames_ptr(frames_out, total_read, source->format,
                                                     source->channels);
            ma_copy_pcm_frames(output, mapped, available, source->format, source->channels);
        }
        const auto commit_result = ma_pcm_rb_commit_read(&source->pcm, available);
        if (commit_result != MA_SUCCESS)
            return commit_result;
        total_read += available;
        source->cursor.fetch_add(available, std::memory_order_release);
    }

    if (frames_read)
        *frames_read = total_read;
    if (total_read != 0) {
        const auto schedule_result = source->schedule_decode();
        if (schedule_result != MA_SUCCESS && schedule_result != MA_INVALID_OPERATION)
            source->fail(schedule_result);
        return MA_SUCCESS;
    }
    if (source->ended.load(std::memory_order_acquire))
        return MA_AT_END;
    const auto schedule_result = source->schedule_decode();
    if (schedule_result != MA_SUCCESS)
        return schedule_result;
    return MA_BUSY;
}

ma_result streaming_source_seek(ma_data_source *data_source, ma_uint64 frame_index) {
    auto *source = streaming_source(data_source);
    if (!source || source->stop_requested.load(std::memory_order_acquire))
        return MA_INVALID_OPERATION;
    source->seek_pending.store(true, std::memory_order_release);
    source->requested_seek.store(frame_index, std::memory_order_release);
    const auto result = source->schedule_decode();
    if (result != MA_SUCCESS) {
        source->seek_pending.store(false, std::memory_order_release);
        source->requested_seek.store(no_stream_seek, std::memory_order_release);
        if (result != MA_INVALID_OPERATION)
            source->fail(result);
    }
    return result;
}

ma_result streaming_source_get_data_format(ma_data_source *data_source, ma_format *format,
                                           ma_uint32 *channels, ma_uint32 *sample_rate,
                                           ma_channel *channel_map, size_t channel_map_capacity) {
    const auto *source = streaming_source(data_source);
    if (!source)
        return MA_INVALID_ARGS;
    if (format)
        *format = source->format;
    if (channels)
        *channels = source->channels;
    if (sample_rate)
        *sample_rate = source->sample_rate;
    if (channel_map)
        ma_channel_map_init_standard(ma_standard_channel_map_default, channel_map,
                                     channel_map_capacity, source->channels);
    return MA_SUCCESS;
}

ma_result streaming_source_get_cursor(ma_data_source *data_source, ma_uint64 *cursor) {
    const auto *source = streaming_source(data_source);
    if (!source || !cursor)
        return MA_INVALID_ARGS;
    *cursor = source->cursor.load(std::memory_order_acquire);
    return MA_SUCCESS;
}

ma_result streaming_source_get_length(ma_data_source *data_source, ma_uint64 *length) {
    const auto *source = streaming_source(data_source);
    if (!source || !length)
        return MA_INVALID_ARGS;
    if (!source->length_known)
        return MA_NOT_IMPLEMENTED;
    *length = source->length;
    return MA_SUCCESS;
}

ma_result AudioStreamingSource::initialize(ma_decoder *source_decoder,
                                           AudioVoiceResource *source_voice,
                                           ma_format source_format, uint32_t source_channels,
                                           uint32_t source_sample_rate, uint64_t source_length,
                                           bool source_length_known) {
    if (!source_decoder || !source_voice || source_format == ma_format_unknown ||
        source_channels == 0 || source_sample_rate == 0)
        return MA_INVALID_ARGS;

    decoder = source_decoder;
    voice = source_voice;
    format = source_format;
    channels = source_channels;
    sample_rate = source_sample_rate;
    length = source_length;
    length_known = source_length_known;

    const auto ring_frame_count = std::min<uint64_t>(
        std::max<uint64_t>(static_cast<uint64_t>(sample_rate) * 2, 4096),
        std::numeric_limits<ma_uint32>::max());
    const auto result = ma_pcm_rb_init(format, channels, static_cast<ma_uint32>(ring_frame_count),
                                       nullptr, nullptr, &pcm);
    if (result != MA_SUCCESS)
        return result;
    const auto worker_result = nk::core::initialize_worker_task(
        decode_task, [this] { decode_job(); }, [this] { job_finished(); });
    if (worker_result != NK_OK) {
        ma_pcm_rb_uninit(&pcm);
        return resource_result(worker_result);
    }
    pcm.ds.vtable = &streaming_source_vtable;
    initialized = true;
    return MA_SUCCESS;
}

ma_result AudioStreamingSource::start() {
    if (!initialized || !decoder || job_scheduled.load(std::memory_order_acquire))
        return MA_INVALID_OPERATION;
    stop_requested.store(false, std::memory_order_release);
    return schedule_decode();
}

void AudioStreamingSource::stop() noexcept {
    stop_requested.store(true, std::memory_order_release);
    worker_condition.notify_all();
    std::unique_lock lock(worker_mutex);
    worker_condition.wait(lock, [this] {
        return pending_jobs.load(std::memory_order_acquire) == 0;
    });
}

ma_result AudioStreamingSource::schedule_decode() noexcept {
    if (stop_requested.load(std::memory_order_acquire))
        return MA_INVALID_OPERATION;

    bool expected = false;
    if (!job_scheduled.compare_exchange_strong(expected, true, std::memory_order_acq_rel))
        return MA_SUCCESS;

    pending_jobs.fetch_add(1, std::memory_order_acq_rel);
    const auto submit_result = nk::core::schedule_worker_task(decode_task);
    if (submit_result == NK_OK)
        return MA_SUCCESS;

    job_scheduled.store(false, std::memory_order_release);
    pending_jobs.fetch_sub(1, std::memory_order_acq_rel);
    worker_condition.notify_all();
    return resource_result(submit_result);
}

void AudioStreamingSource::fail(ma_result result) noexcept {
    if (result == MA_SUCCESS)
        return;
    read_result.store(result, std::memory_order_release);
    ended.store(true, std::memory_order_release);
    if (!ready.load(std::memory_order_acquire))
        load_result.store(result, std::memory_order_release);
    if (result != MA_AT_END)
        audio_voice_report_stream_error(*voice, miniaudio_result_code(result));
    audio_voice_update_load_state(*voice);
}

void AudioStreamingSource::decode_job() noexcept {
    if (stop_requested.load(std::memory_order_acquire))
        return;

    if (seek_pending.load(std::memory_order_acquire) ||
        requested_seek.load(std::memory_order_acquire) != no_stream_seek) {
        const auto target = requested_seek.exchange(no_stream_seek, std::memory_order_acq_rel);
        if (target == no_stream_seek) {
            seek_pending.store(false, std::memory_order_release);
            return;
        }
        ma_pcm_rb_reset(&pcm);
        ended.store(false, std::memory_order_release);
        read_result.store(MA_SUCCESS, std::memory_order_release);
        const auto result = ma_decoder_seek_to_pcm_frame(decoder, target);
        if (result != MA_SUCCESS) {
            fail(result);
        } else {
            cursor.store(target, std::memory_order_release);
            seek_pending.store(false, std::memory_order_release);
        }
        if (requested_seek.load(std::memory_order_acquire) == no_stream_seek)
            seek_pending.store(false, std::memory_order_release);
        return;
    }

    if (ended.load(std::memory_order_acquire))
        return;

    auto writable = ma_pcm_rb_available_write(&pcm);
    if (writable == 0)
        return;

    void *mapped = nullptr;
    const auto acquire_result = ma_pcm_rb_acquire_write(&pcm, &writable, &mapped);
    if (acquire_result != MA_SUCCESS) {
        fail(acquire_result);
        return;
    }
    ma_uint64 decoded = 0;
    const auto result = ma_decoder_read_pcm_frames(decoder, mapped, writable, &decoded);
    if (decoded != 0) {
        const auto commit_result = ma_pcm_rb_commit_write(&pcm, static_cast<ma_uint32>(decoded));
        if (commit_result != MA_SUCCESS) {
            fail(commit_result);
            return;
        }
    }

    if (decoded != 0 && !ready.exchange(true, std::memory_order_acq_rel)) {
        load_result.store(MA_SUCCESS, std::memory_order_release);
        audio_voice_update_load_state(*voice);
    }

    if (result == MA_SUCCESS && decoded != 0)
        return;

    if (result != MA_SUCCESS && result != MA_AT_END) {
        fail(result);
    } else if (decoded == 0) {
        if (!ready.load(std::memory_order_acquire))
            load_result.store(MA_AT_END, std::memory_order_release);
        ended.store(true, std::memory_order_release);
        audio_voice_update_load_state(*voice);
    } else {
        ended.store(true, std::memory_order_release);
        audio_voice_update_load_state(*voice);
    }
}

void AudioStreamingSource::job_finished() noexcept {
    job_scheduled.store(false, std::memory_order_release);
    pending_jobs.fetch_sub(1, std::memory_order_acq_rel);
    worker_condition.notify_all();
    if (stop_requested.load(std::memory_order_acquire) ||
        ended.load(std::memory_order_acquire))
        return;

    const bool seek_requested = seek_pending.load(std::memory_order_acquire) ||
                                requested_seek.load(std::memory_order_acquire) != no_stream_seek;
    if (!seek_requested && ma_pcm_rb_available_write(&pcm) == 0)
        return;
    const auto result = schedule_decode();
    if (result != MA_SUCCESS && result != MA_INVALID_OPERATION)
        fail(result);
}

AudioStreamingSource::~AudioStreamingSource() {
    stop();
    if (initialized)
        ma_pcm_rb_uninit(&pcm);
}

AudioVoiceResource::~AudioVoiceResource() {
    handle.store(NK_INVALID_HANDLE, std::memory_order_release);
    if (sound_initialized)
        ma_sound_uninit(&sound);
    streaming_source.reset();
    if (decoder_initialized)
        ma_decoder_uninit(&decoder);
    if (resource_reader.stream != NK_INVALID_HANDLE)
        nk_resource_close(resource_reader.stream);
}

void audio_voice_end_callback(void *user_data, ma_sound *) noexcept {
    auto *voice = static_cast<AudioVoiceResource *>(user_data);
    if (!voice)
        return;
    voice->logically_playing.store(false, std::memory_order_release);
    voice->virtualized.store(false, std::memory_order_release);
    publish_voice_event(*voice, NK_EVENT_AUDIO_VOICE_COMPLETE);
}
std::weak_ptr<AudioEngineResource> engine_resource;
std::atomic<uint64_t> next_voice_order{1};

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

struct EnumeratedAudioDevice {
    ma_device_id id{};
    std::string name;
    bool is_default = false;
};

nk_result enumerate_audio_devices(std::vector<EnumeratedAudioDevice> &devices) {
    ma_context context{};
    auto result = ma_context_init(nullptr, 0, nullptr, &context);
    if (result != MA_SUCCESS)
        return map_miniaudio_result(result, "could not enumerate audio playback devices");

    ma_device_info *playback_infos = nullptr;
    ma_uint32 playback_count = 0;
    result = ma_context_get_devices(&context, &playback_infos, &playback_count, nullptr, nullptr);
    if (result != MA_SUCCESS) {
        ma_context_uninit(&context);
        return map_miniaudio_result(result, "could not enumerate audio playback devices");
    }

    devices.reserve(playback_count);
    for (ma_uint32 index = 0; index < playback_count; ++index) {
        EnumeratedAudioDevice device;
        device.id = playback_infos[index].id;
        device.name = playback_infos[index].name;
        device.is_default = playback_infos[index].isDefault == MA_TRUE;
        devices.push_back(std::move(device));
    }

    result = ma_context_uninit(&context);
    return result == MA_SUCCESS
               ? NK_OK
               : map_miniaudio_result(result, "could not finish enumerating audio devices");
}

nk_result copy_audio_string(const std::string &value, char *buffer, uint32_t *inout_size) {
    if (!inout_size) {
        nk::core::set_error("audio device name size output is missing");
        return NK_ERROR_INVALID_ARGUMENT;
    }
    const auto required = static_cast<uint32_t>(value.size() + 1);
    if (!buffer || *inout_size < required) {
        *inout_size = required;
        return NK_ERROR_BUFFER_TOO_SMALL;
    }
    std::memcpy(buffer, value.c_str(), required);
    *inout_size = required;
    return NK_OK;
}

void audio_device_notification_callback(const ma_device_notification *notification) noexcept {
    if (!notification || !notification->pDevice)
        return;

    auto *engine = static_cast<ma_engine *>(notification->pDevice->pUserData);
    auto *resource = active_engine_resource.load(std::memory_order_acquire);
    if (!resource || &resource->engine != engine)
        return;

    nk_event_kind event_kind = NK_EVENT_NONE;
    switch (notification->type) {
    case ma_device_notification_type_started:
        resource->interrupted.store(false, std::memory_order_release);
        event_kind = NK_EVENT_AUDIO_DEVICE_STARTED;
        break;
    case ma_device_notification_type_stopped:
        event_kind = NK_EVENT_AUDIO_DEVICE_STOPPED;
        break;
    case ma_device_notification_type_rerouted:
        event_kind = NK_EVENT_AUDIO_DEVICE_REROUTED;
        break;
    case ma_device_notification_type_interruption_began:
        resource->interrupted.store(true, std::memory_order_release);
        event_kind = NK_EVENT_AUDIO_DEVICE_INTERRUPTION_BEGAN;
        break;
    case ma_device_notification_type_interruption_ended:
        resource->interrupted.store(false, std::memory_order_release);
        event_kind = NK_EVENT_AUDIO_DEVICE_INTERRUPTION_ENDED;
        break;
    case ma_device_notification_type_unlocked:
        break;
    }

    if (event_kind == NK_EVENT_NONE)
        return;
    nk::core::QueuedEvent event;
    event.kind = event_kind;
    event.source = NK_INVALID_HANDLE;
    nk::core::push_event(std::move(event));
}

bool valid_audio_filter(float cutoff_frequency_hz, uint32_t order,
                        const AudioEngineResource &engine);

void audio_effect_uninitialize(AudioEffectResource &effect) noexcept {
    if (!effect.initialized)
        return;
    if (effect.low_pass)
        ma_lpf_node_uninit(effect.low_pass.get(), &effect.engine->engine.allocationCallbacks);
    if (effect.high_pass)
        ma_hpf_node_uninit(effect.high_pass.get(), &effect.engine->engine.allocationCallbacks);
    if (effect.delay)
        ma_delay_node_uninit(effect.delay.get(), &effect.engine->engine.allocationCallbacks);
    effect.low_pass.reset();
    effect.high_pass.reset();
    effect.delay.reset();
    effect.initialized = false;
}

AudioBusResource::~AudioBusResource() {
    for (auto &effect : effects) {
        effect->bus.reset();
        audio_effect_uninitialize(*effect);
    }
    effects.clear();
    if (initialized)
        ma_sound_group_uninit(&group);
}

ma_node *audio_effect_node(AudioEffectResource &effect) {
    switch (effect.type) {
    case NK_AUDIO_EFFECT_LOW_PASS:
        return effect.low_pass.get();
    case NK_AUDIO_EFFECT_HIGH_PASS:
        return effect.high_pass.get();
    case NK_AUDIO_EFFECT_DELAY:
        return effect.delay.get();
    default:
        return nullptr;
    }
}

nk_result rebuild_audio_bus_effect_chain(AudioBusResource &bus, const char *message) {
    auto result = ma_node_detach_all_output_buses(&bus.group);
    if (result != MA_SUCCESS)
        return map_miniaudio_result(result, message);
    for (const auto &effect : bus.effects) {
        result = ma_node_detach_all_output_buses(audio_effect_node(*effect));
        if (result != MA_SUCCESS)
            return map_miniaudio_result(result, message);
    }

    ma_node *source = &bus.group;
    for (const auto &effect : bus.effects) {
        if (!effect->enabled)
            continue;
        auto *node = audio_effect_node(*effect);
        result = ma_node_attach_output_bus(source, 0, node, 0);
        if (result != MA_SUCCESS)
            return map_miniaudio_result(result, message);
        source = node;
    }
    auto *destination = bus.parent ? static_cast<ma_node *>(&bus.parent->group)
                                   : ma_engine_get_endpoint(&bus.engine->engine);
    result = ma_node_attach_output_bus(source, 0, destination, 0);
    return result == MA_SUCCESS ? NK_OK : map_miniaudio_result(result, message);
}

nk_result reconfigure_audio_filter(AudioEffectResource &effect, AudioBusResource &bus,
                                   float cutoff_frequency_hz, uint32_t order) {
    if (!valid_audio_filter(cutoff_frequency_hz, order, *bus.engine))
        return invalid_argument(
            "audio bus filter cutoff must be below Nyquist and order must be in the range [1, 8]");

    auto *allocation_callbacks = &bus.engine->engine.allocationCallbacks;
    const auto channels = ma_engine_get_channels(&bus.engine->engine);
    const auto sample_rate = ma_engine_get_sample_rate(&bus.engine->engine);
    if (effect.type == NK_AUDIO_EFFECT_LOW_PASS) {
        const auto config = ma_lpf_node_config_init(channels, sample_rate, cutoff_frequency_hz,
                                                    order);
        if (effect.order == order) {
            const auto result = ma_lpf_node_reinit(&config.lpf, effect.low_pass.get());
            if (result != MA_SUCCESS)
                return map_miniaudio_result(result, "could not configure audio bus low-pass effect");
            effect.cutoff_frequency_hz = cutoff_frequency_hz;
            return NK_OK;
        }

        auto replacement = std::make_unique<ma_lpf_node>();
        auto result = ma_lpf_node_init(ma_engine_get_node_graph(&bus.engine->engine), &config,
                                       allocation_callbacks, replacement.get());
        if (result != MA_SUCCESS) {
            ma_lpf_uninit(&replacement->lpf, allocation_callbacks);
            return map_miniaudio_result(result, "could not configure audio bus low-pass effect");
        }
        auto previous = std::move(effect.low_pass);
        result = ma_node_detach_all_output_buses(previous.get());
        if (result != MA_SUCCESS) {
            ma_lpf_node_uninit(replacement.get(), allocation_callbacks);
            effect.low_pass = std::move(previous);
            return map_miniaudio_result(result, "could not reconfigure audio bus low-pass effect");
        }
        effect.low_pass = std::move(replacement);
        const auto chain_result = rebuild_audio_bus_effect_chain(
            bus, "could not reconfigure audio bus low-pass effect");
        if (chain_result != NK_OK) {
            auto failed = std::move(effect.low_pass);
            effect.low_pass = std::move(previous);
            rebuild_audio_bus_effect_chain(bus, "could not restore audio bus low-pass effect");
            ma_lpf_node_uninit(failed.get(), allocation_callbacks);
            return chain_result;
        }
        ma_lpf_node_uninit(previous.get(), allocation_callbacks);
    } else {
        const auto config = ma_hpf_node_config_init(channels, sample_rate, cutoff_frequency_hz,
                                                    order);
        if (effect.order == order) {
            const auto result = ma_hpf_node_reinit(&config.hpf, effect.high_pass.get());
            if (result != MA_SUCCESS)
                return map_miniaudio_result(result, "could not configure audio bus high-pass effect");
            effect.cutoff_frequency_hz = cutoff_frequency_hz;
            return NK_OK;
        }

        auto replacement = std::make_unique<ma_hpf_node>();
        auto result = ma_hpf_node_init(ma_engine_get_node_graph(&bus.engine->engine), &config,
                                       allocation_callbacks, replacement.get());
        if (result != MA_SUCCESS) {
            ma_hpf_uninit(&replacement->hpf, allocation_callbacks);
            return map_miniaudio_result(result, "could not configure audio bus high-pass effect");
        }
        auto previous = std::move(effect.high_pass);
        result = ma_node_detach_all_output_buses(previous.get());
        if (result != MA_SUCCESS) {
            ma_hpf_node_uninit(replacement.get(), allocation_callbacks);
            effect.high_pass = std::move(previous);
            return map_miniaudio_result(result, "could not reconfigure audio bus high-pass effect");
        }
        effect.high_pass = std::move(replacement);
        const auto chain_result = rebuild_audio_bus_effect_chain(
            bus, "could not reconfigure audio bus high-pass effect");
        if (chain_result != NK_OK) {
            auto failed = std::move(effect.high_pass);
            effect.high_pass = std::move(previous);
            rebuild_audio_bus_effect_chain(bus, "could not restore audio bus high-pass effect");
            ma_hpf_node_uninit(failed.get(), allocation_callbacks);
            return chain_result;
        }
        ma_hpf_node_uninit(previous.get(), allocation_callbacks);
    }
    effect.cutoff_frequency_hz = cutoff_frequency_hz;
    effect.order = order;
    return NK_OK;
}

constexpr float audio_full_angle_radians = 6.28318530717958647692f;

bool valid_audio_vec3(const nk_audio_vec3 &value) {
    return std::isfinite(value.x) && std::isfinite(value.y) && std::isfinite(value.z);
}

bool valid_audio_direction(const nk_audio_vec3 &value) {
    return valid_audio_vec3(value) && (value.x != 0.0f || value.y != 0.0f || value.z != 0.0f);
}

bool valid_audio_cone(float inner_angle_radians, float outer_angle_radians, float outer_gain) {
    return std::isfinite(inner_angle_radians) && std::isfinite(outer_angle_radians) &&
           std::isfinite(outer_gain) && inner_angle_radians >= 0.0f &&
           inner_angle_radians <= outer_angle_radians &&
           outer_angle_radians <= audio_full_angle_radians && outer_gain >= 0.0f &&
           outer_gain <= 1.0f;
}

bool valid_audio_gain_limits(float min_gain, float max_gain) {
    return std::isfinite(min_gain) && std::isfinite(max_gain) && min_gain >= 0.0f &&
           min_gain <= max_gain;
}

bool valid_audio_distance_limits(float min_distance, float max_distance) {
    return std::isfinite(min_distance) && std::isfinite(max_distance) && min_distance > 0.0f &&
           min_distance <= max_distance;
}

bool valid_audio_attenuation_model(nk_audio_attenuation_model model) {
    return model <= NK_AUDIO_ATTENUATION_EXPONENTIAL;
}

bool valid_audio_positioning(nk_audio_positioning positioning) {
    return positioning <= NK_AUDIO_POSITIONING_RELATIVE;
}

bool valid_audio_filter(float cutoff_frequency_hz, uint32_t order,
                        const AudioEngineResource &engine) {
    const auto sample_rate = ma_engine_get_sample_rate(&engine.engine);
    return std::isfinite(cutoff_frequency_hz) && cutoff_frequency_hz > 0.0f && order > 0 &&
           order <= MA_MAX_FILTER_ORDER && sample_rate > 0 &&
           static_cast<double>(cutoff_frequency_hz) < static_cast<double>(sample_rate) * 0.5;
}

bool valid_audio_delay(uint32_t delay_pcm_frames, float decay) {
    return delay_pcm_frames > 0 && std::isfinite(decay) && decay >= 0.0f && decay <= 1.0f;
}

bool valid_audio_delay_gain(float gain) {
    return std::isfinite(gain) && gain >= 0.0f && gain <= 1.0f;
}

nk_audio_vec3 audio_vec3(ma_vec3f value) {
    return {value.x, value.y, value.z};
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
        !voice.load_status_query_ready.load(std::memory_order_acquire))
        return;

    ma_result result = MA_BUSY;
    if (voice.clip && voice.clip->resource_stream) {
        if (!voice.streaming_source)
            return;
        result = voice.streaming_source->load_result.load(std::memory_order_acquire);
    } else {
        if (!voice.load_notification_signaled.load(std::memory_order_acquire))
            return;
        const auto *data_source = ma_sound_get_data_source(&voice.sound);
        if (!data_source)
            return;
        result = ma_resource_manager_data_source_result(
            reinterpret_cast<const ma_resource_manager_data_source *>(data_source));
    }
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

bool valid_voice_steal_policy(nk_audio_voice_steal_policy policy) {
    return policy <= NK_AUDIO_VOICE_STEAL_LOWEST_PRIORITY;
}

nk_result voice_options(const nk_audio_voice_options *options, uint32_t &flags) {
    flags = 0;
    if (!options)
        return NK_OK;
    if (options->struct_size < sizeof(nk_audio_voice_options))
        return invalid_argument("audio voice options are missing or too small");
    if (options->flags & ~supported_voice_flags)
        return invalid_argument("audio voice options contain unsupported flags");
    flags = options->flags;
    return NK_OK;
}

nk_result bus_options(const nk_audio_bus_options *options, nk_audio_bus &parent) {
    parent = NK_INVALID_HANDLE;
    if (!options)
        return NK_OK;
    if (options->struct_size < sizeof(nk_audio_bus_options))
        return invalid_argument("audio bus options are missing or too small");
    parent = options->parent;
    return NK_OK;
}

std::shared_ptr<AudioEngineResource> ensure_engine(nk_result &out_result) {
    out_result = NK_OK;
    std::lock_guard lock(engine_mutex);
    if (auto current = engine_resource.lock())
        return current;

    auto next = std::make_shared<AudioEngineResource>();
    auto device_config = pending_device_config;
    auto config = ma_engine_config_init();
    config.pPlaybackDeviceID = device_config.has_playback_device_id
                                   ? &device_config.playback_device_id
                                   : nullptr;
    config.sampleRate = device_config.sample_rate;
    config.channels = device_config.channels;
    config.periodSizeInFrames = device_config.period_size_in_frames;
    config.periodSizeInMilliseconds = device_config.period_size_in_milliseconds;
    config.noAutoStart = device_config.no_auto_start ? MA_TRUE : MA_FALSE;
    config.notificationCallback = audio_device_notification_callback;

    active_engine_resource.store(next.get(), std::memory_order_release);
    const auto result = ma_engine_init(&config, &next->engine);
    if (result != MA_SUCCESS) {
        auto *expected = next.get();
        active_engine_resource.compare_exchange_strong(expected, nullptr,
                                                        std::memory_order_acq_rel);
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

std::shared_ptr<AudioEngineResource> current_engine() {
    std::lock_guard lock(engine_mutex);
    return engine_resource.lock();
}

template <typename Function>
nk_result with_engine(const char *message, Function &&function) {
    nk_result engine_result = NK_OK;
    auto engine = ensure_engine(engine_result);
    if (!engine)
        return engine_result;
    return function(*engine, message);
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

std::shared_ptr<AudioClipResource> create_clip_from_asset(nk_resource_asset asset,
                                                          nk_result &out_result) {
    out_result = NK_OK;
    nk_result engine_result = NK_OK;
    auto engine = ensure_engine(engine_result);
    if (!engine) {
        out_result = engine_result;
        return {};
    }

    nk::core::ResourceAssetBytes encoded_data;
    const auto bytes_result = nk::core::resource_asset_get_bytes(asset, encoded_data);
    if (bytes_result != NK_OK) {
        out_result = bytes_result;
        return {};
    }

    ma_decoder decoder{};
    const auto result = ma_decoder_init_memory(encoded_data->data(), encoded_data->size(), nullptr,
                                               &decoder);
    if (result != MA_SUCCESS) {
        out_result = map_miniaudio_result(result, "could not validate cached audio asset");
        return {};
    }
    ma_decoder_uninit(&decoder);

    auto clip = std::make_shared<AudioClipResource>();
    clip->engine = std::move(engine);
    clip->encoded_data = std::move(encoded_data);
    return clip;
}

std::shared_ptr<AudioClipResource> create_clip_from_stream(const nk_resource *resource,
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
    const auto result = ma_decoder_init(audio_resource_read, audio_resource_seek, &reader, nullptr,
                                        &decoder);
    if (result != MA_SUCCESS) {
        nk_resource_close(reader.stream);
        out_result = map_miniaudio_result(result, "could not validate streaming audio resource");
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
    clip->resource_flags = resource->flags;
    clip->resource_stream = true;
    return clip;
}

nk_result initialize_clip_from_memory(AudioClipResource &clip, const void *data,
                                      uint64_t data_size) {
    if (!data || data_size == 0 ||
        data_size > static_cast<uint64_t>(std::numeric_limits<std::size_t>::max()))
        return invalid_argument("audio clip memory data is invalid");
    auto encoded_data = std::make_shared<std::vector<std::byte>>(static_cast<std::size_t>(data_size));
    std::memcpy(encoded_data->data(), data, encoded_data->size());
    clip.encoded_data = std::move(encoded_data);

    ma_decoder decoder{};
    const auto result = ma_decoder_init_memory(clip.encoded_data->data(), clip.encoded_data->size(),
                                               nullptr, &decoder);
    if (result != MA_SUCCESS)
        return map_miniaudio_result(result, "could not validate audio clip memory");
    ma_decoder_uninit(&decoder);
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

std::shared_ptr<AudioMixSnapshotResource> get_mix_snapshot(nk_audio_mix_snapshot handle) {
    auto resource =
        nk::core::handles().get(handle, nk::core::ResourceType::audio_mix_snapshot);
    if (!resource) {
        nk::core::set_error("invalid audio mix snapshot handle");
        return {};
    }
    auto snapshot =
        std::dynamic_pointer_cast<AudioMixSnapshotResource>(std::move(resource));
    if (!snapshot)
        nk::core::set_error("invalid audio mix snapshot resource");
    return snapshot;
}

std::shared_ptr<AudioEffectResource> get_effect(nk_audio_bus_effect handle) {
    auto resource =
        nk::core::handles().get(handle, nk::core::ResourceType::audio_bus_effect);
    if (!resource) {
        nk::core::set_error("invalid audio bus effect handle");
        return {};
    }
    auto effect = std::dynamic_pointer_cast<AudioEffectResource>(std::move(resource));
    if (!effect)
        nk::core::set_error("invalid audio bus effect resource");
    return effect;
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
    bus->handle.store(handle, std::memory_order_release);
    *out_bus = handle;
    return NK_OK;
}

nk_result insert_mix_snapshot(std::shared_ptr<AudioMixSnapshotResource> snapshot,
                              nk_audio_mix_snapshot *out_snapshot) {
    const auto handle =
        nk::core::handles().insert(nk::core::ResourceType::audio_mix_snapshot, snapshot);
    if (handle == NK_INVALID_HANDLE) {
        nk::core::set_error("could not allocate an audio mix snapshot handle");
        return NK_ERROR_OUT_OF_MEMORY;
    }
    snapshot->handle.store(handle, std::memory_order_release);
    *out_snapshot = handle;
    return NK_OK;
}

void prune_mix_snapshot_targets(AudioMixSnapshotResource &snapshot) {
    snapshot.targets.erase(
        std::remove_if(snapshot.targets.begin(), snapshot.targets.end(), [](const auto &target) {
            const auto bus = target.bus.lock();
            return !bus || bus->handle.load(std::memory_order_acquire) == NK_INVALID_HANDLE;
        }),
        snapshot.targets.end());
}

AudioMixSnapshotTarget *find_mix_snapshot_target(AudioMixSnapshotResource &snapshot,
                                                  const AudioBusResource &bus) {
    const auto it = std::find_if(snapshot.targets.begin(), snapshot.targets.end(),
                                 [&](auto &target) {
                                     const auto candidate = target.bus.lock();
                                     return candidate && candidate.get() == &bus;
                                 });
    return it == snapshot.targets.end() ? nullptr : &*it;
}

struct ResolvedMixSnapshotTarget {
    std::shared_ptr<AudioBusResource> bus;
    float volume = 1.0f;
    bool muted = false;
};

nk_result resolve_mix_snapshot_targets(
    const AudioMixSnapshotResource &snapshot,
    std::vector<ResolvedMixSnapshotTarget> &resolved) {
    resolved.clear();
    resolved.reserve(snapshot.targets.size());
    for (const auto &target : snapshot.targets) {
        auto bus = target.bus.lock();
        if (!bus || bus->handle.load(std::memory_order_acquire) == NK_INVALID_HANDLE)
            return invalid_handle("audio mix snapshot contains a destroyed bus");
        if (snapshot.engine && bus->engine.get() != snapshot.engine.get())
            return invalid_request("audio mix snapshot contains a bus from another audio engine");
        resolved.push_back({std::move(bus), target.volume, target.muted});
    }
    return NK_OK;
}

nk_result apply_mix_snapshot_targets(
    const AudioMixSnapshotResource &snapshot, uint64_t duration_pcm_frames,
    bool scheduled, uint64_t absolute_start_time_pcm_frames) {
    std::vector<ResolvedMixSnapshotTarget> resolved;
    if (const auto result = resolve_mix_snapshot_targets(snapshot, resolved); result != NK_OK)
        return result;
    for (const auto &target : resolved) {
        const auto target_volume = target.muted ? 0.0f : target.volume;
        if (scheduled) {
            ma_sound_set_fade_start_in_pcm_frames(
                &target.bus->group, NK_AUDIO_VOLUME_CURRENT, target_volume,
                duration_pcm_frames, absolute_start_time_pcm_frames);
        } else if (duration_pcm_frames == 0) {
            ma_sound_group_set_volume(&target.bus->group, target_volume);
        } else {
            ma_sound_group_set_fade_in_pcm_frames(
                &target.bus->group, NK_AUDIO_VOLUME_CURRENT, target_volume,
                duration_pcm_frames);
        }
        target.bus->volume = target.volume;
        target.bus->muted = target.muted;
    }
    return NK_OK;
}

std::size_t audio_effect_position(const AudioBusResource &bus,
                                  const AudioEffectResource &effect) {
    const auto it = std::find_if(bus.effects.begin(), bus.effects.end(),
                                 [&](const auto &candidate) {
                                     return candidate.get() == &effect;
                                 });
    return it == bus.effects.end() ? bus.effects.size()
                                   : static_cast<std::size_t>(it - bus.effects.begin());
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
    if (const auto result = voice_options(options, flags); result != NK_OK) {
        out_result = result;
        return {};
    }

    if (clip->encoded_data && (flags & NK_AUDIO_VOICE_ASYNC)) {
        out_result = invalid_argument(
            "asynchronous audio loading is unavailable for memory-backed clips");
        return {};
    }

    auto voice = std::make_shared<AudioVoiceResource>();
    voice->engine = clip->engine;
    voice->clip = std::move(clip);
    const bool asynchronous = (flags & NK_AUDIO_VOICE_ASYNC) != 0;
    voice->asynchronous.store(asynchronous, std::memory_order_relaxed);
    if (asynchronous)
        voice->load_state.store(NK_AUDIO_VOICE_LOADING, std::memory_order_relaxed);

    ma_result result = MA_SUCCESS;
    if (voice->clip->encoded_data) {
        result = ma_decoder_init_memory(voice->clip->encoded_data->data(),
                                        voice->clip->encoded_data->size(), nullptr,
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
            nullptr, &voice->sound);
    } else if (voice->clip->resource_stream) {
        nk_resource resource{};
        resource.struct_size = sizeof(resource);
        resource.flags = voice->clip->resource_flags;
        resource.uri = voice->clip->resource_uri.c_str();
        voice->resource_reader.voice = voice.get();
        const auto open_result =
            nk_resource_open(&resource, NK_RESOURCE_OPEN_READ, &voice->resource_reader.stream);
        if (open_result != NK_OK) {
            out_result = open_result;
            return {};
        }
        result = ma_decoder_init(audio_resource_read, audio_resource_seek,
                                 &voice->resource_reader, nullptr, &voice->decoder);
        if (result != MA_SUCCESS) {
            out_result = map_miniaudio_result(result,
                                              "could not initialize streaming audio decoder");
            return {};
        }
        voice->decoder_initialized = true;
        ma_format format = ma_format_unknown;
        ma_uint32 channels = 0;
        ma_uint32 sample_rate = 0;
        result = ma_decoder_get_data_format(&voice->decoder, &format, &channels, &sample_rate,
                                            nullptr, 0);
        if (result != MA_SUCCESS) {
            out_result = map_miniaudio_result(result,
                                              "could not query streaming audio format");
            return {};
        }
        ma_uint64 length = 0;
        const bool length_known =
            ma_decoder_get_length_in_pcm_frames(&voice->decoder, &length) == MA_SUCCESS;
        voice->streaming_source = std::make_unique<AudioStreamingSource>();
        result = voice->streaming_source->initialize(
            &voice->decoder, voice.get(), format, channels, sample_rate, length, length_known);
        if (result != MA_SUCCESS) {
            out_result = map_miniaudio_result(result,
                                              "could not initialize streaming audio buffer");
            return {};
        }
        const auto stream_result = voice->streaming_source->start();
        if (stream_result != MA_SUCCESS) {
            out_result = map_miniaudio_result(stream_result,
                                              "could not start streaming audio decoder");
            return {};
        }
        const uint32_t sound_flags =
            (flags & NK_AUDIO_VOICE_LOOPING) ? MA_SOUND_FLAG_LOOPING : 0;
        result = ma_sound_init_from_data_source(
            &voice->engine->engine,
            reinterpret_cast<ma_data_source *>(&voice->streaming_source->pcm), sound_flags,
            nullptr, &voice->sound);
    } else if (asynchronous) {
        voice->load_notification.voice = voice.get();
        voice->load_notification.callbacks.onSignal = audio_voice_load_callback;
        auto config = ma_sound_config_init_2(&voice->engine->engine);
        config.pFilePath = voice->clip->path.c_str();
        config.flags = miniaudio_voice_flags(flags);
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
            nullptr, nullptr, &voice->sound);
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
    voice->engine->voices.emplace_back(voice);
    *out_voice = handle;
    audio_voice_update_load_state(*voice);
    audio_voice_publish_load_event(*voice);
    audio_voice_report_stream_error(*voice,
                                    voice->stream_error.load(std::memory_order_acquire));
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

template <typename Function>
nk_result with_effect(nk_audio_bus_effect handle, const char *message, Function &&function) {
    auto effect = get_effect(handle);
    if (!effect)
        return NK_ERROR_INVALID_HANDLE;
    auto bus = effect->bus.lock();
    if (!bus || !effect->initialized) {
        nk::core::set_error("audio bus effect is no longer attached");
        return NK_ERROR_INVALID_HANDLE;
    }
    return function(*effect, *bus, message);
}

std::vector<std::shared_ptr<AudioVoiceResource>> live_engine_voices(AudioEngineResource &engine) {
    std::vector<std::shared_ptr<AudioVoiceResource>> result;
    result.reserve(engine.voices.size());
    engine.voices.erase(
        std::remove_if(engine.voices.begin(), engine.voices.end(), [&](const auto &weak_voice) {
            auto resource = weak_voice.lock();
            if (!resource)
                return true;
            auto voice = std::dynamic_pointer_cast<AudioVoiceResource>(std::move(resource));
            if (!voice || voice->handle.load(std::memory_order_acquire) == NK_INVALID_HANDLE)
                return true;
            result.push_back(std::move(voice));
            return false;
        }),
        engine.voices.end());
    return result;
}

bool voice_in_bus_scope(const AudioVoiceResource &voice, const AudioBusResource &bus) {
    for (auto current = voice.bus; current; current = current->parent) {
        if (current.get() == &bus)
            return true;
    }
    return false;
}

std::vector<std::shared_ptr<AudioBusResource>> voice_bus_scope(const AudioVoiceResource &voice) {
    std::vector<std::shared_ptr<AudioBusResource>> result;
    for (auto current = voice.bus; current; current = current->parent)
        result.push_back(current);
    return result;
}

uint32_t active_voice_count(const std::vector<std::shared_ptr<AudioVoiceResource>> &voices,
                            const AudioBusResource &bus, const AudioVoiceResource *exclude) {
    uint32_t count = 0;
    for (const auto &voice : voices) {
        if (voice.get() != exclude &&
            (voice->virtualized.load(std::memory_order_acquire) ||
             ma_sound_is_playing(&voice->sound)) &&
            voice_in_bus_scope(*voice, bus))
            ++count;
    }
    return count;
}

std::shared_ptr<AudioVoiceResource> choose_voice_to_steal(
    const std::vector<std::shared_ptr<AudioVoiceResource>> &voices, const AudioBusResource &bus,
    const AudioVoiceResource &requesting_voice) {
    std::shared_ptr<AudioVoiceResource> selected;
    for (const auto &candidate : voices) {
        if (candidate.get() == &requesting_voice ||
            candidate->virtualized.load(std::memory_order_acquire) ||
            !ma_sound_is_playing(&candidate->sound) ||
            candidate->priority > requesting_voice.priority ||
            !voice_in_bus_scope(*candidate, bus))
            continue;
        if (!selected) {
            selected = candidate;
            continue;
        }

        bool replace = false;
        switch (bus.steal_policy) {
        case NK_AUDIO_VOICE_STEAL_OLDEST:
            replace = candidate->start_order < selected->start_order;
            break;
        case NK_AUDIO_VOICE_STEAL_QUIETEST:
            replace = ma_sound_get_volume(&candidate->sound) <
                      ma_sound_get_volume(&selected->sound);
            if (!replace && ma_sound_get_volume(&candidate->sound) ==
                                ma_sound_get_volume(&selected->sound))
                replace = candidate->start_order < selected->start_order;
            break;
        case NK_AUDIO_VOICE_STEAL_LOWEST_PRIORITY:
            replace = candidate->priority < selected->priority ||
                      (candidate->priority == selected->priority &&
                       candidate->start_order < selected->start_order);
            break;
        case NK_AUDIO_VOICE_STEAL_NONE:
        default:
            break;
        }
        if (replace)
            selected = candidate;
    }
    return selected;
}

void publish_voice_event(AudioVoiceResource &voice, nk_event_kind kind) noexcept {
    const auto handle = voice.handle.load(std::memory_order_acquire);
    if (handle == NK_INVALID_HANDLE)
        return;
    nk::core::QueuedEvent event;
    event.kind = kind;
    event.source = handle;
    nk::core::push_event(std::move(event));
}

void audio_voice_report_stream_error(AudioVoiceResource &voice, nk_result result) noexcept {
    if (result == NK_OK)
        return;
    nk_result first_error = NK_OK;
    if (!voice.stream_error.compare_exchange_strong(first_error, result,
                                                     std::memory_order_acq_rel))
        result = first_error;
    if (voice.handle.load(std::memory_order_acquire) == NK_INVALID_HANDLE)
        return;

    bool expected = false;
    if (!voice.stream_error_emitted.compare_exchange_strong(expected, true,
                                                           std::memory_order_acq_rel))
        return;

    nk::core::QueuedEvent event;
    event.kind = NK_EVENT_AUDIO_VOICE_STREAM_FAILED;
    event.source = voice.handle.load(std::memory_order_acquire);
    event.result = result;
    if (nk::core::push_event(std::move(event)) != NK_OK)
        voice.stream_error_emitted.store(false, std::memory_order_release);
}

void virtualize_voice(AudioVoiceResource &voice) {
    if (voice.virtualized.load(std::memory_order_acquire))
        return;
    ma_uint64 cursor = 0;
    if (ma_sound_get_cursor_in_pcm_frames(&voice.sound, &cursor) != MA_SUCCESS)
        cursor = 0;
    voice.virtual_cursor_frames = cursor;
    voice.virtual_start_time_frames = ma_engine_get_time_in_pcm_frames(&voice.engine->engine);
    voice.virtualized.store(true, std::memory_order_release);
    publish_voice_event(voice, NK_EVENT_AUDIO_VOICE_VIRTUALIZED);
}

void publish_virtual_voice_completion(AudioVoiceResource &voice) {
    publish_voice_event(voice, NK_EVENT_AUDIO_VOICE_COMPLETE);
}

nk_result admit_voice(AudioVoiceResource &voice,
                      const std::vector<std::shared_ptr<AudioVoiceResource>> &voices) {
    for (const auto &bus : voice_bus_scope(voice)) {
        if (bus->max_voices == 0)
            continue;
        while (active_voice_count(voices, *bus, &voice) >= bus->max_voices) {
            if (bus->steal_policy != NK_AUDIO_VOICE_STEAL_NONE) {
                auto victim = choose_voice_to_steal(voices, *bus, voice);
                if (victim) {
                    const auto result = ma_sound_stop(&victim->sound);
                    if (result != MA_SUCCESS)
                        return map_miniaudio_result(result, "could not steal an audio voice");
                    victim->logically_playing.store(false, std::memory_order_release);
                    victim->virtualized.store(false, std::memory_order_release);
                    publish_voice_event(*victim, NK_EVENT_AUDIO_VOICE_STOLEN);
                    continue;
                }
            }
            if (bus->virtualize) {
                virtualize_voice(voice);
                return NK_OK;
            }
            return invalid_request("audio voice concurrency limit reached");
        }
    }
    return NK_OK;
}

uint64_t virtual_voice_cursor(const AudioVoiceResource &voice) {
    const auto now = ma_engine_get_time_in_pcm_frames(&voice.engine->engine);
    const auto elapsed = now >= voice.virtual_start_time_frames
                             ? now - voice.virtual_start_time_frames
                             : 0;
    const auto cursor = voice.virtual_cursor_frames + elapsed;
    ma_uint64 length = 0;
    if (ma_sound_get_length_in_pcm_frames(&voice.sound, &length) != MA_SUCCESS || length == 0)
        return cursor;
    if (ma_sound_is_looping(&voice.sound))
        return cursor % length;
    return static_cast<uint64_t>(std::min(cursor, length));
}

bool finish_virtual_voice_if_at_end(AudioVoiceResource &voice) {
    if (!voice.virtualized.load(std::memory_order_acquire) ||
        ma_sound_is_looping(&voice.sound))
        return false;
    ma_uint64 length = 0;
    if (ma_sound_get_length_in_pcm_frames(&voice.sound, &length) != MA_SUCCESS || length == 0 ||
        virtual_voice_cursor(voice) < length)
        return false;
    voice.logically_playing.store(false, std::memory_order_release);
    voice.virtualized.store(false, std::memory_order_release);
    publish_virtual_voice_completion(voice);
    return true;
}

nk_result start_voice_backend(AudioVoiceResource &voice, const char *message) {
    const bool was_virtualized = voice.virtualized.load(std::memory_order_acquire);
    if (was_virtualized) {
        const auto cursor = virtual_voice_cursor(voice);
        ma_uint64 length = 0;
        if (!ma_sound_is_looping(&voice.sound) &&
            ma_sound_get_length_in_pcm_frames(&voice.sound, &length) == MA_SUCCESS &&
            length != 0 && cursor >= length) {
            voice.logically_playing.store(false, std::memory_order_release);
            voice.virtualized.store(false, std::memory_order_release);
            publish_virtual_voice_completion(voice);
            return NK_OK;
        }
        auto result = ma_sound_seek_to_pcm_frame(&voice.sound, cursor);
        if (result != MA_SUCCESS)
            return map_miniaudio_result(result, message);
    }
    const auto result = ma_sound_start(&voice.sound);
    if (result != MA_SUCCESS)
        return map_miniaudio_result(result, message);
    voice.virtualized.store(false, std::memory_order_release);
    voice.logically_playing.store(ma_sound_is_playing(&voice.sound), std::memory_order_release);
    if (was_virtualized)
        publish_voice_event(voice, NK_EVENT_AUDIO_VOICE_RESUMED);
    return NK_OK;
}

nk_result start_voice_internal(AudioVoiceResource &voice, const char *message) {
    if (voice.logically_playing.load(std::memory_order_acquire)) {
        if (!voice.virtualized.load(std::memory_order_acquire) &&
            ma_sound_is_playing(&voice.sound))
            return NK_OK;
        voice.logically_playing.store(false, std::memory_order_release);
    }

    voice.start_order = next_voice_order.fetch_add(1, std::memory_order_relaxed);
    voice.logically_playing.store(true, std::memory_order_release);
    auto voices = live_engine_voices(*voice.engine);
    const auto admission_result = admit_voice(voice, voices);
    if (admission_result != NK_OK) {
        voice.logically_playing.store(false, std::memory_order_release);
        return admission_result;
    }
    if (voice.virtualized.load(std::memory_order_acquire))
        return NK_OK;
    const auto result = start_voice_backend(voice, message);
    if (result != NK_OK)
        voice.logically_playing.store(false, std::memory_order_release);
    return result;
}

nk_result promote_virtual_voice(AudioVoiceResource &voice, const char *message) {
    if (!voice.virtualized.load(std::memory_order_acquire))
        return NK_OK;
    voice.logically_playing.store(false, std::memory_order_release);
    auto voices = live_engine_voices(*voice.engine);
    const auto admission_result = admit_voice(voice, voices);
    if (admission_result != NK_OK) {
        voice.logically_playing.store(true, std::memory_order_release);
        return admission_result;
    }
    const auto scope = voice_bus_scope(voice);
    const auto still_blocked = std::any_of(scope.begin(), scope.end(), [&](const auto &bus) {
        return bus->max_voices != 0 &&
               active_voice_count(voices, *bus, &voice) >= bus->max_voices;
    });
    if (still_blocked) {
        voice.logically_playing.store(true, std::memory_order_release);
        return NK_OK;
    }
    voice.logically_playing.store(true, std::memory_order_release);
    const auto result = start_voice_backend(voice, message);
    if (result != NK_OK)
        voice.logically_playing.store(false, std::memory_order_release);
    return result;
}

void promote_virtual_voices(AudioEngineResource &engine) {
    auto voices = live_engine_voices(engine);
    std::sort(voices.begin(), voices.end(), [](const auto &left, const auto &right) {
        if (left->priority != right->priority)
            return left->priority > right->priority;
        return left->start_order < right->start_order;
    });
    for (const auto &voice : voices) {
        if (voice->virtualized.load(std::memory_order_acquire))
            promote_virtual_voice(*voice, "could not resume virtualized audio voice");
    }
}

nk_result start_bus_voices(AudioBusResource &bus, const char *message) {
    nk_result first_error = NK_OK;
    for (const auto &voice : live_engine_voices(*bus.engine)) {
        if (!voice_in_bus_scope(*voice, bus))
            continue;
        const auto result = start_voice_internal(*voice, message);
        if (result != NK_OK && first_error == NK_OK)
            first_error = result;
    }
    return first_error;
}

bool bus_has_concurrency_policy(AudioBusResource &bus) {
    for (const auto &voice : live_engine_voices(*bus.engine)) {
        if (!voice_in_bus_scope(*voice, bus))
            continue;
        for (auto current = voice->bus; current; current = current->parent) {
            if (current->max_voices != 0 || current->virtualize)
                return true;
        }
    }
    return false;
}

nk_result stop_voice_internal(AudioVoiceResource &voice, const char *message) {
    const bool was_virtual = voice.virtualized.exchange(false, std::memory_order_acq_rel);
    voice.logically_playing.store(false, std::memory_order_release);
    if (was_virtual)
        return NK_OK;
    return map_miniaudio_result(ma_sound_stop(&voice.sound), message);
}

nk_result stop_bus_voices(AudioBusResource &bus, const char *message) {
    nk_result first_error = NK_OK;
    for (const auto &voice : live_engine_voices(*bus.engine)) {
        if (!voice_in_bus_scope(*voice, bus))
            continue;
        const auto result = stop_voice_internal(*voice, message);
        if (result != NK_OK && first_error == NK_OK)
            first_error = result;
    }
    promote_virtual_voices(*bus.engine);
    return first_error;
}

nk_result audio_device_state(AudioEngineResource &engine, nk_audio_device_state &state) {
    const auto *device = ma_engine_get_device(&engine.engine);
    if (!device) {
        nk::core::set_error("the miniaudio engine has no playback device");
        return NK_ERROR_UNSUPPORTED;
    }
    if (engine.interrupted.load(std::memory_order_acquire)) {
        state = NK_AUDIO_DEVICE_INTERRUPTED;
        return NK_OK;
    }

    switch (ma_device_get_state(device)) {
    case ma_device_state_uninitialized:
        state = NK_AUDIO_DEVICE_UNINITIALIZED;
        break;
    case ma_device_state_stopped:
        state = NK_AUDIO_DEVICE_STOPPED;
        break;
    case ma_device_state_started:
        state = NK_AUDIO_DEVICE_STARTED;
        break;
    case ma_device_state_starting:
        state = NK_AUDIO_DEVICE_STARTING;
        break;
    case ma_device_state_stopping:
        state = NK_AUDIO_DEVICE_STOPPING;
        break;
    default:
        nk::core::set_error("miniaudio returned an unknown playback device state");
        return NK_ERROR_UNKNOWN;
    }
    return NK_OK;
}

nk_result insert_audio_effect(std::shared_ptr<AudioBusResource> bus,
                              std::shared_ptr<AudioEffectResource> effect,
                              nk_audio_bus_effect *out_effect) {
    bus->effects.push_back(effect);
    const auto handle =
        nk::core::handles().insert(nk::core::ResourceType::audio_bus_effect, effect);
    if (handle == NK_INVALID_HANDLE) {
        bus->effects.pop_back();
        nk::core::set_error("could not allocate an audio bus effect handle");
        return NK_ERROR_OUT_OF_MEMORY;
    }
    effect->handle = handle;
    const auto chain_result = rebuild_audio_bus_effect_chain(
        *bus, "could not attach audio bus effect chain");
    if (chain_result != NK_OK) {
        bus->effects.pop_back();
        nk::core::handles().erase(handle, nk::core::ResourceType::audio_bus_effect);
        effect->bus.reset();
        audio_effect_uninitialize(*effect);
        return chain_result;
    }
    *out_effect = handle;
    return NK_OK;
}

nk_result create_audio_filter_effect(std::shared_ptr<AudioBusResource> bus,
                                     nk_audio_effect_type type, float cutoff_frequency_hz,
                                     uint32_t order, nk_audio_bus_effect *out_effect) {
    if (!valid_audio_filter(cutoff_frequency_hz, order, *bus->engine))
        return invalid_argument(
            "audio bus filter cutoff must be below Nyquist and order must be in the range [1, 8]");

    auto effect = std::make_shared<AudioEffectResource>();
    effect->engine = bus->engine;
    effect->bus = bus;
    effect->type = type;
    effect->cutoff_frequency_hz = cutoff_frequency_hz;
    effect->order = order;
    const auto channels = ma_engine_get_channels(&bus->engine->engine);
    const auto sample_rate = ma_engine_get_sample_rate(&bus->engine->engine);
    auto *allocation_callbacks = &bus->engine->engine.allocationCallbacks;
    ma_result result = MA_SUCCESS;
    if (type == NK_AUDIO_EFFECT_LOW_PASS) {
        effect->low_pass = std::make_unique<ma_lpf_node>();
        const auto config = ma_lpf_node_config_init(channels, sample_rate, cutoff_frequency_hz,
                                                    order);
        result = ma_lpf_node_init(ma_engine_get_node_graph(&bus->engine->engine), &config,
                                  allocation_callbacks, effect->low_pass.get());
        if (result != MA_SUCCESS)
            ma_lpf_uninit(&effect->low_pass->lpf, allocation_callbacks);
    } else {
        effect->high_pass = std::make_unique<ma_hpf_node>();
        const auto config = ma_hpf_node_config_init(channels, sample_rate, cutoff_frequency_hz,
                                                    order);
        result = ma_hpf_node_init(ma_engine_get_node_graph(&bus->engine->engine), &config,
                                  allocation_callbacks, effect->high_pass.get());
        if (result != MA_SUCCESS)
            ma_hpf_uninit(&effect->high_pass->hpf, allocation_callbacks);
    }
    if (result != MA_SUCCESS)
        return map_miniaudio_result(result, "could not create audio bus filter effect");
    effect->initialized = true;
    return insert_audio_effect(std::move(bus), std::move(effect), out_effect);
}

nk_result create_audio_delay_effect(std::shared_ptr<AudioBusResource> bus,
                                    uint32_t delay_pcm_frames, float decay,
                                    nk_audio_bus_effect *out_effect) {
    if (!valid_audio_delay(delay_pcm_frames, decay))
        return invalid_argument(
            "audio bus delay frames must be positive and decay must be in the range [0, 1]");

    auto effect = std::make_shared<AudioEffectResource>();
    effect->engine = bus->engine;
    effect->bus = bus;
    effect->type = NK_AUDIO_EFFECT_DELAY;
    effect->delay_pcm_frames = delay_pcm_frames;
    effect->decay = decay;
    effect->delay = std::make_unique<ma_delay_node>();
    const auto config = ma_delay_node_config_init(
        ma_engine_get_channels(&bus->engine->engine), ma_engine_get_sample_rate(&bus->engine->engine),
        delay_pcm_frames, decay);
    const auto result = ma_delay_node_init(ma_engine_get_node_graph(&bus->engine->engine), &config,
                                           &bus->engine->engine.allocationCallbacks,
                                           effect->delay.get());
    if (result != MA_SUCCESS)
        return map_miniaudio_result(result, "could not create audio bus delay effect");
    effect->initialized = true;
    return insert_audio_effect(std::move(bus), std::move(effect), out_effect);
}

} // namespace

extern "C" {

nk_result NK_CALL nk_audio_device_configure(const nk_audio_device_options *options) {
    return nk::core::result_boundary(
        "unexpected error while configuring the audio device", [&]() -> nk_result {
            if (const auto result = enter_audio_ui(); result != NK_OK)
                return result;
            if (!options)
                return invalid_argument("audio device options are missing");
            if (options->struct_size < sizeof(nk_audio_device_options))
                return invalid_argument("audio device options are missing or too small");
            if (options->no_auto_start > 1)
                return invalid_argument("audio device no_auto_start must be zero or one");
            if (options->period_size_in_frames != 0 &&
                options->period_size_in_milliseconds != 0)
                return invalid_argument(
                    "audio device period must be specified in frames or milliseconds, not both");
            if (options->channels > MA_MAX_CHANNELS)
                return invalid_argument("audio device channel count is too large");

            std::lock_guard lock(engine_mutex);
            if (engine_resource.lock())
                return invalid_request(
                    "audio device configuration is immutable after the audio engine starts");

            PendingAudioDeviceConfig next;
            next.playback_device_index = options->playback_device_index;
            next.sample_rate = options->sample_rate;
            next.channels = options->channels;
            next.period_size_in_frames = options->period_size_in_frames;
            next.period_size_in_milliseconds = options->period_size_in_milliseconds;
            next.no_auto_start = options->no_auto_start != 0;

            if (next.playback_device_index != NK_AUDIO_DEVICE_DEFAULT) {
                std::vector<EnumeratedAudioDevice> devices;
                if (const auto result = enumerate_audio_devices(devices); result != NK_OK)
                    return result;
                if (next.playback_device_index >= devices.size())
                    return invalid_argument("audio playback device index is out of range");
                next.playback_device_id = devices[next.playback_device_index].id;
                next.has_playback_device_id = true;
            }
            pending_device_config = next;
            return NK_OK;
        });
}

nk_result NK_CALL nk_audio_device_get_count(uint32_t *out_count) {
    return nk::core::result_boundary(
        "unexpected error while getting audio device count", [&]() -> nk_result {
            if (const auto result = enter_audio_ui(); result != NK_OK)
                return result;
            if (!out_count)
                return invalid_argument("audio device count output is missing");
            std::vector<EnumeratedAudioDevice> devices;
            if (const auto result = enumerate_audio_devices(devices); result != NK_OK)
                return result;
            *out_count = static_cast<uint32_t>(devices.size());
            return NK_OK;
        });
}

nk_result NK_CALL nk_audio_device_get_name(uint32_t index, char *buffer, uint32_t *inout_size) {
    return nk::core::result_boundary(
        "unexpected error while getting an audio device name", [&]() -> nk_result {
            if (const auto result = enter_audio_ui(); result != NK_OK)
                return result;
            if (!inout_size)
                return invalid_argument("audio device name size output is missing");
            std::vector<EnumeratedAudioDevice> devices;
            if (const auto result = enumerate_audio_devices(devices); result != NK_OK)
                return result;
            if (index >= devices.size())
                return invalid_argument("audio playback device index is out of range");
            return copy_audio_string(devices[index].name, buffer, inout_size);
        });
}

nk_result NK_CALL nk_audio_device_is_default(uint32_t index, nk_bool *out_default) {
    return nk::core::result_boundary(
        "unexpected error while getting an audio device default flag", [&]() -> nk_result {
            if (const auto result = enter_audio_ui(); result != NK_OK)
                return result;
            if (!out_default)
                return invalid_argument("audio device default flag output is missing");
            std::vector<EnumeratedAudioDevice> devices;
            if (const auto result = enumerate_audio_devices(devices); result != NK_OK)
                return result;
            if (index >= devices.size())
                return invalid_argument("audio playback device index is out of range");
            *out_default = devices[index].is_default ? 1u : 0u;
            return NK_OK;
        });
}

nk_result NK_CALL nk_audio_device_start(void) {
    return nk::core::result_boundary(
        "unexpected error while starting the audio device", [&]() -> nk_result {
            if (const auto result = enter_audio_ui(); result != NK_OK)
                return result;
            return with_engine("could not start the audio device", [](AudioEngineResource &engine,
                                                                       const char *message) {
                return map_miniaudio_result(ma_engine_start(&engine.engine), message);
            });
        });
}

nk_result NK_CALL nk_audio_device_stop(void) {
    return nk::core::result_boundary(
        "unexpected error while stopping the audio device", [&]() -> nk_result {
            if (const auto result = enter_audio_ui(); result != NK_OK)
                return result;
            auto engine = current_engine();
            if (!engine)
                return invalid_request("the audio device has not been initialized");
            return map_miniaudio_result(ma_engine_stop(&engine->engine),
                                        "could not stop the audio device");
        });
}

nk_result NK_CALL nk_audio_device_restart(void) {
    return nk::core::result_boundary(
        "unexpected error while restarting the audio device", [&]() -> nk_result {
            if (const auto result = enter_audio_ui(); result != NK_OK)
                return result;
            auto engine = current_engine();
            if (!engine)
                return invalid_request("the audio device has not been initialized");
            auto result = ma_engine_stop(&engine->engine);
            if (result != MA_SUCCESS)
                return map_miniaudio_result(result, "could not restart the audio device");
            result = ma_engine_start(&engine->engine);
            if (result != MA_SUCCESS)
                return map_miniaudio_result(result, "could not restart the audio device");
            engine->interrupted.store(false, std::memory_order_release);
            return NK_OK;
        });
}

nk_result NK_CALL nk_audio_device_get_state(nk_audio_device_state *out_state) {
    return nk::core::result_boundary(
        "unexpected error while getting the audio device state", [&]() -> nk_result {
            if (const auto result = enter_audio_ui(); result != NK_OK)
                return result;
            if (!out_state)
                return invalid_argument("audio device state output is missing");
            auto engine = current_engine();
            if (!engine) {
                *out_state = NK_AUDIO_DEVICE_UNINITIALIZED;
                return NK_OK;
            }
            return audio_device_state(*engine, *out_state);
        });
}

nk_result NK_CALL nk_audio_bus_create(const nk_audio_bus_options *options,
                                      nk_audio_bus *out_bus) {
    return nk::core::result_boundary(
        "unexpected error while creating an audio bus", [&]() -> nk_result {
            if (const auto result = enter_audio_ui(); result != NK_OK)
                return result;
            if (!out_bus)
                return invalid_argument("audio bus output is missing");
            *out_bus = NK_INVALID_HANDLE;
            nk_audio_bus parent_handle = NK_INVALID_HANDLE;
            if (const auto result = bus_options(options, parent_handle); result != NK_OK)
                return result;
            std::shared_ptr<AudioBusResource> parent;
            if (parent_handle != NK_INVALID_HANDLE) {
                parent = get_bus(parent_handle);
                if (!parent)
                    return NK_ERROR_INVALID_HANDLE;
            }
            nk_result engine_result = NK_OK;
            auto engine = ensure_engine(engine_result);
            if (!engine)
                return engine_result;
            if (parent && parent->engine.get() != engine.get())
                return invalid_request("audio bus parent belongs to another audio engine");
            auto bus = std::make_shared<AudioBusResource>();
            bus->engine = std::move(engine);
            bus->parent = std::move(parent);
            const auto result = ma_sound_group_init(
                &bus->engine->engine, 0, bus->parent ? &bus->parent->group : nullptr, &bus->group);
            if (result != MA_SUCCESS)
                return map_miniaudio_result(result, "could not create audio mixer bus");
            bus->initialized = true;
            if (const auto chain_result = rebuild_audio_bus_effect_chain(
                    *bus, "could not route audio mixer bus");
                chain_result != NK_OK)
                return chain_result;
            return insert_bus(std::move(bus), out_bus);
        });
}

nk_result NK_CALL nk_audio_bus_set_parent(nk_audio_bus bus_handle, nk_audio_bus parent_handle) {
    return nk::core::result_boundary(
        "unexpected error while reparenting an audio bus", [&]() -> nk_result {
            if (const auto result = enter_audio_ui(); result != NK_OK)
                return result;
            auto bus = get_bus(bus_handle);
            if (!bus)
                return NK_ERROR_INVALID_HANDLE;

            std::shared_ptr<AudioBusResource> parent;
            if (parent_handle != NK_INVALID_HANDLE) {
                if (parent_handle == bus_handle)
                    return invalid_request("an audio bus cannot be its own parent");
                parent = get_bus(parent_handle);
                if (!parent)
                    return NK_ERROR_INVALID_HANDLE;
                if (parent->engine.get() != bus->engine.get())
                    return invalid_request("audio bus parent belongs to another audio engine");
                for (auto ancestor = parent; ancestor; ancestor = ancestor->parent) {
                    if (ancestor.get() == bus.get())
                        return invalid_request("audio bus parent would create a routing cycle");
                }
            }
            if (bus->parent.get() == parent.get())
                return NK_OK;

            auto previous = std::move(bus->parent);
            bus->parent = std::move(parent);
            const auto result = rebuild_audio_bus_effect_chain(
                *bus, "could not route reparented audio bus");
            if (result == NK_OK)
                return NK_OK;
            bus->parent = std::move(previous);
            rebuild_audio_bus_effect_chain(*bus, "could not restore audio bus routing");
            return result;
        });
}

nk_result NK_CALL nk_audio_bus_get_parent(nk_audio_bus bus_handle, nk_audio_bus *out_parent) {
    return nk::core::result_boundary(
        "unexpected error while getting an audio bus parent", [&]() -> nk_result {
            if (const auto result = enter_audio_ui(); result != NK_OK)
                return result;
            if (!out_parent)
                return invalid_argument("audio bus parent output is missing");
            auto bus = get_bus(bus_handle);
            if (!bus)
                return NK_ERROR_INVALID_HANDLE;
            *out_parent = bus->parent
                              ? bus->parent->handle.load(std::memory_order_acquire)
                              : NK_INVALID_HANDLE;
            return NK_OK;
        });
}

nk_result NK_CALL nk_audio_bus_destroy(nk_audio_bus bus) {
    return nk::core::result_boundary("unexpected error while destroying an audio bus",
                                     [&]() -> nk_result {
        if (const auto result = enter_audio_ui(); result != NK_OK)
            return result;
        auto value = get_bus(bus);
        if (!value)
            return NK_ERROR_INVALID_HANDLE;
        for (const auto &effect : value->effects) {
            effect->bus.reset();
            audio_effect_uninitialize(*effect);
            nk::core::handles().erase(effect->handle, nk::core::ResourceType::audio_bus_effect);
        }
        value->effects.clear();
        if (!nk::core::handles().erase(bus, nk::core::ResourceType::audio_bus))
            return invalid_handle("invalid audio bus handle");
        value->handle.store(NK_INVALID_HANDLE, std::memory_order_release);
        return NK_OK;
    });
}

nk_result NK_CALL nk_audio_bus_start(nk_audio_bus bus) {
    return nk::core::result_boundary("unexpected error while starting an audio bus", [&]() {
        if (const auto result = enter_audio_ui(); result != NK_OK)
            return result;
        return with_bus(bus, "could not start audio bus", [](AudioBusResource &value,
                                                              const char *message) {
            if (!bus_has_concurrency_policy(value))
                return map_miniaudio_result(ma_sound_group_start(&value.group), message);
            return start_bus_voices(value, message);
        });
    });
}

nk_result NK_CALL nk_audio_bus_stop(nk_audio_bus bus) {
    return nk::core::result_boundary("unexpected error while stopping an audio bus", [&]() {
        if (const auto result = enter_audio_ui(); result != NK_OK)
            return result;
        return with_bus(bus, "could not stop audio bus", [](AudioBusResource &value,
                                                             const char *message) {
            if (!bus_has_concurrency_policy(value))
                return map_miniaudio_result(ma_sound_group_stop(&value.group), message);
            return stop_bus_voices(value, message);
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
                promote_virtual_voices(*value.engine);
                const auto voices = live_engine_voices(*value.engine);
                const auto virtual_playing = std::any_of(
                    voices.begin(), voices.end(), [&](const auto &voice) {
                        return voice_in_bus_scope(*voice, value) &&
                               voice->virtualized.load(std::memory_order_acquire);
                    });
                *out_playing = (virtual_playing || ma_sound_group_is_playing(&value.group))
                                   ? 1u
                                   : 0u;
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

nk_result NK_CALL nk_audio_bus_set_concurrency(
    nk_audio_bus bus, const nk_audio_bus_concurrency_options *options) {
    return nk::core::result_boundary(
        "unexpected error while setting audio bus concurrency", [&]() -> nk_result {
            if (const auto result = enter_audio_ui(); result != NK_OK)
                return result;
            if (!options)
                return invalid_argument("audio bus concurrency options are missing");
            if (options->struct_size < sizeof(nk_audio_bus_concurrency_options))
                return invalid_argument("audio bus concurrency options are missing or too small");
            if (!valid_voice_steal_policy(options->steal_policy))
                return invalid_argument("audio bus concurrency steal policy is invalid");
            if (options->virtualize > 1)
                return invalid_argument("audio bus concurrency virtualization must be zero or one");
            return with_bus(bus, "could not set audio bus concurrency",
                            [&](AudioBusResource &value, const char *) {
                                value.max_voices = options->max_voices;
                                value.steal_policy = options->steal_policy;
                                value.virtualize = options->virtualize != 0;
                                promote_virtual_voices(*value.engine);
                                return NK_OK;
                            });
        });
}

nk_result NK_CALL nk_audio_bus_get_concurrency(
    nk_audio_bus bus, nk_audio_bus_concurrency_options *out_options) {
    return nk::core::result_boundary(
        "unexpected error while getting audio bus concurrency", [&]() -> nk_result {
            if (const auto result = enter_audio_ui(); result != NK_OK)
                return result;
            if (!out_options)
                return invalid_argument("audio bus concurrency output is missing");
            if (out_options->struct_size < sizeof(nk_audio_bus_concurrency_options))
                return invalid_argument("audio bus concurrency output is missing or too small");
            return with_bus(bus, "could not get audio bus concurrency",
                            [&](AudioBusResource &value, const char *) {
                                out_options->max_voices = value.max_voices;
                                out_options->steal_policy = value.steal_policy;
                                out_options->virtualize = value.virtualize ? 1u : 0u;
                                out_options->reserved = 0;
                                out_options->reserved2[0] = 0;
                                out_options->reserved2[1] = 0;
                                return NK_OK;
                            });
        });
}

nk_result NK_CALL nk_audio_mix_snapshot_create(nk_audio_mix_snapshot *out_snapshot) {
    return nk::core::result_boundary(
        "unexpected error while creating an audio mix snapshot", [&]() -> nk_result {
            if (const auto result = enter_audio_ui(); result != NK_OK)
                return result;
            if (!out_snapshot)
                return invalid_argument("audio mix snapshot output is missing");
            *out_snapshot = NK_INVALID_HANDLE;
            return insert_mix_snapshot(std::make_shared<AudioMixSnapshotResource>(), out_snapshot);
        });
}

nk_result NK_CALL nk_audio_mix_snapshot_destroy(nk_audio_mix_snapshot snapshot_handle) {
    return nk::core::result_boundary(
        "unexpected error while destroying an audio mix snapshot", [&]() -> nk_result {
            if (const auto result = enter_audio_ui(); result != NK_OK)
                return result;
            auto snapshot = get_mix_snapshot(snapshot_handle);
            if (!snapshot)
                return NK_ERROR_INVALID_HANDLE;
            if (!nk::core::handles().erase(snapshot_handle,
                                            nk::core::ResourceType::audio_mix_snapshot))
                return invalid_handle("invalid audio mix snapshot handle");
            snapshot->handle.store(NK_INVALID_HANDLE, std::memory_order_release);
            snapshot->targets.clear();
            snapshot->engine.reset();
            return NK_OK;
        });
}

nk_result NK_CALL nk_audio_mix_snapshot_capture_bus(nk_audio_mix_snapshot snapshot_handle,
                                                    nk_audio_bus bus_handle) {
    return nk::core::result_boundary(
        "unexpected error while capturing an audio mix snapshot bus", [&]() -> nk_result {
            if (const auto result = enter_audio_ui(); result != NK_OK)
                return result;
            auto snapshot = get_mix_snapshot(snapshot_handle);
            if (!snapshot)
                return NK_ERROR_INVALID_HANDLE;
            auto bus = get_bus(bus_handle);
            if (!bus)
                return NK_ERROR_INVALID_HANDLE;
            if (snapshot->engine && snapshot->engine.get() != bus->engine.get())
                return invalid_request(
                    "audio mix snapshot bus belongs to another audio engine");
            prune_mix_snapshot_targets(*snapshot);
            auto *target = find_mix_snapshot_target(*snapshot, *bus);
            if (!target) {
                snapshot->targets.push_back({bus, bus->volume, bus->muted});
            } else {
                target->volume = bus->volume;
                target->muted = bus->muted;
            }
            snapshot->engine = bus->engine;
            return NK_OK;
        });
}

nk_result NK_CALL nk_audio_mix_snapshot_set_bus(nk_audio_mix_snapshot snapshot_handle,
                                                nk_audio_bus bus_handle, float volume,
                                                nk_bool muted) {
    return nk::core::result_boundary(
        "unexpected error while setting an audio mix snapshot bus", [&]() -> nk_result {
            if (const auto result = enter_audio_ui(); result != NK_OK)
                return result;
            if (!std::isfinite(volume) || volume < 0.0f)
                return invalid_argument(
                    "audio mix snapshot bus volume must be finite and non-negative");
            if (muted > 1)
                return invalid_argument("audio mix snapshot bus mute must be zero or one");
            auto snapshot = get_mix_snapshot(snapshot_handle);
            if (!snapshot)
                return NK_ERROR_INVALID_HANDLE;
            auto bus = get_bus(bus_handle);
            if (!bus)
                return NK_ERROR_INVALID_HANDLE;
            if (snapshot->engine && snapshot->engine.get() != bus->engine.get())
                return invalid_request(
                    "audio mix snapshot bus belongs to another audio engine");
            prune_mix_snapshot_targets(*snapshot);
            auto *target = find_mix_snapshot_target(*snapshot, *bus);
            if (!target) {
                snapshot->targets.push_back({bus, volume, muted != 0});
            } else {
                target->volume = volume;
                target->muted = muted != 0;
            }
            snapshot->engine = bus->engine;
            return NK_OK;
        });
}

nk_result NK_CALL nk_audio_mix_snapshot_remove_bus(nk_audio_mix_snapshot snapshot_handle,
                                                   nk_audio_bus bus_handle) {
    return nk::core::result_boundary(
        "unexpected error while removing an audio mix snapshot bus", [&]() -> nk_result {
            if (const auto result = enter_audio_ui(); result != NK_OK)
                return result;
            auto snapshot = get_mix_snapshot(snapshot_handle);
            if (!snapshot)
                return NK_ERROR_INVALID_HANDLE;
            auto bus = get_bus(bus_handle);
            if (!bus)
                return NK_ERROR_INVALID_HANDLE;
            prune_mix_snapshot_targets(*snapshot);
            const auto old_size = snapshot->targets.size();
            snapshot->targets.erase(
                std::remove_if(snapshot->targets.begin(), snapshot->targets.end(),
                               [&](const auto &target) {
                                   const auto candidate = target.bus.lock();
                                   return candidate && candidate.get() == bus.get();
                               }),
                snapshot->targets.end());
            if (snapshot->targets.empty())
                snapshot->engine.reset();
            return old_size == snapshot->targets.size()
                       ? invalid_handle("audio bus is not part of the mix snapshot")
                       : NK_OK;
        });
}

nk_result NK_CALL nk_audio_mix_snapshot_clear(nk_audio_mix_snapshot snapshot_handle) {
    return nk::core::result_boundary(
        "unexpected error while clearing an audio mix snapshot", [&]() -> nk_result {
            if (const auto result = enter_audio_ui(); result != NK_OK)
                return result;
            auto snapshot = get_mix_snapshot(snapshot_handle);
            if (!snapshot)
                return NK_ERROR_INVALID_HANDLE;
            snapshot->targets.clear();
            snapshot->engine.reset();
            return NK_OK;
        });
}

nk_result NK_CALL nk_audio_mix_snapshot_get_bus_count(nk_audio_mix_snapshot snapshot_handle,
                                                      uint32_t *out_count) {
    return nk::core::result_boundary(
        "unexpected error while getting an audio mix snapshot bus count", [&]() -> nk_result {
            if (const auto result = enter_audio_ui(); result != NK_OK)
                return result;
            if (!out_count)
                return invalid_argument("audio mix snapshot bus count output is missing");
            auto snapshot = get_mix_snapshot(snapshot_handle);
            if (!snapshot)
                return NK_ERROR_INVALID_HANDLE;
            prune_mix_snapshot_targets(*snapshot);
            *out_count = static_cast<uint32_t>(snapshot->targets.size());
            return NK_OK;
        });
}

nk_result NK_CALL nk_audio_mix_snapshot_apply(nk_audio_mix_snapshot snapshot_handle,
                                              uint64_t duration_pcm_frames) {
    return nk::core::result_boundary(
        "unexpected error while applying an audio mix snapshot", [&]() -> nk_result {
            if (const auto result = enter_audio_ui(); result != NK_OK)
                return result;
            auto snapshot = get_mix_snapshot(snapshot_handle);
            if (!snapshot)
                return NK_ERROR_INVALID_HANDLE;
            return apply_mix_snapshot_targets(*snapshot, duration_pcm_frames, false, 0);
        });
}

nk_result NK_CALL nk_audio_mix_snapshot_apply_at(nk_audio_mix_snapshot snapshot_handle,
                                                 uint64_t duration_pcm_frames,
                                                 uint64_t absolute_start_time_pcm_frames) {
    return nk::core::result_boundary(
        "unexpected error while scheduling an audio mix snapshot", [&]() -> nk_result {
            if (const auto result = enter_audio_ui(); result != NK_OK)
                return result;
            auto snapshot = get_mix_snapshot(snapshot_handle);
            if (!snapshot)
                return NK_ERROR_INVALID_HANDLE;
            return apply_mix_snapshot_targets(*snapshot, duration_pcm_frames, true,
                                              absolute_start_time_pcm_frames);
        });
}

nk_result NK_CALL nk_audio_bus_effect_create_low_pass(nk_audio_bus bus, float cutoff_frequency_hz,
                                                      uint32_t order,
                                                      nk_audio_bus_effect *out_effect) {
    return nk::core::result_boundary(
        "unexpected error while creating an audio bus low-pass effect", [&]() -> nk_result {
            if (const auto result = enter_audio_ui(); result != NK_OK)
                return result;
            if (!out_effect)
                return invalid_argument("audio bus effect output is missing");
            *out_effect = NK_INVALID_HANDLE;
            auto value = get_bus(bus);
            if (!value)
                return NK_ERROR_INVALID_HANDLE;
            return create_audio_filter_effect(std::move(value), NK_AUDIO_EFFECT_LOW_PASS,
                                              cutoff_frequency_hz, order, out_effect);
        });
}

nk_result NK_CALL nk_audio_bus_effect_create_high_pass(nk_audio_bus bus,
                                                       float cutoff_frequency_hz, uint32_t order,
                                                       nk_audio_bus_effect *out_effect) {
    return nk::core::result_boundary(
        "unexpected error while creating an audio bus high-pass effect", [&]() -> nk_result {
            if (const auto result = enter_audio_ui(); result != NK_OK)
                return result;
            if (!out_effect)
                return invalid_argument("audio bus effect output is missing");
            *out_effect = NK_INVALID_HANDLE;
            auto value = get_bus(bus);
            if (!value)
                return NK_ERROR_INVALID_HANDLE;
            return create_audio_filter_effect(std::move(value), NK_AUDIO_EFFECT_HIGH_PASS,
                                              cutoff_frequency_hz, order, out_effect);
        });
}

nk_result NK_CALL nk_audio_bus_effect_create_delay(nk_audio_bus bus, uint32_t delay_pcm_frames,
                                                   float decay, nk_audio_bus_effect *out_effect) {
    return nk::core::result_boundary(
        "unexpected error while creating an audio bus delay effect", [&]() -> nk_result {
            if (const auto result = enter_audio_ui(); result != NK_OK)
                return result;
            if (!out_effect)
                return invalid_argument("audio bus effect output is missing");
            *out_effect = NK_INVALID_HANDLE;
            auto value = get_bus(bus);
            if (!value)
                return NK_ERROR_INVALID_HANDLE;
            return create_audio_delay_effect(std::move(value), delay_pcm_frames, decay,
                                             out_effect);
        });
}

nk_result NK_CALL nk_audio_bus_effect_destroy(nk_audio_bus_effect effect_handle) {
    return nk::core::result_boundary(
        "unexpected error while destroying an audio bus effect", [&]() -> nk_result {
            if (const auto result = enter_audio_ui(); result != NK_OK)
                return result;
            auto effect = get_effect(effect_handle);
            if (!effect)
                return NK_ERROR_INVALID_HANDLE;
            auto bus = effect->bus.lock();
            if (!bus) {
                if (!nk::core::handles().erase(effect_handle,
                                               nk::core::ResourceType::audio_bus_effect))
                    return invalid_handle("invalid audio bus effect handle");
                return NK_OK;
            }
            const auto position = audio_effect_position(*bus, *effect);
            if (position == bus->effects.size())
                return invalid_handle("audio bus effect is not attached to its bus");
            bus->effects.erase(bus->effects.begin() + static_cast<std::ptrdiff_t>(position));
            effect->bus.reset();
            const auto chain_result = rebuild_audio_bus_effect_chain(
                *bus, "could not detach audio bus effect chain");
            if (chain_result != NK_OK) {
                bus->effects.insert(bus->effects.begin() + static_cast<std::ptrdiff_t>(position),
                                    effect);
                effect->bus = bus;
                rebuild_audio_bus_effect_chain(*bus, "could not restore audio bus effect chain");
                return chain_result;
            }
            audio_effect_uninitialize(*effect);
            if (!nk::core::handles().erase(effect_handle,
                                           nk::core::ResourceType::audio_bus_effect))
                return invalid_handle("invalid audio bus effect handle");
            return NK_OK;
        });
}

nk_result NK_CALL nk_audio_bus_effect_get_type(nk_audio_bus_effect effect_handle,
                                               nk_audio_effect_type *out_type) {
    return nk::core::result_boundary(
        "unexpected error while getting an audio bus effect type", [&]() -> nk_result {
            if (const auto result = enter_audio_ui(); result != NK_OK)
                return result;
            if (!out_type)
                return invalid_argument("audio bus effect type output is missing");
            return with_effect(effect_handle, "could not get audio bus effect type",
                               [&](AudioEffectResource &effect, AudioBusResource &,
                                   const char *) -> nk_result {
                                   *out_type = effect.type;
                                   return NK_OK;
                               });
        });
}

nk_result NK_CALL nk_audio_bus_effect_set_enabled(nk_audio_bus_effect effect_handle,
                                                  nk_bool enabled) {
    return nk::core::result_boundary(
        "unexpected error while setting an audio bus effect state", [&]() -> nk_result {
            if (const auto result = enter_audio_ui(); result != NK_OK)
                return result;
            if (enabled > 1)
                return invalid_argument("audio bus effect enabled state must be zero or one");
            return with_effect(effect_handle, "could not set audio bus effect state",
                               [&](AudioEffectResource &effect, AudioBusResource &bus,
                                   const char *) -> nk_result {
                                   const auto previous = effect.enabled;
                                   effect.enabled = enabled != 0;
                                   const auto result = rebuild_audio_bus_effect_chain(
                                       bus, "could not update audio bus effect chain");
                                   if (result != NK_OK)
                                       effect.enabled = previous;
                                   return result;
                               });
        });
}

nk_result NK_CALL nk_audio_bus_effect_is_enabled(nk_audio_bus_effect effect_handle,
                                                 nk_bool *out_enabled) {
    return nk::core::result_boundary(
        "unexpected error while querying an audio bus effect state", [&]() -> nk_result {
            if (const auto result = enter_audio_ui(); result != NK_OK)
                return result;
            if (!out_enabled)
                return invalid_argument("audio bus effect state output is missing");
            return with_effect(effect_handle, "could not query audio bus effect state",
                               [&](AudioEffectResource &effect, AudioBusResource &,
                                   const char *) -> nk_result {
                                   *out_enabled = effect.enabled ? 1u : 0u;
                                   return NK_OK;
                               });
        });
}

nk_result NK_CALL nk_audio_bus_effect_set_position(nk_audio_bus_effect effect_handle,
                                                   uint32_t position) {
    return nk::core::result_boundary(
        "unexpected error while moving an audio bus effect", [&]() -> nk_result {
            if (const auto result = enter_audio_ui(); result != NK_OK)
                return result;
            return with_effect(effect_handle, "could not move audio bus effect",
                               [&](AudioEffectResource &effect, AudioBusResource &bus,
                                   const char *) -> nk_result {
                                   const auto old_position = audio_effect_position(bus, effect);
                                   if (old_position == bus.effects.size())
                                       return invalid_handle(
                                           "audio bus effect is not attached to its bus");
                                   if (position >= bus.effects.size())
                                       return invalid_argument(
                                           "audio bus effect position is outside the chain");
                                   if (old_position == position)
                                       return NK_OK;
                                   auto moved = bus.effects[old_position];
                                   bus.effects.erase(bus.effects.begin() +
                                                     static_cast<std::ptrdiff_t>(old_position));
                                   bus.effects.insert(bus.effects.begin() +
                                                          static_cast<std::ptrdiff_t>(position),
                                                      std::move(moved));
                                   const auto result = rebuild_audio_bus_effect_chain(
                                       bus, "could not reorder audio bus effect chain");
                                   if (result != NK_OK) {
                                       auto restored = bus.effects[position];
                                       bus.effects.erase(bus.effects.begin() +
                                                         static_cast<std::ptrdiff_t>(position));
                                       bus.effects.insert(
                                           bus.effects.begin() +
                                               static_cast<std::ptrdiff_t>(old_position),
                                           std::move(restored));
                                   }
                                   return result;
                               });
        });
}

nk_result NK_CALL nk_audio_bus_effect_get_position(nk_audio_bus_effect effect_handle,
                                                   uint32_t *out_position) {
    return nk::core::result_boundary(
        "unexpected error while getting an audio bus effect position", [&]() -> nk_result {
            if (const auto result = enter_audio_ui(); result != NK_OK)
                return result;
            if (!out_position)
                return invalid_argument("audio bus effect position output is missing");
            return with_effect(effect_handle, "could not get audio bus effect position",
                               [&](AudioEffectResource &effect, AudioBusResource &bus,
                                   const char *) -> nk_result {
                                   const auto position = audio_effect_position(bus, effect);
                                   if (position == bus.effects.size())
                                       return invalid_handle(
                                           "audio bus effect is not attached to its bus");
                                   *out_position = static_cast<uint32_t>(position);
                                   return NK_OK;
                               });
        });
}

nk_result NK_CALL nk_audio_bus_effect_set_low_pass(nk_audio_bus_effect effect_handle,
                                                   float cutoff_frequency_hz, uint32_t order) {
    return nk::core::result_boundary(
        "unexpected error while configuring an audio bus low-pass effect", [&]() -> nk_result {
            if (const auto result = enter_audio_ui(); result != NK_OK)
                return result;
            return with_effect(effect_handle, "could not configure audio bus low-pass effect",
                               [&](AudioEffectResource &effect, AudioBusResource &bus,
                                   const char *) -> nk_result {
                                   if (effect.type != NK_AUDIO_EFFECT_LOW_PASS)
                                       return invalid_argument(
                                           "audio bus effect is not a low-pass filter");
                                   return reconfigure_audio_filter(effect, bus,
                                                                   cutoff_frequency_hz, order);
                               });
        });
}

nk_result NK_CALL nk_audio_bus_effect_get_low_pass(nk_audio_bus_effect effect_handle,
                                                   float *out_cutoff_frequency_hz,
                                                   uint32_t *out_order) {
    return nk::core::result_boundary(
        "unexpected error while getting an audio bus low-pass effect", [&]() -> nk_result {
            if (const auto result = enter_audio_ui(); result != NK_OK)
                return result;
            if (!out_cutoff_frequency_hz || !out_order)
                return invalid_argument("audio bus low-pass effect output is missing");
            return with_effect(effect_handle, "could not get audio bus low-pass effect",
                               [&](AudioEffectResource &effect, AudioBusResource &,
                                   const char *) -> nk_result {
                                   if (effect.type != NK_AUDIO_EFFECT_LOW_PASS)
                                       return invalid_argument(
                                           "audio bus effect is not a low-pass filter");
                                   *out_cutoff_frequency_hz = effect.cutoff_frequency_hz;
                                   *out_order = effect.order;
                                   return NK_OK;
                               });
        });
}

nk_result NK_CALL nk_audio_bus_effect_set_high_pass(nk_audio_bus_effect effect_handle,
                                                    float cutoff_frequency_hz, uint32_t order) {
    return nk::core::result_boundary(
        "unexpected error while configuring an audio bus high-pass effect", [&]() -> nk_result {
            if (const auto result = enter_audio_ui(); result != NK_OK)
                return result;
            return with_effect(effect_handle, "could not configure audio bus high-pass effect",
                               [&](AudioEffectResource &effect, AudioBusResource &bus,
                                   const char *) -> nk_result {
                                   if (effect.type != NK_AUDIO_EFFECT_HIGH_PASS)
                                       return invalid_argument(
                                           "audio bus effect is not a high-pass filter");
                                   return reconfigure_audio_filter(effect, bus,
                                                                   cutoff_frequency_hz, order);
                               });
        });
}

nk_result NK_CALL nk_audio_bus_effect_get_high_pass(nk_audio_bus_effect effect_handle,
                                                    float *out_cutoff_frequency_hz,
                                                    uint32_t *out_order) {
    return nk::core::result_boundary(
        "unexpected error while getting an audio bus high-pass effect", [&]() -> nk_result {
            if (const auto result = enter_audio_ui(); result != NK_OK)
                return result;
            if (!out_cutoff_frequency_hz || !out_order)
                return invalid_argument("audio bus high-pass effect output is missing");
            return with_effect(effect_handle, "could not get audio bus high-pass effect",
                               [&](AudioEffectResource &effect, AudioBusResource &,
                                   const char *) -> nk_result {
                                   if (effect.type != NK_AUDIO_EFFECT_HIGH_PASS)
                                       return invalid_argument(
                                           "audio bus effect is not a high-pass filter");
                                   *out_cutoff_frequency_hz = effect.cutoff_frequency_hz;
                                   *out_order = effect.order;
                                   return NK_OK;
                               });
        });
}

nk_result NK_CALL nk_audio_bus_effect_set_delay_wet(nk_audio_bus_effect effect_handle, float wet) {
    return nk::core::result_boundary(
        "unexpected error while setting an audio bus delay wet gain", [&]() -> nk_result {
            if (const auto result = enter_audio_ui(); result != NK_OK)
                return result;
            if (!valid_audio_delay_gain(wet))
                return invalid_argument("audio bus delay wet gain must be in the range [0, 1]");
            return with_effect(effect_handle, "could not set audio bus delay wet gain",
                               [&](AudioEffectResource &effect, AudioBusResource &,
                                   const char *) -> nk_result {
                                   if (effect.type != NK_AUDIO_EFFECT_DELAY)
                                       return invalid_argument("audio bus effect is not a delay");
                                   ma_delay_node_set_wet(effect.delay.get(), wet);
                                   return NK_OK;
                               });
        });
}

nk_result NK_CALL nk_audio_bus_effect_get_delay_wet(nk_audio_bus_effect effect_handle,
                                                   float *out_wet) {
    return nk::core::result_boundary(
        "unexpected error while getting an audio bus delay wet gain", [&]() -> nk_result {
            if (const auto result = enter_audio_ui(); result != NK_OK)
                return result;
            if (!out_wet)
                return invalid_argument("audio bus delay wet gain output is missing");
            return with_effect(effect_handle, "could not get audio bus delay wet gain",
                               [&](AudioEffectResource &effect, AudioBusResource &,
                                   const char *) -> nk_result {
                                   if (effect.type != NK_AUDIO_EFFECT_DELAY)
                                       return invalid_argument("audio bus effect is not a delay");
                                   *out_wet = ma_delay_node_get_wet(effect.delay.get());
                                   return NK_OK;
                               });
        });
}

nk_result NK_CALL nk_audio_bus_effect_set_delay_dry(nk_audio_bus_effect effect_handle, float dry) {
    return nk::core::result_boundary(
        "unexpected error while setting an audio bus delay dry gain", [&]() -> nk_result {
            if (const auto result = enter_audio_ui(); result != NK_OK)
                return result;
            if (!valid_audio_delay_gain(dry))
                return invalid_argument("audio bus delay dry gain must be in the range [0, 1]");
            return with_effect(effect_handle, "could not set audio bus delay dry gain",
                               [&](AudioEffectResource &effect, AudioBusResource &,
                                   const char *) -> nk_result {
                                   if (effect.type != NK_AUDIO_EFFECT_DELAY)
                                       return invalid_argument("audio bus effect is not a delay");
                                   ma_delay_node_set_dry(effect.delay.get(), dry);
                                   return NK_OK;
                               });
        });
}

nk_result NK_CALL nk_audio_bus_effect_get_delay_dry(nk_audio_bus_effect effect_handle,
                                                   float *out_dry) {
    return nk::core::result_boundary(
        "unexpected error while getting an audio bus delay dry gain", [&]() -> nk_result {
            if (const auto result = enter_audio_ui(); result != NK_OK)
                return result;
            if (!out_dry)
                return invalid_argument("audio bus delay dry gain output is missing");
            return with_effect(effect_handle, "could not get audio bus delay dry gain",
                               [&](AudioEffectResource &effect, AudioBusResource &,
                                   const char *) -> nk_result {
                                   if (effect.type != NK_AUDIO_EFFECT_DELAY)
                                       return invalid_argument("audio bus effect is not a delay");
                                   *out_dry = ma_delay_node_get_dry(effect.delay.get());
                                   return NK_OK;
                               });
        });
}

nk_result NK_CALL nk_audio_bus_effect_set_delay_decay(nk_audio_bus_effect effect_handle,
                                                      float decay) {
    return nk::core::result_boundary(
        "unexpected error while setting an audio bus delay decay", [&]() -> nk_result {
            if (const auto result = enter_audio_ui(); result != NK_OK)
                return result;
            if (!std::isfinite(decay) || decay < 0.0f || decay > 1.0f)
                return invalid_argument("audio bus delay decay must be in the range [0, 1]");
            return with_effect(effect_handle, "could not set audio bus delay decay",
                               [&](AudioEffectResource &effect, AudioBusResource &,
                                   const char *) -> nk_result {
                                   if (effect.type != NK_AUDIO_EFFECT_DELAY)
                                       return invalid_argument("audio bus effect is not a delay");
                                   ma_delay_node_set_decay(effect.delay.get(), decay);
                                   effect.decay = decay;
                                   return NK_OK;
                               });
        });
}

nk_result NK_CALL nk_audio_bus_effect_get_delay_decay(nk_audio_bus_effect effect_handle,
                                                      float *out_decay) {
    return nk::core::result_boundary(
        "unexpected error while getting an audio bus delay decay", [&]() -> nk_result {
            if (const auto result = enter_audio_ui(); result != NK_OK)
                return result;
            if (!out_decay)
                return invalid_argument("audio bus delay decay output is missing");
            return with_effect(effect_handle, "could not get audio bus delay decay",
                               [&](AudioEffectResource &effect, AudioBusResource &,
                                   const char *) -> nk_result {
                                   if (effect.type != NK_AUDIO_EFFECT_DELAY)
                                       return invalid_argument("audio bus effect is not a delay");
                                   *out_decay = ma_delay_node_get_decay(effect.delay.get());
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

nk_result NK_CALL nk_audio_clip_create_from_asset(nk_resource_asset asset,
                                                  nk_audio_clip *out_clip) {
    return nk::core::result_boundary(
        "unexpected error while creating an audio clip from a resource asset",
        [&]() -> nk_result {
            if (const auto result = enter_audio_ui(); result != NK_OK)
                return result;
            if (asset == NK_INVALID_HANDLE || !out_clip)
                return invalid_argument("audio clip resource asset or output is invalid");
            *out_clip = NK_INVALID_HANDLE;
            nk_result clip_result = NK_OK;
            auto clip = create_clip_from_asset(asset, clip_result);
            if (!clip)
                return clip_result;
            return insert_clip(std::move(clip), out_clip);
        });
}

nk_result NK_CALL nk_audio_clip_create_from_stream(const nk_resource *resource,
                                                   nk_audio_clip *out_clip) {
    return nk::core::result_boundary(
        "unexpected error while creating a streaming audio clip", [&]() -> nk_result {
            if (const auto result = enter_audio_ui(); result != NK_OK)
                return result;
            if (!resource || resource->struct_size < sizeof(nk_resource) ||
                (resource->flags & NK_RESOURCE_READABLE) == 0 || !resource->uri ||
                !*resource->uri || !out_clip)
                return invalid_argument("streaming audio resource or output is invalid");
            *out_clip = NK_INVALID_HANDLE;
            nk_result clip_result = NK_OK;
            auto clip = create_clip_from_stream(resource, clip_result);
            if (!clip)
                return clip_result;
            return insert_clip(std::move(clip), out_clip);
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
        value->handle.store(NK_INVALID_HANDLE, std::memory_order_release);
        if (!nk::core::handles().erase(clip, nk::core::ResourceType::audio_clip))
            return invalid_handle("invalid audio clip handle");
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
        const auto engine = voice->engine;
        stop_voice_internal(*voice, "could not stop audio voice");
        voice->handle.store(NK_INVALID_HANDLE, std::memory_order_release);
        if (!nk::core::handles().erase(sound, nk::core::ResourceType::audio_voice))
            return invalid_handle("invalid audio voice handle");
        promote_virtual_voices(*engine);
        return NK_OK;
    });
}

nk_result NK_CALL nk_audio_voice_start(nk_audio_voice sound) {
    return nk::core::result_boundary("unexpected error while starting an audio voice", [&]() {
        if (const auto result = enter_audio_ui(); result != NK_OK)
            return result;
        return with_voice(sound, "could not start audio voice", [](AudioVoiceResource &value,
                                                                  const char *message) {
            return start_voice_internal(value, message);
        });
    });
}

nk_result NK_CALL nk_audio_voice_stop(nk_audio_voice sound) {
    return nk::core::result_boundary("unexpected error while stopping an audio voice", [&]() {
        if (const auto result = enter_audio_ui(); result != NK_OK)
            return result;
        return with_voice(sound, "could not stop audio voice", [](AudioVoiceResource &value,
                                                                 const char *message) {
            const auto result = stop_voice_internal(value, message);
            promote_virtual_voices(*value.engine);
            return result;
        });
    });
}

nk_result NK_CALL nk_audio_voice_rewind(nk_audio_voice sound) {
    return nk::core::result_boundary("unexpected error while rewinding an audio voice", [&]() {
        if (const auto result = enter_audio_ui(); result != NK_OK)
            return result;
        return with_voice(sound, "could not rewind audio voice", [](AudioVoiceResource &value,
                                                                    const char *message) -> nk_result {
            if (value.virtualized.load(std::memory_order_acquire)) {
                value.virtual_cursor_frames = 0;
                value.virtual_start_time_frames =
                    ma_engine_get_time_in_pcm_frames(&value.engine->engine);
                return NK_OK;
            }
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
                finish_virtual_voice_if_at_end(value);
                *out_playing = value.virtualized.load(std::memory_order_acquire)
                                   ? 1u
                                   : (ma_sound_is_playing(&value.sound) ? 1u : 0u);
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
                                  if (value.virtualized.load(std::memory_order_acquire))
                                      *out_at_end = finish_virtual_voice_if_at_end(value) ? 1u : 0u;
                                  else
                                      *out_at_end = ma_sound_at_end(&value.sound) ? 1u : 0u;
                                  return NK_OK;
                              });
        });
}

nk_result NK_CALL nk_audio_voice_set_bus(nk_audio_voice sound, nk_audio_bus bus) {
    return nk::core::result_boundary(
        "unexpected error while routing an audio voice", [&]() -> nk_result {
            if (const auto result = enter_audio_ui(); result != NK_OK)
                return result;
            return with_voice(sound, "could not route audio voice", [&](AudioVoiceResource &value,
                                                                        const char *message)
                                                                       -> nk_result {
                if (value.logically_playing.load(std::memory_order_acquire) ||
                    value.virtualized.load(std::memory_order_acquire))
                    return invalid_request("audio voice bus can only be changed while stopped");

                std::shared_ptr<AudioBusResource> destination;
                if (bus != NK_INVALID_HANDLE) {
                    destination = get_bus(bus);
                    if (!destination)
                        return NK_ERROR_INVALID_HANDLE;
                    if (destination->engine.get() != value.engine.get())
                        return invalid_request(
                            "audio voice and bus belong to different audio engines");
                }

                auto *destination_node = destination
                                             ? static_cast<ma_node *>(&destination->group)
                                             : ma_engine_get_endpoint(&value.engine->engine);
                const auto result = ma_node_attach_output_bus(
                    reinterpret_cast<ma_node *>(&value.sound), 0, destination_node, 0);
                if (result != MA_SUCCESS)
                    return map_miniaudio_result(result, message);
                value.bus = std::move(destination);
                return NK_OK;
            });
        });
}

nk_result NK_CALL nk_audio_voice_get_bus(nk_audio_voice sound, nk_audio_bus *out_bus) {
    return nk::core::result_boundary(
        "unexpected error while querying an audio voice bus", [&]() -> nk_result {
            if (const auto result = enter_audio_ui(); result != NK_OK)
                return result;
            if (!out_bus)
                return invalid_argument("audio voice bus output is missing");
            return with_voice(sound, "could not query audio voice bus",
                              [&](AudioVoiceResource &value, const char *) {
                                  *out_bus = value.bus
                                                   ? value.bus->handle.load(std::memory_order_acquire)
                                                   : NK_INVALID_HANDLE;
                                  return NK_OK;
                              });
        });
}

nk_result NK_CALL nk_audio_voice_set_priority(nk_audio_voice sound, uint32_t priority) {
    return nk::core::result_boundary(
        "unexpected error while setting audio voice priority", [&]() -> nk_result {
            if (const auto result = enter_audio_ui(); result != NK_OK)
                return result;
            return with_voice(sound, "could not set audio voice priority",
                              [&](AudioVoiceResource &value, const char *) {
                                  value.priority = priority;
                                  return NK_OK;
                              });
        });
}

nk_result NK_CALL nk_audio_voice_get_priority(nk_audio_voice sound, uint32_t *out_priority) {
    return nk::core::result_boundary(
        "unexpected error while getting audio voice priority", [&]() -> nk_result {
            if (const auto result = enter_audio_ui(); result != NK_OK)
                return result;
            if (!out_priority)
                return invalid_argument("audio voice priority output is missing");
            return with_voice(sound, "could not get audio voice priority",
                              [&](AudioVoiceResource &value, const char *) {
                                  *out_priority = value.priority;
                                  return NK_OK;
                              });
        });
}

nk_result NK_CALL nk_audio_voice_is_virtualized(nk_audio_voice sound,
                                                 nk_bool *out_virtualized) {
    return nk::core::result_boundary(
        "unexpected error while querying audio voice virtualization", [&]() -> nk_result {
            if (const auto result = enter_audio_ui(); result != NK_OK)
                return result;
            if (!out_virtualized)
                return invalid_argument("audio voice virtualization output is missing");
            return with_voice(sound, "could not query audio voice virtualization",
                              [&](AudioVoiceResource &value, const char *) {
                                  finish_virtual_voice_if_at_end(value);
                                  *out_virtualized =
                                      value.virtualized.load(std::memory_order_acquire) ? 1u : 0u;
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
                              [&](AudioVoiceResource &value, const char *message) -> nk_result {
                                  if (value.virtualized.load(std::memory_order_acquire)) {
                                      const auto sample_rate =
                                          ma_engine_get_sample_rate(&value.engine->engine);
                                      if (sample_rate == 0)
                                          return invalid_request("audio sample rate is unavailable");
                                      *out_seconds = static_cast<float>(virtual_voice_cursor(value)) /
                                                     static_cast<float>(sample_rate);
                                      return NK_OK;
                                  }
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

nk_result NK_CALL nk_audio_listener_set_position(nk_audio_vec3 position) {
    return nk::core::result_boundary(
        "unexpected error while setting the audio listener position", [&]() -> nk_result {
            if (const auto result = enter_audio_ui(); result != NK_OK)
                return result;
            if (!valid_audio_vec3(position))
                return invalid_argument("audio listener position must contain finite values");
            return with_engine("could not set audio listener position",
                               [&](AudioEngineResource &value, const char *) {
                                   ma_engine_listener_set_position(&value.engine, 0, position.x,
                                                                   position.y, position.z);
                                   return NK_OK;
                               });
        });
}

nk_result NK_CALL nk_audio_listener_get_position(nk_audio_vec3 *out_position) {
    return nk::core::result_boundary(
        "unexpected error while getting the audio listener position", [&]() -> nk_result {
            if (const auto result = enter_audio_ui(); result != NK_OK)
                return result;
            if (!out_position)
                return invalid_argument("audio listener position output is missing");
            return with_engine("could not get audio listener position",
                               [&](AudioEngineResource &value, const char *) {
                                   *out_position = audio_vec3(
                                       ma_engine_listener_get_position(&value.engine, 0));
                                   return NK_OK;
                               });
        });
}

nk_result NK_CALL nk_audio_listener_set_direction(nk_audio_vec3 direction) {
    return nk::core::result_boundary(
        "unexpected error while setting the audio listener direction", [&]() -> nk_result {
            if (const auto result = enter_audio_ui(); result != NK_OK)
                return result;
            if (!valid_audio_direction(direction))
                return invalid_argument(
                    "audio listener direction must contain finite, non-zero values");
            return with_engine("could not set audio listener direction",
                               [&](AudioEngineResource &value, const char *) {
                                   ma_engine_listener_set_direction(&value.engine, 0, direction.x,
                                                                    direction.y, direction.z);
                                   return NK_OK;
                               });
        });
}

nk_result NK_CALL nk_audio_listener_get_direction(nk_audio_vec3 *out_direction) {
    return nk::core::result_boundary(
        "unexpected error while getting the audio listener direction", [&]() -> nk_result {
            if (const auto result = enter_audio_ui(); result != NK_OK)
                return result;
            if (!out_direction)
                return invalid_argument("audio listener direction output is missing");
            return with_engine("could not get audio listener direction",
                               [&](AudioEngineResource &value, const char *) {
                                   *out_direction = audio_vec3(
                                       ma_engine_listener_get_direction(&value.engine, 0));
                                   return NK_OK;
                               });
        });
}

nk_result NK_CALL nk_audio_listener_set_velocity(nk_audio_vec3 velocity) {
    return nk::core::result_boundary(
        "unexpected error while setting the audio listener velocity", [&]() -> nk_result {
            if (const auto result = enter_audio_ui(); result != NK_OK)
                return result;
            if (!valid_audio_vec3(velocity))
                return invalid_argument("audio listener velocity must contain finite values");
            return with_engine("could not set audio listener velocity",
                               [&](AudioEngineResource &value, const char *) {
                                   ma_engine_listener_set_velocity(&value.engine, 0, velocity.x,
                                                                   velocity.y, velocity.z);
                                   return NK_OK;
                               });
        });
}

nk_result NK_CALL nk_audio_listener_get_velocity(nk_audio_vec3 *out_velocity) {
    return nk::core::result_boundary(
        "unexpected error while getting the audio listener velocity", [&]() -> nk_result {
            if (const auto result = enter_audio_ui(); result != NK_OK)
                return result;
            if (!out_velocity)
                return invalid_argument("audio listener velocity output is missing");
            return with_engine("could not get audio listener velocity",
                               [&](AudioEngineResource &value, const char *) {
                                   *out_velocity = audio_vec3(
                                       ma_engine_listener_get_velocity(&value.engine, 0));
                                   return NK_OK;
                               });
        });
}

nk_result NK_CALL nk_audio_listener_set_world_up(nk_audio_vec3 world_up) {
    return nk::core::result_boundary(
        "unexpected error while setting the audio listener world up", [&]() -> nk_result {
            if (const auto result = enter_audio_ui(); result != NK_OK)
                return result;
            if (!valid_audio_direction(world_up))
                return invalid_argument(
                    "audio listener world up must contain finite, non-zero values");
            return with_engine("could not set audio listener world up",
                               [&](AudioEngineResource &value, const char *) {
                                   ma_engine_listener_set_world_up(&value.engine, 0, world_up.x,
                                                                   world_up.y, world_up.z);
                                   return NK_OK;
                               });
        });
}

nk_result NK_CALL nk_audio_listener_get_world_up(nk_audio_vec3 *out_world_up) {
    return nk::core::result_boundary(
        "unexpected error while getting the audio listener world up", [&]() -> nk_result {
            if (const auto result = enter_audio_ui(); result != NK_OK)
                return result;
            if (!out_world_up)
                return invalid_argument("audio listener world up output is missing");
            return with_engine("could not get audio listener world up",
                               [&](AudioEngineResource &value, const char *) {
                                   *out_world_up = audio_vec3(
                                       ma_engine_listener_get_world_up(&value.engine, 0));
                                   return NK_OK;
                               });
        });
}

nk_result NK_CALL nk_audio_listener_set_cone(float inner_angle_radians, float outer_angle_radians,
                                             float outer_gain) {
    return nk::core::result_boundary(
        "unexpected error while setting the audio listener cone", [&]() -> nk_result {
            if (const auto result = enter_audio_ui(); result != NK_OK)
                return result;
            if (!valid_audio_cone(inner_angle_radians, outer_angle_radians, outer_gain))
                return invalid_argument(
                    "audio listener cone angles or gain are outside their valid ranges");
            return with_engine("could not set audio listener cone",
                               [&](AudioEngineResource &value, const char *) {
                                   ma_engine_listener_set_cone(&value.engine, 0, inner_angle_radians,
                                                               outer_angle_radians, outer_gain);
                                   return NK_OK;
                               });
        });
}

nk_result NK_CALL nk_audio_listener_get_cone(float *out_inner_angle_radians,
                                             float *out_outer_angle_radians, float *out_outer_gain) {
    return nk::core::result_boundary(
        "unexpected error while getting the audio listener cone", [&]() -> nk_result {
            if (const auto result = enter_audio_ui(); result != NK_OK)
                return result;
            if (!out_inner_angle_radians || !out_outer_angle_radians || !out_outer_gain)
                return invalid_argument("audio listener cone output is missing");
            return with_engine("could not get audio listener cone",
                               [&](AudioEngineResource &value, const char *) {
                                   ma_engine_listener_get_cone(&value.engine, 0,
                                                               out_inner_angle_radians,
                                                               out_outer_angle_radians, out_outer_gain);
                                   return NK_OK;
                               });
        });
}

nk_result NK_CALL nk_audio_listener_set_speed_of_sound(float speed) {
    return nk::core::result_boundary(
        "unexpected error while setting the audio listener speed of sound", [&]() -> nk_result {
            if (const auto result = enter_audio_ui(); result != NK_OK)
                return result;
            if (!std::isfinite(speed) || speed <= 0.0f)
                return invalid_argument("audio listener speed of sound must be finite and positive");
            return with_engine("could not set audio listener speed of sound",
                               [&](AudioEngineResource &value, const char *) {
                                   auto *listener = &value.engine.listeners[0];
                                   ma_spatializer_listener_set_speed_of_sound(listener, speed);
                                   return NK_OK;
                               });
        });
}

nk_result NK_CALL nk_audio_listener_get_speed_of_sound(float *out_speed) {
    return nk::core::result_boundary(
        "unexpected error while getting the audio listener speed of sound", [&]() -> nk_result {
            if (const auto result = enter_audio_ui(); result != NK_OK)
                return result;
            if (!out_speed)
                return invalid_argument("audio listener speed of sound output is missing");
            return with_engine("could not get audio listener speed of sound",
                               [&](AudioEngineResource &value, const char *) {
                                   *out_speed = ma_spatializer_listener_get_speed_of_sound(
                                       &value.engine.listeners[0]);
                                   return NK_OK;
                               });
        });
}

nk_result NK_CALL nk_audio_voice_set_spatialization_enabled(nk_audio_voice sound, nk_bool enabled) {
    return nk::core::result_boundary(
        "unexpected error while setting audio voice spatialization", [&]() -> nk_result {
            if (const auto result = enter_audio_ui(); result != NK_OK)
                return result;
            if (enabled > 1)
                return invalid_argument("audio voice spatialization must be zero or one");
            return with_voice(sound, "could not set audio voice spatialization",
                              [&](AudioVoiceResource &value, const char *) {
                                  ma_sound_set_spatialization_enabled(&value.sound,
                                                                      enabled != 0 ? MA_TRUE
                                                                                   : MA_FALSE);
                                  return NK_OK;
                              });
        });
}

nk_result NK_CALL nk_audio_voice_is_spatialization_enabled(nk_audio_voice sound,
                                                            nk_bool *out_enabled) {
    return nk::core::result_boundary(
        "unexpected error while querying audio voice spatialization", [&]() -> nk_result {
            if (const auto result = enter_audio_ui(); result != NK_OK)
                return result;
            if (!out_enabled)
                return invalid_argument("audio voice spatialization output is missing");
            return with_voice(sound, "could not query audio voice spatialization",
                              [&](AudioVoiceResource &value, const char *) {
                                  *out_enabled = ma_sound_is_spatialization_enabled(&value.sound)
                                                     ? 1u
                                                     : 0u;
                                  return NK_OK;
                              });
        });
}

nk_result NK_CALL nk_audio_voice_set_position(nk_audio_voice sound, nk_audio_vec3 position) {
    return nk::core::result_boundary(
        "unexpected error while setting audio voice position", [&]() -> nk_result {
            if (const auto result = enter_audio_ui(); result != NK_OK)
                return result;
            if (!valid_audio_vec3(position))
                return invalid_argument("audio voice position must contain finite values");
            return with_voice(sound, "could not set audio voice position",
                              [&](AudioVoiceResource &value, const char *) {
                                  ma_sound_set_position(&value.sound, position.x, position.y,
                                                        position.z);
                                  return NK_OK;
                              });
        });
}

nk_result NK_CALL nk_audio_voice_get_position(nk_audio_voice sound, nk_audio_vec3 *out_position) {
    return nk::core::result_boundary(
        "unexpected error while getting audio voice position", [&]() -> nk_result {
            if (const auto result = enter_audio_ui(); result != NK_OK)
                return result;
            if (!out_position)
                return invalid_argument("audio voice position output is missing");
            return with_voice(sound, "could not get audio voice position",
                              [&](AudioVoiceResource &value, const char *) {
                                  *out_position = audio_vec3(ma_sound_get_position(&value.sound));
                                  return NK_OK;
                              });
        });
}

nk_result NK_CALL nk_audio_voice_set_direction(nk_audio_voice sound, nk_audio_vec3 direction) {
    return nk::core::result_boundary(
        "unexpected error while setting audio voice direction", [&]() -> nk_result {
            if (const auto result = enter_audio_ui(); result != NK_OK)
                return result;
            if (!valid_audio_direction(direction))
                return invalid_argument(
                    "audio voice direction must contain finite, non-zero values");
            return with_voice(sound, "could not set audio voice direction",
                              [&](AudioVoiceResource &value, const char *) {
                                  ma_sound_set_direction(&value.sound, direction.x, direction.y,
                                                         direction.z);
                                  return NK_OK;
                              });
        });
}

nk_result NK_CALL nk_audio_voice_get_direction(nk_audio_voice sound,
                                               nk_audio_vec3 *out_direction) {
    return nk::core::result_boundary(
        "unexpected error while getting audio voice direction", [&]() -> nk_result {
            if (const auto result = enter_audio_ui(); result != NK_OK)
                return result;
            if (!out_direction)
                return invalid_argument("audio voice direction output is missing");
            return with_voice(sound, "could not get audio voice direction",
                              [&](AudioVoiceResource &value, const char *) {
                                  *out_direction = audio_vec3(ma_sound_get_direction(&value.sound));
                                  return NK_OK;
                              });
        });
}

nk_result NK_CALL nk_audio_voice_set_velocity(nk_audio_voice sound, nk_audio_vec3 velocity) {
    return nk::core::result_boundary(
        "unexpected error while setting audio voice velocity", [&]() -> nk_result {
            if (const auto result = enter_audio_ui(); result != NK_OK)
                return result;
            if (!valid_audio_vec3(velocity))
                return invalid_argument("audio voice velocity must contain finite values");
            return with_voice(sound, "could not set audio voice velocity",
                              [&](AudioVoiceResource &value, const char *) {
                                  ma_sound_set_velocity(&value.sound, velocity.x, velocity.y,
                                                        velocity.z);
                                  return NK_OK;
                              });
        });
}

nk_result NK_CALL nk_audio_voice_get_velocity(nk_audio_voice sound,
                                              nk_audio_vec3 *out_velocity) {
    return nk::core::result_boundary(
        "unexpected error while getting audio voice velocity", [&]() -> nk_result {
            if (const auto result = enter_audio_ui(); result != NK_OK)
                return result;
            if (!out_velocity)
                return invalid_argument("audio voice velocity output is missing");
            return with_voice(sound, "could not get audio voice velocity",
                              [&](AudioVoiceResource &value, const char *) {
                                  *out_velocity = audio_vec3(ma_sound_get_velocity(&value.sound));
                                  return NK_OK;
                              });
        });
}

nk_result NK_CALL nk_audio_voice_set_attenuation_model(nk_audio_voice sound,
                                                       nk_audio_attenuation_model model) {
    return nk::core::result_boundary(
        "unexpected error while setting audio voice attenuation model", [&]() -> nk_result {
            if (const auto result = enter_audio_ui(); result != NK_OK)
                return result;
            if (!valid_audio_attenuation_model(model))
                return invalid_argument("audio voice attenuation model is invalid");
            return with_voice(sound, "could not set audio voice attenuation model",
                              [&](AudioVoiceResource &value, const char *) {
                                  ma_sound_set_attenuation_model(
                                      &value.sound, static_cast<ma_attenuation_model>(model));
                                  return NK_OK;
                              });
        });
}

nk_result NK_CALL nk_audio_voice_get_attenuation_model(nk_audio_voice sound,
                                                       nk_audio_attenuation_model *out_model) {
    return nk::core::result_boundary(
        "unexpected error while getting audio voice attenuation model", [&]() -> nk_result {
            if (const auto result = enter_audio_ui(); result != NK_OK)
                return result;
            if (!out_model)
                return invalid_argument("audio voice attenuation model output is missing");
            return with_voice(sound, "could not get audio voice attenuation model",
                              [&](AudioVoiceResource &value, const char *) {
                                  *out_model = static_cast<nk_audio_attenuation_model>(
                                      ma_sound_get_attenuation_model(&value.sound));
                                  return NK_OK;
                              });
        });
}

nk_result NK_CALL nk_audio_voice_set_positioning(nk_audio_voice sound,
                                                 nk_audio_positioning positioning) {
    return nk::core::result_boundary(
        "unexpected error while setting audio voice positioning", [&]() -> nk_result {
            if (const auto result = enter_audio_ui(); result != NK_OK)
                return result;
            if (!valid_audio_positioning(positioning))
                return invalid_argument("audio voice positioning is invalid");
            return with_voice(sound, "could not set audio voice positioning",
                              [&](AudioVoiceResource &value, const char *) {
                                  ma_sound_set_positioning(
                                      &value.sound, static_cast<ma_positioning>(positioning));
                                  return NK_OK;
                              });
        });
}

nk_result NK_CALL nk_audio_voice_get_positioning(nk_audio_voice sound,
                                                 nk_audio_positioning *out_positioning) {
    return nk::core::result_boundary(
        "unexpected error while getting audio voice positioning", [&]() -> nk_result {
            if (const auto result = enter_audio_ui(); result != NK_OK)
                return result;
            if (!out_positioning)
                return invalid_argument("audio voice positioning output is missing");
            return with_voice(sound, "could not get audio voice positioning",
                              [&](AudioVoiceResource &value, const char *) {
                                  *out_positioning = static_cast<nk_audio_positioning>(
                                      ma_sound_get_positioning(&value.sound));
                                  return NK_OK;
                              });
        });
}

nk_result NK_CALL nk_audio_voice_set_rolloff(nk_audio_voice sound, float rolloff) {
    return nk::core::result_boundary(
        "unexpected error while setting audio voice rolloff", [&]() -> nk_result {
            if (const auto result = enter_audio_ui(); result != NK_OK)
                return result;
            if (!std::isfinite(rolloff) || rolloff < 0.0f)
                return invalid_argument("audio voice rolloff must be finite and non-negative");
            return with_voice(sound, "could not set audio voice rolloff",
                              [&](AudioVoiceResource &value, const char *) {
                                  ma_sound_set_rolloff(&value.sound, rolloff);
                                  return NK_OK;
                              });
        });
}

nk_result NK_CALL nk_audio_voice_get_rolloff(nk_audio_voice sound, float *out_rolloff) {
    return nk::core::result_boundary(
        "unexpected error while getting audio voice rolloff", [&]() -> nk_result {
            if (const auto result = enter_audio_ui(); result != NK_OK)
                return result;
            if (!out_rolloff)
                return invalid_argument("audio voice rolloff output is missing");
            return with_voice(sound, "could not get audio voice rolloff",
                              [&](AudioVoiceResource &value, const char *) {
                                  *out_rolloff = ma_sound_get_rolloff(&value.sound);
                                  return NK_OK;
                              });
        });
}

nk_result NK_CALL nk_audio_voice_set_gain_limits(nk_audio_voice sound, float min_gain,
                                                 float max_gain) {
    return nk::core::result_boundary(
        "unexpected error while setting audio voice gain limits", [&]() -> nk_result {
            if (const auto result = enter_audio_ui(); result != NK_OK)
                return result;
            if (!valid_audio_gain_limits(min_gain, max_gain))
                return invalid_argument(
                    "audio voice gain limits must be finite, non-negative, and ordered");
            return with_voice(sound, "could not set audio voice gain limits",
                              [&](AudioVoiceResource &value, const char *) {
                                  ma_sound_set_min_gain(&value.sound, min_gain);
                                  ma_sound_set_max_gain(&value.sound, max_gain);
                                  return NK_OK;
                              });
        });
}

nk_result NK_CALL nk_audio_voice_get_gain_limits(nk_audio_voice sound, float *out_min_gain,
                                                 float *out_max_gain) {
    return nk::core::result_boundary(
        "unexpected error while getting audio voice gain limits", [&]() -> nk_result {
            if (const auto result = enter_audio_ui(); result != NK_OK)
                return result;
            if (!out_min_gain || !out_max_gain)
                return invalid_argument("audio voice gain limits output is missing");
            return with_voice(sound, "could not get audio voice gain limits",
                              [&](AudioVoiceResource &value, const char *) {
                                  *out_min_gain = ma_sound_get_min_gain(&value.sound);
                                  *out_max_gain = ma_sound_get_max_gain(&value.sound);
                                  return NK_OK;
                              });
        });
}

nk_result NK_CALL nk_audio_voice_set_distance_limits(nk_audio_voice sound, float min_distance,
                                                     float max_distance) {
    return nk::core::result_boundary(
        "unexpected error while setting audio voice distance limits", [&]() -> nk_result {
            if (const auto result = enter_audio_ui(); result != NK_OK)
                return result;
            if (!valid_audio_distance_limits(min_distance, max_distance))
                return invalid_argument(
                    "audio voice distance limits must be finite, positive, and ordered");
            return with_voice(sound, "could not set audio voice distance limits",
                              [&](AudioVoiceResource &value, const char *) {
                                  ma_sound_set_min_distance(&value.sound, min_distance);
                                  ma_sound_set_max_distance(&value.sound, max_distance);
                                  return NK_OK;
                              });
        });
}

nk_result NK_CALL nk_audio_voice_get_distance_limits(nk_audio_voice sound, float *out_min_distance,
                                                     float *out_max_distance) {
    return nk::core::result_boundary(
        "unexpected error while getting audio voice distance limits", [&]() -> nk_result {
            if (const auto result = enter_audio_ui(); result != NK_OK)
                return result;
            if (!out_min_distance || !out_max_distance)
                return invalid_argument("audio voice distance limits output is missing");
            return with_voice(sound, "could not get audio voice distance limits",
                              [&](AudioVoiceResource &value, const char *) {
                                  *out_min_distance = ma_sound_get_min_distance(&value.sound);
                                  *out_max_distance = ma_sound_get_max_distance(&value.sound);
                                  return NK_OK;
                              });
        });
}

nk_result NK_CALL nk_audio_voice_set_doppler_factor(nk_audio_voice sound, float factor) {
    return nk::core::result_boundary(
        "unexpected error while setting audio voice Doppler factor", [&]() -> nk_result {
            if (const auto result = enter_audio_ui(); result != NK_OK)
                return result;
            if (!std::isfinite(factor) || factor < 0.0f)
                return invalid_argument(
                    "audio voice Doppler factor must be finite and non-negative");
            return with_voice(sound, "could not set audio voice Doppler factor",
                              [&](AudioVoiceResource &value, const char *) {
                                  ma_sound_set_doppler_factor(&value.sound, factor);
                                  return NK_OK;
                              });
        });
}

nk_result NK_CALL nk_audio_voice_get_doppler_factor(nk_audio_voice sound, float *out_factor) {
    return nk::core::result_boundary(
        "unexpected error while getting audio voice Doppler factor", [&]() -> nk_result {
            if (const auto result = enter_audio_ui(); result != NK_OK)
                return result;
            if (!out_factor)
                return invalid_argument("audio voice Doppler factor output is missing");
            return with_voice(sound, "could not get audio voice Doppler factor",
                              [&](AudioVoiceResource &value, const char *) {
                                  *out_factor = ma_sound_get_doppler_factor(&value.sound);
                                  return NK_OK;
                              });
        });
}

nk_result NK_CALL nk_audio_voice_set_cone(nk_audio_voice sound, float inner_angle_radians,
                                          float outer_angle_radians, float outer_gain) {
    return nk::core::result_boundary(
        "unexpected error while setting audio voice cone", [&]() -> nk_result {
            if (const auto result = enter_audio_ui(); result != NK_OK)
                return result;
            if (!valid_audio_cone(inner_angle_radians, outer_angle_radians, outer_gain))
                return invalid_argument("audio voice cone angles or gain are outside their valid ranges");
            return with_voice(sound, "could not set audio voice cone",
                              [&](AudioVoiceResource &value, const char *) {
                                  ma_sound_set_cone(&value.sound, inner_angle_radians,
                                                    outer_angle_radians, outer_gain);
                                  return NK_OK;
                              });
        });
}

nk_result NK_CALL nk_audio_voice_get_cone(nk_audio_voice sound, float *out_inner_angle_radians,
                                          float *out_outer_angle_radians, float *out_outer_gain) {
    return nk::core::result_boundary(
        "unexpected error while getting audio voice cone", [&]() -> nk_result {
            if (const auto result = enter_audio_ui(); result != NK_OK)
                return result;
            if (!out_inner_angle_radians || !out_outer_angle_radians || !out_outer_gain)
                return invalid_argument("audio voice cone output is missing");
            return with_voice(sound, "could not get audio voice cone",
                              [&](AudioVoiceResource &value, const char *) {
                                  ma_sound_get_cone(&value.sound, out_inner_angle_radians,
                                                    out_outer_angle_radians, out_outer_gain);
                                  return NK_OK;
                              });
        });
}

nk_result NK_CALL nk_audio_voice_set_directional_attenuation_factor(nk_audio_voice sound,
                                                                    float factor) {
    return nk::core::result_boundary(
        "unexpected error while setting audio voice directional attenuation", [&]() -> nk_result {
            if (const auto result = enter_audio_ui(); result != NK_OK)
                return result;
            if (!std::isfinite(factor) || factor < 0.0f || factor > 1.0f)
                return invalid_argument(
                    "audio voice directional attenuation factor must be in the range [0, 1]");
            return with_voice(sound, "could not set audio voice directional attenuation",
                              [&](AudioVoiceResource &value, const char *) {
                                  ma_sound_set_directional_attenuation_factor(&value.sound, factor);
                                  return NK_OK;
                              });
        });
}

nk_result NK_CALL nk_audio_voice_get_directional_attenuation_factor(nk_audio_voice sound,
                                                                    float *out_factor) {
    return nk::core::result_boundary(
        "unexpected error while getting audio voice directional attenuation", [&]() -> nk_result {
            if (const auto result = enter_audio_ui(); result != NK_OK)
                return result;
            if (!out_factor)
                return invalid_argument("audio voice directional attenuation output is missing");
            return with_voice(sound, "could not get audio voice directional attenuation",
                              [&](AudioVoiceResource &value, const char *) {
                                  *out_factor =
                                      ma_sound_get_directional_attenuation_factor(&value.sound);
                                  return NK_OK;
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
            promote_virtual_voices(*engine);
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
