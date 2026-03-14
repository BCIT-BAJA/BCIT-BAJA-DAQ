//

#include "pch.h"

#include "DeviceService.h"
#include "StateMachine.h"
#include "Protocol.h"

using namespace std;

// Scan serial ports by reading the registry key:
// HKEY_LOCAL_MACHINE\HARDWARE\DEVICEMAP\SERIALCOMM
bool QueryCOMPortNames(vector<string>* _ports) {
	Assert_True(_ports);
	vector<string>& comport_names = (*_ports);
	comport_names.resize(0);

	HKEY h = NULL;
	if(!Test_True(ERROR_SUCCESS == RegOpenKeyExA(
		HKEY_LOCAL_MACHINE,
		"HARDWARE\\DEVICEMAP\\SERIALCOMM",
		0,
		KEY_READ,
		&h
	))) {
		return false;
	}
	Defer(RegCloseKey(h));

	DWORD valueCount = 0;
	DWORD maxValueNameLen = 0;
	DWORD maxValueLen = 0;
	if(!Test_True(ERROR_SUCCESS == RegQueryInfoKeyA(
		h,
		NULL,
		NULL,
		NULL,
		NULL,
		NULL,
		NULL,
		&valueCount,
		&maxValueNameLen,
		&maxValueLen,
		NULL,
		NULL
	))) {
		return false;
	}

	// Buffers must be large enough for the largest value name/data + terminating NUL
	vector<char> valueName(maxValueNameLen + 1);
	vector<BYTE> valueData(maxValueLen + 1);

	for(DWORD val_i = 0; val_i < valueCount; ++val_i) {
		DWORD nameLen = (DWORD)valueName.size();
		DWORD dataLen = (DWORD)valueData.size();
		DWORD type = 0;

		if(!Test_True(ERROR_SUCCESS == RegEnumValueA(
			h,
			val_i,
			valueName.data(),
			&nameLen,
			NULL,
			&type,
			valueData.data(),
			&dataLen
		))) {
			continue;
		};

		if(type == REG_SZ && Test_True(!valueData.empty())) {
			comport_names.emplace_back();
			comport_names.back() += cast(const char*)valueData.data();
		}
	}

	return true;
}

/*
1. Configuration & State
Functions : GetCommState, SetCommState, BuildCommDCBA, GetCommTimeouts, SetCommTimeouts
These generally fail due to invalid parameters or hardware being physically disconnected.
Fatal Errors :
ERROR_INVALID_HANDLE(6) : The COM port handle is closed or null.
ERROR_INVALID_PARAMETER(87) : Your DCB structure or timeout values are logically impossible.
Non - Fatal / Environmental :
	ERROR_DEVICE_NOT_CONNECTED(1167) : The USB - to - Serial adapter was unplugged.
	ERROR_BUSY(170) : Another process has locked the settings.

3. Buffer & Error Management
Functions: PurgeComm, ClearCommError
These are your "cleanup" crews. They rarely fail unless the handle itself is dead.
    Fatal Errors:
	  ERROR_INVALID_HANDLE (6): The port is no longer valid.
	  ERROR_ACCESS_DENIED (5): You don't have the permissions to flush the buffers.
    Non-Fatal:
	  These functions are often used to resolve errors. For example, ClearCommError will report hardware-level flags (like CE_RXOVER for buffer overflows) in its lpErrors parameter rather than via GetLastError.
*/
/*
ERROR_OPERATION_ABORTED	995	Non-Fatal	The I/O was cancelled by your code.
ERROR_IO_PENDING	997	Normal	Overlapped I/O is in progress (ignore).
ERROR_INVALID_HANDLE	6	Fatal	Handle is null or already closed.
ERROR_ACCESS_DENIED	5	Fatal	Port is in use by another app.
ERROR_GEN_FAILURE	31	Fatal
ERROR_DEVICE_NOT_CONNECTED (fatal)
ERROR_BAD_COMMAND (fatal)
*/
Audit OpenCOMPortHandle_8N1_Audit(const char* comN_str, DWORD baudrate, COMMTIMEOUTS timeouts, HANDLE* out_handle) {
	HANDLE ret = INVALID_HANDLE_VALUE;
	HANDLE h = INVALID_HANDLE_VALUE;
	Defer(
		/* if we aren't returning a valid handle, close it. */
		if(!WindowsHandle_IsValid(ret)) {
			WindowsHandle_CloseIfValid(h);
		} else {
			(*out_handle) = ret;
		}
	);

	char path_str[MAX_PATH] = { 0 };
	snprintf(ArrayArg(path_str), "\\\\.\\%s", comN_str);

	Audit_ReturnIfUntrue(
		INVALID_HANDLE_VALUE !=
		(h = CreateFileA(
			path_str,
			GENERIC_READ | GENERIC_WRITE,
			0,
			NULL,
			OPEN_EXISTING,
			FILE_FLAG_OVERLAPPED,
			NULL
		))
		, "Failed to open %s"
		, comN_str
	);

	// Configure DCB (baud, parity, data bits, stop bits)
	DCB dcb = { 0 };
	dcb.DCBlength = sizeof(DCB);

	Audit_ReturnIfUntrue(GetCommState(h, &dcb));

	snprintf(ArrayArg(path_str)
		, "baud=%u parity=N data=8 stop=1"
		, baudrate
	);
	Audit_ReturnIfUntrue(BuildCommDCBA(path_str, &dcb));
	Audit_ReturnIfUntrue(SetCommState(h, &dcb));
	Audit_ReturnIfUntrue(SetCommTimeouts(h, &timeouts));

	// apparently guru knowledge suggests yielding here for the serial driver to 
	// process the new configuration.
	Sleep(10);

	{
		/*
		// Some drivers (especially high-speed USB-to-Serial or Bluetooth-Serial) have internal limits.
		// For example, if you set a ReadIntervalTimeout of 1ms, but the driver’s internal polling rate is 16ms, the driver might "silent-fail" your request or round it up to 16ms without telling you SetCommTimeouts failed.
		*/
		COMMTIMEOUTS verify = { 0 };
		Audit_ReturnIfUntrue(GetCommTimeouts(h, &verify));
		Audit_ReturnIfUntrue(0 == memcmp(&timeouts, &verify, sizeof(timeouts)), "The Driver failed to accept the COM port timeouts.");
		Ensure_TrueAtCompileTime(sizeof(timeouts) == sizeof(verify));
	}

	// since we changed the driver settings, we'll purge to reset any accumulated data.
	if(!Test_True(PurgeComm(h, 0 
		| PURGE_RXCLEAR
		| PURGE_TXCLEAR
		| PURGE_RXABORT
		| PURGE_TXABORT
	))) {
		/* not fatal, continue */
	}

	ret = h;
	return Audit();
}

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
	DeviceService_State_AwaitIO,
};

Struct(DeviceService_StateMachineData) {
	vector<string> comport_names;
	size_t comport_names_scan_index = 0;

	#define DeviceService_StateMachineData_CountOfEvents 3
	#define DeviceService_StateMachineData_CountOfHandles (1 + DeviceService_StateMachineData_CountOfEvents)
	HANDLE comport_h = INVALID_HANDLE_VALUE; // CloseHandle
	OVERLAPPED comport_ovl_WaitCommEvent = { 0 }; // CloseHandle(.hEvent)
	OVERLAPPED comport_ovl_ReadFile = { 0 }; // CloseHandle(.hEvent)
	OVERLAPPED comport_ovl_WriteFile = { 0 }; // CloseHandle(.hEvent)

	DWORD comport_WaitCommEvent_EvtMask = 0;

#define DeviceService_StateMachineData_ReadFileBuffer_PoolTimeoutCapacity  cast(uint32_t)(4096)
#define DeviceService_StateMachineData_ReadFileBuffer_PoolActualCapacity   cast(uint32_t)(4096 + 1024)
#define DeviceService_StateMachineData_ReadFileBuffer_PoolCount (3)
	struct {
		uint8_t* pool_contiguous = null; // [Count*Capacity]
		uint8_t* pools[DeviceService_StateMachineData_ReadFileBuffer_PoolCount] = { 0 };
		uint32_t pool_free_i = 0;
	} ReadFileBuffer;
};

static void DeviceService_StateMachineData_ReinitAndAllocate(DeviceService_StateMachineData* d) {
	auto rfb = d->ReadFileBuffer;

	if(!rfb.pool_contiguous) {
		rfb.pool_contiguous = cast(uint8_t*)Malloc_OrAbort(
			(1
				*(DeviceService_StateMachineData_ReadFileBuffer_PoolActualCapacity)
				*(DeviceService_StateMachineData_ReadFileBuffer_PoolCount)
			)
		);

		for(uint32_t pool_i = 0; pool_i < Array_CountOf(rfb.pools); ++pool_i) {
			rfb.pools[pool_i] = 
				rfb.pool_contiguous
				+ pool_i*(DeviceService_StateMachineData_ReadFileBuffer_PoolActualCapacity);
		}

		rfb.pool_free_i = 0;
	}

	(*d) = DeviceService_StateMachineData();

	/* keep the read buffer alive */
	d->ReadFileBuffer = rfb;
}

static void DeviceService_StateMachineData_Free(DeviceService_StateMachineData* d) {
	Free_NullSafe(&d->ReadFileBuffer.pool_contiguous);
}

static void DeviceService_StateMachineData_CloseCOMPortHandles(DeviceService_StateMachineData* d) {
#if 0 // todo: this is a race condition, because the driver may still be processing the previous I/O request, and if we close the handle while it's still processing, it could lead to undefined behavior. To avoid this, we need to ensure that all pending I/O operations have completed before closing the handle. Here's a general approach to safely close the COM port handle:
	1. Stop	CancelIoEx(hSerial, &ov)	Tell the driver to give up.
	2. Wait	GetOverlappedResult(...)	Wait for the driver to acknowledge the stop.
	3. Verify	Check ERROR_OPERATION_ABORTED	Confirm the kernel has "released" your memory.
	4. Delete	CloseHandle / delete[]	Now your memory is safe to reclaim.
#endif

	Ensure_TrueAtCompileTime(4 == DeviceService_StateMachineData_CountOfHandles);
	if(WindowsHandle_IsValid(d->comport_h)) {
		Test_True(
			CancelIoEx(
				d->comport_h, 
				NULL // all I/O requests for the hFile parameter are canceled
			) || GetLastError() == ERROR_NOT_FOUND // Else if this function cannot find a request to cancel
		);
	}
	WindowsHandle_CloseIfValid(d->comport_h);
	WindowsHandle_CloseIfValid(d->comport_ovl_WaitCommEvent.hEvent);
	WindowsHandle_CloseIfValid(d->comport_ovl_ReadFile.hEvent);
	WindowsHandle_CloseIfValid(d->comport_ovl_WriteFile.hEvent);
}

/*
2. Event Handling & Overlapped I/O
Functions: WaitCommEvent, GetOverlappedResult, ResetEvent
These are the "traffic controllers" of your serial thread.
    Fatal Errors:
	  ERROR_INVALID_HANDLE (6): Common if the handle is closed while a thread is still waiting.
	  ERROR_IO_INCOMPLETE (996): (Specifically for GetOverlappedResult) Not necessarily "fatal," but means you checked for a result before the operation actually finished.
    Non-Fatal / Expected:
	  ERROR_IO_PENDING (997): Very common. This isn't actually an error; it means the operation is working in the background.
	  ERROR_OPERATION_ABORTED (995): The I/O was cancelled (likely by PurgeComm or thread termination).
*/
static Audit DeviceService_StateMachineData_WaitCommEvent_AuditOverlappedResult(DeviceService_StateMachineData* d) {
	OVERLAPPED* lpOverlapped = &d->comport_ovl_WaitCommEvent;
	/*
	// The calling process can use one of the wait functions to determine
	// the event object's state and then use the GetOverlappedResult function
	// to determine the results of the WaitCommEvent operation.
	// GetOverlappedResult reports the success or failure of the operation,
	// and the variable pointed to by the lpEvtMask parameter is set to indicate
	// the event that occurred. 
	*/
	DWORD NumberOfBytesTransferred;
	Audit_ReturnIfUntrue(GetOverlappedResult(
		d->comport_h, // hFile
		lpOverlapped, // lpOverlapped
		&NumberOfBytesTransferred, // lpNumberOfBytesTransferred
		FALSE // bWait
	));

	if(d->comport_WaitCommEvent_EvtMask & EV_ERR) {
		DWORD ClearCommError_Errors;
		COMSTAT ClearCommError_Stat;
		Audit_ReturnIfUntrue(ClearCommError(
			d->comport_h, // hFile
			&ClearCommError_Errors, // lpErrors
			&ClearCommError_Stat // lpStat
		));

		// we treat all errors as fatal for now, since we don't have any sophisticated error handling or recovery logic in place.
		// In the future, we can add more nuanced handling based on the specific error conditions.
		if(ClearCommError_Errors & CE_BREAK) {
			// Break Detected
			// The hardware detected a break condition. 
			// If you aren't expecting a Break signal, CE_BREAK usually points to a physical hardware failure.
			Log("Received CE_BREAK, Break error\n");
		}
		if(ClearCommError_Errors & CE_FRAME) {
			// Receive Framing error
			Log("Received CE_FRAME, Framing error\n");
			// (common in industrial environments with long cables)
		}
		if(ClearCommError_Errors & CE_OVERRUN) {
			// Receive Overrun Error
			// A character-buffer overrun has occurred. The next character is lost. 
			// Windows usually allocates a small internal buffer (often 4KB or 8KB) to hold data before your ReadFile is called.
			// If your code stops calling ReadFile for more than 80–160 ms at 500k baud, the driver buffer will overflow, and you will get a "Buffer Overrun" error (CE_OVERRUN), even though your 1MB buffer is empty.
			Log("Received CE_OVERRUN, Overrun Error\n");
		}
		if(ClearCommError_Errors & CE_RXOVER) {
			// Receive Queue overflow
			// An input buffer overflow has occurred. There is either no room in the input buffer, or a character was received after the end-of-file (EOF) character. 
			Log("Received CE_RXOVER, Queue Overflow Error\n");
		}
		if(ClearCommError_Errors & CE_RXPARITY) {
			// Receive Parity Error
			// The hardware detected a parity error. 
			Log("Received CE_RXPARITY, Parity Error\n");
			// (common in industrial environments with long cables)
		}

	#if 0 // (not applicable)
		ClearCommError_Stat.fCtsHold : 1;
		ClearCommError_Stat.fDsrHold : 1;
		ClearCommError_Stat.fRlsdHold : 1;
		ClearCommError_Stat.fXoffHold : 1;
		ClearCommError_Stat.fXoffSent : 1;
		ClearCommError_Stat.fEof : 1;
		ClearCommError_Stat.fTxim : 1;
	#endif

		Log("ClearCommError_Stat: cbInQue=%u, cbOutQue=%u\n"
			, ClearCommError_Stat.cbInQue // number of chars in the input queue
			, ClearCommError_Stat.cbOutQue // number of chars in the output queue
		);

	#if 0 // (not possible for WaitCommEvent)
		if(ClearCommError_Errors & CE_TXFULL) // TX Queue is full
		if(ClearCommError_Errors & CE_PTO) // LPTx Timeout
		if(ClearCommError_Errors & CE_IOE) // LPTx I/O Error
		if(ClearCommError_Errors & CE_DNS) // LPTx Device not selected
		if(ClearCommError_Errors & CE_OOP) // LPTx Out-Of-Paper
		if(ClearCommError_Errors & CE_MODE) // Requested mode unsupported
	#endif

		// for now, we raise an audit if there are *any* errors at all.
		Audit_ReturnIfUntrue(!ClearCommError_Errors);
	}

	return Audit();
}
static Audit DeviceService_StateMachineData_WaitCommEvent_Audit(DeviceService_StateMachineData* d) {
	LPOVERLAPPED lpOverlapped = &d->comport_ovl_WaitCommEvent;
	/*
	// If the overlapped operation cannot be completed immediately,
	// the function returns FALSE and the GetLastError function returns ERROR_IO_PENDING,
	// indicating that the operation is executing in the background.
	*/
	while(true) {
		Audit_ReturnIfUntrue(ResetEvent(lpOverlapped->hEvent));
		if(!WaitCommEvent(
			d->comport_h, // hFile
			&d->comport_WaitCommEvent_EvtMask, // lpEvtMask
			lpOverlapped // lpOverlapped
		)) {
			Audit_ReturnIfUntrue(ERROR_IO_PENDING == GetLastError()); // ERROR_IO_PENDING is not a failure; it designates the operation is pending completion asynchronously.
			break;
		}
		Audit_ReturnIfAuditFailed(DeviceService_StateMachineData_WaitCommEvent_AuditOverlappedResult(d));
	}
	return Audit();
}

static Audit DeviceService_StateMachineData_ReadFile_AuditOverlappedResult(DeviceService_StateMachineData* d) {
	LPOVERLAPPED lpOverlapped = &d->comport_ovl_ReadFile;

	// if readfile completes, immediately re-launch it with the next Rented ring buffer[]. Otherwise, die, (because the buffer should basically never overflow) 
	DWORD NumberOfBytesTransferred;
	Audit_ReturnIfUntrue(GetOverlappedResult(
		d->comport_h, // hFile
		lpOverlapped, // lpOverlapped
		&NumberOfBytesTransferred, // lpNumberOfBytesTransferred
		FALSE // bWait
	));

	// (bytes were transferred)
	auto& rfb = d->ReadFileBuffer;
#if 0
	const uint32_t pool_i = (
		  rfb.pool_free_i
		? (rfb.pool_free_i - 1)
		: (rfb.pool_free_i - 1) + DeviceService_StateMachineData_ReadFileBuffer_PoolCount
	);
	const uint8_t* pool = rfb.pools[pool_i];
	(++rfb.pool_free_i);
	if(Array_CountOf(rfb.pools) <= rfb.pool_free_i) {
		rfb.pool_free_i = 0;
	}
#else
	const uint8_t* pool = rfb.pools[0];
#endif

	if(!NumberOfBytesTransferred) {
		return Audit();
	}

	Assert_True(NumberOfBytesTransferred < DeviceService_StateMachineData_ReadFileBuffer_PoolActualCapacity
		, "Looks like our timeouts didn't wok, we overflowed our readfile buffer."
	);

	#if 1
	std::string data_str;
	for(uint32_t ch_i = 0; ch_i < NumberOfBytesTransferred; ++ch_i) {
		char ch = pool[ch_i];
		switch(ch) {
			case '\0': { data_str += "\\0"; } break;
			case '\n': { data_str += "\\n"; } break;
			case '\t': { data_str += "\\t"; } break;
			case '\r': { data_str += "\\r"; } break;
			default:   { data_str += ch; }  break;
		}
	}
	Log("Received: (%u) '%s'\n"
		, NumberOfBytesTransferred
		, data_str.c_str()
	);
	#endif

	// basically, the idea here is to push data into a contiguous accumulator state machine:
	// for Protocol_Begin = { memchr(0x5A),   memcmp(0x5A + 1, 5A + 2, 5A + 3, 5A + 4) }
	// for Protocol_End   = { memchr(0xA5),   memcmp(0xA5 + 1, A5 + 2, A5 + 3, A5 + 4) }
	// Search_Protocol_Begin(IncomingBuffer):
	//    
	//    use memchr() to scan for the Protocol_Begin flag, 0x5A. if the next memcmp() matches, we treat this as Protocol_Begin. Goto FoundProtocolBegin
	// Found_Protocol_Begin(IncomingBuffer):
	//    optimistically search for Protocol_End with memchr() and memcmp().
	//    if we found it, we push the packet (between Protocol_Begin and Protocol_End) into a processing queue, and go back to Search_Protocol_Begin.
	//    else, we accumulate.
	//
	//    if the accumulation has filled up to a maximum limit, without finding Protocol_End,
	//    we ignore our current Protocol_Begin flag,
	//
	// if memchr() finds the 
	// 64 bit Protocol_Begin flag.
	// 64 bit Protocol_End flag.

	// a packet is basically guaranteed to be smaller than (8Kb + 8Kb)
	return Audit();
}

static Audit DeviceService_StateMachineData_ReadFile_Audit(DeviceService_StateMachineData* d) {
	LPOVERLAPPED lpOverlapped = &d->comport_ovl_ReadFile;
	/*
	// If the overlapped operation cannot be completed immediately,
	// the function returns FALSE and the GetLastError function returns ERROR_IO_PENDING,
	// indicating that the operation is executing in the background.
	*/
	auto& rfb = d->ReadFileBuffer;
	while(true) {
		Audit_ReturnIfUntrue(ResetEvent(lpOverlapped->hEvent));
		if(!ReadFile(
			d->comport_h, // hFile
			// rfb.pools[rfb.pool_free_i], // (out) lpBuffer
			rfb.pools[0], // (out) lpBuffer
			DeviceService_StateMachineData_ReadFileBuffer_PoolActualCapacity, // nNumberOfBytesToRead
			NULL, // lpNumberOfBytesRead parameter should be set to NULL
			lpOverlapped // lpOverlapped
		)) {
			Audit_ReturnIfUntrue(ERROR_IO_PENDING == GetLastError()); // ERROR_IO_PENDING is not a failure; it designates the operation is pending completion asynchronously.
			break;
		}
		Audit_ReturnIfAuditFailed(DeviceService_StateMachineData_ReadFile_AuditOverlappedResult(d));
	}

	return Audit();
}

static Audit DeviceService_StateMachineData_AuditOpenCOMPort(DeviceService_StateMachineData* d, const char* comN_str) {
	Defer(
		if(!Audit().success) {
			DeviceService_StateMachineData_CloseCOMPortHandles(d);
		}
	);

	{
		// we want our buffers to be *filled* with 4Kb, the size of one page.
		// in order to allow for jitter, and buffer overruns, we *allocate double* that
		// 8Kb, yet set our total timeout to fill 4Kb.

		// T(imeout)
		// B(audrate)=500_000, 8N1, (10 bits per data-byte),
		//
		// (s/byte) = (1/B seconds/bit)*(10 bits/byte)
		// C(bytes) = (T) / (s/byte)
		// (T) = C * (s/byte)
		//

		const uint32_t ReadTotalTimeout_ms = cast(uint32_t)(
			/*
			// always offset the timeout to be 90% of a 4Kb page:
			// If your buffer is 4KB or less: This usually happens in a single, atomic operation.
			// The OS doesn't have to worry about your buffer crossing a virtual memory page boundary.
			*/
			(-3.99f) + // (-4 ms floor, try to account for possible OS timing jitter)
			(1000.0f)*(10.0f/Hub_Serial_8N1BaudRate)*DeviceService_StateMachineData_ReadFileBuffer_PoolTimeoutCapacity
		);

		// we want the overlapped read to complete as soon as there is a "gap" in transmission, signaling the end of a data packet.
		// Fixed ("total") Timeout = TotalConstant + (TotalMultiplier × BytesRequested)
		COMMTIMEOUTS timeouts = { 0 };
		timeouts.ReadIntervalTimeout = 0; // maximum time allowed to elapse before the arrival of the next byte on the communications line, in milliseconds
		timeouts.ReadTotalTimeoutConstant = ReadTotalTimeout_ms; // added to the product of the ReadTotalTimeoutMultiplier member and the requested number of bytes
		timeouts.ReadTotalTimeoutMultiplier = 0; // multiplied by the requested number of bytes to be read.

		// 
		timeouts.WriteTotalTimeoutConstant = 50; 
		timeouts.WriteTotalTimeoutMultiplier = 10;

		Audit_ReturnIfAuditFailed(OpenCOMPortHandle_8N1_Audit(comN_str, Hub_Serial_8N1BaudRate, timeouts, &d->comport_h));
		Assert_True(WindowsHandle_IsValid(d->comport_h));
	}

	HANDLE* d_event_handles[] = {
		&d->comport_ovl_WaitCommEvent.hEvent,
		&d->comport_ovl_ReadFile.hEvent,
		&d->comport_ovl_WriteFile.hEvent,
	};
	Ensure_TrueAtCompileTime(Array_CountOf(d_event_handles) == DeviceService_StateMachineData_CountOfEvents);
	for(size_t evt_i = 0; evt_i < Array_CountOf(d_event_handles); ++evt_i) {
		HANDLE* evt_handle = d_event_handles[evt_i];
		Assert_True(!WindowsHandle_IsValid(*evt_handle));

		Audit_ReturnIfUntrue((*evt_handle) = CreateEvent(
			NULL, // lpEventAttributes (null, cannot be inherited by child processes)
			/*
			// "The event object should be a manual-reset event.
			// If an auto-reset event is used, the result of a wait operation is undefined
			// if the I/O operation is completed before the wait is finished."
			*/
			FALSE, // bManualReset (creates a manual reset event object)
			FALSE, // bInitialState (initially nonsignaled)
			NULL // lpName (without a name)
		));
	}

	Audit_ReturnIfUntrue(SetCommMask(
		d->comport_h, 0
		| EV_ERR // A line - status error occurred.Line - status errors are CE_FRAME, CE_OVERRUN, and CE_RXPARITY.
	/*#if 0 // these WaitCommEvent(s) do not apply. Either because we're using OVERLAPPED I/O, binary protocol, or unavailable signal wires.
		| EV_TXEMPTY; // The last character in the output buffer was sent.
		| EV_RXCHAR // A character was received and placed in the input buffer.
		| EV_RXFLAG // The event character was received and placed in the input buffer.The event character is specified in the device's DCB structure, which is applied to a serial port by using the SetCommState function.
		| EV_BREAK // A break was detected on input.
		| EV_CTS // The CTS(clear - to - send) signal changed state.
		| EV_DSR // The DSR(data - set - ready) signal changed state.
		| EV_RLSD // The RLSD(receive - line - signal - detect) signal changed state.
		| EV_RING // A ring indicator was detected.
	*/
	));

	Audit_ReturnIfAuditFailed(DeviceService_StateMachineData_WaitCommEvent_Audit(d));
	Audit_ReturnIfAuditFailed(DeviceService_StateMachineData_ReadFile_Audit(d));
	return Audit();
}

static bool DeviceService_StateMachine_AwaitOverlappedIO(DeviceService* _, StateMachine* sm) {
	DeviceService_StateMachineData* d = cast(DeviceService_StateMachineData*)sm->user_data;
	Assert_True(WindowsHandle_IsValid(d->comport_h));

	HANDLE wait_handles[] = {
		d->comport_ovl_ReadFile.hEvent,
		d->comport_ovl_WriteFile.hEvent,
		d->comport_ovl_WaitCommEvent.hEvent,
	};
	Ensure_TrueAtCompileTime(Array_CountOf(wait_handles) == DeviceService_StateMachineData_CountOfEvents);

	bool fatal = false;
	for(size_t evt_i = 0; evt_i < Array_CountOf(wait_handles); ++evt_i) {
		HANDLE evt = wait_handles[evt_i];

		if(!Test_True(WindowsHandle_IsValid(evt))) {
			fatal = true;
			break;
		}
	}

	bool loop = true;
	while(loop && !fatal) {
		// (WaitFor Sleep here, probably for 100 ms or so, that is enough to also poll the main thread queue in a reasonable time)
		DWORD wait_for_multiple_objects_result = WaitForMultipleObjects(
			Array_CountOf(wait_handles), // nCount
			wait_handles, // lpHandles
			FALSE, // bWaitAll
			100 // dwMilliseconds
		);
		switch(wait_for_multiple_objects_result) {
			// WAIT_TIMEOUT
			// The time-out interval elapsed and the conditions specified by the bWaitAll parameter are not satisfied.
			case WAIT_TIMEOUT: {
				loop = false;
			} break;

			// WAIT_OBJECT_0 to (WAIT_OBJECT_0 + nCount– 1)
			// If bWaitAll is FALSE, the return value minus WAIT_OBJECT_0 indicates the lpHandles array index of the object that satisfied the wait. If more than one object became signaled during the call, this is the array index of the signaled object with the smallest index value of all the signaled objects.
			case (WAIT_OBJECT_0 + 0): {
				if(false
					|| Audit_AuditFailed(DeviceService_StateMachineData_ReadFile_AuditOverlappedResult(d))
					|| Audit_AuditFailed(DeviceService_StateMachineData_ReadFile_Audit(d))
				) {
					AuditStack stack;
					Audit_Pop(&stack);
					fatal = true;
				}
			} break;

			case (WAIT_OBJECT_0 + 1): {
				// if writefile completes, immediately re-launch it with the next write queue[], and Return the current queue[] item.
			#if 0
				if(GetOverlappedResult(
					hComm,
					&comport_ovl_read,
					&bytesRead,
					FALSE
				)) {
					printf("Delayed read finished: %d bytes\n", bytesRead);
				}
			#endif
			} break;

			case (WAIT_OBJECT_0 + 2): {
				if(false
					|| Audit_AuditFailed(DeviceService_StateMachineData_WaitCommEvent_AuditOverlappedResult(d))
					|| Audit_AuditFailed(DeviceService_StateMachineData_WaitCommEvent_Audit(d))
				) {
					Audit_Pop();
					fatal = true;
				}
			} break;

		#if 0 // WAIT_ABANDONED is only for mutexes, so it doesn't apply here, but if we had a WaitableMutex in the mix, we would want to handle this case.
			// (WAIT_ABANDONED_0 + nCount– 1) 
			// If bWaitAll is FALSE, the return value minus WAIT_ABANDONED_0 indicates the lpHandles array index of an abandoned mutex object that satisfied the wait.
			// Ownership of the mutex object is granted to the calling thread, and the mutex is set to nonsignaled.
			case (WAIT_ABANDONED_0 + 0): {
			} break;
		#endif

			case WAIT_FAILED: {
				Test_True(wait_for_multiple_objects_result != WAIT_FAILED, "%u", wait_for_multiple_objects_result);
				fatal = true;
			} break;

			default: {
				Test_True(false, "%u", wait_for_multiple_objects_result);
				fatal = true;
			} break;
		}
	}

	// the handles could be invalid
	// the device has gone away.
	// what we're going to do is signal that we intend to shut down the device
	return !fatal;
}

static void DeviceService_StateMachine(DeviceService* _, StateMachine* sm) {
	DeviceService_StateMachineData* d = cast(DeviceService_StateMachineData*)sm->user_data;
	StateMachine_OpenSwitch(sm);

	StateMachine_DefaultState(DeviceService_State_Default) {
		DeviceService_StateMachineData_ReinitAndAllocate(d);
		StateMachine_GoTo(sm, DeviceService_State_COMPortScan);
	}

	StateMachine_State(DeviceService_State_COMPortScan) {
		if(!QueryCOMPortNames(&d->comport_names) || d->comport_names.empty()) {
			Log("We couldn't find any USB Devices!\n");
			StateMachine_Yield_ThenRetry(sm);
		}

		Log("We found %u USB Devices!\n", cast(uint32_t)d->comport_names.size());
		StateMachine_GoTo(sm, DeviceService_State_COMPortScan_ForEach);
	}

	StateMachine_State(DeviceService_State_COMPortScan_ForEach) {
		d->comport_names_scan_index = 0;
	}
	StateMachine_Try(sm) {
		Assert_True(!WindowsHandle_IsValid(d->comport_h));

		const vector<string>& d_comport_names = d->comport_names;
		size_t& d_comport_names_i = d->comport_names_scan_index;

		for(; d_comport_names_i < d_comport_names.size(); ++d_comport_names_i) {
			if(Audit_AuditFailed(DeviceService_StateMachineData_AuditOpenCOMPort(
				d,
				d_comport_names[d_comport_names_i].c_str()
			))) {
				AuditStack stack;
				Audit_Pop(&stack);
				continue;
			}

			Assert_True(WindowsHandle_IsValid(d->comport_h));

			const char* com_port_str = "?";
			if(Test_True(d->comport_names_scan_index < d->comport_names.size())) {
				com_port_str = d->comport_names[d->comport_names_scan_index].c_str();
			}
			Log("%s Connected\n", com_port_str);
			StateMachine_GoTo(sm, DeviceService_State_AwaitIO);
		}

		// oh fuck, we went through all the ports and none of them worked. log, wait, and rescan.
		// (for some reason this spammed a bunch of these errors....)
		Log("We couldn't connect to any USB Device!\n");
		StateMachine_Yield_ThenGoTo(sm, DeviceService_State_COMPortScan);
	}

	StateMachine_State(DeviceService_State_AwaitIO) {
		if(!DeviceService_StateMachine_AwaitOverlappedIO(_, sm)) {
			const char* com_port_str = "?";
			if(Test_True(d->comport_names_scan_index < d->comport_names.size())) {
				com_port_str = d->comport_names[d->comport_names_scan_index].c_str();
			}
			Log("%s Disconnected\n", com_port_str);

			// todo: don't we have to close handles here??

			DeviceService_StateMachineData_CloseCOMPortHandles(d);
			StateMachine_GoTo(sm, DeviceService_State_Default);
		}

		// (jump to sub state machine re: reading / writing / protocol logic here)
		// the reading logic must parse packets beginning and ending with the magic signature, then Return those buffers for reuse in the pool.
		//       the parsed data structures can then be enqueue'd for the main protocol state machine to process.
		//
		// then, the protocol state machine runs, which can kick off (arbitrary) binary writes, and processes the parsed higher level events,
		// verifying signatures, control flow, and packaging the data to be sent to the DataService, or whomever is subscribed.

		StateMachine_Yield_ThenRetry(sm);
	}

	StateMachine_CloseSwitch;
}

intptr_t Thread_DeviceService(void* _) {
	Basic_SetThreadName("DeviceService");

#if c_config(debug)
	puts(MACRO_FunctionSignature());
	Defer(puts(MACRO_FunctionSignature()));
#endif

	DeviceService* self = cast(DeviceService*)_;
	Log_BindThreadLocal(self->log);

	Y_QueueMM<DeviceService_MsgIn>::Consumer qi_consumer = self->qi.Consumer_Rent();
	Defer(self->qi.Consumer_Return(&qi_consumer));

	DeviceService_MsgIn mi;
	TracyCZoneEnd(init);


	DeviceService_StateMachineData sm_data;
	Defer(DeviceService_StateMachineData_Free(&sm_data));

	StateMachine sm;
	sm.user_data = &sm_data;

	while(true) {
		DeviceService_StateMachine(self, &sm);

		uint32_t qi_blocking_timeout_us = 1000*1000;
		{
			bool sm_state_success = true;
			StateMachine_StateType sm_state;
			StateMachine_PeekState(&sm, &sm_state);
			Assert_True(sm_state_success);

			if(sm_state == DeviceService_State_AwaitIO) {
				qi_blocking_timeout_us = 0;
			}
		}

		Y_Rx_e mi_pull = Y_Rx_e::Empty;
		self->qi_produce_event.AwaitSignalUntil(
			qi_blocking_timeout_us,
			[self, &qi_consumer, &mi, &mi_pull]() {
			return (Y_Rx_e::Empty != (mi_pull = qi_consumer.Pull_Rx(&mi)));
		});

		if(mi_pull == Y_Rx_e::Success) {
			switch(mi.type) {
				case DeviceService_MsgIn_e::End: {
					/* note: race: other incoming messages are lost */
					goto end;
				} break;

				default: Test_True(false, "unknown message type %d", mi.type);
			}
		}
	}
	end:;

	return 0;
}
