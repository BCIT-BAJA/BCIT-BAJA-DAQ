//

// Feb 8
// x 30 minutes Get the assertion handler for Windows working
// x 30 minutes Get Mutexes, Atomics, Event queues

// Feb 9
// - 30 minutes Basic packet definition(fake "1khz" data in windows written to.xlsx)
// - 30 minutes Basic UI(just use a single line printf() fflush(stdout) with summary statistics I guess)
// - Stretch Goal : Opening up the serial port and moving the fake data generation to the STM32

// integrate a nice terminal command library like https://github.com/jart/bestline/tree/master
// Audit.h, Basic.h, version.h, etc, all boilerplate
// write assertions (assure, tru) for STM32.
// then define Protocol.h for a packet containing 10kbps 1khz plain-old-data
// integrate the log.h function with threads / message queues (probably requires it tbh for separating logging & input & file writing anyway)
// 
//

// low priority todos
// fix: >LINK : warning LNK4098: defaultlib 'MSVCRT' conflicts with use of other libs; use /NODEFAULTLIB:library 

//
// scan for serial ports
// open serial port
// initiate contact
// use libxlsxwriter to log data
//

#include "PCH.h"

#include "Version.h"

#include "Audit.h"

#define Protocol_Implmentation
#include "Protocol.h"

#define StateMachine_Implementation
#include "StateMachine.h"

#include "Y.h"
#include "Y_EventMM.h"
#include "Y_PoolFairMM.h"
#include "Y_PoolFrugalMM.h"
#include "Y_QueueMM.h"
#include "Y_QueueSS.h"
#include "Y_RWLockMM.h"

#include "OS_AddressEvent.h"
#include "OS_Signal.h"

#include "LogService.h"
#include "DataService.h"
#include "ExcelService.h"
#include "LogService.h"
#include "DeviceService.h"

int main()
{
	printf(Version_StringLiteral() "\n"); 
	fflush(stdout);

	// StateMachine_Demo();
	Audit_Demo();

	LogService log;
	ExcelService excel;
	DeviceService device;

	if(!Test_True(LogService_Create(&log))) { return __LINE__; }
	if(!Test_True(ExcelService_Create(&excel))) { return __LINE__; }
	if(!Test_True(DeviceService_Create(&device))) { return __LINE__; }

	Log("Hello, world!\n");

	Defer(LogService_Destroy(&log));
	Defer(ExcelService_Destroy(&excel));
	Defer(DeviceService_Destroy(&device));

	LogService_Begin(&log);
	ExcelService_Begin(&excel, &log);
	DeviceService_Begin(&device, &log, &excel);

	Defer(
		DeviceService_SignalEnd(&device);
		DeviceService_WaitForEnd(&device);

		ExcelService_SignalEnd(&excel);
		ExcelService_WaitForEnd(&excel);

		LogService_SignalEnd(&log);
		LogService_WaitForEnd(&log);
	);

	bool run = true;
	while(run) {
		if(_kbhit()) {
			const char ch = _getch();
			if(ch == 27) {
				run = false;
			}
		}

		Sleep(10);
	}

	return 0;
}