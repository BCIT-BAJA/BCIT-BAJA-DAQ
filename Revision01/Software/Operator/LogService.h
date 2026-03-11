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

	Inline void Construct_String() {
		Assert_True(type == Log_MsgIn_e::nul);
		type = Log_MsgIn_e::String;
		new (&as.String.object) std::string;
	}
};

struct LogService {
	std::thread thread;
	Y_EventMM qi_produce_event;
	Y_QueueMM<Log_MsgIn> qi;
};

typedef LogService* Logger;
typedef Log_MsgIn Txt;

intptr_t Thread_LogService(void* _);

void Log_BindThreadLocal(Logger l);
Inline bool LogService_Create(LogService* l, const uint32_t drops_n = 64) {
	ZoneScoped;
	if(!/* this goes away */Test_True(l->qi.Create(512))) {
		return false;
	}
	Log_BindThreadLocal(l);
	return true;
}
Inline void LogService_Destroy(LogService* l) {
	ZoneScoped;
	l->qi.Destroy();
}
Inline void LogService_Begin(LogService* l) {
	ZoneScoped;
	l->thread = std::thread(Thread_LogService, l);
}
Inline void LogService_SignalEnd(LogService* l) {
	ZoneScoped;

	Log_MsgIn si;
	si.type = Log_MsgIn_e::End;
	l->qi.ProduceOne_OrYieldAndRetryForever(&si);
	l->qi_produce_event.Signal_One();
}
Inline void LogService_WaitForEnd(LogService* l) {
	ZoneScoped;
	l->thread.join();
}

void Txt_Fmt_(Txt* txt, const char* fmt, va_list va);
void Txt_Append(Txt* txt, const char* str);
void Txt_AppendFormat(Txt* txt, const char* fmt, ...);
void Log_Txt(Logger l, Txt* txt);

/*
// Log <<< Printf ~1.3ms
// :)
*/
void Log(Logger l, const char* fmt, ...);
void Log(const char* fmt, ...);

// todo: it would be nice to add [thread name] [warn/fatal/etc] [timestamp] [function] metadata information, as well as Log_Line(), etc.
//       then you could Push / Pop "scopes" of logging settings per thread.