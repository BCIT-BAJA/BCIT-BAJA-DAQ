//

#pragma once

#include "Compile_PCH.h"

#define _Assure_AtCompileTime3(c, msg) typedef char Assure_AtCompileTime_##msg[(!!(c))*2-1]
#define _Assure_AtCompileTime2(c, line) _Assure_AtCompileTime3(c, at_line_##line)
#define _Assure_AtCompileTime1(c, line) _Assure_AtCompileTime2(c, line)
#define Assure_AtCompileTime(c, ...)    _Assure_AtCompileTime1(c, __LINE__)

bool _assure_true_failed(const char* condition_str, const char* file, const int file_line, const char* format, ...);

// todo: consider stripping the actual #c & __FILE__, but keep __LINE__ and __VA_ARGS__ in release builds !
// todo: check the platform here to minimize codesize bloat.
//       just use full compiler strings on Windows.
#define _ASSURE_TRUE(c, ...) ((uintptr_t)((bool)(c)) || _assure_true_failed(#c, __FILE__, __LINE__, "" __VA_ARGS__))

#if (c_compile(assure_debug) && !defined(__INTELLISENSE__))
#define tru(c, ...)         _ASSURE_TRUE(c, "" __VA_ARGS__)
#define Assure(c, ...) do { _ASSURE_TRUE(c, "" __VA_ARGS__); } while(0)
#else
#define tru(c, ...) (uintptr_t)(!!(c))
#define Assure(c, ...) (void)(c)
#endif
#define assure Assure


#if (defined(assure_implementation) || defined(__INTELLISENSE__))
#include "Basic.h"
#include "Compile.h"

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

#if c_os(windows)
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

#if 0
const char* wsa_error() {
	return wsa_error_no(WSAGetLastError(), null);
}
#endif

void win_error(char* buffer_, size_t buffer_size_) {
	const DWORD errcode = GetLastError();
#if defined _WIN32_WCE
	DWORD rc = FormatMessageW(
		FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS, NULL, errcode,
		MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT), (LPWSTR)buffer_,
		buffer_size_ / sizeof(wchar_t), NULL);
#else
	const DWORD rc = FormatMessageA(
		FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS, NULL, errcode,
		MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT), buffer_,
		static_cast<DWORD> (buffer_size_), NULL);
#endif
	assure(rc);
}

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

static std::mutex g_assure_mutex;

struct assure_id_t {
	int condition_len;
	int file_len;
	int file_line;
};
static assure_id_t _assure_ignore_ids[128];
static int _assure_ignore_ids_n = 0;
bool assure_id_eq(assure_id_t a, assure_id_t b) { return a.condition_len == b.condition_len && a.file_len == b.file_len && a.file_line == b.file_line; }
bool _assure_true_failed(const char* condition_str, const char* file, const int file_line, const char* format, ...) {
	std::lock_guard<std::mutex> lock(g_assure_mutex);

	bool ignored = false;
	assure_id_t this_assure_id = { 0 };
	this_assure_id.condition_len = (int)strlen(condition_str);
	this_assure_id.file_len = (int)strlen(file);
	this_assure_id.file_line = file_line;
	for (int i = 0; i < _assure_ignore_ids_n; ++i) {
		if (assure_id_eq(this_assure_id, _assure_ignore_ids[i])) {
			ignored = true;
			break;
		}
	}

	if (!ignored) {
		static char message[2048] = { 0 };
		int message_n = 0;

		va_list args;
		va_start(args, format);
		message_n += vsnprintf(message + message_n, countof(message) - message_n, format, args);
		va_end(args);

		message_n += snprintf(message + message_n, countof(message) - message_n, "\n\ncondition failed: '%s'\n", condition_str);
#if c_compile(assure_debug)
		message_n += snprintf(message + message_n, countof(message) - message_n, "\nfile: %s\n", file);
#endif
		message_n += snprintf(message + message_n, countof(message) - message_n, "\nline: %d\n", file_line);
		fprintf(stderr, "%s", message);
		fflush(stderr);

		int32_t do_ignore = false;
		int32_t do_quit = false;
#if c_os(windows)
		message_n += snprintf(message + message_n, countof(message) - message_n, "\n\npress CTRL+C to copy this error to your clipboard.");

		os_breakpoint();
		const int mb_id = MessageBoxA(NULL, message, "assertion failed", MB_ABORTRETRYIGNORE | MB_ICONERROR);
		do_quit = mb_id == IDCANCEL || mb_id == IDABORT;
		do_ignore = mb_id == IDIGNORE;
#elif c_os(mac)
		assure_osx(message, &do_quit, &do_ignore);
#elif c_os(ios)
		const SDL_MessageBoxButtonData buttons[] = {
			{ 0, 0, "Abort" },
			{ 0, 1, "Retry" },
			{ 0, 2, "Ignore" },
		};

		const SDL_MessageBoxData messageboxdata = {
			SDL_MESSAGEBOX_INFORMATION, /* .flags */
			NULL,                       /* .window */
			"Assertion Failed",         /* .title */
			message,                    /* .message */
			SDL_arraysize(buttons),     /* .numbuttons */
			buttons,                    /* .buttons */
			NULL                        /* .colorScheme */
		};

		int buttonid = -1;
		int ret = SDL_ShowMessageBox(&messageboxdata, &buttonid);
		do_quit = (buttonid == 0);
		do_ignore = (buttonid == 2);

		// assure_ios(message, &do_quit, &do_ignore, &winsys.win);
#else
		fprintf(stderr, "enter 'i' to ignore, 'q' to quit\n");

		char str[64];
		scanf("%63s", str);
		do_quit = str[0] == 'q';
		do_ignore = str[0] == 'i';
#endif
		if (do_quit) {
			os_breakpoint();
			abort();
		}
		if (do_ignore && _assure_ignore_ids_n + 1 < countof(_assure_ignore_ids)) {
			_assure_ignore_ids[_assure_ignore_ids_n++] = this_assure_id;
		}
	}

	return false;
}
#endif
