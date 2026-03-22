//

//
// Same USB Port : Windows uses the device's VID (Vendor ID), PID (Product ID), and Serial Number to remember it. If those match, it usually reassigns the old COM number.
// This was true for our hardware:
// - even if unplugged to another USB port, the COM number stayed the same. 
// - even if a second unique hardware was plugged in, it got a new COM number, and the old one stayed the same.
//
// Also, it's 80% likely that the user will only have one serial device plugged in, so we can just use the first one we find.
// If they have multiple, they can specify which one to use by name (COM3, COM4, etc).
// and then launch the Service thread with that particular COM port.
//
// Once a COM port has either been scanned or specified, the Service thread should be a state machine that tries to open the COM port, and if it fails, it waits for a little bit and tries again. Once it succeeds, it can start reading data from the COM port and processing it as needed. It is optimistic in that way. So that plugging and unplugging is robust.
//
// The data producer has to go somewhere... where should it go? Well, if the actual data itself is gibberish, for simplicity, the DeviceService should probably figure this out and generate some user readable error, that the device is misbehaving. If it is misbehaving, perhaps it should release the COM port and scan for the next ?
// What I'm really asking here is should the DeviceService output structured data as part of the Protocol, or simply provide a stream of data that could be gibberish?
// My answer is that the DeviceService must talk the device specific protocol, and thus output structured, post parse data.
// Ie, it must negotiate the Protocol.
//
// Signal the device to reply its version, status/state, uptime, connection time, connection signal strength. Perhaps this data includes the second wireless transmitter device, which it has already negotiated with via the state.
//
// then, structured data is sent to the DataService thread.
//
// Listen for constant pushed incoming data.
// Listen for state changes.
//
//
//
// (Perhaps the magic pushed bytes could contain version information.)
//
// Ultimately these Events and Pushes are sent via in-memory Queues to subscribers as structured data:
// // including the data processing thread which applies filtering & statistics,
// // including the .xlsx writer thread.
// // (and the main UI thread may subscribe to device events only?)
//
// Todo: How should the data processing thread behave?
// Todo: How should the .xlsx writer thread behave?
// Todo: How should the main UI thread behave?
// Consider how to answer these questions by iterating all possible events that can occur, and their necessary state changes...
// Todo: Consider that the interaction between threads may pose a problem, like backlogging, error handling, etc. Each are Real Time Threads that must handle their own queues, etc... :)
//

#pragma once

#include "pch.h"

#include "Compile.h"

#include "Y_QueueMM.h"
#include "Y_EventMM.h"

#include "LogService.h"
#include "IOService.h"

enum class DeviceService_MsgIn_e : uint8_t {
	nul = 0,
	End,
};

struct DeviceService_MsgIn {
	DeviceService_MsgIn_e type = DeviceService_MsgIn_e::nul;

	union u {
	#if 0
		struct {
			std::string object;
		} String;
	#endif

		 u() { /* nop */ }
		~u() { /* nop */ }
	} as;

	#if 0
	inl void Construct_String() {
		Assure(type == DeviceService_MsgIn_e::nul);
		type = DeviceService_MsgIn_e::String;
		new (&as.String.object) std::string;
	}
	#endif
};

struct DeviceService {
	std::thread thread;
	Y_EventMM qi_produce_event;
	Y_QueueMM<DeviceService_MsgIn> qi;

	Logger log = null;
};

intptr_t Thread_DeviceService(void* _);

Inline bool DeviceService_Create(DeviceService* _) {
	ZoneScoped;
	if(!Test_True(_->qi.Create(512))) {
		return false;
	}
	return true;
}
Inline void DeviceService_Destroy(DeviceService* _) {
	ZoneScoped;
	_->qi.Destroy();
}
Inline void DeviceService_Begin(DeviceService* _, Logger log) {
	ZoneScoped;
	_->log = log;
	_->thread = std::thread(Thread_DeviceService, _);
}
Inline bool DeviceService_SignalEnd(DeviceService* _) {
	ZoneScoped;

	DeviceService_MsgIn si;
	si.type = DeviceService_MsgIn_e::End;

	_->qi.ProduceOne_OrYieldAndRetryForever(&si);
	_->qi_produce_event.Signal_One();

	return true;
}
Inline void DeviceService_WaitForEnd(DeviceService* _) {
	ZoneScoped;
	_->thread.join();
}
