#if defined(__EMSCRIPTEN__)
#include <emscripten/emscripten.h>
#endif

#include <string>

namespace {

std::string diagnostic_message;
std::string diagnostic_stack;
std::string diagnostic_breadcrumbs;

} // namespace

extern "C" {

void nkui_showcase_diagnostic_report(int, const char *message, const char *stack,
                                     const char *breadcrumbs) {
    diagnostic_message = message ? message : "";
    diagnostic_stack = stack ? stack : "";
    diagnostic_breadcrumbs = breadcrumbs ? breadcrumbs : "";
}

const char *nkui_showcase_diagnostic_message() {
    return diagnostic_message.c_str();
}

const char *nkui_showcase_diagnostic_stack() {
    return diagnostic_stack.c_str();
}

const char *nkui_showcase_diagnostic_breadcrumbs() {
    return diagnostic_breadcrumbs.c_str();
}

} // extern "C"

// The Haxeon guest owns the application loop.  This executable only supplies
// the browser runtime and the NativeKit/UI C ABI exports that the guest imports.
int main() {
    return 0;
}
