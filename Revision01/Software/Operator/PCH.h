#pragma once

#include "Compile_PCH.h"

#include "xlsxwriter.h"

#if c_os(windows)
#include <windows.h>
#include <conio.h>
#include <synchapi.h>
#include <processthreadsapi.h>
#elif defined(__linux__)
#include <sys/prctl.h>
#elif defined(__APPLE__) && defined(__MACH__)
#include <pthread.h>
#endif

#include <cstdlib>
#include <new>
#include <cstring>
#include <csignal>
#include <atomic>
#include <mutex>
#include <string>
#include <vector>
#include <iostream>
#include <bitset>

#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <stdio.h>
#include <stdarg.h>
#include <assert.h>
