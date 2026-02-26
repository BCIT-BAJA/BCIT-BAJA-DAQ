//

#pragma once

#include "pch.h"

#if 0
#include "Compile.h"

#include "Y_QueueMM.h"
#include "Y_EventMM.h"

std::vector<std::string> scanSerialPorts();
HANDLE openSerialPort(const std::string& portName, DWORD baudRate = CBR_115200);

#if 0
enum class SerialPort_MsgIn_e : uint8_t {
	nul = 0,
	End,
};

struct SerialPort_MsgIn {
	SerialPort_MsgIn_e type = SerialPort_MsgIn_e::nul;

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
		Assure(type == SerialPort_MsgIn_e::nul);
		type = SerialPort_MsgIn_e::String;
		new (&as.String.object) std::string;
	}
	#endif
};

struct DeviceService {
	std::thread thread;
	Y_EventMM qi_produce_event;
	Y_QueueMM<SerialPort_MsgIn> qi;
};

intptr_t Thread_DeviceService(void* _);

inl bool DeviceService_Create(DeviceService* _) {
	ZoneScoped;
	if(!Assure_True(_->qi.Create(512))) {
		return false;
	}
	return true;
}
inl void DeviceService_Destroy(DeviceService* _) {
	ZoneScoped;
	_->qi.Destroy();
}
inl void DeviceService_Begin(DeviceService* _) {
	ZoneScoped;
	_->thread = std::thread(Thread_DeviceService, _);
}
inl bool DeviceService_SignalEnd(DeviceService* _) {
	ZoneScoped;

	SerialPort_MsgIn si;
	si.type = SerialPort_MsgIn_e::End;

	Y_QueueMM<SerialPort_MsgIn>::Producer qi_producer = _->qi.Producer_Rent();
	defer(_->qi.Producer_Return(&qi_producer));
	while(qi_producer.Push_Tx(&si) != Y_Tx_e::Success) { Y_Thread_Yield(); } // note: todo: will lock up if full.
	_->qi_produce_event.Signal_One();

	return true;
}
inl void DeviceService_WaitForEnd(DeviceService* _) {
	ZoneScoped;
	_->thread.join();
}

typedef DeviceService* SerialPortger;
typedef SerialPort_MsgIn Txt;

void Txt_Fmt_(Txt* txt, const char* fmt, va_list va);
void Txt_Append(Txt* txt, const char* str);
void Txt_AppendFormat(Txt* txt, const char* fmt, ...);
void SerialPort_Txt(SerialPortger l, Txt* txt);

/*
// SerialPort <<< Printf ~1.3ms
// :)
*/
void SerialPort(SerialPortger l, const char* fmt, ...);
#endif
#endif