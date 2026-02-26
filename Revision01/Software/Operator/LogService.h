//

// idea: there are sinks:
//       one which immediately puts to stdout
//       one which loop puts to imgui
//       one which saves to disk (?)

#pragma once

#include "PCH.h"
#include "Compile.h"

#include "Y_QueueMM.h"
#include "Y_EventMM.h"

enum class Log_MsgIn_e : uint8_t {
	nul = 0,
	End,
	String,
};

struct Log_MsgIn {
	Log_MsgIn_e type = Log_MsgIn_e::nul;

	union u {
		struct {
			std::string object;
		} String;

		 u() { /* nop */ }
		~u() { /* nop */ }
	} as;

#if 0
	inline const Log_MsgIn& operator = (const Log_MsgIn& a) {
		Memcpy(this, &a, sizeof(*this));
		return *this;
	}
#endif

	inl void Construct_String() {
		Assure(type == Log_MsgIn_e::nul);
		type = Log_MsgIn_e::String;
		new (&as.String.object) std::string;
	}
};

struct LogService {
	std::thread thread;
	Y_EventMM qi_produce_event;
	Y_QueueMM<Log_MsgIn> qi;
};


intptr_t Thread_LogService(void* _);

inl bool LogService_Create(LogService* l, const uint32_t drops_n = 64) {
	ZoneScoped;
	if(!/* this goes away */Assure_True(l->qi.Create(512))) {
		return false;
	}
	return true;
}
inl void LogService_Destroy(LogService* l) {
	ZoneScoped;
	l->qi.Destroy();
}
inl void LogService_Begin(LogService* l) {
	ZoneScoped;
	l->thread = std::thread(Thread_LogService, l);
}
inl void LogService_SignalEnd(LogService* l) {
	ZoneScoped;

	Log_MsgIn si;
	si.type = Log_MsgIn_e::End;

	Y_QueueMM<Log_MsgIn>::Producer qi_producer = l->qi.Producer_Rent();
	defer(l->qi.Producer_Return(&qi_producer));
	while(qi_producer.Push_Tx(&si) != Y_Tx_e::Success) { Y_Thread_Yield(); } // note: todo: will lock up if full.
	l->qi_produce_event.Signal_One();
}
inl void LogService_WaitForEnd(LogService* l) {
	ZoneScoped;
	l->thread.join();
}

typedef LogService* Logger;
typedef Log_MsgIn Txt;

void Txt_Fmt_(Txt* txt, const char* fmt, va_list va);
void Txt_Append(Txt* txt, const char* str);
void Txt_AppendFormat(Txt* txt, const char* fmt, ...);
void Log_Txt(Logger l, Txt* txt);

/*
// Log <<< Printf ~1.3ms
// :)
*/
void Log(Logger l, const char* fmt, ...);

#if 0
// take inspiration from https://github.com/rs/zerolog
//                       https://github.com/briandowns/liblogger
// the idea is to use a "stream operator" && instead of C++ style <<
// L() && Lcolor_push() && L("hello, %s!", "world") && Lcolor_pop() && ...
// L() L_red(L("hello, %s!", "world")) && L() && ...
// L_Entry(INFO, L() && L() && L_red(L("hello, %s!", "world")) && L() && ...)
// (L_Entry can check for orphaned nodes!)
//
// L_EntryScope(INFO) { // for(defer()) trick
// }
//
// it's much easier to add the "TraceEntry" to the API because it's actually structured data!
// thus the same API can immediately dump to Console (only for debug builds?), *and* return an ignorable message to the caller.
// In the same vein, an Assert / Halt property can be used to specify opening a GUI window for the user.
//
// this way, *any* data can be appended to one log Entry -- thus one Entry is decoupled from one physical Line.
// ie, Lkv_int("myInt", myint), L_var(#v, v) ... !
//
// This log data can be serialized to the Colored Console, a plain .txt file, a json .txt file, or Colored ImGui!
// Without the concept of an "Entry", it becomes extremely tedious to deal with constant "flushing" paradigm,
// and it's really annoying to view or collapse, or sort that data in a structured view!
//

// todo: colors: this probably means a stream interface, like io_, ie, io_color() && io_string() && io_color() which essentially marks 
//               section of the string to be wrapped with colors particular to that. ie, for terminals, ANSI, for other terminals,
//               no color bytes at all..!
// todo: registering functions, watch variables

// todo: note that some extremely serious faults should be sent to master PCs on the network.

// todo: for ease of typing, use that FOREACH macro trick to expand flags_or(trace_tag_, graphics, network, peers) -> trace_tag_graphics | trace_tag_network ...
// todo: log flush zones, ie for(defer()) trick to log_scope(l) { l(); l(); l(); }

enum_t(uint32_t, trace_tag_t) { // "category"
	trace_tag_null = 0,
	trace_tag_graphics,
	trace_tag_network,
	trace_tag_peers,
	trace_tag_teradek,
};
enum_t(uint32_t, trace_flags_e) {
	trace_flags_flush = (1 << 30),
	trace_flags_notify = (1 << 31) | trace_flags_flush,

	trace_flags_info    = (1 << 31),
	trace_flags_debug   = (1 << 6),
	trace_flags_perf    = (1 << 5),
};
enum_t(uint32_t, trace_level_e) {
	trace_flag_null = 0,

	// <--- direct log() messages, which do not pass through trace.

	trace_flag_fault   = (1 << 0), 
	trace_flag_error   = (1 << 1), 
	trace_flag_warn    = (1 << 2), 
	trace_flag_verbose = (1 << 4),

	trace_flag_fault,
	trace_flag_error,
	trace_flag_warn,
	trace_flag_info,
	trace_flag_verbose,
	trace_flag_perf,
	trace_flag_debug,

	trace_flag_count
};
inl trace_level_e trace_level_constrain(const trace_level_e l) {
	if(l <= trace_level_fault) { return trace_level_fault; }
	if(l >= trace_level_debug) { return trace_level_debug; }
	return l;
}
const char* stringof_trace_level(const trace_level_e l);
v4 colorof_trace_level(const trace_level_e l);

inl void _trace_(log_t* l, const char* fmt, va_list va) c_fmt_va(2) {
	if(!l) { return; }

	// append metadata onto the fmt string
	log_msg_t msg;
	log_(&msg, fmt, va);
	// log_flush(t->l, &msg);
}
inl void _trace(log_t* l, const char* fmt, ...) c_fmt(2) {
	// append metadata onto the fmt string
	va_scope(va, fmt) {
		// _trace_();
	}
}
#define trace(l) _trace(l, __FILE__, __LINE__, __c_func__)
#endif

// todo: it would be nice to combine asshertions with traces, too.
//       essentially what trace_t needs is a recursive type of approach using mem_t!!!!!.
//
// (YYYYMMDD.txt) ( log sink )
//
// [HH:MM:SS.123] Some high level description!
// func_a():100: Entity 3
//  func_b():200: SubEntity 4
//   func_c() source.cc:300 Original error!
//
// the idea is that loopTerm acts as a second sink for trace events. 
// loopTerm can format these events, and provide a really good basis for interoperation with
// Tracy!!!!
//
// some trace event levels FORCE a FLUSH to ensure that no packets are lost.
// some trace events like PERF do not force a flush for perofrmance reasons, since they trigger pretty
// much every frame.
//
//
#if 0
struct trace_t {
	/* errors, can be null, but the fail code is always checked */
	strbuild_t* fail_str = null;
	fail_t fail = 0;
};

// flush fail to log_t!
void log_trace(trace_level_e level, log_t* l, trace_t* t);

#if 0
inl void _trace_info_(const int file_line, const char* fmt, va_list va) c_fmt_va(3) {
}
inl void _trace_info(const int file_line, const char* fmt, ...) c_fmt(3) {
	if(!t || !t->l) { return; }

	va_scope(va, fmt) {
		_trace_info_(t, file_line, fmt, va);
	}
}
#endif

inl void _trace_fail_(const int file_line, const char* fmt, va_list va) c_fmt_va(3) {
	if(!t || !t->fail_str) { return; }

	t->fail_str->put_(fmt, va);
}
inl void _trace_fail(const int file_line, const char* fmt, ...) c_fmt(3) {
	if(!t || !t->fail_str) { return; }

	va_scope(va, fmt) {
		_trace_fail_(t, file_line, fmt, va);
	}
}

#define trace_fail(t, ...) _trace_fail(t, __LINE__, "" __VA_ARGS__)
#define return_trace_fail(fail_u64, t, ...) \
	CompilerAssert(fail_u64); \
	t->ret.fail = fail_u64; \
	trace_fail(t, __VA_ARGS__); \
	return t->ret

struct loopLog_t {
	log_t* l = null;
};

// NIH: credits go to see septag/rizz

// *ui term goals*
// . search, filter, sort: faults, errors, warnings, info, verbose, debugging text signals, their originating file:lines.
// . submit commands with completion, timecode, syntax highlighting
// . autoscroll (unless scrolled already), and auto wrap
// . decent first size

// open up Tracy!!!!
// or example,
// you may want to enable or disable the capture of frame images without recompiling and restarting your pro-
// gram. To be able to do so you must register a callback function using the TracyParameterRegister(callback,
// 	data) macro,
// Debugging variables!
inline void ui_term() {
	using namespace ImGui;

	char cmd[512] = { 0 };

	bool focus_trigger = false; 
	bool unfocus_trigger = false; 

	static bool filters_visible = false;
	static log_filter_flags_e filter_flags = log_filter_null;
	static int selected_i = 0;

	if(Begin("Term")) {
		PushItemWidth(-50.0f);
		SetItemDefaultFocus();
	#if 0 // focus signal edge
		if(focus_trigger) {
			focus_trigger = false;

			SetKeyboardFocusHere(0);
		}
	#endif
		if(InputTextWithHint("##commands", "Terminal Command", aarg(cmd), ImGuiInputTextFlags_EnterReturnsTrue, null, null)) {
			/* execute this command */
			Printf("%s\n", cmd);
		}
		PopItemWidth();

		SameLine(0.0f, 8.0f);
		if(Button("Filters", v2vi(-1.0f, GetFrameHeight()))) {
			filters_visible = !filters_visible;
		}

		if(unfocus_trigger) {
			unfocus_trigger = false;

			SetKeyboardFocusHere(-1);
		}

		if(filters_visible) {
			bool filter_flags_changed = false;
			asshert_static(sizeof(filter_flags) == sizeof(uint32_t));
			filter_flags_changed |= CheckboxFlagsT<uint32_t>("Fault",   &filter_flags, log_filter_fault);   SameLine();
			filter_flags_changed |= CheckboxFlagsT<uint32_t>("Error",   &filter_flags, log_filter_error);   SameLine();
			filter_flags_changed |= CheckboxFlagsT<uint32_t>("Warn",    &filter_flags, log_filter_warn);    SameLine();
			filter_flags_changed |= CheckboxFlagsT<uint32_t>("Info",    &filter_flags, log_filter_info);    SameLine();
			filter_flags_changed |= CheckboxFlagsT<uint32_t>("Verbose", &filter_flags, log_filter_verbose); SameLine();
			filter_flags_changed |= CheckboxFlagsT<uint32_t>("Debug",   &filter_flags, log_filter_debug);
		}

		const float width = GetWindowContentRegionMax().x - GetWindowContentRegionMin().x;
		const float description_width = width - 170.0f;

		struct log_entry_t {
			log_level_e level;
			const char* description;
			const char* file;
			const uint32_t file_line;
		};
		log_entry_t entries[] = {
			{ log_level_fault, "hello, world", __FILE__, cast(uint32_t)__LINE__ },
			{ log_level_error, "jello, girl!", __FILE__, cast(uint32_t)__LINE__ },
			{ log_level_warn, "marsh mellow", __FILE__, cast(uint32_t)__LINE__ },
			{ log_level_info, "few fellows", __FILE__, cast(uint32_t)__LINE__ },
			{ log_level_verbose, "smart cello", __FILE__, cast(uint32_t)__LINE__ },
			{ log_level_debug, "crap yellow", __FILE__, cast(uint32_t)__LINE__ },
		};

		if(BeginTable("LogTable", 3, 0
			| ImGuiTableFlags_Resizable
			| ImGuiTableFlags_RowBg
			| ImGuiTableFlags_ScrollY
			| ImGuiTableFlags_BordersOuterH
			| ImGuiTableFlags_SizingStretchProp
			, v20i
			, 0
		)) {
			TableSetupColumn("Description");
			TableSetupColumn("File");
			TableSetupColumn("Line");
			TableHeadersRow();

			ImGuiListClipper clipper;
			clipper.Begin(countof(entries), -1.0f); 
			while(clipper.Step()) {
				for(int clp_i = clipper.DisplayStart; clp_i < clipper.DisplayEnd; ++clp_i) {
					TableNextRow();

					const log_entry_t* entry = &entries[clp_i];

					const log_level_e entry_level = entry->level;
					const char*    entry_description = entry->description;
					const char*    entry_file        = entry->file;
					const uint32_t entry_file_line   = entry->file_line;

					const char* entry_level_string = stringof_log_level(entry_level);
					const v4    entry_level_color  = colorof_log_level(entry_level);

					TableNextColumn();
					PushStyleColor(ImGuiCol_Text, entry_level_color);

					if(Selectable(entry_description, selected_i == clp_i, 0
						| ImGuiSelectableFlags_SpanAllColumns
						| ImGuiSelectableFlags_AllowDoubleClick
						, v20i
					)) {
						selected_i = clp_i;

						const size_t cliptext_cap = 0
							+ Strlen(entry_file)
							+ 32
							+ Strlen(entry_description)
						;
						char* cliptext = cast(char*)alloca(cliptext_cap);
						asshert(cliptext);

						Snprintf(cliptext, cliptext_cap
							, "(%s:%d) %s"
							, entry_file
							, entry_file_line
							, entry_description
						);
						SetClipboardText(cliptext);
					}

					PopStyleColor();

					TableNextColumn();
					Text(entry_file);

					TableNextColumn();
					Text("%u", entry_file_line);
				}
			}

		#if 0 // ?
			if(GetMaxY() <= GetScrollY()) {
				SetScrollHereY(1.0f);
			}
		#endif

			clipper.End();
			EndTable();
		}
	}
	End();
}

inl bool test(trace_t* t) {
	time_t rawtime = { 0 };
	time(&rawtime);
	struct tm* timeinfo = localtime(&rawtime);
	char yyyymmdd_hhmmss[64] = { 0 };
	StdC::Strftime(yyyymmdd_hhmmss, countof(yyyymmdd_hhmmss), "%Y%m%d-%H%M%S", timeinfo);

	trace_info(t, "[info] [%s] [%s:%d] %s", yyyymmdd_hhmmss, __FILE__, __LINE__, "hello, world!");

	if(rand()%3 == 0) {
		trace_fail(t, "[info] [%s] [%s:%d] %s", yyyymmdd_hhmmss, __FILE__, __LINE__, "oh, crap!");
		return false;
	}

	return true;
}

inl bool loopLog_stage_ui( loopLog_t* log) {
	if(loop->first()) {
		if(!test(t)) {
			asshert(t->fail);
		}
		// add sink to l
	} else if(loop->last()) {
		;
		// remove sink from l
	}

	// gather, sort all sunk messages from (l->) ?
	ui_term();

	return true;
}

// TERM_WATCH_VARIABLE
// TERM_COMMAND_VARIABLE
// TERM_COMMAND_FUNCTION

#if 0 
/**
* Placeholder that installs module via constructor for every macro
* REGISTER_MODULE/REGISTER_MODULE_HIDDEN call
* @param name     non-quoted module name
* @param lclass   class of the module
* @param abi      abi version (specific for every class)
* @param funcname unique function name that will be used to register
*                 the module (as a constructor)
* @param hidden   0/1 - whether the module should be visible by eg. '-c help'
*                 (for technical and deprecated modules), default true
*/
#define REGISTER_MODULE_FUNCNAME(name, info, lclass, abi, funcname, hidden) static void funcname(void)  __attribute__((constructor));\
\
static void funcname(void)\
{\
        register_library(#name, info, lclass, abi, hidden);\
}\
struct NOT_DEFINED_STRUCT_THAT_SWALLOWS_SEMICOLON

/**
* @brief  Registers module to global modules' registry
* @param name   name of the module to be used to load the module (Note that
*               it has to be without quotation marks!)
* @param info   pointer to structure with the (class specific) info about module
* @param lclass member of @ref library_class
* @param abi    ABI version of info parameter, usually defined per class
*               in appropriate class header (eg. video_display.h)
* @note
* Mangling of the constructor function name is because some files may define
* multiple modules (eg. audio playback SDI) and without that, function would
* be defined multiple times under the same name.
*/
#define REGISTER_MODULE(name, info, lclass, abi) REGISTER_MODULE_FUNCNAME(name, info, lclass, abi, UNIQUE_LABEL, 0)
#endif
#endif

#if 0

struct L_entry_t {
	mp_t<char> str;
	const char* file = null;
	const char* func = null;
	const char* t_fmt = null;
	struct tm t;
	int file_line = 0;
};

struct L_scope_t {
	m_t m;

	uint32_t entries_n = 0;
	mp_t<L_entry_t> entries[4];
};

#if 0
#define L_scope() \
	bool _once = false; \
	for(defer(); !_once; _once = true) \
		L_scope_t* _L_scope = ; 
#endif

#if 0
inl bool L_stringlen(L_scope_t* l, const char* str, const uint32_t str_n) {
	Memcpy(&l->buffer[l->buffer_i], str, str_n);
	l->buffer_i += str_n;
	return true;
}
inl bool L_string(L_scope_t* l, const char* str) {
	return L_stringlen(l, str, cast(uint32_t)Strlen(str));
}
#endif

inl bool L_entry_start(L_scope_t* l) {
	mp_t<L_entry_t>* mp_e = &l->entries[l->entries_n];
	++l->entries_n;
	if(!Assure_True(l->entries_n < countof(l->entries))) {
		return false;
	}

	l->m.put(mp_e);
	L_entry_t* e = l->m[*mp_e];

	e->file;
	e->func;
	e->t_fmt;
	e->t;
	e->file_line;

	e->str;
}

inl bool L_entry_finish(L_scope_t* l) {
	;
}

inl bool L_format(L_scope_t* l, const char* fmt, ...) {
	va_scope(va, fmt) {
		l->m.put(&l->entries[
		l->entries[l->entries_n];

		l->buffer_i += StdC::Vsnprintf(l->buffer + l->buffer_i, countof(l->buffer), fmt, va);
		l->buffer[l->buffer_i] = '\0';
	}
	return true;
}
#if 0
inl bool L_int(L_scope_t* scope, const int i) {
	return true;
}
#endif

#define L(fmt, ...) L_format(_L, fmt, __VA_ARGS__);

#if 0
#define Ld(var)     L_format(_L, #var "=%d ", var);
#define Lentry(...) \
	L_stringlen(_L, aarg(__FILE__ ":") - 1); \
	L_format(_L, "%d " __c_func__ " | ", __LINE__); \
	__VA_ARGS__
#endif

inline void test() {
	L_scope_t scope;
	L_scope_t* _L = &scope;

	int var = 123;

	L_entry_start(_L, );
	L_entry_finish(_L, );

#if 0
	Lentry(
		L("hello, ")
		L("world! ")
		Ld(var)
	);
#endif

	puts(_L->buffer);
	_L->buffer[0] = '\0';

	for(defer(printf("hello, world!\n")); ;) {
		break;
	}
}

struct _static_struct_t {
	_static_struct_t() { test(); }
};
static _static_struct_t _static_struct;



#endif

