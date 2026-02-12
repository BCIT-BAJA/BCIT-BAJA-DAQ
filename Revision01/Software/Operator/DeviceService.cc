//

#include "pch.h"
#include "DeviceService.h"

// Scan serial ports by reading the registry key:
// HKEY_LOCAL_MACHINE\HARDWARE\DEVICEMAP\SERIALCOMM
std::vector<std::string> scanSerialPorts() {
	std::vector<std::string> ports;
	HKEY hKey = NULL;
	constexpr char kRegPath[] = "HARDWARE\\DEVICEMAP\\SERIALCOMM";

	if (RegOpenKeyExA(HKEY_LOCAL_MACHINE, kRegPath, 0, KEY_READ, &hKey) != ERROR_SUCCESS) {
		// Key not found or no permission; return empty list
		return ports;
	}

	DWORD valueCount = 0;
	DWORD maxValueNameLen = 0;
	DWORD maxValueLen = 0;
	if (RegQueryInfoKeyA(hKey, NULL, NULL, NULL, NULL, NULL, NULL, &valueCount, &maxValueNameLen, &maxValueLen, NULL, NULL) != ERROR_SUCCESS) {
		RegCloseKey(hKey);
		return ports;
	}

	// Buffers must be large enough for the largest value name/data + terminating NUL
	std::vector<char> valueName(maxValueNameLen + 1);
	std::vector<BYTE> valueData(maxValueLen + 1);

	for (DWORD i = 0; i < valueCount; ++i) {
		DWORD nameLen = (DWORD)valueName.size();
		DWORD dataLen = (DWORD)valueData.size();
		DWORD type = 0;

		LONG ret = RegEnumValueA(hKey, i,
			valueName.data(), &nameLen,
			NULL, &type,
			valueData.data(), &dataLen);
		if (ret != ERROR_SUCCESS) continue;

		if (type == REG_SZ) {
			// data is a null-terminated ANSI string like "COM3"
			std::string port(reinterpret_cast<char*>(valueData.data()), dataLen);
			// trim trailing NULs/newline
			if (!port.empty() && port.back() == '\0') {
				port.resize(std::strlen(port.c_str()));
			}
			ports.push_back(port);
		}
	}

	RegCloseKey(hKey);
	return ports;
}

// Open and configure a serial port (non-overlapped). Returns INVALID_HANDLE_VALUE on failure.
// Typical configuration: 115200 8N1; adjust as needed.
HANDLE openSerialPort(const std::string& portName, DWORD baudRate)
{
	// Windows device path for COM ports >= COM10 requires the \\.\ prefix.
	std::string devicePath = "\\\\.\\" + portName;
	HANDLE h = CreateFileA(devicePath.c_str(),
		GENERIC_READ | GENERIC_WRITE,
		0,              // exclusive access
		NULL,
		OPEN_EXISTING,
		FILE_ATTRIBUTE_NORMAL,
		NULL);
	if (h == INVALID_HANDLE_VALUE) {
		std::cerr << "CreateFile failed for " << devicePath << " (error " << GetLastError() << ")\n";
		return INVALID_HANDLE_VALUE;
	}

	// Configure timeouts
	COMMTIMEOUTS timeouts = {};
	timeouts.ReadIntervalTimeout = 50;
	timeouts.ReadTotalTimeoutConstant = 50;
	timeouts.ReadTotalTimeoutMultiplier = 10;
	timeouts.WriteTotalTimeoutConstant = 50;
	timeouts.WriteTotalTimeoutMultiplier = 10;
	if (!SetCommTimeouts(h, &timeouts)) {
		std::cerr << "SetCommTimeouts failed (error " << GetLastError() << ")\n";
		CloseHandle(h);
		return INVALID_HANDLE_VALUE;
	}

	// Configure DCB (baud, parity, data bits, stop bits)
	DCB dcb = {};
	dcb.DCBlength = sizeof(DCB);
	if (!GetCommState(h, &dcb)) {
		// Try to build a DCB string if GetCommState fails
		std::string dcbSpec = "baud=" + std::to_string(baudRate) + " parity=N data=8 stop=1";
		if (!BuildCommDCBA(dcbSpec.c_str(), &dcb)) {
			std::cerr << "BuildCommDCB failed (error " << GetLastError() << ")\n";
			CloseHandle(h);
			return INVALID_HANDLE_VALUE;
		}
	}
	else {
		dcb.BaudRate = baudRate;
		dcb.ByteSize = 8;
		dcb.Parity = NOPARITY;
		dcb.StopBits = ONESTOPBIT;
	}

	if (!SetCommState(h, &dcb)) {
		std::cerr << "SetCommState failed (error " << GetLastError() << ")\n";
		CloseHandle(h);
		return INVALID_HANDLE_VALUE;
	}

	// Clear buffers
	if (!PurgeComm(h, PURGE_RXCLEAR | PURGE_TXCLEAR | PURGE_RXABORT | PURGE_TXABORT)) {
		std::cerr << "PurgeComm failed (error " << GetLastError() << ")\n";
		// Not fatal; continue
	}

	return h;
}

#if 0
	auto ports = scanSerialPorts();
	if (ports.empty()) {
		std::cout << "No serial ports found via registry.\n";
	}
	else {
		std::cout << "Found serial ports:\n";
		for (const auto& p : ports) std::cout << "  " << p << '\n';
	}

	// Example: open first found port
	if (!ports.empty()) {
		HANDLE h = openSerialPort(ports[0], CBR_115200);
		if (h != INVALID_HANDLE_VALUE) {
			std::cout << "Opened " << ports[0] << " successfully.\n";

			// Example write (send a simple string). Use WriteFile/ReadFile or overlapped IO as needed.
			const char* msg = "Hello device\r\n";
			DWORD written = 0;
			if (!WriteFile(h, msg, (DWORD)strlen(msg), &written, NULL)) {
				std::cerr << "WriteFile failed (error " << GetLastError() << ")\n";
			}
			else {
				std::cout << "Wrote " << written << " bytes.\n";
			}

			CloseHandle(h);
		}
		else {
			std::cerr << "Failed to open " << ports[0] << '\n';
		}
	}
#endif

#if 0
intptr_t Thread_DeviceService(void* _) {
	TracyCZoneN(init, "Init", true);

	#if 0
	rpmalloc_thread_initialize();
	defer(rpmalloc_thread_finalize(false));
	#endif

	DeviceService* self = cast(DeviceService*)_;
	#if 0
	rc_SetThreadName("SerialPort");
	#endif
	
	Y_QueueMM<SerialPort_MsgIn>::Consumer qi_consumer = self->qi.Consumer_Rent();
	defer(self->qi.Consumer_Return(&qi_consumer));

	SerialPort_MsgIn mi;

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
				case SerialPort_MsgIn_e::End: {
					/* note: race: other incoming messages are lost */
					goto end;
				} break;

				case SerialPort_MsgIn_e::String: {
					ZoneScopedN("String");

					auto& str = mi.as.String.object;
					defer(DestructAt_NullSafe(&str));

					if(!str.empty()) {
						// todo: *instead* of dumping this right away to stdout, instead add a timer to awaitsignaluntil, and
						//       dump periodically every half second, either by size limit, or time!
						accum += str;
					}
				} break;

				default: tru(false, "unknown message type %d", mi.type);
			}
		}
	}
	end:;

#if c_config(debug)
	puts(MACRO_FunctionSignature());
#endif

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
	if(txt->type != SerialPort_MsgIn_e::String) {
		txt->Construct_String();
		txt->as.String.object.reserve(512);
	}
	auto& m_str = txt->as.String.object;
	_append_sprintf_va_list(m_str, fmt, va);
}

void Txt_Append(Txt* txt, const char* str) {
	Task_ZoneScoped_NoCallstack;
	if(txt->type != SerialPort_MsgIn_e::String) {
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

void SerialPort_Txt(SerialPortger l, Txt* txt) {
	Task_ZoneScoped_NoCallstack;
	if(!l) {
		return;
	}

	// todo: make this thread local !!!!!!
	Y_QueueMM<SerialPort_MsgIn>::Producer qi_producer = l->qi.Producer_Rent();
	defer(l->qi.Producer_Return(&qi_producer));

	while(qi_producer.Push_Tx(txt) != Y_Tx_e::Success) { Y_Thread_Yield(); } // note: todo: will lock up if full.
	l->qi_produce_event.Signal_One();

	// todo: flush to multiple sinks here.
	// todo: reset the messages here? they were "moved" to a new memory location...
}

void SerialPort(SerialPortger l, const char* fmt, ...) c_fmt(2) {
	Task_ZoneScoped_NoCallstack;
	if(!l) {
		return;
	}

	Txt msg;
	va_scope(va, fmt) {
		Txt_Fmt_(&msg, fmt, va);
	}
	SerialPort_Txt(l, &msg);
}

#endif
