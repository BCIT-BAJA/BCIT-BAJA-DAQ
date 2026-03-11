//

#include "pch.h"
#if 0

#include "DeviceService.h"
#include "StateMachine.h"

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

// http://www.flounder.com/serial.htm
// Another problem I see is people trying to do too much at once.  Using a single thread to do both input and output results in code that is far too convoluted.  Such code is difficult to create, debug, or even reason about successfully and should be avoided. 
// Using separate threads for input and output results in cleaner code, with a nice separation of concerns.

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

// 90% of the time, we will be waiting on new data from the COM handle, not waiting 
// on the thread message queue handle.
// we can even add the message queue handle sleep to be conditional on the state machine state.
Enum(DeviceService_State, StateMachine_StateType) {
	DeviceService_State_Default = 0,
	DeviceService_State_COMPortScan,
	DeviceService_State_COMPortScan_ForEach,
	DeviceService_State_ReadNumber,
};

Struct(DeviceService_StateMachineData) {
	vector<string> ports;
	size_t port_i = 0;
	HANDLE com_port_h = INVALID_HANDLE_VALUE;
};

static void HandlePortError() {
	DWORD errorCode = GetLastError();
	LPVOID errorMsg;

	// Translate the numeric error code into a system message string
	FormatMessage(
		FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
		NULL, errorCode, MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
		(LPTSTR)&errorMsg, 0, NULL
	);

	std::cerr << "Error during " << "" << ": (" << errorCode << ") " << (char*)errorMsg << std::endl;

	LocalFree(errorMsg); // Clean up the system buffer
}

static void DeviceService_StateMachine(DeviceService* _, StateMachine* sm) {
	Logger l = _->log;
	DeviceService_StateMachineData* d = cast(DeviceService_StateMachineData*)sm->user_data;

	StateMachine_OpenSwitch(sm);

	StateMachine_DefaultState(DeviceService_State_Default) {
		(*d) = DeviceService_StateMachineData();
		StateMachine_GoTo(sm, DeviceService_State_COMPortScan);
	}

	StateMachine_State(DeviceService_State_COMPortScan) {
		d->ports = scanSerialPorts();
		if(d->ports.empty()) {
			/* do something */
			Log(l, "We couldn't find any USB Devices!\n");
			StateMachine_Yield_ThenRetry(sm);
		}

		d->port_i = 0;
		StateMachine_GoTo(sm, DeviceService_State_COMPortScan_ForEach);
	}

	StateMachine_State(DeviceService_State_COMPortScan_ForEach) {
		Assure(d->com_port_h == INVALID_HANDLE_VALUE);

		vector<string>& ports = d->ports;
		size_t& port_i = d->port_i;

		for(; port_i < ports.size(); ++port_i) {
			HANDLE h = openSerialPort(ports[port_i], CBR_115200);
			if (h == INVALID_HANDLE_VALUE) {
				continue;
			}

			COMMTIMEOUTS timeouts = { 0 };

			// 1. Max time between two bytes (in milliseconds)
			timeouts.ReadIntervalTimeout = 50;

			// 2. Total timeout = (Multiplier * number of bytes requested) + Constant
			timeouts.ReadTotalTimeoutMultiplier = 10;
			timeouts.ReadTotalTimeoutConstant = 50;

			// Apply the settings to your COM port handle
			if(!AssureTrue(SetCommTimeouts(h, &timeouts))) {
				HandlePortError();
			}

			COMMTIMEOUTS verify = { 0 };
			if (GetCommTimeouts(h, &verify)) {
				if (verify.ReadIntervalTimeout != timeouts.ReadIntervalTimeout) {
					std::cerr << "Warning: Driver modified your timeout values!" << std::endl;
					/* continue; */
				}
			}

			d->com_port_h = h;
			StateMachine_GoTo(sm, DeviceService_State_ReadNumber);
		}

		// oh fuck, we went through all the ports and none of them worked. log, wait, and rescan.
		Log(l, "We couldn't connect to any USB Device!\n");
		StateMachine_GoTo(sm, DeviceService_State_COMPortScan);
	}

	StateMachine_State(DeviceService_State_ReadNumber) {
		Assure(d->com_port_h != INVALID_HANDLE_VALUE);

		DWORD bytesRead;
		char buffer[256];

		BOOL success = ReadFile(
			d->com_port_h, // Handle to the COM port
			buffer,         // Buffer to store the data
			sizeof(buffer), // Number of bytes to read
			&bytesRead,     // Stores the number of bytes actually read
			NULL            // Use NULL for synchronous I/O
		);

		if (success) {
			// Process the data in buffer
		}
	}

	StateMachine_CloseSwitch;
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


	DeviceService_StateMachineData sm_data;
	StateMachine sm;
	sm.user_data = &sm_data;

	while(true) {
		bool sm_state_success = true;
		StateMachine_StateType sm_state = StateMachine_PeekState(&sm, &sm_state_success);
		Assure(sm_state_success);

		uint32_t message_queue_timeout_us = 0;
		if(sm_state == DeviceService_State_COMPortScan) {
			message_queue_timeout_us = 1000*1000;
		}

		Y_Rx_e mi_pull = Y_Rx_e::Empty;
		self->qi_produce_event.AwaitSignalUntil(
			message_queue_timeout_us,
			[self, &qi_consumer, &mi, &mi_pull]() {
			return (Y_Rx_e::Empty != (mi_pull = qi_consumer.Pull_Rx(&mi)));
		});

		if(mi_pull == Y_Rx_e::Empty) {
			/* event queue timed out, run the (blocking) state machine. */
			DeviceService_StateMachine(self, &sm);
		} else if(mi_pull == Y_Rx_e::Success) {
			switch(mi.type) {
				case DeviceService_MsgIn_e::End: {
					/* note: race: other incoming messages are lost */
					goto end;
				} break;

				default: AssureTrue(false, "unknown message type %d", mi.type);
			}
		}
	}
	end:;

	return 0;
}
#endif
