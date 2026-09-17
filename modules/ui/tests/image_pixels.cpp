#include "prepare/image_pixels.h"

#include <cassert>
#include <vector>

int main() {
    const std::vector<uint8_t> straight = {
        240, 180, 60, 0, 100, 50, 200, 128, 7, 11, 13, 255,
    };
    const auto rgba = nkui::prepare_rgba8_pixels(straight);
    assert((rgba == std::vector<uint8_t>{
                        0,
                        0,
                        0,
                        0,
                        50,
                        25,
                        100,
                        128,
                        7,
                        11,
                        13,
                        255,
                    }));

    const auto alpha = nkui::prepare_alpha8_pixels({0, 128, 255});
    assert((alpha == std::vector<uint8_t>{
                         0,
                         0,
                         0,
                         0,
                         128,
                         128,
                         128,
                         128,
                         255,
                         255,
                         255,
                         255,
                     }));
    return 0;
}
