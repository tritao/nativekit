#include "nativekit_audio.h"

#include "core/boundary.hpp"
#include "core/error.hpp"
#include "core/handle_registry.hpp"
#include "core/runtime.hpp"

#include "miniaudio.h"

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
#include <memory>
#include <mutex>
#include <vector>

namespace {

constexpr uint32_t supported_sound_flags = NK_AUDIO_SOUND_LOOPING | NK_AUDIO_SOUND_STREAM |
                                           NK_AUDIO_SOUND_ASYNC;

struct AudioEngineResource final : nk::core::Resource {
    ma_engine engine{};
    bool initialized = false;

    ~AudioEngineResource() override {
        if (initialized)
            ma_engine_uninit(&engine);
    }
};

struct AudioSoundResource final : nk::core::Resource {
    std::shared_ptr<AudioEngineResource> engine;
    std::vector<std::byte> encoded_data;
    ma_decoder decoder{};
    bool decoder_initialized = false;
    ma_sound sound{};
    bool sound_initialized = false;

    ~AudioSoundResource() override {
        if (sound_initialized)
            ma_sound_uninit(&sound);
        if (decoder_initialized)
            ma_decoder_uninit(&decoder);
    }
};

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

nk_result map_miniaudio_result(ma_result result, const char *message) {
    if (result == MA_SUCCESS)
        return NK_OK;
    if (result == MA_INVALID_ARGS) {
        nk::core::set_error(message);
        return NK_ERROR_INVALID_ARGUMENT;
    }
    if (result == MA_OUT_OF_MEMORY) {
        nk::core::set_error(message);
        return NK_ERROR_OUT_OF_MEMORY;
    }
    if (result == MA_NO_BACKEND || result == MA_NO_DEVICE || result == MA_NOT_IMPLEMENTED) {
        nk::core::set_error(message);
        return NK_ERROR_UNSUPPORTED;
    }
    nk::core::set_error(message);
    return NK_ERROR_UNKNOWN;
}

nk_result enter_audio_ui() {
    nk::core::clear_error();
    return nk::core::require_ui_thread();
}

nk_result sound_options(const nk_audio_sound_options *options, uint32_t &flags) {
    flags = 0;
    if (!options)
        return NK_OK;
    if (options->struct_size < sizeof(nk_audio_sound_options))
        return invalid_argument("audio sound options are missing or too small");
    if (options->flags & ~supported_sound_flags)
        return invalid_argument("audio sound options contain unsupported flags");
    flags = options->flags;
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

std::shared_ptr<AudioSoundResource> get_sound(nk_audio_sound handle) {
    auto resource = nk::core::handles().get(handle, nk::core::ResourceType::audio_sound);
    if (!resource) {
        nk::core::set_error("invalid audio sound handle");
        return {};
    }
    auto sound = std::dynamic_pointer_cast<AudioSoundResource>(std::move(resource));
    if (!sound)
        nk::core::set_error("invalid audio sound resource");
    return sound;
}

uint32_t miniaudio_sound_flags(uint32_t flags) {
    uint32_t result = 0;
    if (flags & NK_AUDIO_SOUND_LOOPING)
        result |= MA_SOUND_FLAG_LOOPING;
    if (flags & NK_AUDIO_SOUND_STREAM)
        result |= MA_SOUND_FLAG_STREAM;
    else
        result |= MA_SOUND_FLAG_DECODE;
    if (flags & NK_AUDIO_SOUND_ASYNC)
        result |= MA_SOUND_FLAG_ASYNC;
    return result;
}

nk_result insert_sound(std::shared_ptr<AudioSoundResource> sound, nk_audio_sound *out_sound) {
    const auto handle = nk::core::handles().insert(nk::core::ResourceType::audio_sound, sound);
    if (handle == NK_INVALID_HANDLE) {
        nk::core::set_error("could not allocate an audio sound handle");
        return NK_ERROR_OUT_OF_MEMORY;
    }
    *out_sound = handle;
    return NK_OK;
}

template <typename Function>
nk_result with_sound(nk_audio_sound handle, const char *message, Function &&function) {
    auto sound = get_sound(handle);
    if (!sound)
        return NK_ERROR_INVALID_HANDLE;
    return function(*sound, message);
}

} // namespace

extern "C" {

nk_result NK_CALL nk_audio_sound_create_from_file(const char *path,
                                                  const nk_audio_sound_options *options,
                                                  nk_audio_sound *out_sound) {
    return nk::core::result_boundary(
        "unexpected error while creating an audio sound from a file", [&]() -> nk_result {
            if (const auto result = enter_audio_ui(); result != NK_OK)
                return result;
            if (!path || !*path || !out_sound)
                return invalid_argument("audio file path or output is missing");
            *out_sound = NK_INVALID_HANDLE;
            uint32_t flags = 0;
            if (const auto result = sound_options(options, flags); result != NK_OK)
                return result;
            nk_result engine_result = NK_OK;
            auto engine = ensure_engine(engine_result);
            if (!engine)
                return engine_result;
            auto sound = std::make_shared<AudioSoundResource>();
            sound->engine = std::move(engine);
            const auto result = ma_sound_init_from_file(
                &sound->engine->engine, path, miniaudio_sound_flags(flags), nullptr, nullptr,
                &sound->sound);
            if (result != MA_SUCCESS)
                return map_miniaudio_result(result, "could not load audio file");
            sound->sound_initialized = true;
            return insert_sound(std::move(sound), out_sound);
        });
}

nk_result NK_CALL nk_audio_sound_create_from_memory(const void *data, uint64_t data_size,
                                                    const nk_audio_sound_options *options,
                                                    nk_audio_sound *out_sound) {
    return nk::core::result_boundary(
        "unexpected error while creating an audio sound from memory", [&]() -> nk_result {
            if (const auto result = enter_audio_ui(); result != NK_OK)
                return result;
            if (!data || data_size == 0 || !out_sound ||
                data_size > static_cast<uint64_t>(std::numeric_limits<std::size_t>::max()))
                return invalid_argument("audio memory data or output is invalid");
            *out_sound = NK_INVALID_HANDLE;
            uint32_t flags = 0;
            if (const auto result = sound_options(options, flags); result != NK_OK)
                return result;
            if (flags & NK_AUDIO_SOUND_ASYNC)
                return invalid_argument("asynchronous audio loading requires a file source");
            nk_result engine_result = NK_OK;
            auto engine = ensure_engine(engine_result);
            if (!engine)
                return engine_result;
            auto sound = std::make_shared<AudioSoundResource>();
            sound->engine = std::move(engine);
            sound->encoded_data.resize(static_cast<std::size_t>(data_size));
            std::memcpy(sound->encoded_data.data(), data, sound->encoded_data.size());
            auto result = ma_decoder_init_memory(sound->encoded_data.data(),
                                                 sound->encoded_data.size(), nullptr,
                                                 &sound->decoder);
            if (result != MA_SUCCESS)
                return map_miniaudio_result(result, "could not decode audio memory");
            sound->decoder_initialized = true;
            const uint32_t sound_flags = (flags & NK_AUDIO_SOUND_LOOPING) ? MA_SOUND_FLAG_LOOPING : 0;
            result = ma_sound_init_from_data_source(&sound->engine->engine, &sound->decoder,
                                                    sound_flags, nullptr, &sound->sound);
            if (result != MA_SUCCESS)
                return map_miniaudio_result(result, "could not create an audio sound");
            sound->sound_initialized = true;
            return insert_sound(std::move(sound), out_sound);
        });
}

nk_result NK_CALL nk_audio_sound_destroy(nk_audio_sound sound) {
    return nk::core::result_boundary("unexpected error while destroying an audio sound",
                                     [&]() -> nk_result {
        if (const auto result = enter_audio_ui(); result != NK_OK)
            return result;
        if (!nk::core::handles().erase(sound, nk::core::ResourceType::audio_sound))
            return invalid_handle("invalid audio sound handle");
        return NK_OK;
    });
}

nk_result NK_CALL nk_audio_sound_start(nk_audio_sound sound) {
    return nk::core::result_boundary("unexpected error while starting an audio sound", [&]() {
        if (const auto result = enter_audio_ui(); result != NK_OK)
            return result;
        return with_sound(sound, "could not start audio sound", [](AudioSoundResource &value,
                                                                  const char *message) {
            return map_miniaudio_result(ma_sound_start(&value.sound), message);
        });
    });
}

nk_result NK_CALL nk_audio_sound_stop(nk_audio_sound sound) {
    return nk::core::result_boundary("unexpected error while stopping an audio sound", [&]() {
        if (const auto result = enter_audio_ui(); result != NK_OK)
            return result;
        return with_sound(sound, "could not stop audio sound", [](AudioSoundResource &value,
                                                                 const char *message) {
            return map_miniaudio_result(ma_sound_stop(&value.sound), message);
        });
    });
}

nk_result NK_CALL nk_audio_sound_rewind(nk_audio_sound sound) {
    return nk::core::result_boundary("unexpected error while rewinding an audio sound", [&]() {
        if (const auto result = enter_audio_ui(); result != NK_OK)
            return result;
        return with_sound(sound, "could not rewind audio sound", [](AudioSoundResource &value,
                                                                    const char *message) {
            return map_miniaudio_result(ma_sound_seek_to_pcm_frame(&value.sound, 0), message);
        });
    });
}

nk_result NK_CALL nk_audio_sound_is_playing(nk_audio_sound sound, nk_bool *out_playing) {
    return nk::core::result_boundary(
        "unexpected error while querying an audio sound", [&]() -> nk_result {
            if (const auto result = enter_audio_ui(); result != NK_OK)
                return result;
            if (!out_playing)
                return invalid_argument("audio playing output is missing");
            return with_sound(sound, "could not query audio sound", [&](AudioSoundResource &value,
                                                                         const char *) {
                *out_playing = ma_sound_is_playing(&value.sound) ? 1u : 0u;
                return NK_OK;
            });
        });
}

nk_result NK_CALL nk_audio_sound_at_end(nk_audio_sound sound, nk_bool *out_at_end) {
    return nk::core::result_boundary(
        "unexpected error while querying an audio sound end state", [&]() -> nk_result {
            if (const auto result = enter_audio_ui(); result != NK_OK)
                return result;
            if (!out_at_end)
                return invalid_argument("audio end-state output is missing");
            return with_sound(sound, "could not query audio sound end state",
                              [&](AudioSoundResource &value, const char *) {
                                  *out_at_end = ma_sound_at_end(&value.sound) ? 1u : 0u;
                                  return NK_OK;
                              });
        });
}

nk_result NK_CALL nk_audio_sound_set_volume(nk_audio_sound sound, float volume) {
    return nk::core::result_boundary("unexpected error while setting audio sound volume", [&]() {
        if (const auto result = enter_audio_ui(); result != NK_OK)
            return result;
        if (!std::isfinite(volume) || volume < 0)
            return invalid_argument("audio sound volume must be finite and non-negative");
        return with_sound(sound, "could not set audio sound volume", [&](AudioSoundResource &value,
                                                                          const char *) {
            ma_sound_set_volume(&value.sound, volume);
            return NK_OK;
        });
    });
}

nk_result NK_CALL nk_audio_sound_get_volume(nk_audio_sound sound, float *out_volume) {
    return nk::core::result_boundary("unexpected error while getting audio sound volume", [&]() {
        if (const auto result = enter_audio_ui(); result != NK_OK)
            return result;
        if (!out_volume)
            return invalid_argument("audio sound volume output is missing");
        return with_sound(sound, "could not get audio sound volume", [&](AudioSoundResource &value,
                                                                          const char *) {
            *out_volume = ma_sound_get_volume(&value.sound);
            return NK_OK;
        });
    });
}

nk_result NK_CALL nk_audio_sound_set_pan(nk_audio_sound sound, float pan) {
    return nk::core::result_boundary("unexpected error while setting audio sound pan", [&]() {
        if (const auto result = enter_audio_ui(); result != NK_OK)
            return result;
        if (!std::isfinite(pan) || pan < -1.0f || pan > 1.0f)
            return invalid_argument("audio sound pan must be finite and in the range [-1, 1]");
        return with_sound(sound, "could not set audio sound pan", [&](AudioSoundResource &value,
                                                                       const char *) {
            ma_sound_set_pan(&value.sound, pan);
            return NK_OK;
        });
    });
}

nk_result NK_CALL nk_audio_sound_get_pan(nk_audio_sound sound, float *out_pan) {
    return nk::core::result_boundary("unexpected error while getting audio sound pan", [&]() {
        if (const auto result = enter_audio_ui(); result != NK_OK)
            return result;
        if (!out_pan)
            return invalid_argument("audio sound pan output is missing");
        return with_sound(sound, "could not get audio sound pan", [&](AudioSoundResource &value,
                                                                       const char *) {
            *out_pan = ma_sound_get_pan(&value.sound);
            return NK_OK;
        });
    });
}

nk_result NK_CALL nk_audio_sound_set_pitch(nk_audio_sound sound, float pitch) {
    return nk::core::result_boundary("unexpected error while setting audio sound pitch", [&]() {
        if (const auto result = enter_audio_ui(); result != NK_OK)
            return result;
        if (!std::isfinite(pitch) || pitch <= 0)
            return invalid_argument("audio sound pitch must be finite and positive");
        return with_sound(sound, "could not set audio sound pitch", [&](AudioSoundResource &value,
                                                                        const char *) {
            ma_sound_set_pitch(&value.sound, pitch);
            return NK_OK;
        });
    });
}

nk_result NK_CALL nk_audio_sound_get_pitch(nk_audio_sound sound, float *out_pitch) {
    return nk::core::result_boundary("unexpected error while getting audio sound pitch", [&]() {
        if (const auto result = enter_audio_ui(); result != NK_OK)
            return result;
        if (!out_pitch)
            return invalid_argument("audio sound pitch output is missing");
        return with_sound(sound, "could not get audio sound pitch", [&](AudioSoundResource &value,
                                                                         const char *) {
            *out_pitch = ma_sound_get_pitch(&value.sound);
            return NK_OK;
        });
    });
}

nk_result NK_CALL nk_audio_sound_set_looping(nk_audio_sound sound, nk_bool looping) {
    return nk::core::result_boundary("unexpected error while setting audio sound looping", [&]() {
        if (const auto result = enter_audio_ui(); result != NK_OK)
            return result;
        if (looping > 1)
            return invalid_argument("audio sound looping must be zero or one");
        return with_sound(sound, "could not set audio sound looping", [&](AudioSoundResource &value,
                                                                           const char *) {
            ma_sound_set_looping(&value.sound, looping != 0 ? MA_TRUE : MA_FALSE);
            return NK_OK;
        });
    });
}

nk_result NK_CALL nk_audio_sound_is_looping(nk_audio_sound sound, nk_bool *out_looping) {
    return nk::core::result_boundary(
        "unexpected error while querying audio sound looping", [&]() -> nk_result {
            if (const auto result = enter_audio_ui(); result != NK_OK)
                return result;
            if (!out_looping)
                return invalid_argument("audio looping output is missing");
            return with_sound(sound, "could not query audio sound looping",
                              [&](AudioSoundResource &value, const char *) {
                                  *out_looping = ma_sound_is_looping(&value.sound) ? 1u : 0u;
                                  return NK_OK;
                              });
        });
}

nk_result NK_CALL nk_audio_sound_get_time_seconds(nk_audio_sound sound, float *out_seconds) {
    return nk::core::result_boundary(
        "unexpected error while getting audio sound time", [&]() -> nk_result {
            if (const auto result = enter_audio_ui(); result != NK_OK)
                return result;
            if (!out_seconds)
                return invalid_argument("audio time output is missing");
            return with_sound(sound, "could not get audio sound time",
                              [&](AudioSoundResource &value, const char *message) {
                                  return map_miniaudio_result(
                                      ma_sound_get_cursor_in_seconds(&value.sound, out_seconds),
                                      message);
                              });
        });
}

nk_result NK_CALL nk_audio_sound_get_length_seconds(nk_audio_sound sound, float *out_seconds) {
    return nk::core::result_boundary(
        "unexpected error while getting audio sound length", [&]() -> nk_result {
            if (const auto result = enter_audio_ui(); result != NK_OK)
                return result;
            if (!out_seconds)
                return invalid_argument("audio length output is missing");
            return with_sound(sound, "could not get audio sound length",
                              [&](AudioSoundResource &value, const char *message) {
                                  return map_miniaudio_result(
                                      ma_sound_get_length_in_seconds(&value.sound, out_seconds),
                                      message);
                              });
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
