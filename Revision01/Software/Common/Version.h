//

#ifndef version_h
#define version_h

#include "Compile.h"

#if c_os(windows)
#define Version_OS() "win"
#elif c_os(mac)
#define Version_OS() "mac"
#elif c_os(ios)
#define Version_OS() "ios"
#else
// just assume STM32
#define Version_OS() "stm"
#endif

#if c_config(debug)
#define Version_Config() "debug"
#endif
#if c_config(release)
#define Version_Config() "release"
#endif

#define Version_StringLiteral() \
    Version_OS() \
"-" Version_Config() \
"-" __DATE__

#if 0
#include "Version_MajorMinorPatch.h"
#include "Version_Revision.h"

#define Version_Codename() "recall"

#define _str1(x) #x
#define _str(x) _str1(x)

#define Version_Quad() \
"r" Version_Revision() \
"-v" _str(Version_Major()) \
"-" _str(Version_Minor()) \
"-" _str(Version_Patch()) \

#if c_config(debug)
#define Version_Config() "debug"
#endif
#if c_config(release)
#define Version_Config() "release"
#endif

#if c_os(windows)
#define Version_OS() "win"
#elif c_os(mac)
#define Version_OS() "mac"
#elif c_os(ios)
#define Version_OS() "ios"
#else
// just assume STM32
#define Version_OS() "stm"
#endif

#define Version_Date() \
    Version_Date_yyyy() Version_Date_mm()  Version_Date_dd() \
"-" Version_Date_hour() Version_Date_min() Version_Date_sec() \

#define Version_StringLiteral() \
    Version_Codename() \
"-" Version_OS() \
"-" Version_Quad() \
"-" Version_Config() \
"-" Version_Date()

#define Version_StringLiteral_Strlen() (sizeof(Version_StringLiteral()) - 1)

typedef char version_str_t[128];

#endif
#endif
