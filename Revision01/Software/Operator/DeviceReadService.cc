//

#include "pch.h"

#if 0
#include "DeviceReadService.h"
#include "StateMachine.h"

using namespace std;

// http://www.flounder.com/serial.htm
// Another problem I see is people trying to do too much at once.  Using a single thread to do both input and output results in code that is far too convoluted.  Such code is difficult to create, debug, or even reason about successfully and should be avoided. 
// Using separate threads for input and output results in cleaner code, with a nice separation of concerns.

Enum(DeviceReadService_State, StateMachine_StateType) {
	DeviceReadService_State_Default = 0,
};

Struct(DeviceReadService_StateMachineData) {
};

static void DeviceReadService_StateMachine(DeviceReadService* _, StateMachine* sm) {
	Logger l = _->log;
}

intptr_t Thread_DeviceService(void* _service) {
	Basic_SetThreadName("DeviceService");

#if c_config(debug)
	puts(MACRO_FunctionSignature());
	defer(puts(MACRO_FunctionSignature()));
#endif

	DeviceReadService* self = cast(DeviceReadService*)_service;
	Logger l = self->log;

	Y_QueueMM<DeviceReadService_MsgIn>::Consumer qi_consumer = self->qi.Consumer_Rent();
	defer(self->qi.Consumer_Return(&qi_consumer));

	DeviceReadService_MsgIn mi;
	TracyCZoneEnd(init);

	DeviceReadService_StateMachineData sm_data;
	StateMachine sm;
	sm.user_data = &sm_data;

	while(true) {
		bool sm_state_success = true;
		StateMachine_StateType sm_state = StateMachine_PeekState(&sm, &sm_state_success);
		Assure(sm_state_success);

		uint32_t message_queue_timeout_us = 0;
		if(sm_state == DeviceReadService_State_Default) {
			message_queue_timeout_us = 1000*1000;
		}

		Y_Rx_e mi_pull = Y_Rx_e::Empty;
		self->qi_produce_event.AwaitSignalUntil(
			message_queue_timeout_us,
			[self, &qi_consumer, &mi, &mi_pull]() {
			return (Y_Rx_e::Empty != (mi_pull = qi_consumer.Pull_Rx(&mi)));
		});

		if(mi_pull == Y_Rx_e::Empty) {
			/* event queue timed out, run the (blocking) state machine. */
			DeviceReadService_StateMachine(self, &sm);
		} else if(mi_pull == Y_Rx_e::Success) {
			switch(mi.type) {
				case DeviceReadService_MsgIn_e::End: {
					/* note: race: other incoming messages are lost */
					goto end;
				} break;

				default: AssureTrue(false, "unknown message type %d", mi.type);
			}
		}
	}
	end:;

	return 0;
}

#endif
