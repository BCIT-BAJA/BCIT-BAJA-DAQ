//

#include "PCH.h"
#include "LogService.h"

#if 0
const char* stringof_trace_flag(const trace_level_e l) {
	switch(l) {
		case trace_flag_fault:   return "fault";
		case trace_flag_error:   return "error";
		case trace_flag_warn:    return "warn";
		case trace_flag_info:    return "info";
		case trace_flag_verbose: return "verbose";
		case trace_flag_debug:   return "debug";
	}
	asshert(false);
	return "";
}
v4 colorof_trace_flag(const trace_flag_e l) {
	switch(l) {
		case trace_flag_fault:   ;
		case trace_flag_error:   return v4vi(1.0f, 0.0f, 0.0f, 1.0f);
		case trace_flag_warn:    return v4vi(1.0f, 1.0f, 0.0f, 1.0f);
		case trace_flag_info:    return v4vi(0.9f, 0.9f, 0.9f, 1.0f);
		case trace_flag_verbose: ;
		case trace_flag_debug:   return v4vi(0.65f, 0.65f, 0.65f, 1.0f);
	}
	asshert(false);
	return v41i;
}
#endif

intptr_t Thread_LogService(void* _) {
#if c_config(debug)
	puts(MACRO_FunctionSignature());
	defer(puts(MACRO_FunctionSignature()));
#endif

	#if 0
	rpmalloc_thread_initialize();
	defer(rpmalloc_thread_finalize(false));
	#endif

	LogService* self = cast(LogService*)_;
	#if 0
	rc_SetThreadName("Log");
	#endif
	
	Y_QueueMM<Log_MsgIn>::Consumer qi_consumer = self->qi.Consumer_Rent();
	defer(self->qi.Consumer_Return(&qi_consumer));

	Log_MsgIn mi;

	std::string accum;

	TracyCZoneEnd(init);
	while(true) {
		Y_Rx_e mi_pull = Y_Rx_e::Empty;
		self->qi_produce_event.AwaitSignalUntil((accum.size() ? 25*1000 : Timeout32_e::Infinite),
			[self, &accum, &qi_consumer, &mi, &mi_pull]() {
			return 4096 <= accum.size() || ((mi_pull = qi_consumer.Pull_Rx(&mi)) != Y_Rx_e::Empty);
		});

		if(mi_pull == Y_Rx_e::Empty) {
			ZoneScopedN("Fputs");
			/* timedout */
			fputs(accum.c_str(), stdout);
			accum.resize(0);
		} else if(mi_pull == Y_Rx_e::Contention) {
			ZoneScopedN("Contention");
			Y_Thread_Yield();
		} else if(mi_pull == Y_Rx_e::Success) {
			switch(mi.type) {
				case Log_MsgIn_e::End: {
					/* note: race: other incoming messages are lost */
					goto end;
				} break;

				case Log_MsgIn_e::String: {
					ZoneScopedN("String");

					auto& str = mi.as.String.object;
					defer(DestructAt_NullSafe(&str));

					if(!str.empty()) {
						// todo: *instead* of dumping this right away to stdout, instead add a timer to awaitsignaluntil, and
						//       dump periodically every half second, either by size limit, or time!
						accum += str;
					}
				} break;

				default: Assure_True(false, "unknown message type %d", mi.type);
			}
		}
	}
	end:;

	return 0;
}

void _append_sprintf_va_list(std::string& str, const char* format, va_list args) {
	// 1. Clone the va_list
	va_list args_copy;
	va_copy(args_copy, args); // Correct: Use copy, NOT start

	// 2. Determine required size
	int len = std::vsnprintf(nullptr, 0, format, args_copy);
	va_end(args_copy); // Clean up the copy

	if (len > 0) {
		size_t old_size = str.size();
		str.resize(old_size + len);

		// 3. Write directly into the string
		// We use the ORIGINAL 'args' here
		std::vsnprintf(&str[old_size], len + 1, format, args);
	}
}

// Helper wrapper for standard variadic calls
void _append_sprintf(std::string& str, const char* format, ...) {
	va_list args;
	va_start(args, format);
	_append_sprintf_va_list(str, format, args);
	va_end(args);
}

void Txt_Fmt_(Txt* txt, const char* fmt, va_list va) c_fmt_va(2) {
	Task_ZoneScoped_NoCallstack;
	if(txt->type != Log_MsgIn_e::String) {
		txt->Construct_String();
		txt->as.String.object.reserve(512);
	}
	auto& m_str = txt->as.String.object;
	_append_sprintf_va_list(m_str, fmt, va);
}

void Txt_Append(Txt* txt, const char* str) {
	Task_ZoneScoped_NoCallstack;
	if(txt->type != Log_MsgIn_e::String) {
		txt->Construct_String();
		txt->as.String.object.reserve(512);
	}
	auto& m_str = txt->as.String.object;
	m_str.append(str);
}

#if 0
void Txt_AppendFormat(Txt* txt, const char* fmt, ...) c_fmt(2) {
	Task_ZoneScoped_NoCallstack;
	va_scope(va, fmt) {
		Txt_Fmt_(txt, fmt, va);
	}
}
#endif

void Log_Txt(Logger l, Txt* txt) {
	Task_ZoneScoped_NoCallstack;
	if(!l) {
		return;
	}

	// todo: make this thread local !!!!!!
	Y_QueueMM<Log_MsgIn>::Producer qi_producer = l->qi.Producer_Rent();
	defer(l->qi.Producer_Return(&qi_producer));

	while(qi_producer.Push_Tx(txt) != Y_Tx_e::Success) { Y_Thread_Yield(); } // note: todo: will lock up if full.
	l->qi_produce_event.Signal_One();

	// todo: flush to multiple sinks here.
	// todo: reset the messages here? they were "moved" to a new memory location...
}

/*
// Log <<< Printf ~1.3ms
// :)
*/
void Log(Logger l, const char* fmt, ...) c_fmt(2) {
	Task_ZoneScoped_NoCallstack;
	if(!Assure_True(l)) {
		return;
	}

	Txt msg;
	va_scope(va, fmt) {
		Txt_Fmt_(&msg, fmt, va);
	}
	Log_Txt(l, &msg);
}

