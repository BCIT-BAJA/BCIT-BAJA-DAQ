//

#include "pch.h"

#include "DeviceService.h"
#include "Pausable.h"

using namespace std;

// Scan serial ports by reading the registry key:
// HKEY_LOCAL_MACHINE\HARDWARE\DEVICEMAP\SERIALCOMM
vector<string> scanSerialPorts() {
	vector<string> ports;
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
	vector<char> valueName(maxValueNameLen + 1);
	vector<BYTE> valueData(maxValueLen + 1);

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
			string port(reinterpret_cast<char*>(valueData.data()), dataLen);
			// trim trailing NULs/newline
			if (!port.empty() && port.back() == '\0') {
				port.resize(strlen(port.c_str()));
			}
			ports.push_back(port);
		}
	}

	RegCloseKey(hKey);
	return ports;
}

// Open and configure a serial port (non-overlapped). Returns INVALID_HANDLE_VALUE on failure.
// Typical configuration: 115200 8N1; adjust as needed.
HANDLE openSerialPort(const string& portName, DWORD baudRate)
{
	// Windows device path for COM ports >= COM10 requires the \\.\ prefix.
	string devicePath = "\\\\.\\" + portName;
	HANDLE h = CreateFileA(devicePath.c_str(),
		GENERIC_READ | GENERIC_WRITE,
		0,              // exclusive access
		NULL,
		OPEN_EXISTING,
		FILE_ATTRIBUTE_NORMAL,
		NULL);
	if (h == INVALID_HANDLE_VALUE) {
		cerr << "CreateFile failed for " << devicePath << " (error " << GetLastError() << ")\n";
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
		cerr << "SetCommTimeouts failed (error " << GetLastError() << ")\n";
		CloseHandle(h);
		return INVALID_HANDLE_VALUE;
	}

	// Configure DCB (baud, parity, data bits, stop bits)
	DCB dcb = {};
	dcb.DCBlength = sizeof(DCB);
	if (!GetCommState(h, &dcb)) {
		// Try to build a DCB string if GetCommState fails
		string dcbSpec = "baud=" + to_string(baudRate) + " parity=N data=8 stop=1";
		if (!BuildCommDCBA(dcbSpec.c_str(), &dcb)) {
			cerr << "BuildCommDCB failed (error " << GetLastError() << ")\n";
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
		cerr << "SetCommState failed (error " << GetLastError() << ")\n";
		CloseHandle(h);
		return INVALID_HANDLE_VALUE;
	}

	// Clear buffers
	if (!PurgeComm(h, PURGE_RXCLEAR | PURGE_TXCLEAR | PURGE_RXABORT | PURGE_TXABORT)) {
		cerr << "PurgeComm failed (error " << GetLastError() << ")\n";
		// Not fatal; continue
	}

	return h;
}

#if 0 // overlapped i/o
#include <windows.h>
#include <stdio.h>

	int main() {
		HANDLE hComm;
		OVERLAPPED osReader = { 0 };
		char buffer[128];
		DWORD bytesRead;
		BOOL fWaitingOnRead = FALSE;

		// 1. Open the port with the Overlapped flag
		hComm = CreateFile("\\\\.\\COM3",
			GENERIC_READ | GENERIC_WRITE,
			0,
			NULL,
			OPEN_EXISTING,
			FILE_FLAG_OVERLAPPED,
			NULL);

		if (hComm == INVALID_HANDLE_VALUE) {
			printf("Error opening port\n");
			return 1;
		}

		// 2. Create the event for the overlapped structure
		osReader.hEvent = CreateEvent(NULL, TRUE, FALSE, NULL);
		if (osReader.hEvent == NULL) {
			// Handle error
			return 1;
		}

		// 3. Start an overlapped Read
		if (!ReadFile(hComm, buffer, sizeof(buffer), &bytesRead, &osReader)) {
			if (GetLastError() != ERROR_IO_PENDING) {
				// A real error occurred
				printf("Read failed immediately\n");
			}
			else {
				// The read is happening in the background
				fWaitingOnRead = TRUE;
				printf("Read pending...\n");
			}
		}
		else {
			// Read completed immediately (data was already in the buffer)
			printf("Read %d bytes immediately\n", bytesRead);
		}

		// 4. Do other work or wait for completion
		if (fWaitingOnRead) {
			// Wait for 5 seconds for data to arrive
			DWORD dwRes = WaitForSingleObject(osReader.hEvent, 5000);

			switch (dwRes) {
			case WAIT_OBJECT_0:
				// Read completed! Get the result
				if (GetOverlappedResult(hComm, &osReader, &bytesRead, FALSE)) {
					printf("Delayed read finished: %d bytes\n", bytesRead);
				}
				fWaitingOnRead = FALSE;
				break;

			case WAIT_TIMEOUT:
				printf("Timeout: No data arrived.\n");
				break;

			default:
				// Error in WaitForSingleObject
				break;
			}
		}

		CloseHandle(osReader.hEvent);
		CloseHandle(hComm);
		return 0;
	}
#endif

Enum(DeviceService_State, uint32_t) {
	DeviceService_State_Nul = 0,

	// unfortunately, most of these are blocking calls, so no incoming messages are processed.
	// it requires deliberate design to make sure the queue is processed in between, or that states finish
	// loop execution quickly.

	// basically, the state machine is a simple ascending / descending chain.
	// #1 Scan | Gather COM port signatures
	//           Attempt to open each COM port signature
	//           If one succeeds, move to State #2
	//           Else Next COM Port
	//           Else Finished COM Ports, Log, Wait for 1 second, and rescan
	// #2 Open | Open COM port. If it fails, pop back into #1 Scan

	Scan,
};

static pausable State_Root(pausable* pt, DeviceService* device) {
	Logger l = device->log;

	auto ports = scanSerialPorts();
	if (!ports.empty()) {
		HANDLE h = openSerialPort(ports[0], CBR_115200);
		if (h != INVALID_HANDLE_VALUE) {
			CloseHandle(h);
		}
	}

	static DeviceService_State state = DeviceService_State_Nul;
	static uint32_t com_i;

	if(*pt == pausable_closed) {
		*pt = pausable_init;
	}
	pause_open(pt);
	{
		// scan for com ports here

		// for each com port, attempt to open it.

		// if it is opened, recurse here

		// inside the opened com port, read data

		// if read data succeeds, try to make it intelligible for 10 seconds.
		// if it is not intelligible for 10 seconds, skip
		// else if it is intelligible, interpret the data, and publish it to the excel queue.

		for(com_i = 0; com_i < 10; ++com_i) {
			Log(l, "COM PORT %u\n", com_i);
			if(com_i + 1 < 10) {
				pause_here;
			}
		}
	}
	pause_closer;
}

intptr_t Thread_DeviceService(void* _) {
	Basic_SetThreadName("DeviceService");

#if c_config(debug)
	puts(MACRO_FunctionSignature());
	defer(puts(MACRO_FunctionSignature()));
#endif

	DeviceService* self = cast(DeviceService*)_;
	Logger l = self->log;

	Y_QueueMM<DeviceService_MsgIn>::Consumer qi_consumer = self->qi.Consumer_Rent();
	defer(self->qi.Consumer_Return(&qi_consumer));

	DeviceService_MsgIn mi;
	TracyCZoneEnd(init);

	pausable statemachine = pausable_init;
	while(true) {
		// importantly, the AwaitSignalUntil depends on our current state.

		Log(l, "EVENT QUEUE\n");
		Y_Rx_e mi_pull = Y_Rx_e::Empty;
		self->qi_produce_event.AwaitSignalUntil(10*1000, // 10 ms
			[self, &qi_consumer, &mi, &mi_pull]() {
			return (Y_Rx_e::Empty != (mi_pull = qi_consumer.Pull_Rx(&mi)));
		});

		if(mi_pull == Y_Rx_e::Empty) {
			// event queue timed out, run the state machine.
			State_Root(&statemachine, self);
		} else if(mi_pull == Y_Rx_e::Contention) {
			Y_Thread_Yield();
		} else if(mi_pull == Y_Rx_e::Success) {
			switch(mi.type) {
				case DeviceService_MsgIn_e::End: {
					/* note: race: other incoming messages are lost */
					goto end;
				} break;

				#if 0
				case DeviceService_MsgIn_e::String: {
					ZoneScopedN("String");

					auto& str = mi.as.String.object;
					defer(DestructAt_NullSafe(&str));

					if(!str.empty()) {
						// todo: *instead* of dumping this right away to stdout, instead add a timer to awaitsignaluntil, and
						//       dump periodically every half second, either by size limit, or time!
						accum += str;
					}
				} break;
				#endif

				#if 0
				vector<uint16_t> data; // roughly 2khz
				for(uint32_t dat_i = 0; dat_i < 200; ++dat_i) {
					data.push_back(cast(uint16_t)dat_i);
				}

				ExcelService_PublishData(&excel, data);
				#endif

				default: Assure_True(false, "unknown message type %d", mi.type);
			}
		}
	}
	end:;

	return 0;
}
