//
#if 0

#pragma once

#include "pch.h"

#include "Compile.h"

#include "Y_EventMM.h"
#include "Y_QueueMM.h"
#include "Y_QueueSS.h"
#include "Y_PoolFairMM.h"

#include "LogService.h"
#include "ExcelService.h"

enum class DeviceReadService_MsgIn_e : uint8_t {
	nul = 0,
	End,
};

struct DeviceReadService_MsgIn {
	DeviceReadService_MsgIn_e type = DeviceReadService_MsgIn_e::nul;

	union u {
		 u() { /* nop */ }
		~u() { /* nop */ }
	} as;
};

struct DeviceReadService_ProducedDataBuffer {
	std::vector<uint8_t> data;
};

struct DeviceReadService_ProducedDataBuffer_Rental {
	Y_PoolRental<DeviceReadService_ProducedDataBuffer> rental;
};

struct DeviceReadService {
	std::thread thread;
	Y_EventMM qi_produce_event;
	Y_QueueMM<DeviceReadService_MsgIn> qi;

	Y_PoolFairMM<DeviceReadService_ProducedDataBuffer> qo_pool;
	Y_QueueSS<DeviceReadService_ProducedDataBuffer_Rental> qo;

	HANDLE com_port_h = INVALID_HANDLE_VALUE;
	Logger log = null;
};

intptr_t Thread_DeviceReadService(void* _);

inl bool DeviceReadService_Create(DeviceReadService* _) {
	ZoneScoped;
	if(!Audit(_->qi.Create(512))) {
		return false;
	}

	uint32_t qo_pool_capacity = 64;
	if(!Audit(_->qo_pool.Create_AndConstructEach(qo_pool_capacity))) {
		;
	}

	if(!Audit(_->qo.Create(qo_pool_capacity))) {
		return false;
	}

	return true;
}
inl void DeviceReadService_Destroy(DeviceReadService* _) {
	ZoneScoped;

	// here's the thing, 


	_->qi.Destroy();
}
inl void DeviceReadService_Begin(DeviceReadService* _, Logger log) {
	ZoneScoped;
	_->log = log;
	_->thread = std::thread(Thread_DeviceReadService, _);
}
inl bool DeviceReadService_SignalEnd(DeviceReadService* _) {
	ZoneScoped;

	DeviceReadService_MsgIn si;
	si.type = DeviceReadService_MsgIn_e::End;

	_->qi.ProduceOne_OrYieldAndRetryForever(&si);
	_->qi_produce_event.Signal_One();

	return true;
}
inl void DeviceReadService_WaitForEnd(DeviceReadService* _) {
	ZoneScoped;
	_->thread.join();
}
#endif
