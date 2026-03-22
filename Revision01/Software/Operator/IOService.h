//

#pragma once

#include "pch.h"

#include "Compile.h"

#include "Y_QueueSS.h"
#include "Y_IndexRentalMM.h"

#include "LogService.h"

bool OS_FileExists(const char* szPath);
bool OS_GetLastModified_AsString(const char* filename, SYSTEMTIME* out_st);

// i think a combination is a good median solution.
// perhaps i can dump a .csv, but then run a second process that attempts to use libxlsx to graph such an outlier.
// (1khz)(60s)(60min)(2hr) = 7 million rows. say, 80 bytes, 7 million * 80 bytes ~560 megabytes of data.
// filtering must be applied, and everything should be extremely stable, with no memory allocations,
// and no user input / false behaviour.
// in addition, any crashes should just autostart the program again...
//
// checkout xlslib. .xls 
// https://github.com/JanX2/xlslib/blob/rebased-on-svn/xlslib/src/xlslib.h
// it allows repeated dumping of .xls object, unlike this other crappy lib :(((

//
// annoyingly, .xlsx is not meant for streaming.
// this means we'll have to put limits on bandwidth, or chunk data or something... IDK.
// We'll have to test writing speed. It *should* be fast enough to handle at LEAST 10khz, god 
// if this library can't do that due to ridiculous flushes...
//
// okay, this .xlsx file writer library is terrible.
// it's probably soooo much better to write a zlib compressed .bin... :d
// then just flush to .xlsx occassionally?
// to be honest, it makes sense just to use an in-memory buffer instead, then pump data to disk often?
// then just flush on certain boundaries, or after a certain time?
// todo: use itoa: https://github.com/jeaiii/itoa/tree/main/itoa

#if 0
struct FileJob {
	uint64_t requestId;
	char data[1024];
};
#endif

Enum(IOService_IORequestType, uint32_t) {
	IOService_IORequestType_nul = 0,
	IOService_IORequestType_WriteFile,
	IOService_IORequestType_ReadFile,
	IOService_IORequestType_WaitCommEvent,
};

struct IOService_IOReply {
	IOService_IORequestType request_type = IOService_IORequestType_nul;
	uint64_t request_tag = 0;
	DWORD NumberOfBytesTransferred = 0;
};

// this memory must exist for the entire lifetime of the request,
// must be valid until the IO thread processes the request and sends back a reply.
struct IOService_IORequest {
	OVERLAPPED Overlapped = { 0 };
	IOService_IORequestType type = 0;
	uint64_t tag = 0;
	Y_QueueSS_WithBlockingPull<IOService_IOReply>* reply_q = null;
};

struct IOService;
struct IOClient {
	IOService* ios = null;
	HANDLE server_iocp_h = INVALID_HANDLE_VALUE;

	// here's the thing, to prevent crashes,
	// this SPSC reply queue must exist for as long as the IOService exists.
	// hence why the IOService has to "rent" and "return" such a queue to the client handle.
	Y_QueueSS_WithBlockingPull<IOService_IOReply>* replies_q = null;
	IndexRental replies_q_rental;
};

struct IOService {
	HANDLE iocp_h = INVALID_HANDLE_VALUE;
	Logger log;
	std::thread thread;

	std::atomic<ULONG_PTR> unique_completion_key = 0;
};

extern Y_QueueSS_WithBlockingPull<IOService_IOReply> g_replies[64];
extern Y_IndexRentalMM_Frugal<Array_CountOf(g_replies)> g_replies_index_rental; // if you wanted to clean up, you could scan for non-zero locks.

Inline bool IOService_RentClient(IOService* ios, IOClient* out_client, const uint32_t arg_entries_capacity) {
	ZoneScoped;

	Assert_True(ios);
	Assert_True(WindowsHandle_IsValid(ios->iocp_h));
	Assert_True(out_client);
	Assert_True(!out_client->ios);
	Assert_True(!WindowsHandle_IsValid(out_client->server_iocp_h));
	Assert_True(!out_client->replies_q);

	if(!Test_True(g_replies_index_rental.M_Rent(&out_client->replies_q_rental))) {
		return false;
	}

	out_client->ios = ios;
	out_client->server_iocp_h = ios->iocp_h;
	// todo: (we can do something smart here, and lazy allocate, but it doesn't matter)
	out_client->replies_q = &g_replies[out_client->replies_q_rental.index];
	out_client->replies_q->Destroy();
	return Test_True(out_client->replies_q->Create(arg_entries_capacity));
}
Inline void IOService_ReturnClient(IOService* ios, IOClient* _) {
	Assert_True(ios);
	Assert_True(_);

	/*
	// (don't bother destroying the queue before returning it to the pool)
	*/
	g_replies_index_rental.M_Return(&_->replies_q_rental);
}
Inline bool IOClient_AssociateHandle(IOClient* _, HANDLE h, ULONG_PTR arg_unique_completion_key = 0) {
	/*
	ExistingCompletionPort:
	If this parameter specifies an existing I / O completion port,
	the function associates it with the handle specified by the FileHandle parameter.
	The function returns the handle of the existing I / O completion port if successful;
	it does not create a new I / O completion port.
	*/
	/*
	CompletionKey:
	it is attached to the file handle specified in the FileHandle parameter at the time of association
	with an I/O completion port. This completion key should be **unique for each file handle**,
	and it accompanies the file handle throughout the internal completion queuing process
	*/
	if(!arg_unique_completion_key) {
		Assert_True(_->ios);
		arg_unique_completion_key = (1 + _->ios->unique_completion_key.fetch_add(1, std::memory_order_relaxed));
	}
	Assert_True(WindowsHandle_IsValid(_->server_iocp_h));
	return Test_True(_->server_iocp_h == CreateIoCompletionPort(
		h, // FileHandle
		_->server_iocp_h, // ExistingCompletionPort
		arg_unique_completion_key, // CompletionKey
		0 // NumberOfConcurrentThreads
	));
}

intptr_t Thread_IOService(void* _);

Inline bool IOService_Create(IOService* _) {
	ZoneScoped;
	Assert_True(!WindowsHandle_IsValid(_->iocp_h));
	HANDLE iocp_h = CreateIoCompletionPort(
		INVALID_HANDLE_VALUE, // FileHandle
		null, // ExistingCompletionPort
		0, // CompletionKey
		1 // NumberOfConcurrentThreads
	);
	if(!WindowsHandle_IsValid(iocp_h)) {
		return false;
	}
	_->iocp_h = iocp_h;
	_->log = null;
	return true;
}
Inline void IOService_Destroy(IOService* _) {
	ZoneScoped;
	WindowsHandle_CloseIfValid(_->iocp_h);
}
Inline void IOService_Begin(IOService* _, Logger log) {
	ZoneScoped;
	Assert_True(WindowsHandle_IsValid(_->iocp_h));
	_->log = log;
	_->thread = std::thread(Thread_IOService, _);
}
Inline bool IOService_SignalEnd(IOService* _) {
	ZoneScoped;

	if(Test_True(WindowsHandle_IsValid(_->iocp_h))) {
		Test_True(PostQueuedCompletionStatus(
			_->iocp_h, // CompletionPort
			0, // dwNumberOfBytesTransferred
			0, // dwCompletionKey
			NULL // lpOverlapped
		));
	}

	return true;
}
Inline void IOService_WaitForEnd(IOService* _) {
	ZoneScoped;
	_->thread.join();
}

Inline void IOService_Demo(Logger log) {
	IOService ios;
	Test_True(IOService_Create(&ios));
	Defer(IOService_Destroy(&ios));

	IOService_Begin(&ios, log);
	Defer(
		IOService_SignalEnd(&ios);
		IOService_WaitForEnd(&ios);
	);

	IOClient ioclient;
	Test_True(IOService_RentClient(&ios, &ioclient, 512));
	Defer(IOService_ReturnClient(&ios, &ioclient));

	const char* live_filename_str = "live.csv";
	if(OS_FileExists(live_filename_str)) {
		SYSTEMTIME dead_st = { 0 };
		if(!OS_GetLastModified_AsString(live_filename_str, &dead_st)) {
			GetLocalTime(&dead_st);
		}

		char dead_filename_str[MAX_PATH] = { 0 };
		snprintf(AArg(dead_filename_str),
			"dead_%04d-%02d-%02d %02d%02d%02d.xlsx",
			dead_st.wYear,
			dead_st.wMonth,
			dead_st.wDay,
			dead_st.wHour,
			dead_st.wMinute,
			dead_st.wSecond
		);

		Test_True(CopyFileA(live_filename_str, dead_filename_str, false));
	}

	HANDLE live_h;
	Test_True(WindowsHandle_IsValid(live_h = CreateFileA(
		live_filename_str,
		GENERIC_READ | GENERIC_WRITE,
		0,
		NULL,
		CREATE_ALWAYS,
		FILE_FLAG_OVERLAPPED,
		NULL
	)));
	Test_True(IOClient_AssociateHandle(&ioclient, live_h));

	IOService_IORequest* write_request = new IOService_IORequest();
	Defer(delete write_request);
	write_request->Overlapped.Pointer = 0;
	write_request->type = IOService_IORequestType_WriteFile;
	write_request->tag = 0;
	write_request->reply_q = ioclient.replies_q;
	Test_True(
		!WriteFile(
			live_h, // hFile
			StringLiteralArg("Hello, world!\r\n"), // lpBuffer, nNumberOfBytesToWrite (must exist over duration of write)
			NULL, // lpNumberOfBytesWritten
			&write_request->Overlapped // lpOverlapped
		)
		&& GetLastError() == ERROR_IO_PENDING
	);

	IOService_IOReply reply;
	bool running = true;
	while(running) {
		if(ioclient.replies_q->S_Pull_Blocking(&reply) == Y_Rx_e::Success) {
			switch(reply.request_type) {
				case IOService_IORequestType_WriteFile: {
					Log("IOService_IORequestType_WriteFile; Tag " FMT_u64 " completed %u bytes transferred.\n"
						, reply.request_tag
						, reply.NumberOfBytesTransferred 
					);
					running = false;
				} break;
			}
		}
	}
}

#if 0
#include <windows.h>
#include <iostream>

enum class IoOp { Read, Write, Event };

struct IOReply {
	uint64_t requestId;
	DWORD bytes;
	IoOp op;
	DWORD eventMask; // Specific to WaitCommEvent
	bool success;
};

struct FileJob {
	OVERLAPPED ov;
	uint64_t requestId;
	IoOp op;
	Y_QueueSS<IOReply>* replyQueue;
	DWORD commMask; // The OS writes the triggered event here
	char data[1024];
};

static DWORD WINAPI IoWorker(LPVOID lpParam) {
	HANDLE hIOCP = (HANDLE)lpParam;
	DWORD bytes = 0;
	ULONG_PTR key = 0;
	LPOVERLAPPED pOv = nullptr;

	while (GetQueuedCompletionStatus(hIOCP, &bytes, &key, &pOv, INFINITE)) {
		if (key == 0) break; // Shutdown

		if (pOv) {
			FileJob* job = (FileJob*)pOv;
			IOReply reply;
			reply.requestId = job->requestId;
			reply.bytes = bytes;
			reply.op = job->op;
			reply.success = (bytes > 0 || job->op == IoOp::Event);
			reply.eventMask = job->commMask; // Valid if op == Event

			job->replyQueue->S_Push(&reply);

			// For Serial, we usually delete Write jobs here.
			// Read and Event jobs are often 're-fired' or handled by Logic.
			if (job->op == IoOp::Write) delete job;
		}
	}
	return 0;
}

static void SerialService_Demo() {
	Y_QueueSS<IOReply> replyQueue;
	replyQueue.Create(128);

	HANDLE hIOCP = CreateIoCompletionPort(INVALID_HANDLE_VALUE, NULL, 0, 1);

	// 1. Open COM Port (Use \\.\COM# for numbers > 9)
	HANDLE hComm = CreateFileA("\\\\.\\COM3", GENERIC_READ | GENERIC_WRITE, 0, NULL,
		OPEN_EXISTING, FILE_FLAG_OVERLAPPED, NULL);

	if (hComm == INVALID_HANDLE_VALUE) return;
	CreateIoCompletionPort(hComm, hIOCP, 1, 0);

	// 2. Set the mask for WaitCommEvent (e.g., Error and RX Character)
	SetCommMask(hComm, EV_ERR | EV_RXCHAR);

	HANDLE hThread = CreateThread(NULL, 0, IoWorker, hIOCP, 0, NULL);

	// --- FIRE WAIT COM EVENT ---
	// This tells the OS: "Let me know when an error or character happens"
	FileJob* eJob = new FileJob();
	ZeroMemory(&eJob->ov, sizeof(OVERLAPPED));
	eJob->requestId = 777;
	eJob->op = IoOp::Event;
	eJob->replyQueue = &replyQueue;
	WaitCommEvent(hComm, &eJob->commMask, &eJob->ov);

	// --- FIRE ASYNC WRITE ---
	FileJob* wJob = new FileJob();
	ZeroMemory(&wJob->ov, sizeof(OVERLAPPED));
	wJob->requestId = 888;
	wJob->op = IoOp::Write;
	wJob->replyQueue = &replyQueue;
	strcpy_s(wJob->data, "AT\r\n");
	WriteFile(hComm, wJob->data, 4, NULL, &wJob->ov);

	// --- FIRE ASYNC READ ---
	FileJob* rJob = new FileJob();
	ZeroMemory(&rJob->ov, sizeof(OVERLAPPED));
	rJob->requestId = 999;
	rJob->op = IoOp::Read;
	rJob->replyQueue = &replyQueue;
	ReadFile(hComm, rJob->data, 1024, NULL, &rJob->ov);

	// Logic Loop
	bool running = true;
	while (running) {
		IOReply reply;
		if (replyQueue.S_Pull(&reply) == Y_Rx_e::Success) {
			if (reply.op == IoOp::Event) {
				if (reply.eventMask & EV_ERR) std::cout << "COM ERROR detected!\n";
				if (reply.eventMask & EV_RXCHAR) std::cout << "Data is ready to be read!\n";

				// CRITICAL: You must re-fire WaitCommEvent to keep monitoring
				ZeroMemory(&eJob->ov, sizeof(OVERLAPPED));
				WaitCommEvent(hComm, &eJob->commMask, &eJob->ov);
			}

			if (reply.op == IoOp::Read) {
				std::cout << "Read " << reply.bytes << " bytes.\n";
				// Re-fire ReadFile for continuous reading
				ZeroMemory(&rJob->ov, sizeof(OVERLAPPED));
				ReadFile(hComm, rJob->data, 1024, NULL, &rJob->ov);
			}
		}
		Sleep(1);
	}

	// Cleanup omitted for brevity
}
#endif
