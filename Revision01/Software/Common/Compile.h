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
#define DebugOnly(Call) Call
#else
#define DebugOnly(Call) (void)0
#endif

/* C/C++ interoperability boilerplate */
#define Struct(T) \
	struct T; \
	typedef struct T T; \
	struct T

#define Union(U) \
	union U; \
	typedef union U U; \
	union U

#if 1
#define Enum(E, T) \
	typedef T E; \
	enum : T
#endif

#if 0
#define Enum(E, T) \
	enum class E : T 
#endif

/* dang winbase.h! */
#undef Yield

/* macro concatenate */
#define _CC_impl(x, y) x##y
#define _CC(x, y) _CC_impl(x, y)

#define endian_big (*(uint16_t *)"\0\xff" < 0x100)
#define endian_little (!endian_big)

#ifdef __cplusplus
#define type_of(T) decltype(T)
#else
#define type_of(T) typeof(T)
#endif

typedef uint32_t OS_ErrorType;
#if c_os(windows)
#define OS_SetLastError(E) SetLastError(E)
#define OS_GetLastError() GetLastError()
#else
// idk, errno? 
#endif

/* value-cast vs type-cast */
#undef cast
#define cast(T) (T)
#define typecast(T) *cast(T*)&

#define null NULL
#define nop (void)0
#define unused(v) (void)(v)
#define Unused unused
#define stringify(v) #v
#define stringof(v) ((char*)&v, #v)
#define case_stringof(c) case c: return #c
#define case_fallthrough [[fallthrough]]
#define SWITCH_FallThroughToNextCase() [[fallthrough]]

#define _EnsureTrue_AtCompileTime3(c, msg) typedef char EnsureTrue_AtCompileTime_##msg[(!!(c))*2-1]
#define _EnsureTrue_AtCompileTime2(c, line) _EnsureTrue_AtCompileTime3(c, FAILED_at_line_##line)
#define _EnsureTrue_AtCompileTime1(c, line) _EnsureTrue_AtCompileTime2(c, line)
#define  Ensure_TrueAtCompileTime(c, ...)   _EnsureTrue_AtCompileTime1(c, __LINE__)

/*
// countof(), countof_unsafe()
//
// motivation:
// the typical countof() macro "sizeof(A) / sizeof(*A)"
// returns an incorrect count after converting a stack allocated array
// into a heap allocated array pointer.
//
// T  a[123];                    // sizeof(a) / sizeof(*a) -> 123;
// T* a = malloc(123*sizeof(T)); // sizeof(a) / sizeof(*a) -> wrong!
//
// in order to prevent this, two macros are proposed:
//
// countof(A) completely disallows pointer sized arrays.
// u8  a[8]; // countof(t) will not compile, downgrade to countof_unsafe(a)
// u16 a[4]; // countof(t) will not compile, downgrade to countof_unsafe(a)
// u32 a[2]; // countof(t) will not compile, downgrade to countof_unsafe(a)
// u64 a[1]; // countof(t) will not compile, downgrade to countof_unsafe(a)
//
// countof_unsafe() catches T* t; only when (a) T is larger than a pointer or (b) 3,5,6,7 bytes.
// u8*  a; // countof_unsafe(a) -> 8 (incorrect)
// u16* a; // countof_unsafe(a) -> 4 (incorrect)
// u32* a; // countof_unsafe(a) -> 2 (incorrect)
// u64* a; // countof_unsafe(a) -> 1 (incorrect)
*/
#ifdef _countof
#define countof _countof
#else
#define _countof(A)        (uint32_t)( (sizeof(A)/sizeof(*A)) / (size_t)(sizeof(A) != sizeof(void*)) )
#define _countof_unsafe(A) (uint32_t)( (sizeof(A)/sizeof(*A)) / (size_t)(sizeof(A) % sizeof(*A) == 0) )

#if 1 // note: countof() returns bad results when used in conjunction with math, like (countof(uint32_t A[2]) - 1)
#define countof         _countof
#define countof_unsafe  _countof_unsafe
#else
#define countof         _countof_unsafe
#define countof_unsafe  _countof_unsafe
#endif
#endif
#define Array_CountOf(A) _countof(A)
#define Array_StrlenOf(s) ( (int)(sizeof(s)/sizeof(*s) - 1) / (int)(sizeof(*s) == sizeof(char)) )

/* array argument */
#define AArg(A) (A), Array_CountOf(A)
#define ArrayArg AArg

// #define offset_of(s,m) __builtin_offsetof(s,m)
#ifdef __cplusplus
#define offset_of(s,m) ((::size_t)&reinterpret_cast<char const volatile&>((((s*)0)->m)))
#else
#define offset_of(s,m) ((size_t)&(((s*)0)->m))
#if 0
#define offset_of(T, V) (cast(ptrdiff_t)&((cast((T)*)0)->(V)))
#endif
#endif

/*
//
// Defer
//
*/
#ifdef __cplusplus
template<typename F> struct _ScopedFunction {
	F f;
	_ScopedFunction(F f) : f(f) { }
	~_ScopedFunction() { f(); }
};
template<typename F> inline _ScopedFunction<F> _ScopedFunction_Create(F f) { return _ScopedFunction<F>(f); };

#define Defer(code) \
	auto _CC(scope_exit_, __COUNTER__) = _ScopedFunction_Create([&](){ code; })
#endif
// todo: the one weakness of cscope/cdefer is that returns and breaks are not handled.
//       this can get solved using cpp_scope, cpp_defer instead, which constructs a special object in the for loop init section.
#define cdefer(...)         for(char _CC(_i,__LINE__) = 1;                    _CC(_i,__LINE__)--;  (__VA_ARGS__))
#define cscope(data, open, ...) char _CC(_i,__LINE__) = 1; for(data; ((open), _CC(_i,__LINE__)--); (__VA_ARGS__))
#define va_scope(va, fmt) cscope(va_list va, va_start(va, fmt), va_end(va))
#if 0 /* usage */
	// before -1
	// middle: 0
	// after 1
	cscope(int x = -1, (printf("before "), printf("%d\n", x)), ++x, printf("after %d\n", x)) {
		x = 0;
		printf("middle: %d\n", x);
	}
#endif


/* todo: StringLiteral_{ Define when *_implementation, otherwise, Declare */
#define StringLiteral_Declare(S, L) \
	extern const char S[sizeof(L)]

#define StringLiteral_Define(S, L) \
	StringLiteral_Declare(S, L); \
	const char S[] = L


/* OS/compiler specific */
#if defined(_MSC_VER)
#define ThreadLocal __declspec(thread)
#elif defined(__GNUC__) || defined(__clang__)
#define ThreadLocal __thread
#elif __STDC_VERSION__ >= 201112L
#define ThreadLocal _Thread_local
#elif __cplusplus >= 201103L
#define ThreadLocal thread_local
#else
#error "Compiler does not support thread-local storage"
#endif

#if defined(WIN32)
#define OS_TriggerDebugBreak() __debugbreak()
#elif defined(__GNUC__) || defined(__clang__)
#define os_breakpoint() __builtin_trap()
#else
#define os_breakpoint() raise(SIGTRAP)
#endif


/* get the size of a structure variable without an instantiated object present in scope */
#define size_of(T, V) (sizeof((cast((T)*)0)->(V)))

#if defined(__GNUC__) || defined(__clang__)
#define Inline inline
#define unreachable __builtin_unreachable()
// #define MPACK_NORETURN(fn) fn __attribute__((__noreturn__))
#elif defined(_MSC_VER)
#define Inline __forceinline
#define COMPILER_Unreachable __assume(0)
// #define MPACK_NORETURN(fn) __declspec(noreturn) fn
#endif

#ifdef _MSC_VER
#define fmt_i64 "%I64d"
#define fmt_u64 "%I64u"
#else
#define fmt_i64 "%lld"
#define fmt_u64 "%llu"
#endif

#if defined(__clang__) || defined(__GNUC__)
#define c_fmt(fmt_nth)    __attribute__((format(printf, fmt_nth, fmt_nth + 1)))
#define c_fmt_va(fmt_nth) __attribute__((format(printf, fmt_nth, 0)))
#else
#define c_fmt(fmt_nth)
#define c_fmt_va(fmt_nth)
#endif


/* using parenthesis for MACRO_ is a style choice; it further differentiates the fact that these represent "dynamic" data. */
#define MACRO_Ignore(...)
#define MACRO_CompileDate() __DATE__
#define MACRO_Line() __LINE__
#define MACRO_File() __FILE__
#define MACRO_Function() __FUNCTION__
#ifdef __FUNCSIG__
#define MACRO_FunctionSignature() __FUNCSIG__
#else
#define MACRO_FunctionSignature() __PRETTY_FUNCTION__
#endif

//
// Compilers(GCC, Clang, and MSVC) are "smart." If they see you filling a buffer with zeros, but then you never read from that buffer again before it goes out of scope, the optimizer applies Dead Store Elimination(DSE).It decides the memset was useless work and deletes it entirely to save CPU cycles.
// 
// This is a disaster if you are :
// 
// Clearing sensitive data(passwords / keys) from RAM.
// 
// Writing to Memory - Mapped I / O(MMIO) where the act of writing triggers hardware(like your FPGA).
void* (* const volatile _Memset_Explicit)(void*, int, size_t) = std::memset;
Inline void MemClear_Explicit(void* p, size_t n) {
	_Memset_Explicit(p, 0, n);
}

#define MemClear_Object(O) MemClear_Explicit(&(O), sizeof(O))

Inline void* Malloc_Aligned(size_t size, size_t alignment) {
#if defined(_WIN32)
	return _aligned_malloc(size, alignment);
#else
#error untested code VVV
	// std::aligned_alloc requires size to be a multiple of alignment
	size_t remainder = size % alignment;
	if (remainder != 0) size += (alignment - remainder);
	return std::aligned_alloc(alignment, size);
#endif
}

#define RoundUp(x, Round) ((((x) + (Round) - 1) / (Round)) * (Round))

#ifndef __cplusplus
#undef bool
#endif
typedef uint8_t bool_t;
typedef uint8_t char_t;
typedef uint8_t u8_t;
typedef  int8_t s8_t;

typedef uint16_t u16_t;
typedef  int16_t s16_t;

typedef uint32_t u32_t;
typedef  int32_t s32_t;
typedef    float f32_t;

typedef uint64_t u64_t;
typedef  int64_t s64_t;
typedef   double f64_t;

#ifdef __cplusplus
typedef size_t    zuint;
typedef ptrdiff_t zint;
#endif

typedef size_t    zuint;
typedef ptrdiff_t zint;
typedef uint32_t unichar_t;

/* here's how you disable a class' copy operator = */
#define class_nocopy(classname)           \
private:                                  \
classname(const classname&);              \
classname& operator = (const classname &) \

/**
* Traits class which tests if a type is a pointer.
*/
// see: https://github.com/EpicGames/UnrealEngine/blob/16dc333db3d6439c7f2886cf89db8907846c0e8a/Engine/Source/Runtime/Core/Public/Templates/IsPointer.h#L11
template <typename T>
struct TIsPointer {
	enum { Value = false };
};
template <typename T> struct TIsPointer<T*> { enum { Value = true }; };
template <typename T> struct TIsPointer<const          T> { enum { Value = TIsPointer<T>::Value }; };
template <typename T> struct TIsPointer<      volatile T> { enum { Value = TIsPointer<T>::Value }; };
template <typename T> struct TIsPointer<const volatile T> { enum { Value = TIsPointer<T>::Value }; };


