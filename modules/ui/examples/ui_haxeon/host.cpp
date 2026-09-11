#if defined(__EMSCRIPTEN__)
#include <emscripten/emscripten.h>
#endif

// The Haxeon guest owns the application loop.  This executable only supplies
// the browser runtime and the NativeKit/UI C ABI exports that the guest imports.
int main() {
    return 0;
}
