//

#pragma once

#include "Y.h"
#include "Basic.h"

template<typename T>
struct Y_PoolFairMM {
	struct Entry {
		/* even: unclaimed, odd: claimed */
		alignas(kCacheLineSize) std::atomic<uint64_t> exclusive;

		typename std::aligned_storage<sizeof(T), alignof(T)>::type storage;

		/* prevent adjacent allocations from sharing cachelines */
		uint8_t _padding[kCacheLineSize - sizeof(storage) - sizeof(exclusive)];

		Entry() {
			Assure_AtCompileTime(kCacheLineSize == alignof(Entry));
			Assure_AtCompileTime(kCacheLineSize == sizeof(Entry));
		}
	};

	uint32_t m_entries_capacity = 0;
	Entry* m_entries = null;

	alignas(kCacheLineSize) std::atomic<uint64_t> m_fair_a { 0 };

	/* prevent adjacent allocations from sharing cachelines */
	uint8_t _padding[kCacheLineSize - sizeof(m_fair_a)];

	Y_PoolFairMM() {
		Assure_AtCompileTime(2*kCacheLineSize == sizeof(Y_PoolFairMM));
	}

	~Y_PoolFairMM() {
		Assure(!m_entries, "Did you forget to call Destroy()?");
	}

	/* must be called before M_* without racing! */
	bool Create(const uint32_t arg_entries_capacity) {
		Task_ZoneScoped_NoCallstack;

		Assure(arg_entries_capacity);
		Assure(arg_entries_capacity < 4*1000*1000);

		Assure(!m_entries);
		Assure(!m_entries_capacity);

		/* we allocate one extra entry to prevent false sharing with adjacent memory. */
		if(!Assure_True(Basic_ArrayPointer_New(m_entries, (1 + arg_entries_capacity)))) {
			return false;
		}

		m_entries_capacity = arg_entries_capacity;

		m_fair_a.store(0, std::memory_order_relaxed);

		MemclearC(m_entries, arg_entries_capacity*sizeof(m_entries[0]));
		return true;
	}

	void Destroy() {
		Task_ZoneScoped_NoCallstack;

		Basic_ArrayPointer_Delete(m_entries);
		m_entries_capacity = 0;
		m_fair_a.store(0, std::memory_order_relaxed);
	}

	/* note: this can be called multiple times without much performance penalty! */
	Y_Rx_e M_Rent_Rx(Y_PoolRental<T>* out) {
		Task_ZoneScoped_NoCallstack;

		Assure(out);

		Assure(!out->key);
		Assure(!out->key_epoch);

		uint64_t fair_i = m_fair_a.fetch_add(1, std::memory_order_relaxed);
		fair_i %= m_entries_capacity;

		Entry* entry = &m_entries[fair_i];
		uint64_t exclusive = entry->exclusive.load(std::memory_order_relaxed);
		if(exclusive % 2 == 0) {
			/*
			// acquire: order: reads/writes cannot move above operation.
			// acquire: fence: Rx this entry's T data.
			*/
			if(entry->exclusive.compare_exchange_strong(exclusive, (exclusive + 1), std::memory_order_acquire)) {
				out->key = cast(void*)cast(uintptr_t)fair_i;
				out->key_epoch = (exclusive + 1);
				Memcpy(&out->value, entry->storage.data, sizeof(T));
				return Y_Rx_e::Success;
			}
		}

		/* Someone is holding an entry for too long, or, you should raise the capacity. */
		out->key = 0;
		out->key_epoch = 0;
		MemclearC(&out->value, sizeof(T));
		return Y_Rx_e::Empty;
	}

	Y_Tx_e M_ReturnTx(Y_PoolRental<T>* arg) {
		Task_ZoneScoped_NoCallstack;

		defer(
			arg->key = 0;
			arg->key_epoch = 0;
			MemclearC(&arg->value, sizeof(T));
		);

		const uint64_t entry_i = cast(uint64_t)cast(uintptr_t)arg->key;
		if(!Assure_True(entry_i < m_entries_capacity)) {
			return Y_Tx_e::Success;
		}

		uint64_t exclusive = arg->key_epoch;
		if(!Assure_True(exclusive % 2 != 0)) {
			return Y_Tx_e::Success;
		}

		Entry* entry = &m_entries[entry_i];
		if(entry->exclusive.load(std::memory_order_relaxed) == exclusive) {
			Memcpy(entry->storage.mCharData, &arg->value, sizeof(T));
			/*
			// release: order: reads/writes cannot move below operation.
			// release: fence: Tx this entry's T data.
			*/
			if(entry->exclusive.compare_exchange_strong(exclusive, (exclusive + 1), std::memory_order_release)) {
				return Y_Tx_e::Success;
			}

			Assure(false);
		}

		/* rental was already returned. */
		return Y_Tx_e::Success;
	}
};