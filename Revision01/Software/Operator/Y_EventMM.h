//

#pragma once

/*
// see: https://www.1024cores.net/home/lock-free-algorithms/eventcounts
// see: https://github.com/facebook/folly/blob/main/folly/experimental/EventCount.h
*/

#include "PCH.h"

#include "OS_AddressEvent.h"

/*
// see: https://devblogs.microsoft.com/oldnewthing/20170601-00/?p=96265 " If there is no contention, then (WaitOnAddress) operates entirely in user mode.  "
// see: https://devblogs.microsoft.com/oldnewthing/20160823-00/?p=94145 " WaitOnAddress lets you create a synchronization object out of any data variable, even a byte "
// see: https://devblogs.microsoft.com/oldnewthing/20160825-00/?p=94165 " Implementing a critical section in terms of WaitOnAddress"
// todo: use std::atomic<>::wait() // see https://github.com/m-ou-se/atomic-wait/blob/main/src/macos.rs
// see: https://shift.click/blog/futex-like-apis/ ( __ulock_wait/__ulock_wake on apple)
// see: https://github.com/apple/darwin-xnu/blob/2ff845c2e033bd0ff64b5b6aa6063a1f8f65aa32/bsd/sys/ulock.h
// see: https://www.modernescpp.com/index.php/performancecomparison-of-condition-variables-and-atomics-in-c-20
// see: https://github.com/ogiroux/atomic_wait/blob/master/include/atomic_wait
// see: https://marabos.nl/atomics/building-locks.html
// see: https://www.1024cores.net/home/lock-free-algorithms/eventcounts " Eventcounts allow to separate a lockfree data structure and blocking/signaling logic, so that there is generally no need to reimplement and inject it into each and every lockfree algorithm "
// see: https://gist.github.com/mratsim/04a29bdd98d6295acda4d0677c4d0041 Eventcounts (implementation)
// " On platforms that support futex-style waiting with a time limit, the risk of overflowing can be mitigated by using a timeout for the wait operation of a few seconds. Sending four billion notifications will take significantly longer, at which point the risk of a few additional seconds will have very little impact. This completely removes any risk of the program locking up due to a waiting thread wrongly staying asleep forever. "
//
// The most notable exception is macOS. While its kernel does support these operations, it is not exposed through any stable, publicly usable, C function that we can use. However, macOS does ship with a recent version of libc++, an implementation of the C++ standard library. This library includes support for C++20, which is the version of C++ that comes with built-in support for very basic atomic wait and wake operations (like std::atomic<T>::wait()). While it’s somewhat tricky to make use of that from Rust for a variety of reasons, it is certainly possible, giving us access to basic futex-like wait and wake functionality on macOS as well.
// **Only 32-bit atomics are supported, because that’s the only size that’s supported on all major platforms.**
//
// goldmine: https://github.com/sbcl/sbcl/blob/ebdd0992955931afa69d4713f8ed95b9f6d4a294/src/runtime/darwin-os.c
//
// NOTE: An even more fine grained spinloop function is the PAUSE instruction.
//    "pause prevents speculative loads from causing memory-ordering mis-speculation pipeling clears (aka machine nukes). It's useful inside spin loops that are waiting to see a value in memory."
//       Next, if *many* PAUSE instructions don't work, we YIELD for a handful of time slices.
//       Then, we fall back to an OS Futex, with a timeout in nanoseconds. __ulock on macos, WaitForAddress on windows.
//
// Excellent SPMC ring buffer: https://github.com/RaphiaRa/spms_ring/tree/24b3e34c273abc8ba290cf07b6f57c7b5b1e1743
//
//
*/

struct Y_EventMM {
private:
	enum Pair : uint64_t {
		Pair_Notifies_Mask = 0xFFFFFFFF00000000ull,
		Pair_Waiters_Mask  = 0x00000000FFFFFFFFull,

		Pair_Notifies_Shift = 32ull,
		Pair_Notifies_PlusOne = (1ull << Pair_Notifies_Shift),
		Pair_Waiters_PlusOne  = 1,
		Pair_Waiters_MinusOne = cast(uint64_t)(-1),
	};

	alignas(kCacheLineSize) std::atomic<uint64_t> m_pair { 0 };
	/* prevent adjacent allocations from sharing cachelines */
	uint8_t _padding[kCacheLineSize - sizeof(m_pair)];

	void Signal(const bool everyone) {
		uint64_t pair;
		{
			Task_ZoneScopedN_NoCallstack("fetch_add");
		#if 0
			/* acq_rel: order: reads/writes cannot move above/below operation.
			// acq_rel: fence: receive/send external data.
			//
			//  note: the fence here is probably overkill / redundant, since the condition() itself
			//        may issue a memory release fence, especially considering that we only signal on lock free data structures anyway.
			//        however, it allows for relaxed loads & stores to be synchronized using this Y primitive.
			//        it is a serious tradeoff, though: this may stall for multiple microseconds, which can be the same as paying
			//        for two Signal_One operations! 
			*/
			pair = m_pair.fetch_add(Pair_Notifies_PlusOne, std::memory_order_acq_rel);
		#else
			pair = m_pair.fetch_add(Pair_Notifies_PlusOne, std::memory_order_release);
		#endif
		}

		if(pair & Pair_Waiters_Mask) {
			if(everyone) {
				Task_ZoneScopedN("OS::Everyone");
				OS_AddressEvent::Signal_Everyone(m_pair);
			} else {
				Task_ZoneScopedN("OS::One");
				OS_AddressEvent::Signal_One(m_pair);
			}
		}
	}

public:
	void Signal_One() {
		Task_ZoneScoped_NoCallstack;
		return Signal(false);
	}

	void Signal_Everyone() {
		Task_ZoneScoped_NoCallstack;
		return Signal(true);
	}

	template<typename Condition>
	uint64_t AwaitSignalUntil(const uint32_t timeout_us, Condition condition) {
		uint64_t c = condition();
		if(c) {
			return c;
		}

		/* acq_rel: order: reads/writes cannot move above or below operation.
		// acq_rel: fence: receive (& send) changes to condition.
		*/
		uint64_t pair;
		{
			Task_ZoneScopedN_NoCallstack("Pair_Waiters_PlusOne");
			pair = m_pair.fetch_add(Pair_Waiters_PlusOne, std::memory_order_acq_rel);
		}
		bool timedout = false;

		while(true) {
			/*
			// recheck the condition with new acquired data.
			// if this condition were not re-checked, the Signal would race our Await:
			//    consider if condition() = false => suspended before Waiters_PlusOne,
			//    the post-condition-mutation signal isn't sent, yet we immediately wait.
			*/
			c = condition();
			if(c || timedout) {
				/* seq_cst: order: reads/writes cannot move above or below operation.
				// seq_cst: fence: send & receive changes to pair.
				//
				//    although relaxed would be correct here,
				//    seq_cst forces the affected memory accesses to propagate to every core,
				//    thereby potentially avoiding an OS_AddressEvent on another thread.
				*/
				{
					Task_ZoneScopedN_NoCallstack("Pair_Waiters_PlusOne");
					pair = m_pair.fetch_add(Pair_Waiters_MinusOne, std::memory_order_seq_cst);
				}
				assure(pair & Pair_Waiters_Mask);
				return c;
			}

			/* record the last checked notify count. we will retry our condition once this changes. */
			const uint64_t notifies = (pair >> Pair_Notifies_Shift);

			while(true) {
				/* acquire: order: reads/writes cannot move above operation.
				// acquire: fence: receive changes to condition.
				*/
				{
					Task_ZoneScopedN_NoCallstack("m_pair.load acquire");
					pair = m_pair.load(std::memory_order_acquire);
				}
				assure(pair & Pair_Waiters_Mask);
				if((pair >> Pair_Notifies_Shift) != notifies) {
					break;
				}

				if(timedout) {
					break;
				}

				if(Await_e::Timedout == OS_AddressEvent::AwaitSignal_IfValueEquals(m_pair, pair, timeout_us)) {
					timedout = true;

					/* loop to re-acquire() */
				}
			}

			assure(pair & Pair_Waiters_Mask);

			/* since notifies has changed, ... */
		}
		unreachable;
	}

	template<typename Condition>
	uint64_t AwaitSignalUntil(Condition condition) {
		return AwaitSignalUntil(Timeout32_e::Infinite, condition);
	}
};


