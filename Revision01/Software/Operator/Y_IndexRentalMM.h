//

#pragma once

#include "Y.h"
#include "Basic.h"

struct IndexRental {
	uint64_t _lock = 0;
	uint32_t _rented = 0;
	uint32_t index = 0;

	~IndexRental() {
		Assert_True(!_rented, "Did you forget to return this Rental?");
	}
};

template<uint32_t Capacity>
struct Y_IndexRentalMM_Frugal {
	struct ExclusiveIndexEntry {
		alignas(kCacheLineSize) std::atomic<uint64_t> odd_even_lock = 0;
		uint8_t _padding_to_prevent_false_sharing[kCacheLineSize - sizeof(odd_even_lock)];
		ExclusiveIndexEntry() {
			Ensure_TrueAtCompileTime(kCacheLineSize == alignof(ExclusiveIndexEntry));
			Ensure_TrueAtCompileTime(kCacheLineSize == sizeof(ExclusiveIndexEntry));
		}
	};

	alignas(kCacheLineSize) ExclusiveIndexEntry m_entries[Capacity];

	bool M_Rent(IndexRental* out_rental) {
		Assert_True(out_rental);
		for(uint32_t twice_i = 0; twice_i < 2; ++twice_i) {
			for(uint32_t candidate_i = 0; candidate_i < Capacity; ++candidate_i) {
				ExclusiveIndexEntry* candidate = &m_entries[candidate_i];

				auto lock_old = candidate->odd_even_lock.load(std::memory_order_relaxed);
				if(!(lock_old & 1)) {
					if(candidate->odd_even_lock.compare_exchange_strong(
						lock_old,
						(lock_old + 1),
						std::memory_order_acquire
					)) {
						out_rental->_lock = (lock_old + 1);
						out_rental->_rented = 1;
						out_rental->index = candidate_i;
						return true;
					}
				}
			}
		}
		return false;
	}

	void M_Return(IndexRental* rental) {
		Assert_True(rental);
		if(!(true
			&& Test_True(rental->_rented)
			&& Test_True(rental->_lock & 1)
			&& Test_True(rental->index < Capacity)
		)) {
			return;
		}

		ExclusiveIndexEntry* entry = &m_entries[rental->index];
		Test_True(entry->odd_even_lock.compare_exchange_strong(
			rental->_lock,
			(rental->_lock + 1),
			std::memory_order_release
		));

		(*rental) = IndexRental();
	}
};