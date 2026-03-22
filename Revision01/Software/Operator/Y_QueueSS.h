//

#pragma once

#include "Y.h"

#include "Basic.h"

/*
// Adapted from rigtorp's SPSCQueue
// https://github.com/rigtorp/MPMCQueue
//
// see: https://rigtorp.se/ringbuffer/
*/

/* sYnc QUEUE, Single to Single */
template<typename T>
struct Y_QueueSS {
	struct Entry {
		/*
		// aligned storage for your T, without the hassle of constructors and destructors.
		// your T is Memcpy'd to and from this storage space.
		*/
		alignas(kCacheLineSize) uint8_t storage[sizeof(T)];

		/* prevent adjacent allocations from sharing cachelines */
		uint8_t _padding[kCacheLineSize - sizeof(T)];

		Entry() {
			Ensure_TrueAtCompileTime(0 == offset_of(Entry, storage));
			Ensure_TrueAtCompileTime(kCacheLineSize == sizeof(Entry));
			Ensure_TrueAtCompileTime(kCacheLineSize == alignof(Entry));
			Ensure_TrueAtCompileTime(kCacheLineSize % alignof(T) == 0);
			Ensure_TrueAtCompileTime(sizeof(T) < kCacheLineSize);
		}
	};

	uint32_t m_entries_capacity = 0;
	Entry* m_entries = null;

	alignas(kCacheLineSize) std::atomic<int64_t> m_write_i_a { 0 };
	alignas(kCacheLineSize) int64_t m_produce_cached_read_i = 0;

	alignas(kCacheLineSize) std::atomic<int64_t> m_read_i_a { 0 };
	alignas(kCacheLineSize) int64_t m_consume_cached_write_i = 0;
	/* prevent adjacent allocations from sharing cachelines */
	uint8_t _padding[kCacheLineSize - sizeof(m_consume_cached_write_i)];

	Y_QueueSS() {
		Ensure_TrueAtCompileTime(alignof(Y_QueueSS) == kCacheLineSize);
		Ensure_TrueAtCompileTime(sizeof(Y_QueueSS) % kCacheLineSize == 0);
	}

	~Y_QueueSS() {
		Assert_True(!m_entries, "Did you forget to call Destroy()?");
	}

	/* Must be called before S_* without racing ! */
	bool Create(const uint32_t arg_entries_capacity) {
		Task_ZoneScoped_NoCallstack;

		Assert_True(arg_entries_capacity);
		Assert_True(arg_entries_capacity < 4*1000*1000);

		Assert_True(!m_entries);
		Assert_True(!m_entries_capacity);

		/* allocate one extra entry to prevent false sharing with adjacent memory. */
		Entry* entries = null;
		if(!Test_True(Basic_ArrayPointer_New(entries, (1 + arg_entries_capacity)))) {
			return false;
		}

		m_entries_capacity = arg_entries_capacity;
		m_entries = entries;

		m_write_i_a.store(0, std::memory_order_relaxed);
		m_read_i_a.store(0, std::memory_order_relaxed);

		Memclear(entries, arg_entries_capacity*sizeof(entries[0]));
		return true;
	}

	void Destroy() {
		Task_ZoneScoped_NoCallstack;

		Basic_ArrayPointer_Delete_NullSafe(m_entries);
		m_entries_capacity = 0;
		m_write_i_a = 0;
		m_consume_cached_write_i = 0;
		m_read_i_a = 0;
		m_produce_cached_read_i = 0;
	}

	// note: contention is impossible.
	Y_Tx_e S_Push(const T* in) {
		Task_ZoneScoped_NoCallstack;

		if(!Test_True(in)) {
			return Y_Tx_e::Success;
		}

		/* relaxed: order: none
		//          fence: none
		*/
		const int64_t write_i = m_write_i_a.load(std::memory_order_relaxed);
		int64_t next_i = (write_i + 1);

		/* wrap without using % */
		if(m_entries_capacity <= next_i) {
			Assert_True(m_entries_capacity == next_i);
			next_i = 0;
			Assert_True(0 == next_i);
		}

		if(m_produce_cached_read_i == next_i) {
			/*
			// acquire: order: reads/writes cannot move above operation.
			//          fence: receives entry data. (but why? spurious avoidance?)
			*/
			m_produce_cached_read_i = m_read_i_a.load(std::memory_order_acquire);
			if(!Test_True(m_produce_cached_read_i != next_i)) {
				return Y_Tx_e::Full;
			}
		}

		Memcpy(m_entries[write_i].storage, in, sizeof(T));

		/*
		// release: order: reads/writes cannot be moved below operation.
		//          fence: Tx entry data.
		*/
		m_write_i_a.store(next_i, std::memory_order_release);
		return Y_Tx_e::Success;
	}

	// note: contention is impossible.
	Y_Rx_e S_Pull(T* out) {
		Task_ZoneScoped_NoCallstack;

		if(!Test_True(out)) {
			return Y_Rx_e::Success;
		}

		/*
		// relaxed: order: none
		//          fence: none
		*/
		const int64_t read_i = m_read_i_a.load(std::memory_order_relaxed);
		if(m_consume_cached_write_i == read_i) {
			/*
			// acquire: order: reads/writes cannot move above operation.
			//          fence: Rx receives entry data.
			*/
			m_consume_cached_write_i = m_write_i_a.load(std::memory_order_acquire);
			if(m_consume_cached_write_i == read_i) {
				return Y_Rx_e::Empty;
			}
		}

		Memcpy(out, m_entries[read_i].storage, sizeof(T));

		int64_t next_i = (read_i + 1);
		/* wrap without using % */
		if(m_entries_capacity <= next_i) {
			Assert_True(m_entries_capacity == next_i);
			next_i = 0;
		}

		/*
		// release: order: reads/writes cannot move below operation.
		//          fence: (null) (but why release? shouldn't it be Simplex?)
		*/
		m_read_i_a.store(next_i, std::memory_order_release);
		return Y_Rx_e::Success;
	}
};

#include "OS_AddressEvent.h"
template<typename T>
struct Y_QueueSS_WithBlockingPull {
	Y_QueueSS<T> q;
	bool Create(const uint32_t arg_entries_capacity) {
		return q.Create(arg_entries_capacity);
	}

	void Destroy() {
		q.Destroy();
	}

	Y_Rx_e S_Pull_Blocking(T* out, uint32_t timeout_ms = Timeout32_e::Infinite) {
		Task_ZoneScoped_NoCallstack;

		// 1. First, try a standard non-blocking pull (Fast Path)
		{
			Y_Rx_e result = q.S_Pull(out);
			if(result != Y_Rx_e::Empty) {
				return result;
			}
		}

		// 2. If empty, prepare to wait on the address of the write index
		// We need a local copy of what we THINK the value is. 
		// WaitOnAddress only sleeps if the current value matches this 'undesired' value.
		int64_t current_read_i = q.m_consume_cached_write_i;

		// We wait while the write index is still equal to our current read index (meaning empty)
		while(q.m_write_i_a.load(std::memory_order_relaxed) == current_read_i) {

			// WaitOnAddress( AddressToWatch, CompareValueAddr, Size, Timeout )
			DWORD dwMilliseconds;
			if(timeout_ms == Timeout32_e::Infinite) {
				dwMilliseconds = INFINITE;
			} else if(timeout_ms == Timeout32_e::ASAP) {
				dwMilliseconds = 0;
			} else {
				dwMilliseconds = timeout_ms;
			}

			if(!WaitOnAddress(
				&q.m_write_i_a,
				&current_read_i,
				sizeof(q.m_write_i_a),
				dwMilliseconds
			)) { 
				if(Test_True(GetLastError() == ERROR_TIMEOUT)) {
					return Y_Rx_e::Empty;
				}
			}
		}

		// 3. The write pointer changed! Now pull for real.
		Y_Rx_e result = q.S_Pull(out);
		Assert_True(result == Y_Rx_e::Success);
		return result;
	}

	Y_Tx_e S_Push(const T* in) {
		Y_Tx_e result = q.S_Push(in);
		if(result == Y_Tx_e::Success) {
			WakeByAddressSingle(&q.m_write_i_a);
		}
		return result;
	}
};
