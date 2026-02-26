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
		typename std::aligned_storage<sizeof(T), kCacheLineSize>::type storage;

		/* prevent adjacent allocations from sharing cachelines */
		uint8_t _padding[kCacheLineSize - sizeof(T)];

		Entry() {
			Assure_AtCompileTime(0 == offset_of(Entry, storage));
			Assure_AtCompileTime(kCacheLineSize == sizeof(Entry));
			Assure_AtCompileTime(kCacheLineSize == alignof(Entry));
			Assure_AtCompileTime(kCacheLineSize % alignof(T) == 0);
		}
	};

	uint32_t m_entries_capacity = 0;
	Entry* m_entries = null;

	alignas(kCacheLineSize) std::atomic<uint64_t> m_write_i_a { 0 };
	alignas(kCacheLineSize) uint64_t m_produce_cached_read_i = 0;

	alignas(kCacheLineSize) std::atomic<uint64_t> m_read_i_a { 0 };
	alignas(kCacheLineSize) uint64_t m_consume_cached_write_i = 0;

	/* prevent adjacent allocations from sharing cachelines */
	uint8_t _padding[kCacheLineSize - sizeof(m_consume_cached_write_i)];

	Y_QueueSS() { }

	~Y_QueueSS() {
		Assure(!m_entries, "Did you forget to call Destroy()?");
	}

	/* Must be called before S_* without racing ! */
	bool Create(const uint32_t arg_entries_capacity) {
		Task_ZoneScoped_NoCallstack;

		Assure(arg_entries_capacity);
		Assure(arg_entries_capacity < 4*1000*1000);

		Assure(!m_entries);
		Assure(!m_entries_capacity);

		/* allocate one extra entry to prevent false sharing with adjacent memory. */
		T* entries = null;
		if(!Assure_True(Basic_ArrayPointer_New(entries, (1 + arg_entries_capacity)))) {
			return false;
		}

		m_entries_capacity = arg_entries_capacity;
		m_entries = entries;

		m_write_i_a.store(0, std::memory_order_relaxed);
		m_read_i_a.store(0, std::memory_order_relaxed);

		MemclearC(entries, arg_entries_capacity*sizeof(entries[0]));
		return true;
	}

	/* note: can be spurious due to relaxed loads! */
	intptr_t Count() {
		Task_ZoneScoped_NoCallstack;

		const intptr_t read_i = cast(intptr_t)m_read_i_a.load(std::memory_order_relaxed);
		const intptr_t write_i = cast(intptr_t)m_write_i_a.load(std::memory_order_relaxed);

		const intptr_t delta = (write_i - read_i);
		if(delta < 0) {
			delta += m_entries_capacity;
		}
		return delta;
	}

	void Destroy() {
		Task_ZoneScoped_NoCallstack;

		Assure(Count() <= 0);

		Basic_ArrayPointer_Delete(m_entries);
		m_entries_capacity = 0;
		m_write_i_a = 0;
		m_consume_cached_write_i = 0;
		m_read_i_a = 0;
		m_produce_cached_read_i = 0;
	}

	// todo: produce multiple! in that case, we basically avoid the store(release)
	// note: contention is impossible.
	Y_Tx_e S_Produce_Tx(const T* in) {
		Task_ZoneScoped_NoCallstack;

		if(!Assure_True(in)) {
			return Y_Tx_e::Success;
		}

		/* relaxed: order: none
		//          fence: none
		*/
		const uint64_t write_i = m_write_i_a.load(std::memory_order_relaxed);
		const uint64_t next_i = (write_i + 1);

		/* wrap without using % */
		if(m_entries_capacity <= next_i) {
			Assure(m_entries_capacity == next_i);
			next_i = 0;
		}

		if(m_produce_cached_read_i == next_i) {
			/*
			// acquire: order: reads/writes cannot move above operation.
			//          fence: receives entry data. (but why? spurious avoidance?)
			*/
			m_produce_cached_read_i = m_read_i_a.load(std::memory_order_acquire);
			if(m_produce_cached_read_i == next_i) {
				Assure(false);
				return Y_Tx_e::Full;
			}
		}

		Memcpy(m_entries[write_i].storage.mCharData, in, sizeof(T));

		/*
		// release: order: reads/writes cannot be moved below operation.
		//          fence: Tx entry data.
		*/
		m_write_i_a.store(next_i, std::memory_order_release);
		return Y_Tx_e::Success;
	}

	// todo: consume multiple!
	// note: contention is impossible.
	Y_Rx_e S_Consume_Rx(T* out) {
		Task_ZoneScoped_NoCallstack;

		if(!Assure_True(out)) {
			return Y_Rx_e::Success;
		}

		/*
		// relaxed: order: none
		//          fence: none
		*/
		const uint64_t read_i = m_read_i_a.load(std::memory_order_relaxed);
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

		Memcpy(out, m_entries[read_i].storage.mCharData, sizeof(T));

		const uint64_t next_i = (read_i + 1);
		/* wrap without using % */
		if(m_entries_capacity <= next_i) {
			Assure(m_entries_capacity == next_i);
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
