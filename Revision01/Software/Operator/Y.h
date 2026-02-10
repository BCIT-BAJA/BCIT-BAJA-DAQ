//

#pragma once

/* sYnc conventions
// SS: Single Producer to Single Consumer
// MM: Many Producers to Many Consumers
//
// S_: A Single thread may call this function at one time.
// M_: Many threads may call this function at any time.
// (No prefix): Defaults to Lx.
//
// Tx: Transmit / releases memory fence
// Rx: Receive / acquires memory fence
// Lx: (Re)Laxed / no memory fence. do not rely upon to synchronize reads/writes external to function.
// 
// Queue: Push to, Pull from.
// Pool: Rent from, Return to.
*/

// todo: thread begin, thread end would be a useful primitive!.

#include "Basic.h"

inline void Y_Thread_Yield() {
	Task_ZoneScoped_NoCallstack;
#if c_os(windows)
	SwitchToThread();
#else
	std::this_thread::yield();
#endif
}

typedef std::thread::id Y_ThreadId;
inline Y_ThreadId Y_ThreadId_Get() {
	Y_ThreadId id;
	id = std::this_thread::get_id();
	return id;
}

enum class Y_Tx_e : uint32_t {
	Success = 0,
	Contention,
	Full,
};

enum class Y_Rx_e : uint32_t {
	Success = 0,
	Contention,
	Empty,
};

template<typename T>
union Y_PoolRental {
	struct {
		void* key;
		uint64_t key_epoch;

		T value;
	};

	Y_PoolRental() {
		key = 0;
		key_epoch = 0;
		MemclearC(&value, sizeof(T));
		/* omit T's constructor! */
	}

	~Y_PoolRental() {
		/* omit T's destructor! */
		assure(!key, "shouldn't you have returned me?");
	}
};
