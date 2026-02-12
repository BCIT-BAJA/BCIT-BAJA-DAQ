//

#pragma once


/*
// see: https://concurrencyfreaks.blogspot.com/2013/09/distributed-cache-line-counter-scalable.html
*/
#if 0

// Use 0 for writer's "unlocked" and 1 for "locked" state
#define DCLC_RWL_UNLOCKED    0
#define DCLC_RWL_LOCKED      1

// Cache line optimization constants
#define DCLC_CACHE_LINE          64               // Size in bytes of a cache line
#define DCLC_CACHE_PADD          (DCLC_CACHE_LINE-sizeof(std::atomic<int>))
#define DCLC_NUMBER_OF_CORES     32
#define DCLC_HASH_RATIO           3
#define DCLC_COUNTERS_RATIO      (DCLC_HASH_RATIO*DCLC_CACHE_LINE/sizeof(int))
#endif

#include "Basic.h"

struct Y_RWLockMM {
	struct ReaderEntry {
		alignas(kCacheLineSize) std::atomic<uint64_t> count_a;

		/* prevent adjacent false sharing */
		uint8_t _padding[kCacheLineSize - sizeof(count_a)];
	};

	/* note: it's expected that readers may hash to the same entry. */
	uint32_t m_entries_capacity = 0;
	ReaderEntry* m_entries = null;

	alignas(kCacheLineSize) std::atomic<uint64_t> m_writer_a { 0 };
	uint8_t _padding[kCacheLineSize - sizeof(m_writer_a)];

	Y_RWLockMM() {
		Assure_AtCompileTime(2*kCacheLineSize == sizeof(Y_RWLockMM));
	}

	~Y_RWLockMM() {
		Assure(!m_entries, "did you forget to Destroy()?");
	}

	/* must call before M_*! */
	bool Create(const uint32_t arg_entries_capacity) {
		Task_ZoneScoped_NoCallstack;

		Assure(arg_entries_capacity);
		Assure(arg_entries_capacity < 64);

		Assure(!m_entries);
		Assure(!m_entries_capacity);

		/* we allocate one extra entry to prevent false sharing with adjacent memory. */
		if(!tru(Basic_ArrayPointer_New(m_entries, (1 + arg_entries_capacity)))) {
			return false;
		}

		m_entries_capacity = arg_entries_capacity;

		m_writer_a.store(0, std::memory_order_relaxed);

		MemClear_Explicit(m_entries, arg_entries_capacity*sizeof(m_entries[0]));
		return true;
	}

	void Destroy() {
		Task_ZoneScoped_NoCallstack;

		Basic_ArrayPointer_Delete(m_entries);
		m_entries_capacity = 0;
		m_writer_a.store(0, std::memory_order_relaxed);
	}

	bool M_Read_Lock_Rx(const uint64_t thread_hash) {
		Task_ZoneScoped_NoCallstack;

		Assure(m_entries);
		Assure(m_entries_capacity);

		ReaderEntry* entry = &m_entries[thread_hash % m_entries_capacity];
		entry->count_a.fetch_add(1, std::memory_order_relaxed);

		bool contended = m_writer_a.load(std::memory_order_relaxed) != 0;

		if(!contended) {
			/*
			// acquire: order: reads/writes cannot move above operation.
			// acquire: fence: Rx guarded external data from writer.
			*/
			contended = m_writer_a.load(std::memory_order_acquire) != 0;
		}

		if(contended) {
			entry->count_a.fetch_add(-1, std::memory_order_relaxed);
			return false;
		}

		return true;
	}

	void M_Read_Unlock_Lx(const uint64_t thread_hash) {
		Task_ZoneScoped_NoCallstack;

		Assure(m_entries);
		Assure(m_entries_capacity);

		ReaderEntry* entry = &m_entries[thread_hash % m_entries_capacity];
		const uint64_t c = entry->count_a.fetch_add(-1, std::memory_order_relaxed);
		Assure(c, "Unmatched Lock / Unlock!");
	}

	void M_Write_Lock_Await_Rx() {
		Task_ZoneScoped_NoCallstack;

		Assure(m_entries);
		Assure(m_entries_capacity);

		/*
		// (success)
		// acquire: order: reads/writes cannot move above operation.
		// acquire: fence: Rx other writer's updates to our guarded external data.
		*/
		uint64_t old = 0;
		while(!m_writer_a.compare_exchange_strong(old, 1, std::memory_order_acquire, std::memory_order_relaxed)) {
			old = 0;

			Y_Thread_Yield();
		}

		for(uint32_t rdr_i = 0; rdr_i < m_entries_capacity; ++rdr_i) {
			ReaderEntry* rdr = &m_entries[rdr_i];
			while(rdr->count_a.load(std::memory_order_relaxed)) {
				Y_Thread_Yield();
			}
		}
	}

	void M_Write_Unlock_Tx() {
		Assure(m_writer_a.load(std::memory_order_relaxed) == 1, "Unmatched Lock/Unlock");
		m_writer_a.store(0, std::memory_order_release);
	}
};
