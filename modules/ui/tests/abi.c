#include "nativekit_ui.h"

int main(void) {
    return nkui_api_version() == NKUI_API_VERSION ? 0 : 1;
}
