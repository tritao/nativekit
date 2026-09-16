#ifndef NATIVEKIT_UI_IMAGE_DECODE_H
#define NATIVEKIT_UI_IMAGE_DECODE_H

#include <cstdint>

namespace nkui {

uint8_t *decode_image_file(const char *path, int &width, int &height);
void free_decoded_image(uint8_t *pixels);

} // namespace nkui

#endif
