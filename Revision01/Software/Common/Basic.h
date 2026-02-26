//

#pragma once

#include "Assure.h"
#include "Compile.h"

// https://web.archive.org/web/20170602083648/http://www.insomniacgames.com/core-coding-standard/
/*
Create/Destroy
Of an object, where a C++ constructor is not appropriate. This is the case if an object can be re-created multiple times. Use this instead of placement new if possible. It makes the code clearer.
Create must return a success flag.
Init/Shutdown | Used for systems and managers.
Start/Stop | Start() is used to start something running, either synchronously or asynchronously, until Stop() is called. For example: a particle effect, a music track, a streaming system, or a timer.
Suspend/Resume | For use between Start/Stop, to temporarily suspend that what is running.
Begin/End | Used to bracket some kind of aggregation or definition, such as a triangle strip, command buffer, goal list.
Enter/Exit | Of a state or mode.
Add/Remove | An element in a collection.
Open/Close | Of a data stream. Input/output, or between processes.
Acquire/Release OR { Rent/Return }
Exclusive ownership of a resource. Acquire must return an error code.

g_GlobalVariable;
s_LocalVariable;

namespace YourEnum { enum {
	kSomeValue;
} }

YourEnum::kSomeValue;
*/

// todo: include a crash handler at program startup which reads emailable data from some persistence system. ie, 
//       consider at launch, that we have an option --watchdog, which serves as a hot-restart / heartbeat monitor / crash handler using IPC mechanism.
//       this way, we don't need to write much complicated serialization code, we can just dump some memory structures as a message to the second redundant
//       process, and have that process primed to immediately spawn a UI window and try to relaunch again! this also solves the problem of trying to call
//       complicated OS process functions after we're in a bad state. for whatever reason, we just immediately re-launch if we can't get a heartbeat response.
//
// In brief: the "lifeguard" or "heartbeat" process is recall itself, set to a special operating mode.
//           this way, there is less complication with multi versioned .exes inside the folder; the IPC mechanisms are sync'd by default; and the app
//           won't need to call complicated OS functions after a serious fault / problem has occured. it is as simple as a basic "heartbeat" from the
//           redundant lifeguard process. the lifeguard process can immediately launch a window, and a crash window (in the background) can linger.
//
//           the worst case is, the lifeguard process isn't there, and so be it, it crashes like a regular program. oh well! the only thing i can think
//           of is if the user launches the process again and again, there could be some delicate logic to prevent stomping files and statuses, etc.
//
// todo: introduce a proper assertion handler, with stacktracing, variable retracing, etc
// see: https://www.drdobbs.com/assertions/184403861
// see: https://www.drdobbs.com/cpp/enhancing-assertions/184403745
// todo: style: add some 'b's in front of booleans

#ifdef __INTELLISENSE__
#define shared_impl
#endif

/* macro concatenate */
#define _cc_impl(x, y) x##y
#define _cc(x, y) _cc_impl(x, y)



#define endian_big (*(uint16_t *)"\0\xff" < 0x100)
#define endian_little (!endian_big)

#ifdef __cplusplus
#define type_of(T) decltype(T)
#else
#define type_of(T) typeof(T)
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

/* get the size of a structure variable without an instantiated object present in scope */
#define size_of(T, V) (sizeof((cast((T)*)0)->(V)))


/* OS/compiler specific */
#if defined(WIN32)
#define os_breakpoint() __debugbreak()
#elif defined(__GNUC__) || defined(__clang__)
#define os_breakpoint() __builtin_trap()
#else
#define os_breakpoint() raise(SIGTRAP)
#endif

#if defined(__GNUC__) || defined(__clang__)
#define inl inline
#define unreachable __builtin_unreachable()
// #define MPACK_NORETURN(fn) fn __attribute__((__noreturn__))
#elif defined(_MSC_VER)
#define inl __forceinline
#define unreachable __assume(0)
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
#define strlenof(s) ( (int)(sizeof(s)/sizeof(*s) - 1) / (int)(sizeof(*s) == sizeof(char)) )

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

/* array argument */
#define aarg(A) (A), countof(A)

// #define offset_of(s,m) __builtin_offsetof(s,m)
#ifdef __cplusplus
#define offset_of(s,m) ((::size_t)&reinterpret_cast<char const volatile&>((((s*)0)->m)))
#else
#define offset_of(s,m) ((size_t)&(((s*)0)->m))
#if 0
#define offset_of(T, V) (cast(ptrdiff_t)&((cast((T)*)0)->(V)))
#endif
#endif

// todo: the one weakness of cscope/cdefer is that returns and breaks are not handled.
//       this can get solved using cpp_scope, cpp_defer instead, which constructs a special object in the for loop init section.
#define cdefer(...)         for(char _cc(_i,__LINE__) = 1;                    _cc(_i,__LINE__)--;  (__VA_ARGS__))
#define cscope(data, open, ...) char _cc(_i,__LINE__) = 1; for(data; ((open), _cc(_i,__LINE__)--); (__VA_ARGS__))
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



/* todo: StringLiteral_{ Define when *_implementation, otherwise, Declare */
#define StringLiteral_Declare(S, L) \
	extern const char S[sizeof(L)]

#define StringLiteral_Define(S, L) \
	StringLiteral_Declare(S, L); \
	const char S[] = L

#ifdef __cplusplus
template<typename F> struct _ScopedFunction {
	F f;
	_ScopedFunction(F f) : f(f) { }
	~_ScopedFunction() { f(); }
};
template<typename F> inline _ScopedFunction<F> _ScopedFunction_Create(F f) { return _ScopedFunction<F>(f); };

#define defer(code) \
	auto _cc(scope_exit_, __COUNTER__) = _ScopedFunction_Create([&](){ code; })
#endif
#define Defer defer

// 
// Compilers(GCC, Clang, and MSVC) are "smart." If they see you filling a buffer with zeros, but then you never read from that buffer again before it goes out of scope, the optimizer applies Dead Store Elimination(DSE).It decides the memset was useless work and deletes it entirely to save CPU cycles.
// 
// This is a disaster if you are :
// 
// Clearing sensitive data(passwords / keys) from RAM.
// 
// Writing to Memory - Mapped I / O(MMIO) where the act of writing triggers hardware(like your FPGA).
void* (* const volatile _Memset_Explicit)(void*, int, size_t) = std::memset;
inl void MemClear_Explicit(void* p, size_t n) {
	_Memset_Explicit(p, 0, n);
}

inl void* Malloc_Aligned(size_t size, size_t alignment) {
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

template<typename T> inline T* ConstructAt_NullSafe(T* p) {
	if(Assure_True(p)) {
		new (p) T();
	}
	return p;
}

template<typename T> inline T* DestructAt_NullSafe(T* p) {
	if(Assure_True(p)) {
		p->~T();
	}
	return p;
}

inl const void* Free_NullSafe(const void* p) {
	if(Assure_True(p)) {
		free(cast(void*)p);
	}
	return p;
}

inl void Free_Aligned(void* ptr) {
#if defined(_WIN32)
	_aligned_free(ptr);
#else
#error untested code VVV
	std::free(ptr);
#endif
}

#define Basic_Pointer_New(Ptr) \
( \
	(Ptr = cast(type_of(Ptr))Malloc_Aligned( \
		sizeof(type_of(*Ptr)), \
		( alignof(type_of(*Ptr)) + sizeof(void*) - 1) / sizeof(void*) \
	)) \
	&& ConstructAt_NullSafe(Ptr) \
)

#define Basic_Pointer_Delete(Ptr) \
	do { \
		if (Ptr) { \
			(Ptr)->~decltype(*Ptr)(); \
			Free_Aligned(Ptr); \
			Ptr = null; \
		} \
	} while(0)

#define Basic_ArrayPointer_New(Ptr, Count) \
( \
	Ptr = cast(type_of(Ptr))Malloc_Aligned( \
		(Count)*sizeof(type_of(*Ptr)), \
		( alignof(type_of(*Ptr)) + sizeof(void*) - 1 ) / sizeof(void*) \
	) \
)

#define Basic_ArrayPointer_Delete(Ptr) \
	do { \
		if (Ptr) { \
			Free_Aligned(Ptr); \
			Ptr = null; \
		} \
	} while(0)

#if 0
// todo: it'd be nice to put an aligned pointer / generational counter in here, or at least a magic byte, so that we *know* it's actually an aligned pointer, right?
//       you know, to really make sure that it's free'd, use-after-free, etc.
void* malloc_aligned(size_t size, size_t alignment);
void free_aligned(void** aligned_ptr) {
	if(!aligned_ptr) { return; }
	if(*aligned_ptr) {
		free(((void**)(*aligned_ptr))[-1]);
	}
	*aligned_ptr = null;
}
#endif

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

#define u8_max  cast(uint8_t )(-1)
#define u16_max cast(uint16_t)(-1)
#define u32_max cast(uint32_t)(-1)
#define u64_max cast(uint64_t)(-1)

#define x_pod_x(T)
#define x_pods \
	x_pod_x(bool) \
	x_pod_x(char) \
	x_pod_x(u8)   \
	x_pod_x(s8)   \
	x_pod_x(u16)  \
	x_pod_x(s16)  \
	x_pod_x(u32)  \
	x_pod_x(s32)  \
	x_pod_x(f32)  \
	x_pod_x(u64)  \
	x_pod_x(s64)  \
	x_pod_x(f64)  \

#undef x_pod_x
#define x_pod_x(T) \
	T##_t min_##T(const T##_t a, const T##_t b); \
	T##_t max_##T(const T##_t a, const T##_t b); \
	T##_t clamp_##T(const T##_t lo, const T##_t x, const T##_t hi); \

x_pods;

#undef x_pod_x
#define x_pod_x(T) \
	inline T##_t min_##T(const T##_t a, const T##_t b) { return a > b ? b : a; } \
	inline T##_t max_##T(const T##_t a, const T##_t b) { return a < b ? b : a; } \
	inline T##_t clamp_##T(const T##_t lo, const T##_t x, const T##_t hi) { \
		if(x < lo) { return lo; } \
		if(x > hi) { return hi; } \
		return x; \
	}

x_pods;

#undef x_pods
#undef x_pod_x

template <typename T>
inline bool UnsignedAdditionWouldOverflow(T x, T y){
	const T temp = (T)(x + y);
	return (temp < x) && (temp < y);
}

#if 0/* todo: hardware accelerated bswap builtin? */
Union(bswap_u16_u) {
	uint16_t u;
	uint8_t u8[2];
};

Union(bswap_u32_u) {
	uint32_t u;
	uint8_t u8[4];
};

uint16_t bswap_u16(uint16_t u);
uint32_t bswap_u32(uint32_t u);
#endif

#if 0
uint32_t norm_to_uint(const uint32_t lo, const float norm, const uint32_t hi) {
	return lo + cast(uint32_t)(mclamp01(norm)*(hi - lo));
}
#endif

#if 0
uint32_t strn(const char* s);
void str_copy_n(const char* source, const uint32_t n, char* out_destination);
bool_t str_equal(const char* a, const char* b);
bool_t str_equal_lower(const char* a, const char* b);
bool_t strn_equal(const char* a, const uint32_t a_n, const char* b, const uint32_t b_n);
void strn_print(const char* s, const uint32_t s_n);

/* immutable (constant/literal) string helper */
fuct(cstr_t) {
	const char* s;
	/* const */uint32_t len; /* strlen */
};
/* mutable string helper */
fuct(mstr_t) {
	char* s;
	uint32_t len; /* strlen; assumed that s-capacity > n + 1 */
};
// asshert_static(sizeof(cstr_t) == sizeof(mstr_t));
#define cstrm(mstr) cast_type(cstr_t)mstr

/* constant string literal init */
#define cstrli(str_literal)   { str_literal, (sizeof(str_literal) - 1/* null */) }
#define cstrl(str_literal) cstr(str_literal, (sizeof(str_literal) - 1/* null */)

/* constant string init */
#define cstri(s, n) { s, n }

/* constant string buffer init */
#define mstri(s, n) { s, n }

cstr_t cstr(const char* s, const uint32_t n);
mstr_t mstr(      char* s, const uint32_t n);
#define cstr_equal(a, b) strn_equal((a).s, (a).len, (b).s, (b).len)
#define mstr_equal(a, b) strn_equal((a).s, (a).len, (b).s, (b).len)
#define cstr_print(z)    strn_print((z).s, (z).len)
#define mstr_print(z)    strn_print((z).s, (z).len)

void mstr_push_null(const mstr_t s, char* out_char);
void mstr_pop_null(const mstr_t s, const char in_char);

extern const cstr_t delimeter_sheet[2]
#ifdef shared_impl
= { cstrli("\t"), cstrli(",") }
#endif
;
extern const cstr_t delimeter_newline[2]    
#ifdef shared_impl
= { cstrli("\n"), cstrli("\r\n") }
#endif
;
extern const cstr_t delimeter_whitespace[4] 
#ifdef shared_impl
= { cstrli("\n"), cstrli("\t"), cstrli(" "), cstrli("\r") }
#endif
;

fuct(split_t) {
	mstr_t str;
	/* public state */
	mstr_t cell;

	/* convenience */
	uint32_t counter;

	const cstr_t* delimeters;
	uint32_t delimeters_n;

	/* private state */
	uint32_t str_i;
};

/* note: be careful with delimeters which are substrings. order them largest -> smallest to break ties by order */
bool_t split_begin(split_t* s, const mstr_t str, const cstr_t* delimeters, const uint32_t delimeters_n);
bool_t split_cell(split_t* s);
#endif

#define mpi 3.14159265358979323846f

inline float Angle_ToDegrees(const float a) { return a*180.0f/mpi; }
inline float Angle_ToRadians(const float a) { return a*mpi/180.0f; }

#if 0 
float Angle_SignedMod(const float a, const float limit) /* todo: fix this function, make it more efficient */
#ifdef shared_impl
{
	const float limit_2 = limit / 2.0f;

	const float z = fmodf(a, limit); /* (-limit, limit) note: modf preserves signedness, so could be negative: ie -2*359 -> -359 */
	const float a360 = fmodf((z > 0.0f ? z : (limit + z)), limit); /* [0, limit) */
	const float a180 = (a360 <= limit_2 ? a360 : (a360 - limit)); /* (-limit/2, limit/2] */
	return a180;
}
#endif
;

#define Deg_normf(deg) angle_normf(deg, 360.0f)
#define rad_normf(rad) angle_normf(rad, 2.0f*mpi)
#endif

char Char_ToLower(const char ch);
inl bool Char_IsSpace(const char ch) { return ch <= 32 || 127 <= ch; }
inl bool Char_IsSlash(const char ch) { return ch == '/' || ch == '\\'; }

#if 0
#define path_n 260
typedef struct { char s[path_n]; } path_t;
#define pathi { '\0' }
inl path_t p_cstr(const char* str) { path_t p; strcpyn(p.s, str, path_n); return p; }
inl zint p_len(const path_t path) { return (zint)strlen(path.s); }
inl path_t p_file(const path_t a) {
	const zint len = p_len(a);
	for(zint i = len - 1; i >= 0; --i) {
		if(c_separator(a.s[i])) { return p_cstr(&a.s[i + 1]); }
	}
	return a;
}
inl path_t p_folder(path_t a) {
	const zint len = p_len(a);
	for(zint i = len - 1; i >= 0; --i) {
		if(c_separator(a.s[i])) { a.s[i] = '\0'; return a; }
	}
	return a;
}
inl path_t p_app(const path_t a, const path_t b) { path_t r; snprintf(r.s, countof(r.s), "%s%s", a.s, b.s); return r; }
inline path_t path_trim(path_t trimmed) {
	path_t tmp = trimmed;
	const zint a_len = p_len(tmp);
	zint start_i = 0;
#if c_os(windows)
	for(; start_i < a_len; ++start_i) {
		const char ch = tmp.s[start_i];
		if(!c_separator(ch) && !c_space(ch)) { break; }
	}
#endif
	strcpyn(trimmed.s, tmp.s + start_i, path_n);
	
	const zint trimmed_len = p_len(trimmed);
	zint end_len = trimmed_len;
	for(; end_len > 0; --end_len) {
		const char ch = trimmed.s[end_len - 1];
		if(!c_separator(ch) && !c_space(ch)) { break; }
	}

	trimmed.s[end_len] = '\0';
	return trimmed;
}
inl path_t p_cat(path_t a, path_t b, const char sep) {
	a = path_trim(a);
	b = path_trim(b);
	if(p_len(a) <= 0) { return b; }
	if(p_len(b) <= 0) { return a; }
	path_t r = pathi;
	snprintf(r.s, countof(r.s), "%s%c%s", a.s, sep, b.s);
	return r;
}
inl path_t p_catb(const path_t a, const path_t b) { return p_cat(a, b, '\\'); }
inl path_t p_catf(const path_t a, const path_t b) { return p_cat(a, b, '/'); }
#endif

#if 0
inl path_t path() { const path_t p = pathi; return p; }
inl path_t p_cstrn(const char* str, const zint str_n) { path_t p; strcpyn(p.s, str, str_n); return p; }
inl path_t p_cstrlen(const char* str, const zint str_len) { path_t p; strcpyn(p.s, str, str_len + 1); return p; }
inl path_t p_fmt(const char* fmt, ...) { 
	path_t path = pathi;
	va_list args; 
	va_start(args, fmt); 
	vsnprintf(path.s, asize(path.s), fmt, args); 
	va_end(args);
	return path;
}
inl path_t p_slash(path_t p, const char bad, const char good) {
	const zint len = p_len(p);
	zint i = 0;
	for(zint z = 0; z < len; ++z) {
		if(p.s[z] == bad || p.s[z] == good) {
			p.s[i] = good;
		} else {
			if(p.s[i] == good) { ++i; }
			p.s[i] = p.s[z];
			++i;
		}
	}
	if(p.s[i] == good) { ++i; }
	p.s[i] = '\0';
	return p;
}
inl path_t p_slashb(const path_t p) { return p_slash(p, '/', '\\'); }
inl path_t p_slashf(const path_t p) { return p_slash(p, '\\', '/'); }
inl path_t p_pre_ext(path_t a) {
	const zint len = p_len(a);
	for(zint i = 0; i < len; ++i) {
		if(a.s[i] == '.') { a.s[i] = '\0'; break; }
	}
	return a;
}
inl path_t p_ext(path_t a) {
	const zint len = p_len(a);
	for(zint i = 0; i < len; ++i) {
		if(a.s[i] == '.') { return p_cstr(&a.s[i]); }
	}
	return a;
}
// printf("%s\n", p_abs(p_cstr("C:/sample/include.c"), p_cstr("./folder/../../bin/./folder/snapcode.dll")).s); // => C:/bin/folder/snapcode.dll 
inl path_t p_abslen(const path_t abs, const char* rel, const zint rel_len) {
	const char period = '.';

	path_t ret = p_folder(abs); 
	zint last_i = 0;
	for(zint cur_i = 0; cur_i < rel_len; ++cur_i) {
		const char ch = rel[cur_i];
		if(!c_separator(ch)) { continue; }
		const zint len = cur_i - last_i;
		const char* str = &rel[last_i];
		last_i = cur_i + 1;
		if(len == 1 && str[0] == period) { continue; }
		if(len == 2 && str[0] == period && str[1] == period) { ret = p_folder(ret); continue; }
		ret = p_catf(ret, p_cstrn(str, len + 1));
	}
	ret = p_catf(ret, p_cstrn(&rel[last_i], rel_len - last_i + 1));

	return ret;
}
inl path_t p_abs(const path_t abs, const char* rel) { return p_abslen(abs, rel, strlen(rel)); }

inl path_t p_lower(path_t p) {
	const zint len = p_len(p);
	for(zint i = 0; i < len; ++i) {
		p.s[i] = to_lower(p.s[i]);
	}
	return p;
}

inl path_t p_upper(path_t p) {
	const zint len = p_len(p);
	for(zint i = 0; i < len; ++i) {
		p.s[i] = to_upper(p.s[i]);
	}
	return p;
}
#endif

#if 0
inl bool s_begins_with(const char* s, const char* begin) {
	const zint s_len = strlen(s);
	const zint begin_len = strlen(begin);
	if(s_len < begin_len) { return false; }
	for(zint i = 0; i < begin_len; ++i) {
		if(s[i] != begin[i]) { return false; }
	}
	return true;
}

inl bool s_ends_with(const char* s, const char* end) {
	const zint s_len = strlen(s);
	const zint end_len = strlen(end);
	if(s_len < end_len) { return false; }
	for(zint i = 0; i < end_len; ++i) {
		if(s[s_len - i - 1] != end[end_len - i - 1]) { return false; }
	}
	return true;
}

inl bool s_ends_with_any_of(const char* s, const char* ends[], const zint ends_n) {
	for(zint end_i = 0; end_i < ends_n; ++end_i) { if(s_ends_with(s, ends[end_i])) { return true; } }
	return false;
}

inl bool s_contains(const char* s, const char* sub) {
	const zint s_len = strlen(s);
	const zint sub_len = strlen(sub);
	if(s_len < sub_len) { return false; }
	for(zint s_i = 0; s_i + sub_len <= s_len; ++s_i) {
		zint sub_i = 0;
		for(; sub_i < sub_len; ++sub_i) {
			if(to_lower(s[s_i + sub_i]) != to_lower(sub[sub_i])) { break; }
		}
		if(sub_i >= sub_len) { return true; }
	}
	return false;
}

inl bool path_extension_is_any_of(const cstr_t& str, const cstr_t* exts, const uint32_t exts_n) {
	assure(str.s[str.len] == '\0');
	const char_u8* period = eastl::CharTypeStringRFind(str.s + str.len - 1, str.s, '.');

	if(period && period != str.s) {
		for(uint32_t ext_i = 0; ext_i < exts_n; ++ext_i) {
			const cstr_t* ext = &exts[ext_i];
			if(0 == SDL_strncasecmp(period, ext->s, ext->len)) {
				return true;
			}
		}
	}

	return false;
}
#endif

/* note: cv_hash has been adapted from lmdb */
typedef uint64_t hash64_t;
#define hash64_null 0
#define hash64_max ((hash64_t)0xcbf29ce484222325ULL)
/* perform a 64 bit Fowler/Noll/Vo FNV-1a hash on a buffer */
hash64_t hash64(hash64_t h, const void* data_, const size_t data_n);

bool_t PatternInString_PeriodStar(const char* pat, const char* str);

#if 0
std::chrono::system_clock::time_point steady_to_system(const std::chrono::steady_clock::time_point t);

void time_s_init();

// note: the following piece of timing code has been adapted from glfw (https://github.com/glfw/glfw)
extern uint64_t _time_s_value();
extern uint64_t _time_s_frequency;
extern uint64_t _time_s_offset;

double time_s();
float time_sf();

#define every_s(S) \
	static float _cc(prev_,__LINE__) = time_sf(); \
	const float  _cc(now_,__LINE__)  = time_sf(); \
	const bool   _cc(trigger_,__LINE__) = _cc(now_,__LINE__) - _cc(prev_,__LINE__) >= cast(float)(S); \
	if(_cc(trigger_,__LINE__)) { _cc(prev_,__LINE__) = _cc(now_,__LINE__); } \
	if(_cc(trigger_,__LINE__))

#define every_s_now(S) \
	static float _cc(prev_,__LINE__) = -1.01f*cast(float)S; \
	const float  _cc(now_,__LINE__)  = time_sf(); \
	const bool   _cc(trigger_,__LINE__) = _cc(now_,__LINE__) - _cc(prev_,__LINE__) >= cast(float)(S); \
	if(_cc(trigger_,__LINE__)) { _cc(prev_,__LINE__) = _cc(now_,__LINE__); } \
	if(_cc(trigger_,__LINE__))

void tp_format_hhmmss(const std::chrono::time_point<std::chrono::steady_clock>& tp, char* out_str, const size_t out_str_n);
void tp_format_yyyymmdd_hhmmss(const std::chrono::time_point<std::chrono::steady_clock>& tp, char* out_str, const size_t out_str_n);
#endif

#if 0
#define seq_default (cast(uint32_t)-1)
struct seq_t { /* sequence i(nstance) */
	uint32_t i   = seq_default; /* i(nstruction last executed) */
	uint32_t neo = seq_default; /* neo (next) state */
	uint32_t cur = seq_default; /* cur(rent) state */
	uint32_t pre = seq_default; /* pre(vious) state */
};
inl seq_t seq_new(uint32_t neo) {
	seq_t s;
	s.neo = neo;
	return s;
}
inl seq_t* _seq_init(seq_t* s) {
	if(s->neo != seq_default) {
		s->pre = s->cur;
		s->cur = s->neo;
		s->i   = s->neo;
		s->neo = seq_default;
	}
	return s;
}
inl seq_t* _seq_next(seq_t* s) {
	if(s->neo != seq_default) {
		s->pre = s->cur;
		s->cur = s->neo;
		s->i   = s->neo;
		s->neo = seq_default;
		return s;
	}
	return null;
}
#define _seqctr(N) (1000000 + _ctr(N))
Assure_AtCompileTime(_seqctr(0) == _seqctr(1));
#define seq_run(S) for(seq_t* _seq = _seq_init(S); !!_seq; _seq = _seq_next(S)) switch(_seq->i)
#define seq_sub()     case _seqctr(0): _seq->i = _seqctr(1);
#define seq_state(id) case         id: _seq->i = id;
// todo: fixme: notice how seq_next violates the "if cur = state, then state ran at least once" rule!
#define seq_next(id)  _seq->neo = (id); _seq = _seq_next(_seq); break
#define seq_jump(id)  _seq->neo = (id); break
#define seq_state_default(id) default: seq_jump(id); seq_state(id)
#endif

#ifdef XXH_VERSION_MAJOR
typedef XXH64_hash_t hash64_t;
inl hash64_t rc_hash(const void* data, const size_t len) {
	ZoneScoped;
	// for small data, reaching out to XXH3 code area is probably a cold cache hit,
	// every time!
	const hash64_t h = XXH3_64bits(data, len);
	if(h) {
		return h;
	}
	return 1;
}
#endif

#if 0
template<typename T>
struct defers_t {
	eastl::vector<void (*)(T*)> funcs;

	void Run(T* _) {
		for(size_t i = 0; i < funcs.size(); ++i) {
			(funcs[i])(_);
		}
	}

#if 0
	inl void defers_push(const void (*fn)(T)) {
		d->funcs.push_back(fn);
	}

	template<typename T>
	inl void defers_popAll(defers_t<T>* d, T _) {
		for(size_t f_i = 0; f_i < d->funcs.size(); ++f_i) {
			if(d->funcs[f_i]) {
				d->funcs[f_i](_);
			}
		}
		d->funcs.resize(0);
	}
#endif
};
#endif

#if 0 // ie, open https://qa.recall.live
void OpenWebpage(const char* url) {
#ifdef _WIN32
	ShellExecuteA(nullptr, nullptr, url, nullptr, nullptr, 0);
#elif defined __APPLE__
	char buf[1024];
	sprintf(buf, "open %s", url);
	system(buf);
#elif defined __EMSCRIPTEN__
	EM_ASM({ window.open(UTF8ToString($0), '_blank') }, url);
#else
	char buf[1024];
	sprintf(buf, "xdg-open %s", url);
	system(buf);
#endif
}
#endif

#if defined(__cpp_lib_hardware_interference_size)
using std::hardware_destructive_interference_size;
#else
// Standard fallback for x86 and most ARM
constexpr std::size_t hardware_destructive_interference_size = 64;
#endif
#define kCacheLineSize std::hardware_destructive_interference_size

#define rc_SetThreadName(Cstr) \
	EA::Thread::SetThreadName(Cstr); \
	tracy::SetThreadName(Cstr)

#if defined(_WIN32) || defined(_WIN64)
// Windows uses a wide string (wchar_t) for thread names
#define Basic_SetThreadName(name) SetThreadDescription(GetCurrentThread(), L##name)
#elif defined(__linux__)
// Linux limits thread names to 16 characters (including null terminator)
#define Basic_SetThreadName(name) prctl(PR_SET_NAME, name, 0, 0, 0)
#elif defined(__APPLE__) && defined(__MACH__)
// macOS only allows setting the name for the current thread
#define Basic_SetThreadName(name) pthread_setname_np(name)
#else
#define Basic_SetThreadName(name) ((void)0)
#endif

#define MACRO_Empty
#define _MACRO_CommaFunc(...) ,

#define MACRO_ArgsToList(...) (__VA_ARGS__)

#define MACRO_HeadArg(Head, ...) Head
#define MACRO_TailArgList(Head, ...) (__VA_ARGS__)

#define MACRO_Arg_1( __n, ...) __n
#define MACRO_Arg_2( __1,__n, ...) __n
#define MACRO_Arg_3( __1,__2,__n, ...) __n
#define MACRO_Arg_4( __1,__2,__3,__n, ...) __n
#define MACRO_Arg_5( __1,__2,__3,__4,__n, ...) __n
#define MACRO_Arg_6( __1,__2,__3,__4,__5,__n, ...) __n
#define MACRO_Arg_7( __1,__2,__3,__4,__5,__6,__n, ...) __n
#define MACRO_Arg_8( __1,__2,__3,__4,__5,__6,__7,__n, ...) __n
#define MACRO_Arg_9( __1,__2,__3,__4,__5,__6,__7,__8,__n, ...) __n
#define MACRO_Arg_10(__1,__2,__3,__4,__5,__6,__7,__8,__9,__n, ...) __n
#define MACRO_Arg_11(__1,__2,__3,__4,__5,__6,__7,__8,__9,_10,__n, ...) __n
#define MACRO_Arg_12(__1,__2,__3,__4,__5,__6,__7,__8,__9,_10,_11,__n, ...) __n
#define MACRO_Arg_13(__1,__2,__3,__4,__5,__6,__7,__8,__9,_10,_11,_12,__n, ...) __n
#define MACRO_Arg_14(__1,__2,__3,__4,__5,__6,__7,__8,__9,_10,_11,_12,_13,__n, ...) __n
#define MACRO_Arg_15(__1,__2,__3,__4,__5,__6,__7,__8,__9,_10,_11,_12,_13,_14,__n, ...) __n
#define MACRO_Arg_16(__1,__2,__3,__4,__5,__6,__7,__8,__9,_10,_11,_12,_13,_14,_15,__n, ...) __n

#define MACRO_ContainsComma(...)     MACRO_Arg_16(__VA_ARGS__,  1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 0)
#define MACRO_CommaCountPlusOne(...) MACRO_Arg_16(__VA_ARGS__, 15,14,13,12,11,10, 9, 8, 7, 6, 5, 4, 3, 2, 1)

#define MACRO_IsParenthesized(Head, ...) MACRO_ContainsComma(_MACRO_CommaFunc Head)

#define _MACRO_IsEmpty_Else_IfIsSpace(Item) MACRO_ContainsComma(_MACRO_CommaFunc Item ())
#define _MACRO_IsEmpty_IfIsParenthesized0(Item) _MACRO_IsEmpty_Else_IfIsSpace(Item)
#define _MACRO_IsEmpty_IfIsParenthesized1(Item) 0
#define _MACRO_IsEmpty_IfIsParenthesized(Item) MACRO_Join_2(_MACRO_IsEmpty_IfIsParenthesized, MACRO_IsParenthesized(Item))(Item)
#define  MACRO_IsEmpty(Item) _MACRO_IsEmpty_IfIsParenthesized(Item)

#define _MACRO_IsArgCountZero_Else_Else_IfHeadEmpty1(...) 1
#define _MACRO_IsArgCountZero_Else_Else_IfHeadEmpty0(...) 0
#define _MACRO_IsArgCountZero_Else_Else_IfHeadEmpty(...) MACRO_Join_2(_MACRO_IsArgCountZero_Else_Else_IfHeadEmpty, MACRO_IsEmpty(MACRO_HeadArg(__VA_ARGS__)))(__VA_ARGS__)
#define _MACRO_IsArgCountZero_Else_IfArgsContainComma0(...) _MACRO_IsArgCountZero_Else_Else_IfHeadEmpty(__VA_ARGS__)
#define _MACRO_IsArgCountZero_Else_IfArgsContainComma1(...) 0
#define _MACRO_IsArgCountZero_Else_IfArgsContainComma(...) MACRO_Join_2(_MACRO_IsArgCountZero_Else_IfArgsContainComma, MACRO_ContainsComma(__VA_ARGS__))(__VA_ARGS__)
#define _MACRO_IsArgCountZero_IfIsParenthesized0(...) _MACRO_IsArgCountZero_Else_IfArgsContainComma(__VA_ARGS__)
#define _MACRO_IsArgCountZero_IfIsParenthesized1(...) 0
#define  MACRO_IsArgCountZero(...) MACRO_Join_2(_MACRO_IsArgCountZero_IfIsParenthesized, MACRO_IsParenthesized(__VA_ARGS__))(__VA_ARGS__)

#define _MACRO_ArgCount_IfIsArgCountZero0(...) MACRO_CommaCountPlusOne(__VA_ARGS__)
#define _MACRO_ArgCount_IfIsArgCountZero1(...) 0
#define  MACRO_ArgCount(...) MACRO_Join_2(_MACRO_ArgCount_IfIsArgCountZero, MACRO_IsArgCountZero(__VA_ARGS__))(__VA_ARGS__)

#define _MACRO_Joiner_0()
#define _MACRO_Joiner_1( __1)                                                             __1
#define _MACRO_Joiner_2( __1,__2)                                                         __1##__2
#define _MACRO_Joiner_3( __1,__2,__3)                                                     __1##__2##__3
#define _MACRO_Joiner_4( __1,__2,__3,__4)                                                 __1##__2##__3##__4
#define _MACRO_Joiner_5( __1,__2,__3,__4,__5)                                             __1##__2##__3##__4##__5
#define _MACRO_Joiner_6( __1,__2,__3,__4,__5,__6)                                         __1##__2##__3##__4##__5##__6
#define _MACRO_Joiner_7( __1,__2,__3,__4,__5,__6,__7)                                     __1##__2##__3##__4##__5##__6##__7
#define _MACRO_Joiner_8( __1,__2,__3,__4,__5,__6,__7,__8)                                 __1##__2##__3##__4##__5##__6##__7##__8
#define _MACRO_Joiner_9( __1,__2,__3,__4,__5,__6,__7,__8,__9)                             __1##__2##__3##__4##__5##__6##__7##__8##__9
#define _MACRO_Joiner_10(__1,__2,__3,__4,__5,__6,__7,__8,__9,_10)                         __1##__2##__3##__4##__5##__6##__7##__8##__9##_10
#define _MACRO_Joiner_11(__1,__2,__3,__4,__5,__6,__7,__8,__9,_10,_11)                     __1##__2##__3##__4##__5##__6##__7##__8##__9##_10##_11
#define _MACRO_Joiner_12(__1,__2,__3,__4,__5,__6,__7,__8,__9,_10,_11,_12)                 __1##__2##__3##__4##__5##__6##__7##__8##__9##_10##_11##_12
#define _MACRO_Joiner_13(__1,__2,__3,__4,__5,__6,__7,__8,__9,_10,_11,_12,_13)             __1##__2##__3##__4##__5##__6##__7##__8##__9##_10##_11##_12##_13
#define _MACRO_Joiner_14(__1,__2,__3,__4,__5,__6,__7,__8,__9,_10,_11,_12,_13,_14)         __1##__2##__3##__4##__5##__6##__7##__8##__9##_10##_11##_12##_13##_14
#define _MACRO_Joiner_15(__1,__2,__3,__4,__5,__6,__7,__8,__9,_10,_11,_12,_13,_14,_15)     __1##__2##__3##__4##__5##__6##__7##__8##__9##_10##_11##_12##_13##_14##_15
#define _MACRO_Joiner_16(__1,__2,__3,__4,__5,__6,__7,__8,__9,_10,_11,_12,_13,_14,_15,_16) __1##__2##__3##__4##__5##__6##__7##__8##__9##_10##_11##_12##_13##_14##_15##_16

#define MACRO_Join_0()
#define MACRO_Join_1( __1)                                                             _MACRO_Joiner_1( __1)
#define MACRO_Join_2( __1,__2)                                                         _MACRO_Joiner_2( __1,__2)
#define MACRO_Join_3( __1,__2,__3)                                                     _MACRO_Joiner_3( __1,__2,__3)
#define MACRO_Join_4( __1,__2,__3,__4)                                                 _MACRO_Joiner_4( __1,__2,__3,__4)
#define MACRO_Join_5( __1,__2,__3,__4,__5)                                             _MACRO_Joiner_5( __1,__2,__3,__4,__5)
#define MACRO_Join_6( __1,__2,__3,__4,__5,__6)                                         _MACRO_Joiner_6( __1,__2,__3,__4,__5,__6)
#define MACRO_Join_7( __1,__2,__3,__4,__5,__6,__7)                                     _MACRO_Joiner_7( __1,__2,__3,__4,__5,__6,__7)
#define MACRO_Join_8( __1,__2,__3,__4,__5,__6,__7,__8)                                 _MACRO_Joiner_8( __1,__2,__3,__4,__5,__6,__7,__8)
#define MACRO_Join_9( __1,__2,__3,__4,__5,__6,__7,__8,__9)                             _MACRO_Joiner_9( __1,__2,__3,__4,__5,__6,__7,__8,__9)
#define MACRO_Join_10(__1,__2,__3,__4,__5,__6,__7,__8,__9,_10)                         _MACRO_Joiner_10(__1,__2,__3,__4,__5,__6,__7,__8,__9,_10)
#define MACRO_Join_11(__1,__2,__3,__4,__5,__6,__7,__8,__9,_10,_11)                     _MACRO_Joiner_11(__1,__2,__3,__4,__5,__6,__7,__8,__9,_10,_11)
#define MACRO_Join_12(__1,__2,__3,__4,__5,__6,__7,__8,__9,_10,_11,_12)                 _MACRO_Joiner_12(__1,__2,__3,__4,__5,__6,__7,__8,__9,_10,_11,_12)
#define MACRO_Join_13(__1,__2,__3,__4,__5,__6,__7,__8,__9,_10,_11,_12,_13)             _MACRO_Joiner_13(__1,__2,__3,__4,__5,__6,__7,__8,__9,_10,_11,_12,_13)
#define MACRO_Join_14(__1,__2,__3,__4,__5,__6,__7,__8,__9,_10,_11,_12,_13,_14)         _MACRO_Joiner_14(__1,__2,__3,__4,__5,__6,__7,__8,__9,_10,_11,_12,_13,_14)
#define MACRO_Join_15(__1,__2,__3,__4,__5,__6,__7,__8,__9,_10,_11,_12,_13,_14,_15)     _MACRO_Joiner_15(__1,__2,__3,__4,__5,__6,__7,__8,__9,_10,_11,_12,_13,_14,_15)
#define MACRO_Join_16(__1,__2,__3,__4,__5,__6,__7,__8,__9,_10,_11,_12,_13,_14,_15,_16) _MACRO_Joiner_16(__1,__2,__3,__4,__5,__6,__7,__8,__9,_10,_11,_12,_13,_14,_15,_16)

/* Duplicating MACRO_Join_2 works around a glitch in MSVC */
#define _MACRO_Join_Joiner_2(__1,__2) __1##__2
#define _MACRO_Join_Join_2(__1,__2) _MACRO_Join_Joiner_2(__1,__2)
#define MACRO_Join(...) _MACRO_Join_Join_2(MACRO_Join_, MACRO_ArgCount(__VA_ARGS__))(__VA_ARGS__)

#define _MACRO_Map_Run(Macro, _MacroArgs_, Item, Item_I, Item_N, ItemCount) Macro(_MacroArgs_, Item, Item_I, Item_N, ItemCount)

#define _MACRO_Map_0(...)
#define _MACRO_Map_1( Macro, _MacroArgs_, ItemCount, _Items_) _MACRO_Map_Run(Macro, _MacroArgs_, MACRO_HeadArg _Items_,  0,  1, ItemCount)
#define _MACRO_Map_2( Macro, _MacroArgs_, ItemCount, _Items_) _MACRO_Map_Run(Macro, _MacroArgs_, MACRO_HeadArg _Items_,  1,  2, ItemCount) _MACRO_Map_1( Macro, _MacroArgs_, ItemCount, MACRO_TailArgList _Items_)
#define _MACRO_Map_3( Macro, _MacroArgs_, ItemCount, _Items_) _MACRO_Map_Run(Macro, _MacroArgs_, MACRO_HeadArg _Items_,  2,  3, ItemCount) _MACRO_Map_2( Macro, _MacroArgs_, ItemCount, MACRO_TailArgList _Items_)
#define _MACRO_Map_4( Macro, _MacroArgs_, ItemCount, _Items_) _MACRO_Map_Run(Macro, _MacroArgs_, MACRO_HeadArg _Items_,  3,  4, ItemCount) _MACRO_Map_3( Macro, _MacroArgs_, ItemCount, MACRO_TailArgList _Items_)
#define _MACRO_Map_5( Macro, _MacroArgs_, ItemCount, _Items_) _MACRO_Map_Run(Macro, _MacroArgs_, MACRO_HeadArg _Items_,  4,  5, ItemCount) _MACRO_Map_4( Macro, _MacroArgs_, ItemCount, MACRO_TailArgList _Items_)
#define _MACRO_Map_6( Macro, _MacroArgs_, ItemCount, _Items_) _MACRO_Map_Run(Macro, _MacroArgs_, MACRO_HeadArg _Items_,  5,  6, ItemCount) _MACRO_Map_5( Macro, _MacroArgs_, ItemCount, MACRO_TailArgList _Items_)
#define _MACRO_Map_7( Macro, _MacroArgs_, ItemCount, _Items_) _MACRO_Map_Run(Macro, _MacroArgs_, MACRO_HeadArg _Items_,  6,  7, ItemCount) _MACRO_Map_6( Macro, _MacroArgs_, ItemCount, MACRO_TailArgList _Items_)
#define _MACRO_Map_8( Macro, _MacroArgs_, ItemCount, _Items_) _MACRO_Map_Run(Macro, _MacroArgs_, MACRO_HeadArg _Items_,  7,  8, ItemCount) _MACRO_Map_7( Macro, _MacroArgs_, ItemCount, MACRO_TailArgList _Items_)
#define _MACRO_Map_9( Macro, _MacroArgs_, ItemCount, _Items_) _MACRO_Map_Run(Macro, _MacroArgs_, MACRO_HeadArg _Items_,  8,  9, ItemCount) _MACRO_Map_8( Macro, _MacroArgs_, ItemCount, MACRO_TailArgList _Items_)
#define _MACRO_Map_10(Macro, _MacroArgs_, ItemCount, _Items_) _MACRO_Map_Run(Macro, _MacroArgs_, MACRO_HeadArg _Items_,  9, 10, ItemCount) _MACRO_Map_9( Macro, _MacroArgs_, ItemCount, MACRO_TailArgList _Items_)
#define _MACRO_Map_11(Macro, _MacroArgs_, ItemCount, _Items_) _MACRO_Map_Run(Macro, _MacroArgs_, MACRO_HeadArg _Items_, 10, 11, ItemCount) _MACRO_Map_10(Macro, _MacroArgs_, ItemCount, MACRO_TailArgList _Items_)
#define _MACRO_Map_12(Macro, _MacroArgs_, ItemCount, _Items_) _MACRO_Map_Run(Macro, _MacroArgs_, MACRO_HeadArg _Items_, 11, 12, ItemCount) _MACRO_Map_11(Macro, _MacroArgs_, ItemCount, MACRO_TailArgList _Items_)
#define _MACRO_Map_13(Macro, _MacroArgs_, ItemCount, _Items_) _MACRO_Map_Run(Macro, _MacroArgs_, MACRO_HeadArg _Items_, 12, 13, ItemCount) _MACRO_Map_12(Macro, _MacroArgs_, ItemCount, MACRO_TailArgList _Items_)
#define _MACRO_Map_14(Macro, _MacroArgs_, ItemCount, _Items_) _MACRO_Map_Run(Macro, _MacroArgs_, MACRO_HeadArg _Items_, 13, 14, ItemCount) _MACRO_Map_13(Macro, _MacroArgs_, ItemCount, MACRO_TailArgList _Items_)
#define _MACRO_Map_15(Macro, _MacroArgs_, ItemCount, _Items_) _MACRO_Map_Run(Macro, _MacroArgs_, MACRO_HeadArg _Items_, 14, 15, ItemCount) _MACRO_Map_14(Macro, _MacroArgs_, ItemCount, MACRO_TailArgList _Items_)
#define _MACRO_Map_16(Macro, _MacroArgs_, ItemCount, _Items_) _MACRO_Map_Run(Macro, _MacroArgs_, MACRO_HeadArg _Items_, 15, 16, ItemCount) _MACRO_Map_15(Macro, _MacroArgs_, ItemCount, MACRO_TailArgList _Items_)

#define MACRO_Map(Macro, _MacroArgs_, ...) \
	MACRO_Join_2(_MACRO_Map_, MACRO_ArgCount(__VA_ARGS__))( \
		Macro, \
		_MacroArgs_, \
		MACRO_ArgCount(__VA_ARGS__), \
		MACRO_ArgsToList(__VA_ARGS__) \
	)

#define tracy(CALL) \
	TracyCZoneN(_cc(_zone_,__LINE__), #CALL, true); \
	CALL; \
	TracyCZoneEnd(_cc(_zone_,__LINE__)); \

#if !c_feature(tracy)
#define Thread_ZoneScoped
#define Task_ZoneScoped

#define Task_ZoneScoped_NoCallstack
#define Task_ZoneScopedN_NoCallstack(Name)
#define Task_ZoneScopedC_NoCallstack(Color) 
#define Task_ZoneScopedNC_NoCallstack(Name, Color)

#define Task_ZoneScoped 
#define Task_ZoneScopedN(Name) 
#define Task_ZoneScopedC(Color) 
#define Task_ZoneScopedNC(Name, Color) 
#define Task_ZoneText( txt, size )
#define Task_ZoneTextV( varname, txt, size )
#define Task_ZoneName( txt, size ) 
#define Task_ZoneNameV( varname, txt, size )
#define Task_ZoneColor( color )
#define Task_ZoneColorV( varname, color )
#define Task_ZoneValue( value )
#define Task_ZoneValueV( varname, value )
#define Task_ZoneIsActive
#define Task_ZoneIsActiveV( varname )

#undef ZoneNamed
#undef ZoneNamedN
#undef ZoneNamedC
#undef ZoneNamedNC
#undef ZoneTransient
#undef ZoneTransientN

#undef ZoneTransient
#undef ZoneTransientN

#undef ZoneScoped
#undef ZoneScopedN
#undef ZoneScopedC
#undef ZoneScopedNC

#undef ZoneText
#undef ZoneTextV
#undef ZoneName
#undef ZoneNameV
#undef ZoneColor
#undef ZoneColorV
#undef ZoneValue
#undef ZoneValueV
#undef ZoneIsActive
#undef ZoneIsActiveV

#define TracyCZoneN(...)
#define TracyCZoneEnd(...)

#define TracyMessage(...)

#define ZoneNamed(x,y)
#define ZoneNamedN(x,y,z)
#define ZoneNamedC(x,y,z)
#define ZoneNamedNC(x,y,z,w)

#define ZoneTransient(x,y)
#define ZoneTransientN(x,y,z)

#define ZoneScoped
#define ZoneScopedN(x)
#define ZoneScopedC(x)
#define ZoneScopedNC(x,y)

#define ZoneText(x,y)
#define ZoneTextV(x,y,z)
#define ZoneName(x,y)
#define ZoneNameV(x,y,z)
#define ZoneColor(x)
#define ZoneColorV(x,y)
#define ZoneValue(x)
#define ZoneValueV(x,y)
#define ZoneIsActive false
#define ZoneIsActiveV(x) false
#else

#ifdef __GNUC__
#define Thread_ZoneScoped ZoneScopedN(__PRETTY_FUNCTION__)
#else
#define Thread_ZoneScoped ZoneScopedN(MACRO_Function)
#endif

/*
// Regular Zone* macros ARE NOT SAFE ACROSS Fiber THREAD CONTEXT SWITCHES.
*/

#define TracyCZone_NoCallstack( ctx, active ) static const struct ___tracy_source_location_data TracyConcat(__tracy_source_location,__LINE__) = { NULL, __func__,  __FILE__, (uint32_t)__LINE__, 0 }; TracyCZoneCtx ctx = ___tracy_emit_zone_begin( &TracyConcat(__tracy_source_location,__LINE__), active );
#define TracyCZoneN_NoCallstack( ctx, name, active ) static const struct ___tracy_source_location_data TracyConcat(__tracy_source_location,__LINE__) = { name, __func__,  __FILE__, (uint32_t)__LINE__, 0 }; TracyCZoneCtx ctx = ___tracy_emit_zone_begin( &TracyConcat(__tracy_source_location,__LINE__), active );
#define TracyCZoneC_NoCallstack( ctx, color, active ) static const struct ___tracy_source_location_data TracyConcat(__tracy_source_location,__LINE__) = { NULL, __func__,  __FILE__, (uint32_t)__LINE__, color }; TracyCZoneCtx ctx = ___tracy_emit_zone_begin( &TracyConcat(__tracy_source_location,__LINE__), active );
#define TracyCZoneNC_NoCallstack( ctx, name, color, active ) static const struct ___tracy_source_location_data TracyConcat(__tracy_source_location,__LINE__) = { name, __func__,  __FILE__, (uint32_t)__LINE__, color }; TracyCZoneCtx ctx = ___tracy_emit_zone_begin( &TracyConcat(__tracy_source_location,__LINE__), active );

#define TracyCZoneEnd_NoCallstack(c) TracyCZoneEnd(c)

/* sometimes callstack collection can pause a thread for ~10us */
#define Task_ZoneScoped_NoCallstack \
	TracyCZone_NoCallstack(         ___tracy_scoped_zone, true); \
	defer(TracyCZoneEnd_NoCallstack(___tracy_scoped_zone))

#define Task_ZoneScopedN_NoCallstack(Name) \
	TracyCZoneN_NoCallstack(        ___tracy_scoped_zone, Name, true); \
	defer(TracyCZoneEnd_NoCallstack(___tracy_scoped_zone))

#define Task_ZoneScopedC_NoCallstack(Color) \
	TracyCZoneC_NoCallstack(        ___tracy_scoped_zone, Color, true); \
	defer(TracyCZoneEnd_NoCallstack(___tracy_scoped_zone))

#define Task_ZoneScopedNC_NoCallstack(Name, Color) \
	TracyCZoneC_NoCallstack(        ___tracy_scoped_zone, Name, Color, true); \
	defer(TracyCZoneEnd_NoCallstack(___tracy_scoped_zone))


#define Task_ZoneScoped \
	TracyCZone(         ___tracy_scoped_zone, true); \
	defer(TracyCZoneEnd(___tracy_scoped_zone))

#define Task_ZoneScopedN(Name) \
	TracyCZoneN(        ___tracy_scoped_zone, Name, true); \
	defer(TracyCZoneEnd(___tracy_scoped_zone))

#define Task_ZoneScopedC(Color) \
	TracyCZoneC(        ___tracy_scoped_zone, Color, true); \
	defer(TracyCZoneEnd(___tracy_scoped_zone))

#define Task_ZoneScopedNC(Name, Color) \
	TracyCZoneC(        ___tracy_scoped_zone, Name, Color, true); \
	defer(TracyCZoneEnd(___tracy_scoped_zone))

#define Task_ZoneText( txt, size )           TracyCZoneText( ___tracy_scoped_zone, txt, size)
#define Task_ZoneTextV( varname, txt, size ) TracyCZoneText(              varname, txt, size)
#define Task_ZoneName( txt, size )           TracyCZoneName( ___tracy_scoped_zone, txt, size)
#define Task_ZoneNameV( varname, txt, size ) TracyCZoneName(              varname, txt, size)
#define Task_ZoneColor( color )              TracyCZoneColor(___tracy_scoped_zone, color)
#define Task_ZoneColorV( varname, color )    TracyCZoneColor(             varname, color)
#define Task_ZoneValue( value )              TracyCZonMACRO_Arg_1ue(___tracy_scoped_zone, value)
#define Task_ZoneValueV( varname, value )    TracyCZoneValue(             varname, value)
#define Task_ZoneIsActive                                 !!(___tracy_scoped_zone.active)
#define Task_ZoneIsActiveV( varname )                     !!(             varname.active)

#if 0
#undef ZoneNamed
#undef ZoneNamedN
#undef ZoneNamedC
#undef ZoneNamedNC
#undef ZoneTransient
#undef ZoneTransientN

#undef ZoneNamed
#undef ZoneNamedN
#undef ZoneNamedC
#undef ZoneNamedNC

#undef ZoneTransient
#undef ZoneTransientN

#undef ZoneScoped ZoneNamed
#undef ZoneScopedN
#undef ZoneScopedC
#undef ZoneScopedNC

#undef ZoneText
#undef ZoneTextV
#undef ZoneName
#undef ZoneNameV
#undef ZoneColor
#undef ZoneColorV
#undef ZoneValue
#undef ZoneValueV
#undef ZoneIsActive
#undef ZoneIsActiveV

#define ZoneNamed(x,y)
#define ZoneNamedN(x,y,z)
#define ZoneNamedC(x,y,z)
#define ZoneNamedNC(x,y,z,w)

#define ZoneTransient(x,y)
#define ZoneTransientN(x,y,z)

#define ZoneScoped
#define ZoneScopedN(x)
#define ZoneScopedC(x)
#define ZoneScopedNC(x,y)

#define ZoneText(x,y)
#define ZoneTextV(x,y,z)
#define ZoneName(x,y)
#define ZoneNameV(x,y,z)
#define ZoneColor(x)
#define ZoneColorV(x,y)
#define ZoneValue(x)
#define ZoneValueV(x,y)
#define ZoneIsActive false
#define ZoneIsActiveV(x) false
#endif

#endif


