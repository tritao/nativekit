#include "image_decode.h"

#define STB_IMAGE_STATIC
#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"

#include <limits>
#include <memory>

namespace nkui {
namespace {

class StbImageDecoder final : public ImageDecoder {
  public:
    bool decode_file(const char *path, DecodedImage &out_image) const override {
        out_image = {};
        if (!path || !*path)
            return false;

        int width = 0;
        int height = 0;
        int channels = 0;
        using StbPixels = std::unique_ptr<uint8_t, decltype(&stbi_image_free)>;
        StbPixels pixels(stbi_load(path, &width, &height, &channels, 4), stbi_image_free);
        if (!pixels || width <= 0 || height <= 0)
            return false;

        const uint64_t byte_count = static_cast<uint64_t>(width) * height * 4u;
        if (byte_count > std::numeric_limits<uint32_t>::max())
            return false;

        out_image.width = static_cast<uint32_t>(width);
        out_image.height = static_cast<uint32_t>(height);
        out_image.rgba8.assign(pixels.get(), pixels.get() + static_cast<size_t>(byte_count));
        return true;
    }

    const char *name() const override { return "stb_image"; }
};

} // namespace

const ImageDecoder &default_image_decoder() {
    static const StbImageDecoder decoder;
    return decoder;
}

} // namespace nkui
