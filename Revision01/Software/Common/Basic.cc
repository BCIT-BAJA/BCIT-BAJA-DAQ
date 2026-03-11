//

#include "PCH.h"

#include "Basic.h"

char Char_ToLower(const char ch) {
	if('A' <= ch && ch <= 'Z') { return cast(char)(cast(int)ch - cast(int)'A' + cast(int)'a'); }
	return ch;
}

#if 0
uint16_t bswap_u16(uint16_t u) {
	bswap_u16_u in;
	in.u = u;
	bswap_u16_u out;
	out.u8[0] = in.u8[1];
	out.u8[1] = in.u8[0];
	return out.u;
}
uint32_t bswap_u32(uint32_t u) {
	bswap_u32_u in;
	in.u = u;
	bswap_u32_u out;
	out.u8[0] = in.u8[3];
	out.u8[1] = in.u8[2];
	out.u8[2] = in.u8[1];
	out.u8[3] = in.u8[0];
	return out.u;
}
#endif

#if 0
void* malloc_aligned(size_t size, size_t alignment) {
	void* real_ptr = malloc(size + alignment + sizeof(void*));
	if (!real_ptr) return NULL;

	// Calculate the aligned address
	uintptr_t addr = (uintptr_t)real_ptr + alignment + sizeof(void*);
	void* aligned_ptr = (void*)(addr - (addr % alignment));

	// Store the real pointer right before the aligned pointer so we can free it later
	((void**)aligned_ptr)[-1] = real_ptr;

	return aligned_ptr;
}
#endif

#if 0
uint32_t strn(const char* s) {
	uint32_t n = 0;
	while(s[0] != '\0') { ++s; ++n; }
	return n;
}
void str_copy_n(const char* source, const uint32_t n, char* out_destination) {
	for(uint32_t i = 0; i < n; ++i) {
		out_destination[i] = source[i];
	}
}
bool_t str_equal(const char* a, const char* b) {
	while(a[0] != '\0' || b[0] != '\0') {
		if((*a) != (*b)) { return false; }
		++a;
		++b;
	}
	return true;
}
bool_t str_equal_lower(const char* a, const char* b) {
	while(a[0] != '\0' || b[0] != '\0') {
		if(ch_lower(*a) != ch_lower(*b)) { return false; }
		++a;
		++b;
	}
	return true;
}
bool_t strn_equal(const char* a, const uint32_t a_n, const char* b, const uint32_t b_n) {
	if(a_n != b_n) { return false; }
	for(uint32_t i = 0; i < a_n; ++i) {
		if(a[i] != b[i]) { return false; }
	}
	return true;
}
void strn_print(const char* s, const uint32_t s_n) {
	if(s == null) { return; }
	if(s[s_n] == '\0') {
		printf("%s", s);
	} else {
		for(uint32_t i = 0; i < s_n; ++i) {
			printf("%c", s[i]);
		}
	}
}

cstr_t cstr(const char* s, const uint32_t n) { const cstr_t ret = cstri(s, n); return ret; }
mstr_t mstr(char* s, const uint32_t n) { const mstr_t ret = mstri(s, n); return ret; }

void mstr_push_null(const mstr_t s, char* out_char) {
	(*out_char) = s.s[s.len];
	s.s[s.len] = '\0';
}
void mstr_pop_null(const mstr_t s, const char in_char) {
	s.s[s.len] = in_char;
}

/* note: be careful with delimeters which are substrings. order them largest -> smallest to break ties by order */
bool_t split_begin(split_t* s, const mstr_t str, const cstr_t* delimeters, const uint32_t delimeters_n) {
	ZoneScoped;
	zerom((*s));
	if(str.s == null || delimeters == null || delimeters_n <= 0) { return false; }

	s->str = str;

	/* iterator is empty */
	s->cell.s = null;
	s->cell.len = 0;

	s->counter = 0;
	s->delimeters = delimeters;
	s->delimeters_n = delimeters_n;

	s->str_i = 0;
	return true;
}
bool_t split_cell(split_t* s) {
	ZoneScoped;
	if(s->str_i >= s->str.len) {
		/* iterator is empty */
		s->cell.s = null;
		s->cell.len = 0;
		return false;
	}
	++s->counter;

	/* look ahead for a match. */
	const char* sub = &s->str.s[s->str_i];
	const uint32_t sub_n = (s->str.len - s->str_i);

	uint32_t smallest_data_n = sub_n;
	uint32_t largest_delim_n = 0;

	for(uint32_t dlm_i = 0; dlm_i < s->delimeters_n; ++dlm_i) {
		for(uint32_t data_n = 0; data_n < sub_n; ++data_n) {
			bool_t match = true;

			const char* delimeter = s->delimeters[dlm_i].s;

			// todo: use the 'n' value in str_t instead of '\0' ?
			uint32_t delim_n = 0;
			for(; delim_n < sub_n; ++delim_n) {
				const char a = sub[data_n + delim_n];
				const char b = delimeter[delim_n];

				if(b == '\0') { break; }
				if(a != b) { match = false; break; }
			}

			/* priority: smallest data match; largest delimeter; else first in list */
			if(match && (data_n < smallest_data_n || delim_n > largest_delim_n)) { 
				smallest_data_n = data_n;
				largest_delim_n = delim_n;
			}

			if(match) { break; }
		}
	}

	/* we found a delimeter ahead, or hit the end */
	s->cell.s = &s->str.s[s->str_i];
	s->cell.len = smallest_data_n;
	s->str_i += smallest_data_n + largest_delim_n/* skip the delimeter */;

	return true;
}
#endif

#if 0
int p_compare(const void* in_a, const void* in_b) {
	const path_t* a = (const path_t*)in_a;
	const path_t* b = (const path_t*)in_b;
	return strncmp(a->s, b->s, path_n);
}
#endif

hash64_t hash64(hash64_t h, const void* data_, const size_t data_n) {
	ZoneScoped;
	h = (h != hash64_null ? h : hash64_max);

	const uint8_t* data     = cast(uint8_t*)data_;
	const uint8_t* data_end = data + data_n;

	if(data) {
		while(data < data_end) {
			/* xor the bottom with the current octet */
			h ^= (hash64_t)*data++;

			/* multiply by the 64 bit FNV magic prime mod 2^64 */
			h += 
				(h << 1) + (h << 4) + (h << 5) +
				(h << 7) + (h << 8) + (h << 40)
			;
		}
	}

	return (h != hash64_null ? h : hash64_max);
}

/* https://web.archive.org/web/20181022022915/https://dogankurt.com/wildcard.html */
bool_t PatternInString_PeriodStar(const char* pat, const char* str) {
	ZoneScoped;
	const char* locp = null;
	const char* locs = null;

	while(*str) {
		/* we encounter a star */
		if(*pat == '*') {
			locp = ++pat;
			locs = str;
			if(*pat == '\0') {
				return 1;
			}
			continue;
		}

		/* we have a mismatch */
		if(*str != *pat && *pat != '?') {
			if(!locp) {
				return 0;
			}
			str = ++locs;
			pat = locp;
			continue;
		}

		pat++;
		str++;
	}

	/* check if the pattern's ended */
	while(*pat == '*') {
		pat++;
	}

	return (*pat == '\0');
}

#if 0
bool match_wild(const char* str, const char* pat) {
	const char* last = null;
	const char* star = null;

	while(*str) {
		switch(*pat) {
			case '?': {
				++str;
				++pat;
				continue;
			} break;

			case '*': {
				do {
					++pat;
				} while(*pat == '*');

				if(*pat == '\0') {
					return true;
				}

				star = pat;
				goto NextMatch;
			} break;
		}

		if(*str == *pat) {
			++str;
			++pat;
			continue;
		}

		if(star == null) {
			return 0;
		}
		pat = star;
		str = last + 1;

	NextMatch:;
		while(*str != *pat && *pat != '?') {
			if(*++str == '\0') {
				return 0;
			}
		}
		last = str;
		++str;
		++pat;
	}

	while(*pat == '*') {
		pat++;
	}
	return (*pat == '\0');
}
#endif

#if 0
// todo: replace this stuff with eastdc::stopwatch or eastl::chrono
// todo: see https://github.com/HowardHinnant/date
using namespace std::chrono;

static system_clock::time_point _system_base;
static steady_clock::time_point _steady_base;
system_clock::time_point steady_to_system(const steady_clock::time_point t) {
	return _system_base + duration_cast<system_clock::duration>(t - _steady_base);
}

uint64_t _time_s_frequency = (uint64_t)-1;
uint64_t _time_s_offset    = (uint64_t)-1;

// todo: this is not portable. should probably replace this api using chrono.
#if c_os(windows)
uint64_t _time_s_value() {
	ZoneScoped;
	uint64_t value;
	if(QueryPerformanceCounter((LARGE_INTEGER*)&value)) { return value; }
	return (uint64_t)timeGetTime();
}
void time_s_init() {
	ZoneScoped;
	_system_base = system_clock::now();
	_steady_base = steady_clock::now();

	if(!QueryPerformanceFrequency((LARGE_INTEGER*)&_time_s_frequency)) { _time_s_frequency = 1000; }
	_time_s_offset = _time_s_value();
}
#else
bool _time_s_monotonic = false;
uint64_t _time_s_value() {
	#ifdef CLOCK_MONOTONIC
	if(_time_s_monotonic) {
		struct timespec ts;
		clock_gettime(CLOCK_MONOTONIC, &ts);
		return (uint64_t)ts.tv_sec*(uint64_t)1000000000 + (uint64_t)ts.tv_nsec;
	}
	#endif

	struct timeval tv;
	gettimeofday(&tv, NULL);
	return (uint64_t)tv.tv_sec*(uint64_t)1000000 + (uint64_t)tv.tv_usec;
}
void time_s_init() {
    _system_base = system_clock::now();
    _steady_base = steady_clock::now();

	#ifdef CLOCK_MONOTONIC
	struct timespec ts;
	if(clock_gettime(CLOCK_MONOTONIC, &ts) == 0) {
		_time_s_monotonic = true;
		_time_s_frequency = 1000000000;
		_time_s_offset = _time_s_value();
		return;
	}
	#endif

	_time_s_monotonic = false;
	_time_s_frequency = 1000000;
	_time_s_offset = _time_s_value();
}
#endif

double time_s() { return (double)(_time_s_value() - _time_s_offset)/(double)_time_s_frequency; }
float time_sf() { return (float)(_time_s_value() - _time_s_offset)/(float)_time_s_frequency; }

void tp_format_hhmmss(const time_point<steady_clock>& tp, char* out_str, const size_t out_str_n) {
	ZoneScoped;
	const  time_t t  = system_clock::to_time_t(steady_to_system(tp));
	struct tm*    tl = localtime(&t);
	strftime(out_str, out_str_n, "%H:%M:%S", tl);
}

void tp_format_yyyymmdd_hhmmss(const time_point<steady_clock>& tp, char* out_str, const size_t out_str_n) {
	ZoneScoped;
	const  time_t t  = system_clock::to_time_t(steady_to_system(tp));
	struct tm*    tl = localtime(&t);
	strftime(out_str, out_str_n, "%Y-%m-%d %I:%M:%S %p", tl);
}
#endif

// see ImFileLoadToMemory

/* Note: MACRO_* Requires MSVC's /Zc:preprocessor (Standard Conforming Preprocessor) */
Ensure_TrueAtCompileTime(1 == MACRO_HeadArg(1, 2, 3));
Ensure_TrueAtCompileTime(2 == MACRO_HeadArg MACRO_TailArgList(1, 2, 3));
Ensure_TrueAtCompileTime(3 == MACRO_HeadArg MACRO_TailArgList MACRO_TailArgList(1, 2, 3));

Ensure_TrueAtCompileTime(1 == MACRO_Arg_1(1, 2, 3));
Ensure_TrueAtCompileTime(2 == MACRO_Arg_2(1, 2, 3));
Ensure_TrueAtCompileTime(3 == MACRO_Arg_3(1, 2, 3));

Ensure_TrueAtCompileTime(1 == MACRO_Arg_1 MACRO_ArgsToList(1, 2, 3));
Ensure_TrueAtCompileTime(2 == MACRO_Arg_2 MACRO_ArgsToList(1, 2, 3));
Ensure_TrueAtCompileTime(3 == MACRO_Arg_3 MACRO_ArgsToList(1, 2, 3));

Ensure_TrueAtCompileTime(0 == MACRO_ContainsComma());
Ensure_TrueAtCompileTime(0 == MACRO_ContainsComma(()));
Ensure_TrueAtCompileTime(0 == MACRO_ContainsComma(No));
Ensure_TrueAtCompileTime(0 == MACRO_ContainsComma(/* No */));
Ensure_TrueAtCompileTime(1 == MACRO_ContainsComma(,));
Ensure_TrueAtCompileTime(1 == MACRO_ContainsComma(,,));

Ensure_TrueAtCompileTime(1 == MACRO_CommaCountPlusOne());
Ensure_TrueAtCompileTime(2 == MACRO_CommaCountPlusOne(,));
Ensure_TrueAtCompileTime(3 == MACRO_CommaCountPlusOne(,,3));
Ensure_TrueAtCompileTime(1 == MACRO_CommaCountPlusOne(1));
Ensure_TrueAtCompileTime(2 == MACRO_CommaCountPlusOne(1,2));

Ensure_TrueAtCompileTime(0 == MACRO_IsParenthesized());
Ensure_TrueAtCompileTime(0 == MACRO_IsParenthesized(No()));
Ensure_TrueAtCompileTime(0 == MACRO_IsParenthesized(MACRO_Empty));
Ensure_TrueAtCompileTime(0 == MACRO_IsParenthesized(No));
Ensure_TrueAtCompileTime(0 == MACRO_IsParenthesized(/* No */));
Ensure_TrueAtCompileTime(0 == MACRO_IsParenthesized(1, 2, 3));
Ensure_TrueAtCompileTime(0 == MACRO_IsParenthesized(1, (2, 3)));
Ensure_TrueAtCompileTime(1 == MACRO_IsParenthesized((1, 2), 3));
Ensure_TrueAtCompileTime(1 == MACRO_IsParenthesized(()));
Ensure_TrueAtCompileTime(1 == MACRO_IsParenthesized((1, 2, 3)));
Ensure_TrueAtCompileTime(1 == MACRO_IsParenthesized((MACRO_Empty)));

Ensure_TrueAtCompileTime(0 == MACRO_IsEmpty(No));
Ensure_TrueAtCompileTime(0 == MACRO_IsEmpty("No"));
Ensure_TrueAtCompileTime(0 == MACRO_IsEmpty(()));
Ensure_TrueAtCompileTime(0 == MACRO_IsEmpty((,2,3,4,)));
Ensure_TrueAtCompileTime(1 == MACRO_IsEmpty());
Ensure_TrueAtCompileTime(1 == MACRO_IsEmpty(MACRO_Empty));
Ensure_TrueAtCompileTime(1 == MACRO_IsEmpty(        ));
Ensure_TrueAtCompileTime(1 == MACRO_IsEmpty(/* Yes */));

Ensure_TrueAtCompileTime(0 == MACRO_IsArgCountZero(No));
Ensure_TrueAtCompileTime(0 == MACRO_IsArgCountZero("No"));
Ensure_TrueAtCompileTime(0 == MACRO_IsArgCountZero(()));
Ensure_TrueAtCompileTime(0 == MACRO_IsArgCountZero(1,2,3,4,5));
Ensure_TrueAtCompileTime(0 == MACRO_IsArgCountZero(((1,2,3,4,5))));
Ensure_TrueAtCompileTime(0 == MACRO_IsArgCountZero(,2,3,4,5));
Ensure_TrueAtCompileTime(0 == MACRO_IsArgCountZero(,));
Ensure_TrueAtCompileTime(0 == MACRO_IsArgCountZero(,,,,));
Ensure_TrueAtCompileTime(0 == MACRO_IsArgCountZero(MACRO_Empty,MACRO_Empty));
Ensure_TrueAtCompileTime(1 == MACRO_IsArgCountZero());
Ensure_TrueAtCompileTime(1 == MACRO_IsArgCountZero(MACRO_Empty));
Ensure_TrueAtCompileTime(1 == MACRO_IsArgCountZero(        ));
Ensure_TrueAtCompileTime(1 == MACRO_IsArgCountZero(/* Yes */));

#define _MACRO_List0 ()
#define _MACRO_List123 (1, 2, 3)

Ensure_TrueAtCompileTime(0 == MACRO_ArgCount());
Ensure_TrueAtCompileTime(0 == MACRO_ArgCount(MACRO_Empty));
Ensure_TrueAtCompileTime(0 == MACRO_ArgCount _MACRO_List0);
Ensure_TrueAtCompileTime(3 == MACRO_ArgCount _MACRO_List123);
Ensure_TrueAtCompileTime(1 == MACRO_ArgCount(1));
Ensure_TrueAtCompileTime(1 == MACRO_ArgCount((1)));
Ensure_TrueAtCompileTime(2 == MACRO_ArgCount(1, 2));
Ensure_TrueAtCompileTime(2 == MACRO_ArgCount((1), (2, 3)));
Ensure_TrueAtCompileTime(1 == MACRO_ArgCount( ((),2,3,4,5) ));
Ensure_TrueAtCompileTime(2 == MACRO_ArgCount( ((),2,3,4,5), 2 ));
Ensure_TrueAtCompileTime(5 == MACRO_ArgCount( (),2,3,4,5 ));
Ensure_TrueAtCompileTime(5 == MACRO_ArgCount(,2,3,4,5));

Ensure_TrueAtCompileTime(MACRO_Join_0() true);
Ensure_TrueAtCompileTime(123 == MACRO_Join_1(123));
Ensure_TrueAtCompileTime(123 == MACRO_Join_2(1,23));
Ensure_TrueAtCompileTime(123 == MACRO_Join_3(1,2,3));

Ensure_TrueAtCompileTime(MACRO_Join() true);
Ensure_TrueAtCompileTime(123456789 == MACRO_Join(123456789));
Ensure_TrueAtCompileTime(123456789 == MACRO_Join(1,23456789));
Ensure_TrueAtCompileTime(123456789 == MACRO_Join(1,2,3456789));
Ensure_TrueAtCompileTime(123456789 == MACRO_Join(1,2,3,456789));
Ensure_TrueAtCompileTime(123456789 == MACRO_Join(1,2,3,4,56789));
Ensure_TrueAtCompileTime(123456789 == MACRO_Join(1,2,3,4,5,6789));
Ensure_TrueAtCompileTime(123456789 == MACRO_Join(1,2,3,4,5,6,789));
Ensure_TrueAtCompileTime(123456789 == MACRO_Join(1,2,3,4,5,6,7,89));
Ensure_TrueAtCompileTime(123456789 == MACRO_Join(1,2,3,4,5,6,7,8,9));
Ensure_TrueAtCompileTime(123456789 == MACRO_Join(,,,,,123456789,,,,));

#define _MACRO_Identity_Each(_Args_, Item, ...) , Item
#define MACRO_Identity(...) MACRO_Map(_MACRO_Identity_Each, (), __VA_ARGS__)
Ensure_TrueAtCompileTime( (false MACRO_Identity(false, false, true)) );
