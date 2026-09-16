#include "image_decode.h"

#define STB_IMAGE_STATIC
#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"

namespace nkui {

uint8_t *decode_image_file(const char *path, int &width, int &height) {
    int channels = 0;
    return stbi_load(path, &width, &height, &channels, 4);
}

void free_decoded_image(uint8_t *pixels) { stbi_image_free(pixels); }

} // namespace nkui
