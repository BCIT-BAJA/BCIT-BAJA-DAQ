//

#pragma once

#include "pch.h"

#include "Compile.h"

#include "Y_QueueMM.h"
#include "Y_EventMM.h"

#include "LogService.h"

enum class ExcelService_MsgIn_e : uint8_t {
	nul = 0,
	End,
	Data,
};

struct ExcelService_MsgIn {
	ExcelService_MsgIn_e type = ExcelService_MsgIn_e::nul;

	// todo: I don't really like this design, this is a constant memory alloc/free churn.
	//       It would be better to have a ciruclar buffer of 10 seconds of data, say.
	union u {
		struct {
			std::vector<uint16_t> vec;
		} Data;

		 u() { /* nop */ }
		~u() { /* nop */ }
	} as;

	Inline void Construct_Data(const std::vector<uint16_t>& _) {
		Assert_True(type == ExcelService_MsgIn_e::nul);
		type = ExcelService_MsgIn_e::Data;
		new (&as.Data.vec) type_of(as.Data.vec)(_);
	}
};

struct ExcelService {
	std::thread thread;
	Y_EventMM qi_produce_event;
	Y_QueueMM<ExcelService_MsgIn> qi;

	Logger log;
};

intptr_t Thread_ExcelService(void* _);

Inline bool ExcelService_Create(ExcelService* _) {
	ZoneScoped;
	if(!Test_True(_->qi.Create(512))) {
		return false;
	}
	return true;
}
Inline void ExcelService_Destroy(ExcelService* _) {
	ZoneScoped;
	_->qi.Destroy();
}
Inline void ExcelService_Begin(ExcelService* _, Logger l) {
	ZoneScoped;
	_->thread = std::thread(Thread_ExcelService, _);
	_->log = l;
}
Inline bool ExcelService_SignalEnd(ExcelService* _) {
	ZoneScoped;

	ExcelService_MsgIn si;
	si.type = ExcelService_MsgIn_e::End;

	_->qi.ProduceOne_OrYieldAndRetryForever(&si);
	_->qi_produce_event.Signal_One();

	return true;
}
Inline void ExcelService_WaitForEnd(ExcelService* _) {
	ZoneScoped;
	_->thread.join();
}

void ExcelService_PublishData(ExcelService* _, const std::vector<uint16_t>& data);
