//

// Feb 8
// x 30 minutes Get the assertion handler for Windows working
// x 30 minutes Get Mutexes, Atomics, Event queues

// Feb 9
// - 30 minutes Basic packet definition(fake "1khz" data in windows written to.xlsx)
// - 30 minutes Basic UI(just use a single line printf() fflush(stdout) with summary statistics I guess)
// - Stretch Goal : Opening up the serial port and moving the fake data generation to the STM32

// integrate a nice terminal command library like https://github.com/jart/bestline/tree/master
// Assure.h, Basic.h, version.h, etc, all boilerplate
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

#define assure_implementation
#include "Assure.h"

#define Protocol_Implmentation
#include "Protocol.h"

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

	LogService log;
	ExcelService excel;

	if(!tru(LogService_Create(&log))) { return __LINE__; }
	if(!tru(ExcelService_Create(&excel))) { return __LINE__; }

	defer(LogService_Destroy(&log));
	defer(ExcelService_Destroy(&excel));

	LogService_Begin(&log);
	ExcelService_Begin(&excel, &log);

	defer(
		LogService_SignalEnd(&log);
		ExcelService_SignalEnd(&excel);

		LogService_WaitForEnd(&log);
		ExcelService_WaitForEnd(&excel);
	);

	std::vector<uint16_t> data; // roughly 2khz
	for(uint32_t dat_i = 0; dat_i < 200; ++dat_i) {
		data.push_back(cast(uint16_t)dat_i);
	}

	bool run = true;
	while(run) {
		if(_kbhit()) {
			const char ch = _getch();
			if(ch == 27) {
				run = false;
			}
		}

		ExcelService_PublishData(&excel, data);
		Sleep(10);
	}

	return 0;
}