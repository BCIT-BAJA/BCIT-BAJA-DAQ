//

#pragma once

/*
todo: the fact that i can mispell c_compile(wanblows) and have it still compile is very concerning
      i remember i tried to get it to fail to compile somehow. it needs a static assert.
*/

#include "Compile_PCH.h"

/* features */
#define c_feature(F) c_compile(feature_##F)
#define _c_feature_tracy() 0
// #define _c_feature_hitch_detection() 1
// #define _c_feature_viewer_fullscreen() 0

/* programs, typically to test standalone wip demo code */
#define c_program(P) c_compile(program_##P)
// #define _c_program_recall()               1
// #define _c_program_viewer()               0

#if c_config(debug)
#define MACRO_DebugOnly(...) __VA_ARGS__
#else
#define MACRO_DebugOnly(...)
#endif

/* dang winbase.h! */
#undef Yield
