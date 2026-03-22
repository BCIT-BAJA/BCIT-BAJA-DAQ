//

#include "pch.h"
#include "IOService.h"

bool OS_FileExists(const char* szPath) {
	DWORD dwAttrib = GetFileAttributesA(szPath);

	return (dwAttrib != INVALID_FILE_ATTRIBUTES &&
		!(dwAttrib & FILE_ATTRIBUTE_DIRECTORY));
}

bool OS_GetLastModified_AsString(const char* filename, SYSTEMTIME* out_st) {
	WIN32_FILE_ATTRIBUTE_DATA fileInfo;

	// 1. Get file attributes including timestamps
	if (GetFileAttributesExA(filename, GetFileExInfoStandard, &fileInfo)) {

		// 2. Convert FILETIME to SYSTEMTIME (UTC)
		SYSTEMTIME stUTC;
		if(!Test_True(FileTimeToSystemTime(&fileInfo.ftLastWriteTime, &stUTC))) {
			return false;
		}

		// 3. Convert UTC to Local Time (optional but usually preferred)
		if(!Test_True(SystemTimeToTzSpecificLocalTime(NULL, &stUTC, out_st))) {
			return false;
		}

		return true;
	}

	return false;
}

intptr_t Thread_IOService(void* _) {
	Basic_SetThreadName("IOService");

#if c_config(debug)
	puts(MACRO_FunctionSignature());
	Defer(puts(MACRO_FunctionSignature()));
#endif

	IOService* self = cast(IOService*)_;
	Log_BindThreadLocal(self->log);

	/*
	// GetQueuedCompletionStatus
	// if the port handle associated with it is closed while the call is outstanding,
	// the function returns FALSE,
	// *lpOverlapped will be NULL, 
	// GetLastError will return ERROR_ABANDONED_WAIT_0
	*/

	HANDLE iocp_h = self->iocp_h;
	DWORD NumberOfBytesTransferred = 0;
	ULONG_PTR CompletionKey = 0;
	LPOVERLAPPED lpOverlapped = nullptr;
	while(Test_True(GetQueuedCompletionStatus(
		iocp_h, // CompletionPort
		&NumberOfBytesTransferred, // lpNumberOfBytesTransferred
		&CompletionKey, // lpCompletionKey
		&lpOverlapped, // lpOverlapped
		INFINITE // dwMilliseconds
	))) {
		/*
		// If *lpOverlapped is NULL,
		// the function did not dequeue a completion packet from the completion port.
		// In this case, the function does not store information
		// in the variables pointed to by the
		// lpNumberOfBytes and lpCompletionKey parameters,
		// and their values are indeterminate.
		*/
		if(!lpOverlapped || CompletionKey == 0) {
			// PostQueuedCompletionStatus
			break;
		}

		/*
		//
		// If *lpOverlapped is not NULL and the function dequeues a completion packet
		// for a failed I/O operation from the completion port, the function stores
		// information about the failed operation in the variables
		// pointed to by lpNumberOfBytes, lpCompletionKey, and lpOverlapped.
		// To get extended error information, call GetLastError.
		*/

		Assert_True(lpOverlapped);
		Assert_True(CompletionKey);
		Ensure_TrueAtCompileTime(0 == STRUCT_MemberAddress(IOService_IORequest, Overlapped));
		IOService_IORequest* request = cast(IOService_IORequest*)lpOverlapped;
		request->Overlapped;

		if(Test_True(request->reply_q)) {
			IOService_IOReply reply;
			reply.request_type = request->type;
			reply.request_tag = request->tag;
			reply.NumberOfBytesTransferred = NumberOfBytesTransferred;
			request->reply_q->S_Push(&reply);
		}
	}

	Assert_True(GetLastError() != ERROR_ABANDONED_WAIT_0);

	return 0;
}

Y_QueueSS_WithBlockingPull<IOService_IOReply> g_replies[64];
Y_IndexRentalMM_Frugal<Array_CountOf(g_replies)> g_replies_index_rental;
ThreadLocal IOClient* tls_client = null;

