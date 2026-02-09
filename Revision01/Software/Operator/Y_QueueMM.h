//

#pragma once

#include "Y.h"
#include "basic.h"

/*
// Adapted from rigtorp's MPMCQueue
// https://github.com/rigtorp/MPMCQueue
//
// We forego codegen overhead from functions & constructors/destructors.
//
// For algorithmic performance analysis, see
// DESIGNING A HIGH THROUGHPUT BOUNDED MULTI-PRODUCER, MULTI-CONSUMER QUEUE
// REGINALD A. FRANK
// https://oaktrust.library.tamu.edu/bitstream/handle/1969.1/194366/FRANK-FINALTHESIS-2021.pdf
// Summary
// There is increasing contention with very small payloads and more than 16 concurrent producers & consumers,
// however, it offers extremely low latency & high throughput otherwise. It is appropriate for our workloads
// of tasks with durations of [200us, 30ms], given its excellent average latency of 50ns.
// Given these spacious workloads, it will likely scale nicely well beyond 16 concurrent producers & consumers,
// as long as there is minimal contention, especially with respect to the thundering herd problem.
// Further, if it does not, appending to multiple lanes on contention as discussed in
// https://travisdowns.github.io/blog/2020/07/06/concurrency-costs.html
// would alleviate contention from scaling beyond 8 threads with extreme submission requirements.
//
// Further resources
// https://rigtorp.se/ringbuffer/
// https://github.com/cameron314/concurrentqueue/blob/master/concurrentqueue.h
*/

/* sYnc QUEUE, Many to Many ("Turn" Queue) */
template<typename T>
struct Y_QueueMM {
	struct ClientCache;

	struct Entry {
		/*
		// LSB 0 (even): consumed.
		// LSB 1 (odd): unconsumed.
		// (>> 1) gives the applicable loop counter.
		//
		// where "loop" is how many times produced_nth had wrapped around before
		// producing this entry. on the initial produce, each entry would contain
		// loop = 0, LSB = 1 (unconsumed)
		*/
		alignas(kCacheLineSize) std::atomic<uint64_t> coded_loop;

		/*
		// aligned storage for your T, without the hassle of constructors and destructors.
		// your T is Memcpy'd to and from this storage space.
		*/
		typename std::aligned_storage<sizeof(T), alignof(T)>::type storage;

		Entry() {
			Assure_AtCompileTime(0 == offset_of(Entry, coded_loop));
			Assure_AtCompileTime(kCacheLineSize == sizeof(Entry));
			Assure_AtCompileTime(kCacheLineSize == alignof(Entry));
		}
	};

	struct Server {
		std::atomic<uint64_t> m_rents_n_a { 0 };
	#if c_config(debug)
		std::atomic<uint64_t> m_returns_n_a { 0 };
	#endif

		uint32_t m_entries_capacity = 0;
		Entry* m_entries = null;

		alignas(kCacheLineSize) std::atomic<uint64_t> m_writer_a { 0 };
		alignas(kCacheLineSize) std::atomic<uint64_t> m_reader_a { 0 };

		/* prevent adjacent allocations from sharing cachelines */
		uint8_t _padding[kCacheLineSize - sizeof(m_reader_a)];
	} server;

	struct ClientCache {
		// ThreadId m_create_thread_id // can be checked during operations !
		uint64_t m_client_uid = 0;
		uint32_t m_entries_capacity = 0;
		Entry* m_entries = null;
		std::atomic<uint64_t>* m_writer_a = null;
		std::atomic<uint64_t>* m_reader_a = null;
	};

	struct Producer {
		ClientCache cache;
		Y_Tx_e Push_Tx(const T* in);
	};

	struct Consumer {
		ClientCache cache;
		uint64_t cached_writer = cast(uint64_t)(-1);

		/*
		// this is convenient for bypassing an event that's not relevant to this queue.
		// this trades two "Consume" acquire loads for one relaxed load in the case when WriteDetected = false.
		// otherwise, we pay an extra relaxed load each time on the writer, which is unused data.
		*/
		bool WriteDetected() {
			Task_ZoneScoped_NoCallstack;

			const uint64_t fetched_writer = cache.m_writer_a->load(std::memory_order_relaxed);
			if(cached_writer == fetched_writer) {
				/* since writer only counts up, there is NO possibility that */
				return false;
			}
			cached_writer = fetched_writer;
			/* yes, it could be possible that another thread has snaked the new entry, but we're gonna check anyway! */
			return true;
		}

		Y_Rx_e Pull_Rx(T* out);
	};

	Y_QueueMM() {
		Assure_AtCompileTime(1*kCacheLineSize == offset_of(Y_QueueMM, server.m_writer_a));
		Assure_AtCompileTime(2*kCacheLineSize == offset_of(Y_QueueMM, server.m_reader_a));
		Assure_AtCompileTime(3*kCacheLineSize == sizeof(Y_QueueMM));
	}

	~Y_QueueMM() {
		Assure(!server.m_entries, "Did you forget to call Destroy()?");
	}

	bool Create(const uint32_t arg_entries_capacity);
	void Destroy();

	/* note: can be negative! */
	intptr_t M_Count_Lx();

	ClientCache _Cache_Rent() {
		Task_ZoneScoped_NoCallstack;

	#if 0 /* note: thread_local is a serious problem with respect to fibers!! because context switches become invisible beyond yield barriers! must make these transitions explicit !!! */
		thread_local
	#endif
		ClientCache cache;

	#if 0
		/* double check the thread id here ! */
		if(cache.m_client_uid) {
			return cache;
		}
	#endif

		Assure(server.m_entries);
		Assure(server.m_entries_capacity);

		// todo: note: tying consumers & producers together here doesn't make sense !!!!
		uint64_t client_uid;
		{
			Task_ZoneScopedN_NoCallstack("client_uid_fetch_add");
			client_uid = (1 + server.m_rents_n_a.fetch_add(1, std::memory_order_relaxed));
		}

		// assert that we have enough clients.

		cache = {
			.m_client_uid = client_uid,
			.m_entries_capacity = server.m_entries_capacity,
			.m_entries = server.m_entries,
			.m_writer_a = &server.m_writer_a,
			.m_reader_a = &server.m_reader_a,
		};

		return cache;
	}

	void _Cache_Return(ClientCache* client) {
		// todo: check if the Rent != Return thread. that can mean a fiber context switch occured without us knowing.
		// todo: note: this won't effect the thread_local Client!

	#if c_config(debug)
		Task_ZoneScoped_NoCallstack;

		if(tru(client) && tru(client->m_client_uid)) {
			const uint64_t returns_n = (1 + server.m_returns_n_a.fetch_add(1, std::memory_order_relaxed));
			Assure(returns_n <= server.m_rents_n_a.load(std::memory_order_relaxed), "Too many returns!");

			(*client) = ClientCache();
		}
	#endif
	}

	Consumer Consumer_Rent() {
		Consumer c;
		c.cache = _Cache_Rent();
		return c;
	}

	Producer Producer_Rent() {
		Producer c;
		c.cache = _Cache_Rent();
		return c;
	}

	void Consumer_Return(Consumer* c) {
		_Cache_Return(&c->cache);
	}

	void Producer_Return(Producer* c) {
		_Cache_Return(&c->cache);
	}
};

template<typename T>
/* void */bool Y_QueueMM<T>::Create(const uint32_t arg_entries_capacity) {
	Task_ZoneScoped_NoCallstack;

	Assure(arg_entries_capacity);
	Assure(arg_entries_capacity < 4*1000*1000);

	Assure(!server.m_entries);
	Assure(!server.m_entries_capacity);

#if c_config(debug)
	Assure(server.m_returns_n_a.load(std::memory_order_relaxed) == server.m_rents_n_a.load(std::memory_order_relaxed), "No outstanding clients allowed!");
#endif

	/* we allocate one extra entry to prevent false sharing with adjacent memory. */
	if(!/* Check_Allocation */tru(rc_ArrayPtr_Allocate(server.m_entries, (1 + arg_entries_capacity)))) {
		// Check_Allocation:
		// Allocations are critical faults in recall.
		// we immediately Trace() what we can, without allocating anything. the Trace / Trap subsystems must limit its allocations to support this.
		return false;
	}

	MemClear_Explicit(server.m_entries, arg_entries_capacity*sizeof(server.m_entries[0]));

	server.m_entries_capacity = arg_entries_capacity;
	server.m_writer_a = 0;
	server.m_reader_a = 0;
	return true;
}

template<typename T>
void Y_QueueMM<T>::Destroy() {
	Task_ZoneScoped_NoCallstack;

#if c_config(debug)
	Assure(server.m_returns_n_a.load(std::memory_order_relaxed) == server.m_rents_n_a.load(std::memory_order_relaxed), "No outstanding clients allowed!");
#endif

	rc_ArrayPtr_Free(server.m_entries);
	server.m_entries_capacity = 0;
	server.m_writer_a = 0;
	server.m_reader_a = 0;
}

template<typename T>
intptr_t Y_QueueMM<T>::M_Count_Lx() {
	Task_ZoneScoped_NoCallstack;

	const intptr_t reader = cast(intptr_t)server.m_reader_a.load(std::memory_order_relaxed);
	const intptr_t writer = cast(intptr_t)server.m_writer_a.load(std::memory_order_relaxed);

	return (writer - reader);
}

template<typename T>
Y_Tx_e Y_QueueMM<T>::Producer::Push_Tx(const T* in) {
	Task_ZoneScoped_NoCallstack;

	Assure(in);

	Assure(cache.m_entries);
	Assure(cache.m_entries_capacity);

	uint64_t writer = cache.m_writer_a->load(std::memory_order_acquire);

	/* even: consumed, odd: unconsumed */
	const uint32_t entries_capacity = cache.m_entries_capacity;
	Entry* write = &cache.m_entries[writer % entries_capacity];
	uint64_t coded_loop_even_consumed = ((writer / entries_capacity) << 1);

	/*
	// acquire: order: reads/writes cannot move above operation.
	// acquire: fence: receive changes to m_writer_a, storage.
	*/
	if(write->coded_loop.load(std::memory_order_acquire) == coded_loop_even_consumed) {
		if(cache.m_writer_a->compare_exchange_strong(writer, (writer + 1), std::memory_order_relaxed)) {
			memcpy(&write->storage, in, sizeof(T));

			/*
			// release: order: reads/writes cannot move below operation.
			// release: fence: send changes to storage.
			*/
			write->coded_loop.store((1 + coded_loop_even_consumed), std::memory_order_release);
			return Y_Tx_e::Success;
		}

		/* contention. */
	} else {
		/* stale entry. */
		const uint64_t writer_fetch = cache.m_writer_a->load(std::memory_order_acquire);
		if(writer_fetch == writer) {
			/* dont overwrite an unconsumed entry. */
			return Y_Tx_e::Full;
		}

		/* contention. */
	#if 0 /* we could loop here, until our entry is pushed, but we leave that decision to the caller. */
		writer = writer_fetch;
		coded_loop_even_consumed = ((writer_fetch / entries_capacity) << 1);
	#endif
	}

	return Y_Tx_e::Contention;
}

template<typename T>
Y_Rx_e Y_QueueMM<T>::Consumer::Pull_Rx(T* out) {
	Task_ZoneScoped_NoCallstack;

	Assure(cache.m_entries);
	Assure(cache.m_entries_capacity);

	uint64_t reader = cache.m_reader_a->load(std::memory_order_acquire);

	const uint32_t entries_capacity = cache.m_entries_capacity;
	Entry* read = &cache.m_entries[reader % entries_capacity];
	/* even: consumed <- odd: unconsumed */
	uint64_t coded_loop_odd_unconsumed = (1 + ((reader / entries_capacity) << 1));

	/*
	// acquire: order: reads/writes cannot move above operation.
	// acquire: fence: receive changes to m_reader_a, storage.
	*/
	if(read->coded_loop.load(std::memory_order_acquire) == coded_loop_odd_unconsumed) {
		if(cache.m_reader_a->compare_exchange_strong(reader, (reader + 1), std::memory_order_relaxed)) {
			memcpy(out, &read->storage, sizeof(T));

			/*
			// set coded_loop to consumed!
			//
			// release: order: reads/writes cannot move below operation.
			// release: fence: send changes to m_reader_a, storage.
			*/
			read->coded_loop.store((1 + coded_loop_odd_unconsumed), std::memory_order_release);
			return Y_Rx_e::Success;
		}

		/* contention */
	} else {
		/* stale entry. */

		/*
		// acquire: order: reads/writes cannot move above operation.
		// acquire: fence: receive changes to (null).
		*/
		const uint64_t reader_fetch = cache.m_reader_a->load(std::memory_order_acquire);
		if(reader_fetch == reader) {
			return Y_Rx_e::Empty;
		}

		/* contention */
	#if 0 /* we could loop here */
		c->reader = reader_fetch;
		coded_loop_odd_unconsumed = (1 + ((c->reader / c->entries_capacity) << 1));
	#endif
	}

	return Y_Rx_e::Contention;
}

