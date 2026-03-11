//

/* ie, c_compile(program_main) */
#define c_compile(C) _c_##C()

#define c_os(O) c_compile(os_##O)
/* Check for windows, otherwise  just assume we're compiling for STM32 */
#if defined(_WIN32)
#define _c_os_windows() 1
#define _c_os_stm32() 0
#else
#define _c_os_windows() 0
#define _c_os_stm32() 1
#endif
#define _c_os_mac() 0
#define _c_os_ios() 0
#define _c_os_apple() (_c_os_mac() || _c_os_ios())

/* On Windows, should be compiling for 64 Bit */
#if c_os(windows)
#ifndef _WIN64
#error
#endif
#endif
#if 1 != (c_os(windows) + c_os(stm32))
#error
#endif

/* Standard Release/Debug Build -Define */
#define c_config(C) c_compile(config_##C)
#if NDEBUG
#define _c_config_release() 1
#define _c_config_debug()   0
#else
#define _c_config_release() 0
#define _c_config_debug()   1
#endif

#if 1 != (c_config(release) + c_config(debug))
#error
#endif

/* https://stackoverflow.com/questions/16696297/ftell-at-a-position-past-2gb */
#define _FILE_OFFSET_BITS 64
#define _LARGEFILE_SOURCE
#define _LARGEFILE64_SOURCE
#define _FILE_OFFSET_BITS 64
