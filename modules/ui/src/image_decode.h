#ifndef NATIVEKIT_UI_IMAGE_DECODE_H
#define NATIVEKIT_UI_IMAGE_DECODE_H

#include <cstdint>
#include <vector>

namespace nkui {

struct DecodedImage {
    uint32_t width = 0;
    uint32_t height = 0;
    std::vector<uint8_t> rgba8;
};

class ImageDecoder {
  public:
    virtual ~ImageDecoder() = default;

    virtual bool decode_file(const char *path, DecodedImage &out_image) const = 0;
    virtual const char *name() const = 0;
};

/** Returns the decoder selected for the current platform and build. */
const ImageDecoder &default_image_decoder();

} // namespace nkui

#endif
