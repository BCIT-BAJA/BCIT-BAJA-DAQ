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
			Ensure_TrueAtCompileTime(kCacheLineSize == alignof(Entry));
			Ensure_TrueAtCompileTime(kCacheLineSize == sizeof(Entry));
		}
	};

	uint32_t m_entries_capacity = 0;
	Entry* m_entries = null;
	bool m_entries_constructed = false;

	alignas(kCacheLineSize) std::atomic<uint64_t> m_fair_a { 0 };

	/* prevent adjacent allocations from sharing cachelines */
	uint8_t _padding[kCacheLineSize - sizeof(m_fair_a)];

	Y_PoolFairMM() {
		Ensure_TrueAtCompileTime(2*kCacheLineSize == sizeof(Y_PoolFairMM));
	}

	~Y_PoolFairMM() {
		Assert_True(!m_entries, "Did you forget to call Destroy()?");
	}

	/* must be called before M_* without racing! */
	bool _Create(const uint32_t arg_entries_capacity) {
		Task_ZoneScoped_NoCallstack;

		Assert_True(arg_entries_capacity);
		Assert_True(arg_entries_capacity < 4*1000*1000);

		Assert_True(!m_entries);
		Assert_True(!m_entries_capacity);
		Assert_True(!m_entries_constructed);

		/* we allocate one extra entry to prevent false sharing with adjacent memory. */
		if(!Test_True(Basic_ArrayPointer_New(m_entries, (1 + arg_entries_capacity)))) {
			return false;
		}

		m_entries_capacity = arg_entries_capacity;

		m_fair_a.store(0, std::memory_order_relaxed);
	}

	bool Create_AndZeroEach(const uint32_t arg_entries_capacity) {
		if(!_Create(arg_entries_capacity)) {
			return false;
		}

		MemclearC(m_entries, arg_entries_capacity*sizeof(m_entries[0]));
		return true;
	}

	bool Create_AndConstructEach(const uint32_t arg_entries_capacity) {
		if(!_Create(arg_entries_capacity)) {
			return false;
		}

		for(uint32_t ent_i = 0; ent_i < m_entries_capacity; ++ent_i) {
			ConstructAt_NullSafe(&m_entries[ent_i]);
		}
		m_entries_constructed = true;
	}

	void Destroy() {
		Task_ZoneScoped_NoCallstack;

		// note: this will crash if there are outstanding rentals! (since the memory will be zeroed)
		if(m_entries_constructed) {
			for(uint32_t ent_i = 0; ent_i < m_entries_capacity; ++ent_i) {
				DestructAt_NullSafe(&m_entries[ent_i]);
			}
			m_entries_constructed = false;
		}

		Basic_ArrayPointer_Delete_NullSafe(m_entries);
		m_entries_capacity = 0;
		m_fair_a.store(0, std::memory_order_relaxed);
	}

	/* note: this can be called multiple times without much performance penalty! */
	Y_Rx_e M_Rent(Y_PoolRental<T>* out_rental) {
		Task_ZoneScoped_NoCallstack;

		Assert_True(out_rental);

		Assert_True(!out_rental->key);
		Assert_True(!out_rental->key_epoch);

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
				out_rental->key = cast(void*)cast(uintptr_t)fair_i;
				out_rental->key_epoch = (exclusive + 1);
				Memcpy(&out_rental->value, entry->storage.data, sizeof(T));
			#if 1 // todo: does this introduce a bug ? i don't think so ...
				MemclearC(entry->storage.data, sizeof(T));
			#endif
				return Y_Rx_e::Success;
			}
		}

		/* Someone is holding an entry for too long, or, you should raise the capacity. */
		out_rental->key = 0;
		out_rental->key_epoch = 0;
		MemclearC(&out_rental->value, sizeof(T));
		return Y_Rx_e::Empty;
	}

	Y_Tx_e M_Return(Y_PoolRental<T>* rental) {
		Task_ZoneScoped_NoCallstack;

		Defer(
			rental->key = 0;
			rental->key_epoch = 0;
			MemclearC(&rental->value, sizeof(T));
		);

		const uint64_t entry_i = cast(uint64_t)cast(uintptr_t)rental->key;
		if(!Test_True(entry_i < m_entries_capacity)) {
			return Y_Tx_e::Success;
		}

		uint64_t exclusive = rental->key_epoch;
		if(!Test_True(exclusive % 2 != 0)) {
			return Y_Tx_e::Success;
		}

		Entry* entry = &m_entries[entry_i];
		if(entry->exclusive.load(std::memory_order_relaxed) == exclusive) {
			Memcpy(entry->storage.mCharData, &rental->value, sizeof(T));
			/*
			// release: order: reads/writes cannot move below operation.
			// release: fence: Tx this entry's T data.
			*/
			if(entry->exclusive.compare_exchange_strong(exclusive, (exclusive + 1), std::memory_order_release)) {
				return Y_Tx_e::Success;
			}

			Assert_True(false);
		}

		/* rental was already returned. */
		return Y_Tx_e::Success;
	}
};