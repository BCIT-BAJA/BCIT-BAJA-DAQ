//

/* https://en.wikipedia.org/wiki/Futex */

/* https://github.com/bfredl/zig/blob/408d8df86c55decd79020d472855624d98e32b03/lib/std/Thread/Futex.zig#L174 */

#pragma once

#include "Basic.h"
#include "Compile.h"

/* WaitOnAddress function 
// https://learn.microsoft.com/en-us/windows/win32/api/synchapi/nf-synchapi-waitonaddress
*/

/* WakeByAddressSingle function
// https://learn.microsoft.com/en-us/windows/win32/api/synchapi/nf-synchapi-wakebyaddresssingle
*/

/* WakeByAddressAll function
// https://learn.microsoft.com/en-us/windows/win32/api/synchapi/nf-synchapi-wakebyaddressall 
*/

/* xnu/bsd/sys/ulock.h
// https://github.com/apple-oss-distributions/xnu
// https://github.com/apple-oss-distributions/xnu/blob/5c2921b07a2480ab43ec66f5b9e41cb872bc554f/bsd/sys/ulock.h
*/
#if c_os(apple)
/*
* Copyright (c) 2015 Apple Inc. All rights reserved.
*
* @APPLE_OSREFERENCE_LICENSE_HEADER_START@
*
* This file contains Original Code and/or Modifications of Original Code
* as defined in and that are subject to the Apple Public Source License
* Version 2.0 (the 'License'). You may not use this file except in
* compliance with the License. The rights granted to you under the License
* may not be used to create, or enable the creation or redistribution of,
* unlawful or unlicensed copies of an Apple operating system, or to
* circumvent, violate, or enable the circumvention or violation of, any
* terms of an Apple operating system software license agreement.
*
* Please obtain a copy of the License at
* http://www.opensource.apple.com/apsl/ and read it before using this file.
*
* The Original Code and all software distributed under the License are
* distributed on an 'AS IS' basis, WITHOUT WARRANTY OF ANY KIND, EITHER
* EXPRESS OR IMPLIED, AND APPLE HEREBY DISCLAIMS ALL SUCH WARRANTIES,
* INCLUDING WITHOUT LIMITATION, ANY WARRANTIES OF MERCHANTABILITY,
* FITNESS FOR A PARTICULAR PURPOSE, QUIET ENJOYMENT OR NON-INFRINGEMENT.
* Please see the License for the specific language governing rights and
* limitations under the License.
*
* @APPLE_OSREFERENCE_LICENSE_HEADER_END@
*/

// #ifndef _SYS_ULOCK_H
// #define _SYS_ULOCK_H

// #include <mach/mach_port.h>
// #include <sys/cdefs.h>
// #include <stdint.h>

// __BEGIN_DECLS

// #if PRIVATE

// #ifdef XNU_KERNEL_PRIVATE
// extern mach_port_name_t ipc_entry_name_mask(mach_port_name_t name);

// static __inline mach_port_name_t
// ulock_owner_value_to_port_name(uint32_t uval)
// {
	/*
	* userland uses the least significant bits for flags as these are
	* never used in the mach port name, and are generally always set by
	* the ipc_entry code in the kernel. Here we reconstruct a mach port
	* name that we can use in the kernel.
	*/
// 	return ipc_entry_name_mask((mach_port_name_t)uval);
// }

// extern int ulock_wake(struct task *task, uint32_t operation, user_addr_t addr, uint64_t wake_value);

// #else
// static __inline mach_port_name_t
// ulock_owner_value_to_port_name(uint32_t uval)
// {
// 	return uval | 0x3;
// }
// #endif

// #ifndef KERNEL

extern int __ulock_wait(uint32_t operation, void *addr, uint64_t value,
	uint32_t timeout);             /* timeout is specified in microseconds */
extern int __ulock_wait2(uint32_t operation, void *addr, uint64_t value,
	uint64_t timeout, uint64_t value2);
extern int __ulock_wake(uint32_t operation, void *addr, uint64_t wake_value);

// #endif /* !KERNEL */

/*
* operation bits [7, 0] contain the operation code.
*
* NOTE: make sure to add logic for handling any new
*       types to kdp_ulock_find_owner()
*/
#define UL_COMPARE_AND_WAIT             1
#define UL_UNFAIR_LOCK                  2
#define UL_COMPARE_AND_WAIT_SHARED      3
#define UL_UNFAIR_LOCK64_SHARED         4
#define UL_COMPARE_AND_WAIT64           5
#define UL_COMPARE_AND_WAIT64_SHARED    6
/* obsolete names */
#define UL_OSSPINLOCK                   UL_COMPARE_AND_WAIT
#define UL_HANDOFFLOCK                  UL_UNFAIR_LOCK
/* These operation code are only implemented in (DEVELOPMENT || DEBUG) kernels */
#define UL_DEBUG_SIMULATE_COPYIN_FAULT  253
#define UL_DEBUG_HASH_DUMP_ALL          254
#define UL_DEBUG_HASH_DUMP_PID          255

/*
* operation bits [15, 8] contain the flags for __ulock_wake
*/
#define ULF_WAKE_ALL                    0x00000100
#define ULF_WAKE_THREAD                 0x00000200
#define ULF_WAKE_ALLOW_NON_OWNER        0x00000400

/*
* operation bits [23, 16] contain the flags for __ulock_wait
*
* @const ULF_WAIT_WORKQ_DATA_CONTENTION
* The waiter is contending on this lock for synchronization around global data.
* This causes the workqueue subsystem to not create new threads to offset for
* waiters on this lock.
*
* @const ULF_WAIT_CANCEL_POINT
* This wait is a cancelation point
*
* @const ULF_WAIT_ADAPTIVE_SPIN
* Use adaptive spinning when the thread that currently holds the unfair lock
* is on core.
*/
#define ULF_WAIT_WORKQ_DATA_CONTENTION  0x00010000
#define ULF_WAIT_CANCEL_POINT           0x00020000
#define ULF_WAIT_ADAPTIVE_SPIN          0x00040000

/*
* operation bits [31, 24] contain the generic flags
*/
#define ULF_NO_ERRNO                    0x01000000

/*
* masks
*/
#define UL_OPCODE_MASK          0x000000FF
#define UL_FLAGS_MASK           0xFFFFFF00
#define ULF_GENERIC_MASK        0xFFFF0000

#define ULF_WAIT_MASK           (ULF_NO_ERRNO | \
	                         ULF_WAIT_WORKQ_DATA_CONTENTION | \
	                         ULF_WAIT_CANCEL_POINT | ULF_WAIT_ADAPTIVE_SPIN)

#define ULF_WAKE_MASK           (ULF_NO_ERRNO | \
	                         ULF_WAKE_ALL | \
	                         ULF_WAKE_THREAD | \
	                         ULF_WAKE_ALLOW_NON_OWNER)

// #endif /* PRIVATE */

// __END_DECLS

// #endif

// #endif
#endif

#if 0
int futex_wait(int* lock_word, int oldval, long sec, unsigned long usec) {
	unsigned long timeout;
	if(sec < 0) {
		timeout = 0;
	} else {
		if(__builtin_umull_overflow((unsigned long)sec, 1000000000ul, &timeout)
			|| __builtin_umull_overflow(usec, 1000ul, &usec)
			|| __builtin_uaddl_overflow(usec, timeout, &timeout)) {
			timeout = 0xfffffffffffffffful;
		}
	}

	int ret = __ulock_wait2(UL_COMPARE_AND_WAIT | ULF_NO_ERRNO, lock_word, oldval, timeout, 0);
	if(ret == 0)
		return 0;
	else if(ret == -ETIMEDOUT)
		return 1;
	else if(ret == -EINTR)
		return 2;
	else
		return -1;
}

int futex_wake(int* lock_word, int n) {
	__ulock_wake(UL_COMPARE_AND_WAIT | ULF_NO_ERRNO | (n == 1 ? 0 : ULF_WAKE_ALL), lock_word, 0);
	/*
	// according to: https://github.com/ziglang/zig/blob/0a6cd257b9c8a9093b966e3851dc8261e19b531a/lib/std/Thread/Futex.zig#L235
	// returns EINTR, EFAULT, ENOENT, EALREADY, or zero.
	// accordint to: https://github.com/PureDarwin/PureDarwin/blob/446bae96448a8d9ab46a6f25494c25fb4e53f154/src/Kernel/xnu/bsd/kern/sys_ulock.c#L424
	*/
	return 0;
}
#endif


/*
// Note WaitOnAddress is guaranteed to return when the address is signaled, but it is also allowed to return for other reasons.
// For this reason, after WaitOnAddress returns the caller should compare the new value with the original undesired value to confirm that the value has actually changed.
example: 

	ULONG g_TargetValue; // global, accessible to all threads
	ULONG CapturedValue;
	ULONG UndesiredValue;

	UndesiredValue = 0;
	CapturedValue = g_TargetValue;
	while (CapturedValue == UndesiredValue) {
		WaitOnAddress(&g_TargetValue, &UndesiredValue, sizeof(ULONG), INFINITE);
		CapturedValue = g_TargetValue;
	}
*/

enum Timeout32_e: uint32_t {
	Infinite = 0,
	ASAP = cast(uint32_t)(-1),
};

// __ulock_wait  | timeout:u32 in microseconds.
// __ulock_wait2 | timeout:u64 in nanoseconds.
// WaitOnAddress | timeout:DWORD in milliseconds.

// WaitOnAddress | may return false => GetLastError(): ERROR_TIMEOUT

enum class Await_e : bool {
	Timedout = false,
	SignaledSpuriously = true,
};

namespace OS_AddressEvent {
inl Await_e AwaitSignal_IfValueEquals(const volatile void* address64, const uint64_t value64, const uint32_t timeout_us) {
	Assure(cast(uintptr_t)address64 % alignof(std::atomic<uint64_t>) == 0);
	// typecast(eastl::atomic<uint64_t>)address64;

	Assure_AtCompileTime(Timeout32_e::Infinite == 0);
#if c_os(apple)
	int timeout_sys_us = timeout_us;
	if(timeout_us == Timeout32_e::ASAP) {
		timeout_sys_us = 1;
	}

	while(true) {
		int ret = __ulock_wait((UL_COMPARE_AND_WAIT | ULF_NO_ERRNO), cast(void*)address64, value64, timeout_us);
		if(0 <= ret) {
			return Await_e::SignaledSpuriously;
		}

		ret = -ret;
		if(ret == ETIMEDOUT) {
			assure(timeout_us != Timeout32_e::Infinite);
			return Await_e::Timedout;
		}
	}
#endif

#if c_os(windows)
	DWORD timeout_sys_ms;
	if(timeout_us == Timeout32_e::Infinite) {
		timeout_sys_ms = INFINITE;
	} else if(timeout_us == Timeout32_e::ASAP) {
		timeout_sys_ms = 0; // todo: verify: does this work? or does it require ms = 1?
	} else {
		Assure(!UnsignedAdditionWouldOverflow(timeout_us, 1000u));
		timeout_sys_ms = (timeout_us + 499) / 1000;
	}

	TracyMessage("Sleep", sizeof("Sleep"));
	const BOOL ret = WaitOnAddress(cast(volatile void*)address64, cast(PVOID)&value64, sizeof(uint64_t), timeout_sys_ms);
	TracyMessage("Wake", sizeof("Wake"));
	Assure(ret || (timeout_us != Timeout32_e::Infinite && GetLastError() == ERROR_TIMEOUT));
	return ret ? Await_e::SignaledSpuriously : Await_e::Timedout;
#endif

	unreachable;
}

#if c_os(apple)
namespace Private {
inl void _OS_AddressEvent_Signal(const bool all, const volatile void* address64) {
	uint32_t flags = (UL_COMPARE_AND_WAIT | ULF_NO_ERRNO);
	if(all) {
		flags |= ULF_WAKE_ALL;
	}

	while(true) {
		int ret = __ulock_wake(flags, cast(void*)address64, 0);
		if(0 <= ret) {
			/* success */
			return;
		}

		ret = -ret;
		if(ret == ENOENT) {
			/* success: nothing was woken */
			return;
		}

		/* spurious failure */
	}

	unreachable;
}
}
#endif

inl void Signal_One(const volatile void* address64) {
	Assure(cast(uintptr_t)address64 % alignof(std::atomic<uint64_t>) == 0);
#if c_os(apple)
	return Private::Signal(false, address64);
#endif
#if c_os(windows)
	return WakeByAddressSingle(cast(LPVOID)address64);
#endif
	unreachable;
}

inl void Signal_Everyone(const volatile void* address64) {
	Assure(cast(uintptr_t)address64 % alignof(std::atomic<uint64_t>) == 0);
#if c_os(apple)
	return Private::Signal(true, address64);
#endif
#if c_os(windows)
	return WakeByAddressAll(cast(LPVOID)address64);
#endif
	unreachable;
}

#define Alias(Type) \
inl Await_e AwaitSignal_IfValueEquals(const Type* address64, const Type value64, const uint32_t timeout_us) { \
	Assure_AtCompileTime(sizeof(Type) == sizeof(uint64_t)); \
	Assure_AtCompileTime(alignof(Type) == alignof(uint64_t)); \
	return AwaitSignal_IfValueEquals(cast(const volatile void*)address64, cast(uint64_t)value64, timeout_us); \
} \
inl Await_e AwaitSignal_IfValueEquals(const Type& address64, const Type value64, const uint32_t timeout_us) { \
	return AwaitSignal_IfValueEquals(cast(const volatile void*)&address64, cast(uint64_t)value64, timeout_us); \
} \
inl void Signal_One(const Type* address64) { \
	return Signal_One(cast(const volatile void*)address64); \
} \
inl void Signal_One(const Type& address64) { \
	return Signal_One(cast(const volatile void*)&address64); \
} \
inl void Signal_Everyone(const Type* address64) { \
	return Signal_Everyone(cast(const volatile void*)address64); \
} \
inl void Signal_Everyone(const Type& address64) { \
	return Signal_Everyone(cast(const volatile void*)&address64); \
} \

Alias(std::atomic<uint64_t>);
Alias(std::atomic<int64_t>);
#undef Alias

}
