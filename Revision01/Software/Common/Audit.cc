//

#pragma once

#include "PCH.h"
#include "Audit.h"

struct AssertGUID {
	union {
		struct {
			uint16_t condition_strlen;
			uint16_t file_strlen;
			uint16_t file_line;
			uint16_t _unused;
		};
		uint64_t hash = 0;
	};
};

bool AssertGUID_IsEqual(AssertGUID a, AssertGUID b) {
	return a.hash == b.hash; 
}

bool _Assert_OnConditionFalse(_AssertMetadata_DeclareArguments, const char* format, ...) {
	static std::mutex g_assert_mutex;
	static AssertGUID g_ignores[128];
	static int g_ignores_n = 0;

	std::lock_guard<std::mutex> lock(g_assert_mutex);

	bool ignored = false;

	AssertGUID this_assert_id = { 0 };
	this_assert_id.condition_strlen = (int)strlen(metadata_condition_str);
	this_assert_id.file_strlen = (int)strlen(metadata_file_str);
	this_assert_id.file_line = metadata_file_line;
	for(int i = 0; i < g_ignores_n; ++i) {
		if(AssertGUID_IsEqual(this_assert_id, g_ignores[i])) {
			ignored = true;
			break;
		}
	}

	if(!ignored) {
		static char message[2048] = { 0 };
		int message_n = 0;

		{
			va_list args;
			va_start(args, format);
			message_n += vsnprintf(
				message + message_n,
				Array_CountOf(message) - message_n,
				format,
				args
			);
			va_end(args);
		}

	#if c_os(windows)
		uint32_t os_error_code = GetLastError();
		char os_error_code_str[MAX_PATH] = { 0 };
		FormatMessageA(
			FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
			NULL,
			os_error_code,
			MAKELANGID(
				LANG_NEUTRAL,
				SUBLANG_DEFAULT
			),
			os_error_code_str,
			static_cast<DWORD>(sizeof(os_error_code_str) - 1),
			NULL
		);
	#endif

		message_n += snprintf(
			message + message_n,
			Array_CountOf(message) - message_n,
			"\n\ncondition failed: '%s'\n"
		#if c_config(debug)
			"\nfile: %s\n"
		#endif
			"\nline: %d\n"
		#if c_os(windows)
			"\nGetLastError: %d %s\n"
		#endif
			, metadata_condition_str
		#if c_config(debug)
			, metadata_file_str
		#endif
			, metadata_file_line
		#if c_os(windows)
			, os_error_code
			, os_error_code_str
		#endif
		);

		fprintf(stderr, "%s", message);
		fflush(stderr);

		int32_t do_ignore = false;
		int32_t do_quit = false;

#if c_os(windows)
		message_n += snprintf(
			message + message_n,
			Array_CountOf(message) - message_n,
			"\n\npress CTRL+C to copy this error to your clipboard."
		);

		OS_TriggerDebugBreak();
		const int mb_id = MessageBoxA(
			NULL,
			message,
			"Assert Failed",
			MB_ABORTRETRYIGNORE | MB_ICONERROR
		);

		do_quit = mb_id == IDCANCEL || mb_id == IDABORT;
		do_ignore = mb_id == IDIGNORE;
#elif c_os(mac)
		assert_osx(message, &do_quit, &do_ignore);
#elif c_os(ios)
		const SDL_MessageBoxButtonData buttons[] = {
			{ 0, 0, "Abort" },
			{ 0, 1, "Retry" },
			{ 0, 2, "Ignore" },
		};

		const SDL_MessageBoxData messageboxdata = {
			SDL_MESSAGEBOX_INFORMATION, /* .metadata */
			NULL,                       /* .window */
			"Assert Failed",         /* .title */
			message,                    /* .message */
			SDL_arraysize(buttons),     /* .numbuttons */
			buttons,                    /* .buttons */
			NULL                        /* .colorScheme */
		};

		int buttonid = -1;
		int ret = SDL_ShowMessageBox(&messageboxdata, &buttonid);
		do_quit = (buttonid == 0);
		do_ignore = (buttonid == 2);

		// assert_ios(message, &do_quit, &do_ignore, &winsys.win);
#else
		fprintf(stderr, "enter 'i' to ignore, 'q' to quit\n");

		char str[64];
		scanf("%63s", str);
		do_quit = str[0] == 'q';
		do_ignore = str[0] == 'i';
#endif

		if(do_quit) {
			OS_TriggerDebugBreak();
			abort();
		}

		if(do_ignore && g_ignores_n + 1 < Array_CountOf(g_ignores)) {
			g_ignores[g_ignores_n++] = this_assert_id;
		}
	}

	return false;
}

ThreadLocal AuditError t_audit_error;
ThreadLocal Auditor t_auditor;

bool _Audit_OnConditionFalse(_Auditor_Metadata_DeclareArguments, const char* format, ...) {
	Auditor* _ = &t_auditor;

	if(!Test_True(0 < _->depth_nth && _->depth_nth <= Array_CountOf(_->scope_at_depth))) {
		return false;
	}

	AuditScope* scope;
	{
		uint32_t host_depth_i = --(_->depth_nth);
		Assert_True(host_depth_i < Array_CountOf(_->scope_at_depth));

		Assert_True(_->scope_open_mask.test(host_depth_i));
		// do not close this scope, allowing the caller to recover by PopGather() or PopDiscard().
		/* _->scope_open_mask.reset(close_depth_i); */

		scope = &_->scope_at_depth[host_depth_i];
	}

	// scope->metadata; // filled in already?
	scope->metadata.file_line = metadata_file_line;
	scope->metadata.condition_str = metadata_condition_str;
#if c_config(debug)
	scope->metadata.file_str = metadata_file_str;
	scope->metadata.function_signature_str = metadata_function_signature_str;
#endif
	scope->audit_error = Audit_GetLastError();
	scope->os_error = OS_GetLastError();
	scope->format_error_str.resize(0);

	if(format[0]) {
		va_list args;
		va_start(args, format);
		Defer(va_end(args));

		if(scope->format_error_str.empty()) {
			scope->format_error_str.resize(128);
		}

		/*
		// vsnprintf return code:
		// The number of characters that would have been written 
		// if n had been sufficiently large, not counting the terminating null character.
		// If an encoding error occurs, a negative number is returned.
		// Notice that only when this returned value is non-negative
		// and less than n, the string has been completely written.
		*/
		int vsnprintf_ret = vsnprintf(
			scope->format_error_str.data(),
			scope->format_error_str.size(),
			format,
			args
		);
		if(!(0 <= vsnprintf_ret && vsnprintf_ret < cast(int)scope->format_error_str.size())) {
			// the number of characters that would have been written if n had been sufficiently large.
			Assert_True(0 <= vsnprintf_ret);
			scope->format_error_str.resize(vsnprintf_ret + 1);

			vsnprintf_ret = vsnprintf(
				scope->format_error_str.data(),
				scope->format_error_str.size(),
				format,
				args
			);

			Assert_True(0 <= vsnprintf_ret && vsnprintf_ret < cast(int)scope->format_error_str.size());
		}
	}

	return false;
}

// todo: a full pop is sometimes completely unessessary, 
//       especially before we know os_last_error_code, etc.
// todo: also, we can consider having audit_last_error_code, as in
//       an equivalent user_error_code stored in TLS.

// Peek() returns error codes from scope[1] ?
AuditPeek Audit_PeekChild() {
	Auditor* _ = &t_auditor;

	AuditPeek peek;

	uint32_t child_depth_i = _->depth_nth; 
	if(!Test_True(child_depth_i < Array_CountOf(_->scope_at_depth))) {
		// Uh oh, the Audit system is really broken.
		// hopefully we're lucky that the last OS call was related.
		peek.audit_error = Audit_GetLastError();
		peek.os_error = OS_GetLastError();
		return peek;
	}

	Test_True(_->scope_open_mask.test(child_depth_i), "You can't Peek into a closed Audit!");

	AuditScope* child_scope = &_->scope_at_depth[child_depth_i];
	peek.audit_error = child_scope->audit_error;
	peek.os_error = child_scope->os_error;
	return peek;
}

// Pop() performs a deep copy of the AuditStack.
void Audit_Pop(AuditStack* out) {
	Auditor* _ = &t_auditor;

	// Pop!
	uint32_t host_depth_i = (_->depth_nth);
	Assert_True(host_depth_i < Array_CountOf(_->scope_at_depth));
	Assert_True(_->scope_open_mask.test(host_depth_i), "No active Audit to Pop!");

	if(out) {
		out->auditor_depth_i = host_depth_i;

		uint32_t out_scope_i = 0;
		for(uint32_t d_i = host_depth_i; d_i < Array_CountOf(_->scope_at_depth);
			(++d_i, ++out_scope_i)
		) {
			if(!_->scope_open_mask.test(d_i)) {
				break;
			}

			AuditScope* out_scope = &out->scopes[out_scope_i];
			(*out_scope) = _->scope_at_depth[d_i];
		}

		out->scopes_n = out_scope_i;
	}

	// here we "close" out scope by resetting scope_open_mask.
	for(uint32_t d_i = host_depth_i; d_i < Array_CountOf(_->scope_at_depth); ++d_i) {
		if(!_->scope_open_mask.test(d_i)) {
			break;
		}
		_->scope_open_mask.reset(d_i);
	}
}

void Audit_Push(const AuditStack* in) {
	if(!Test_True(in)) {
		return;
	}

	Auditor* _ = &t_auditor;
	// (sanity check the stack_to_push)
	Assert_True(
		in->auditor_depth_i +
		in->scopes_n
		<=
		Array_CountOf(_->scope_at_depth)
	);

	// Push!
	const uint32_t host_scope_i = (_->depth_nth);
	Assert_True(host_scope_i < Array_CountOf(_->scope_at_depth));
	// The host scope must match the incoming scope.
	Assert_True(host_scope_i == in->auditor_depth_i);
	// Our current host scope must be closed.
	Assert_True(!_->scope_open_mask.test(host_scope_i)
		, "You must Pop the active Audit! (Since you cannot overwrite an active Audit)"
	);

	uint32_t d_i = host_scope_i;
	for(
		uint32_t in_scope_i = 0;
		in_scope_i < in->scopes_n;
		(++d_i, ++in_scope_i)
	) {
		Assert_True(!_->scope_open_mask.test(d_i));
		// here we "re-open" our host scope.
		_->scope_open_mask.set(d_i);

		AuditScope* scope = &_->scope_at_depth[d_i];
		(*scope) = in->scopes[in_scope_i];
	}
}


#if 0
/* see zmq_abort */
void rc_abort(const char* errmsg_) {
#if c_os(windows)
	//  Raise STATUS_FATAL_APP_EXIT.
	ULONG_PTR extra_info[1];
	extra_info[0] = (ULONG_PTR)errmsg_;
	RaiseException(0x40000015, EXCEPTION_NONCONTINUABLE, 1, extra_info);
#else
	unused(errmsg_);
	// print_backtrace();
	abort();
#endif
}
#endif

#if 0
const char* errno_to_string(int errno_) {
	switch (errno_) {
#if c_os(windows)
	case ENOTSUP:          return "Not supported";
	case EPROTONOSUPPORT:  return "Protocol not supported";
	case ENOBUFS:          return "No buffer space available";
	case ENETDOWN:         return "Network is down";
	case EADDRINUSE:       return "Address in use";
	case EADDRNOTAVAIL:    return "Address not available";
	case ECONNREFUSED:     return "Connection refused";
	case EINPROGRESS:      return "Operation in progress";
#endif
	case EHOSTUNREACH:     return "Host unreachable";
	}
	return strerror(errno_);
}
#endif

#if c_os(windows)
#if 0
// todo: a string analysis will reveal zmq source
const char* wsa_error_no(int no_, const char* wsae_wouldblock_string_) {
	//  TODO:  It seems that list of Windows socket errors is longer than this.
	//         Investigate whether there's a way to convert it into the string
	//         automatically (wsaError->HRESULT->string?).
	switch (no_) {
	case WSABASEERR:          return "No Error";
	case WSAEINTR:            return "Interrupted system call";
	case WSAEBADF:            return "Bad file number";
	case WSAEACCES:           return "Permission denied";
	case WSAEFAULT:           return "Bad address";
	case WSAEINVAL:           return "Invalid argument";
	case WSAEMFILE:           return "Too many open files";
	case WSAEWOULDBLOCK:      return wsae_wouldblock_string_;
	case WSAEINPROGRESS:      return "Operation now in progress";
	case WSAEALREADY:         return "Operation already in progress";
	case WSAENOTSOCK:         return "Socket operation on non-socket";
	case WSAEDESTADDRREQ:     return "Destination address required";
	case WSAEMSGSIZE:         return "Message too long";
	case WSAEPROTOTYPE:       return "Protocol wrong type for socket";
	case WSAENOPROTOOPT:      return "Bas protocol option";
	case WSAEPROTONOSUPPORT:  return "Protocol not supported";
	case WSAESOCKTNOSUPPORT:  return "Socket type not supported";
	case WSAEOPNOTSUPP:       return "Operation not supported on socket";
	case WSAEPFNOSUPPORT:     return "Protocol family not supported";
	case WSAEAFNOSUPPORT:     return "Address family not supported by protocol family";
	case WSAEADDRINUSE:       return "Address already in use";
	case WSAEADDRNOTAVAIL:    return "Can't assign requested address";
	case WSAENETDOWN:         return "Network is down";
	case WSAENETUNREACH:      return "Network is unreachable";
	case WSAENETRESET:        return "Net dropped connection or reset";
	case WSAECONNABORTED:     return "Software caused connection abort";
	case WSAECONNRESET:       return "Connection reset by peer";
	case WSAENOBUFS:          return "No buffer space available";
	case WSAEISCONN:          return "Socket is already connected";
	case WSAENOTCONN:         return "Socket is not connected";
	case WSAESHUTDOWN:        return "Can't send after socket shutdown";
	case WSAETOOMANYREFS:     return "Too many references can't splice";
	case WSAETIMEDOUT:        return "Connection timed out";
	case WSAECONNREFUSED:     return "Connection refused";
	case WSAELOOP:            return "Too many levels of symbolic links";
	case WSAENAMETOOLONG:     return "File name too long";
	case WSAEHOSTDOWN:        return "Host is down";
	case WSAEHOSTUNREACH:     return "No Route to Host";
	case WSAENOTEMPTY:        return "Directory not empty";
	case WSAEPROCLIM:         return "Too many processes";
	case WSAEUSERS:           return "Too many users";
	case WSAEDQUOT:           return "Disc Quota Exceeded";
	case WSAESTALE:           return "Stale NFS file handle";
	case WSAEREMOTE:          return "Too many levels of remote in path";
	case WSASYSNOTREADY:      return "Network SubSystem is unavailable";
	case WSAVERNOTSUPPORTED:  return "WINSOCK DLL Version out of range";
	case WSANOTINITIALISED:   return "Successful WSASTARTUP not yet performed";
	case WSAHOST_NOT_FOUND:   return "Host not found";
	case WSATRY_AGAIN:        return "Non-Authoritative Host not found";
	case WSANO_RECOVERY:      return "Non-Recoverable errors: FORMERR REFUSED NOTIMP";
	case WSANO_DATA:          return "Valid name no data record of requested";
	}

	return "?";
}
#endif

#if 0
const char* wsa_error() {
	return wsa_error_no(WSAGetLastError(), null);
}
#endif

#if 0
int wsa_error_to_errno(const int errcode_) {
	switch (errcode_) {
		//  10004 - Interrupted system call.
	case WSAEINTR:           return EINTR;
		//  10009 - File handle is not valid.
	case WSAEBADF:           return EBADF;
		//  10013 - Permission denied.
	case WSAEACCES:          return EACCES;
		//  10014 - Bad address.
	case WSAEFAULT:          return EFAULT;
		//  10022 - Invalid argument.
	case WSAEINVAL:          return EINVAL;
		//  10024 - Too many open files.
	case WSAEMFILE:          return EMFILE;
		//  10035 - Operation would block.
	case WSAEWOULDBLOCK:     return EBUSY;
		//  10036 - Operation now in progress.
	case WSAEINPROGRESS:     return EAGAIN;
		//  10037 - Operation already in progress.
	case WSAEALREADY:        return EAGAIN;
		//  10038 - Socket operation on non-socket.
	case WSAENOTSOCK:        return ENOTSOCK;
		//  10039 - Destination address required.
	case WSAEDESTADDRREQ:    return EFAULT;
		//  10040 - Message too long.
	case WSAEMSGSIZE:        return EMSGSIZE;
		//  10041 - Protocol wrong type for socket.
	case WSAEPROTOTYPE:      return EFAULT;
		//  10042 - Bad protocol option.
	case WSAENOPROTOOPT:     return EINVAL;
		//  10043 - Protocol not supported.
	case WSAEPROTONOSUPPORT: return EPROTONOSUPPORT;
		//  10044 - Socket type not supported.
	case WSAESOCKTNOSUPPORT: return EFAULT;
		//  10045 - Operation not supported on socket.
	case WSAEOPNOTSUPP:      return EFAULT;
		//  10046 - Protocol family not supported.
	case WSAEPFNOSUPPORT:    return EPROTONOSUPPORT;
		//  10047 - Address family not supported by protocol family.
	case WSAEAFNOSUPPORT:    return EAFNOSUPPORT;
		//  10048 - Address already in use.
	case WSAEADDRINUSE:      return EADDRINUSE;
		//  10049 - Cannot assign requested address.
	case WSAEADDRNOTAVAIL:   return EADDRNOTAVAIL;
		//  10050 - Network is down.
	case WSAENETDOWN:        return ENETDOWN;
		//  10051 - Network is unreachable.
	case WSAENETUNREACH:     return ENETUNREACH;
		//  10052 - Network dropped connection on reset.
	case WSAENETRESET:       return ENETRESET;
		//  10053 - Software caused connection abort.
	case WSAECONNABORTED:    return ECONNABORTED;
		//  10054 - Connection reset by peer.
	case WSAECONNRESET:      return ECONNRESET;
		//  10055 - No buffer space available.
	case WSAENOBUFS:         return ENOBUFS;
		//  10056 - Socket is already connected.
	case WSAEISCONN:         return EFAULT;
		//  10057 - Socket is not connected.
	case WSAENOTCONN:        return ENOTCONN;
		//  10058 - Can't send after socket shutdown.
	case WSAESHUTDOWN:       return EFAULT;
		//  10059 - Too many references can't splice.
	case WSAETOOMANYREFS:    return EFAULT;
		//  10060 - Connection timed out.
	case WSAETIMEDOUT:       return ETIMEDOUT;
		//  10061 - Connection refused.
	case WSAECONNREFUSED:    return ECONNREFUSED;
		//  10062 - Too many levels of symbolic links.
	case WSAELOOP:           return EFAULT;
		//  10063 - File name too long.
	case WSAENAMETOOLONG:    return EFAULT;
		//  10064 - Host is down.
	case WSAEHOSTDOWN:       return EAGAIN;
		//  10065 - No route to host.
	case WSAEHOSTUNREACH:    return EHOSTUNREACH;
		//  10066 - Directory not empty.
	case WSAENOTEMPTY:       return EFAULT;
		//  10067 - Too many processes.
	case WSAEPROCLIM:        return EFAULT;
		//  10068 - Too many users.
	case WSAEUSERS:          return EFAULT;
		//  10069 - Disc Quota Exceeded.
	case WSAEDQUOT:          return EFAULT;
		//  10070 - Stale NFS file handle.
	case WSAESTALE:          return EFAULT;
		//  10071 - Too many levels of remote in path.
	case WSAEREMOTE:         return EFAULT;
		//  10091 - Network SubSystem is unavailable.
	case WSASYSNOTREADY:     return EFAULT;
		//  10092 - WINSOCK DLL Version out of range.
	case WSAVERNOTSUPPORTED: return EFAULT;
		//  10093 - Successful WSASTARTUP not yet performed.
	case WSANOTINITIALISED:  return EFAULT;
		//  11001 - Host not found.
	case WSAHOST_NOT_FOUND:  return EFAULT;
		//  11002 - Non-Authoritative Host not found.
	case WSATRY_AGAIN:       return EFAULT;
		//  11003 - Non-Recoverable errors: FORMERR REFUSED NOTIMP.
	case WSANO_RECOVERY:     return EFAULT;
		//  11004 - Valid name no data record of requested.
	case WSANO_DATA:         return EFAULT;
	}

	wsa_assert(false);
	return 0;
}
#endif
#endif


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

// todo: for ease of typing, use that FOREACH macro trick to expand metadata_or(trace_tag_, graphics, network, peers) -> trace_tag_graphics | trace_tag_network ...
// todo: log flush zones, ie for(defer()) trick to log_scope(l) { l(); l(); l(); }

enum_t(uint32_t, trace_tag_t) { // "category"
	trace_tag_null = 0,
	trace_tag_graphics,
	trace_tag_network,
	trace_tag_peers,
	trace_tag_teradek,
};
enum_t(uint32_t, trace_metadata_e) {
	trace_metadata_flush = (1 << 30),
	trace_metadata_notify = (1 << 31) | trace_metadata_flush,

	trace_metadata_info    = (1 << 31),
	trace_metadata_debug   = (1 << 6),
	trace_metadata_perf    = (1 << 5),
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
	static log_filter_metadata_e filter_metadata = log_filter_null;
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
		if(InputTextWithHint("##commands", "Terminal Command", AArg(cmd), ImGuiInputTextFlags_EnterReturnsTrue, null, null)) {
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
			bool filter_metadata_changed = false;
			asshert_static(sizeof(filter_metadata) == sizeof(uint32_t));
			filter_metadata_changed |= CheckboxFlagsT<uint32_t>("Fault",   &filter_metadata, log_filter_fault);   SameLine();
			filter_metadata_changed |= CheckboxFlagsT<uint32_t>("Error",   &filter_metadata, log_filter_error);   SameLine();
			filter_metadata_changed |= CheckboxFlagsT<uint32_t>("Warn",    &filter_metadata, log_filter_warn);    SameLine();
			filter_metadata_changed |= CheckboxFlagsT<uint32_t>("Info",    &filter_metadata, log_filter_info);    SameLine();
			filter_metadata_changed |= CheckboxFlagsT<uint32_t>("Verbose", &filter_metadata, log_filter_verbose); SameLine();
			filter_metadata_changed |= CheckboxFlagsT<uint32_t>("Debug",   &filter_metadata, log_filter_debug);
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
	if(!Audit(l->entries_n < countof(l->entries))) {
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
	L_stringlen(_L, AArg(__FILE__ ":") - 1); \
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


