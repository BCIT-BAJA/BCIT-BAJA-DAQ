//

#include "pch.h"

#include "DeviceService.h"
#include "StateMachine.h"

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

HANDLE OpenCOMPortHandle_8N1(const char* comN_str, DWORD baudrate) {
	HANDLE ret = INVALID_HANDLE_VALUE;
	HANDLE h = INVALID_HANDLE_VALUE;
	Defer(
		if(!WindowsHandle_IsValid(ret)) {
			WindowsHandle_CloseIfValid(h);
		}
	);

	char path_str[MAX_PATH] = { 0 };
	snprintf(ArrayArg(path_str), "\\\\.\\%s", comN_str);

	h = CreateFileA(
		path_str,
		GENERIC_READ | GENERIC_WRITE,
		0,
		NULL,
		OPEN_EXISTING,
		FILE_FLAG_OVERLAPPED,
		NULL
	);
	if(!Test_True(
		h != INVALID_HANDLE_VALUE
		, "Failed to open %s"
		, comN_str
	)) {
		return INVALID_HANDLE_VALUE;
	}

	// Configure DCB (baud, parity, data bits, stop bits)
	DCB dcb = { 0 };
	dcb.DCBlength = sizeof(DCB);

	if(!Test_True(GetCommState(h, &dcb))) {
		return INVALID_HANDLE_VALUE;
	}

	snprintf(ArrayArg(path_str)
		, "baud=%u parity=N data=8 stop=1"
		, baudrate
	);
	if(!Test_True(BuildCommDCBA(path_str, &dcb))) {
		return INVALID_HANDLE_VALUE;
	}

	if(!Test_True(SetCommState(h, &dcb))) {
		return INVALID_HANDLE_VALUE;
	}

	// todo: provide reasonable timeouts for high speed communication.
	//       ie latency vs throughput.
	COMMTIMEOUTS timeouts = { 0 };
	timeouts.ReadIntervalTimeout = 50;
	timeouts.ReadTotalTimeoutConstant = 50;
	timeouts.ReadTotalTimeoutMultiplier = 10;
	timeouts.WriteTotalTimeoutConstant = 50;
	timeouts.WriteTotalTimeoutMultiplier = 10;
	if(!Test_True(SetCommTimeouts(h, &timeouts))) {
		return INVALID_HANDLE_VALUE;
	}

	// apparently guru knowledge suggests yielding here for the serial driver to 
	// process the new configuration.
	Sleep(10);

	{
		/*
		// Some drivers (especially high-speed USB-to-Serial or Bluetooth-Serial) have internal limits.
		// For example, if you set a ReadIntervalTimeout of 1ms, but the driver’s internal polling rate is 16ms, the driver might "silent-fail" your request or round it up to 16ms without telling you SetCommTimeouts failed.
		*/
		COMMTIMEOUTS verify = { 0 };
		if(!Test_True(GetCommTimeouts(h, &verify))) {
			return INVALID_HANDLE_VALUE;
		}

		Ensure_TrueAtCompileTime(sizeof(timeouts) == sizeof(verify));
		if(!Test_True(0 == memcmp(&timeouts, &verify, sizeof(timeouts)), "The Driver failed to accept the COM port timeouts.")) {
			return INVALID_HANDLE_VALUE;
		}
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
	return h;
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
	DeviceService_State_ReadNumber,
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
};

static void DeviceService_StateMachineData_CloseCOMPort(DeviceService_StateMachineData* d) {
#if 0 // todo: this is a race condition, because the driver may still be processing the previous I/O request, and if we close the handle while it's still processing, it could lead to undefined behavior. To avoid this, we need to ensure that all pending I/O operations have completed before closing the handle. Here's a general approach to safely close the COM port handle:
	1. Stop	CancelIoEx(hSerial, &ov)	Tell the driver to give up.
	2. Wait	GetOverlappedResult(...)	Wait for the driver to acknowledge the stop.
	3. Verify	Check ERROR_OPERATION_ABORTED	Confirm the kernel has "released" your memory.
	4. Delete	CloseHandle / delete[]	Now your memory is safe to reclaim.
#endif

	Ensure_TrueAtCompileTime(4 == DeviceService_StateMachineData_CountOfHandles);
	WindowsHandle_CloseIfValid(d->comport_ovl_WaitCommEvent.hEvent);
	WindowsHandle_CloseIfValid(d->comport_ovl_WriteFile.hEvent);
	WindowsHandle_CloseIfValid(d->comport_ovl_ReadFile.hEvent);
	WindowsHandle_CloseIfValid(d->comport_h);
}

static bool DeviceService_StateMachineData_TryWaitCommEvent(DeviceService_StateMachineData* d) {
	/*
	// If the overlapped operation cannot be completed immediately,
	// the function returns FALSE and the GetLastError function returns ERROR_IO_PENDING,
	// indicating that the operation is executing in the background.
	*/
	if(!Test_True(ResetEvent(d->comport_ovl_WaitCommEvent.hEvent))) {
		return false;
	}

	if(!Test_True(
		WaitCommEvent(
			d->comport_h,
			&d->comport_WaitCommEvent_EvtMask,
			&d->comport_ovl_WaitCommEvent
		)
		|| ERROR_IO_PENDING == GetLastError()
	)) {
		return false;
	}

	return true;
}

static bool DeviceService_StateMachineData_TryOpenCOMPort(DeviceService_StateMachineData* d, const char* comN_str) {
	DeviceService_StateMachineData_CloseCOMPort(d);

	bool ret = false;
	Defer(
		if(!ret) {
			DeviceService_StateMachineData_CloseCOMPort(d);
		}
	);

	d->comport_h = OpenCOMPortHandle_8N1(comN_str, CBR_115200);
	if(!WindowsHandle_IsValid(d->comport_h)) {
		return false;
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

		if(!Test_True((*evt_handle) = CreateEvent(
			NULL, // lpEventAttributes (null, cannot be inherited by child processes)
			/*
			// "The event object should be a manual-reset event.
			// If an auto-reset event is used, the result of a wait operation is undefined
			// if the I/O operation is completed before the wait is finished."
			*/
			FALSE, // bManualReset (creates a manual reset event object)
			FALSE, // bInitialState (initially nonsignaled)
			NULL // lpName (without a name)
		))) {
			return false;
		}
	}

	if(!Test_True(SetCommMask(
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
	))) {
		return false;
	}

	if(!DeviceService_StateMachineData_TryWaitCommEvent(d)) {
		return false;
	}

	ret = true;
	return ret;
}

static bool DeviceService_StateMachine_AwaitOverlappedIO(DeviceService* _, StateMachine* sm) {
	DeviceService_StateMachineData* d = cast(DeviceService_StateMachineData*)sm->user_data;
	Assert_True(d->comport_h != INVALID_HANDLE_VALUE);

	HANDLE wait_handles[] = {
		d->comport_ovl_ReadFile.hEvent,
		d->comport_ovl_WriteFile.hEvent,
		d->comport_ovl_WaitCommEvent.hEvent,
	};
	Ensure_TrueAtCompileTime(Array_CountOf(wait_handles) == DeviceService_StateMachineData_CountOfEvents);

	for(size_t evt_i = 0; evt_i < Array_CountOf(wait_handles); ++evt_i) {
		HANDLE evt = wait_handles[evt_i];

		if(!Test_True(WindowsHandle_IsValid(evt))) {
			// todo: do something here
			return false;
		}
	}

	bool fatal = false;
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
				// if readfile completes, immediately re-launch it with the next Rented ring buffer[]. Otherwise, die, (because the buffer should basically never overflow) 
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
				Defer(
					if(!fatal && !DeviceService_StateMachineData_TryWaitCommEvent(d)) {
						fatal = true;
					}
				);

				/*
				// The calling process can use one of the wait functions to determine
				// the event object's state and then use the GetOverlappedResult function
				// to determine the results of the WaitCommEvent operation.
				// GetOverlappedResult reports the success or failure of the operation,
				// and the variable pointed to by the lpEvtMask parameter is set to indicate
				// the event that occurred. 
				*/
				DWORD NumberOfBytesTransferred;
				if(!Test_True(GetOverlappedResult(
					d->comport_h, // hFile
					&d->comport_ovl_WaitCommEvent, // lpOverlapped
					&NumberOfBytesTransferred, // lpNumberOfBytesTransferred
					FALSE // bWait
				))) {
					fatal = true;
					break;
				}

				if(d->comport_WaitCommEvent_EvtMask & EV_ERR) {
					DWORD ClearCommError_Errors;
					COMSTAT ClearCommError_Stat;
					if(!Test_True(ClearCommError(
						d->comport_h, // hFile
						&ClearCommError_Errors, // lpErrors
						&ClearCommError_Stat // lpStat
					))) {
						fatal = true;
						break;
					}

					// we treat all errors as fatal for now, since we don't have any sophisticated error handling or recovery logic in place.
					// In the future, we can add more nuanced handling based on the specific error conditions.
					fatal = !!ClearCommError_Errors;

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
}

static void DeviceService_StateMachine(DeviceService* _, StateMachine* sm) {
	DeviceService_StateMachineData* d = cast(DeviceService_StateMachineData*)sm->user_data;
	StateMachine_OpenSwitch(sm);

	StateMachine_DefaultState(DeviceService_State_Default) {
		(*d) = DeviceService_StateMachineData();
		StateMachine_GoTo(sm, DeviceService_State_COMPortScan);
	}

	StateMachine_State(DeviceService_State_COMPortScan) {
		if(!QueryCOMPortNames(&d->comport_names) || d->comport_names.empty()) {
			Log("We couldn't find any USB Devices!\n");
			StateMachine_Yield_ThenRetry(sm);
		}
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
			if(!DeviceService_StateMachineData_TryOpenCOMPort(d, d_comport_names[d_comport_names_i].c_str())) {
				continue;
			}

		#if 0
			// 3. Start an overlapped Read
			if (!ReadFile(hComm, buffer, sizeof(buffer), &bytesRead, &comport_ovl_read)) {
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
		#endif

			StateMachine_GoTo(sm, DeviceService_State_ReadNumber);
		}

		// oh fuck, we went through all the ports and none of them worked. log, wait, and rescan.
		Log("We couldn't connect to any USB Device!\n");
		StateMachine_GoTo(sm, DeviceService_State_COMPortScan);
	}

	StateMachine_State(DeviceService_State_ReadNumber) {
		DeviceService_StateMachine_AwaitOverlappedIO(_, sm);

		// todo:
		if(false) {
			Log("Device Service Fatal Error\n");
			// todo: (do something smart here, like cleanup, etc.)
			//       since we have asynchronous operations happening here..!
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
	StateMachine sm;
	sm.user_data = &sm_data;

	while(true) {
		DeviceService_StateMachine(self, &sm);

		uint32_t qi_blocking_timeout_us = 0;
		{
			bool sm_state_success = true;
			StateMachine_StateType sm_state;
			StateMachine_PeekState(&sm, &sm_state);
			Assert_True(sm_state_success);

			if(sm_state == DeviceService_State_COMPortScan) {
				qi_blocking_timeout_us = 1000*1000;
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
