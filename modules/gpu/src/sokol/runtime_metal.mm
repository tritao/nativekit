/* Sokol's Metal implementation requires Objective-C; keep its shared runtime
 * lease and image registry in runtime.c while compiling that translation unit
 * as Objective-C++ for the Metal backend. */
#include "runtime.c"
