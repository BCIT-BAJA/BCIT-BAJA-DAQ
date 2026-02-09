//

#include "pch.h"
#include "OS_Signal.h"
#include "basic.h"

std::atomic<int> received_termination_signal = 0;
std::atomic<bool> uninstalled = false;

#ifndef SIGINT
#error Include csignal
#endif

const char* OS_Signal_ToString(int signal) {
	switch(signal) {
		/*
		If a process is being run from terminal and that terminal suddenly goes
		away then the process receives this signal. “HUP” is short for “hang up”
		and refers to hanging up the telephone in the days of telephone modems.
		*/
	#ifdef SIGHUP
		case SIGHUP: { return stringify(SIGHUP); }
	#endif

		/*
		The process was “interrupted”. This happens when you press Control+C on
		the controlling terminal.
		*/
	#ifdef SIGINT
		case SIGINT: { return stringify(SIGINT); }
	#endif

		/*
		(No comment)
		*/
	#ifdef SIGQUIT
		case SIGQUIT: { return stringify(SIGQUIT); }
	#endif

		/*
		Illegal instruction. The program contained some machine code the CPU can't understand.
		*/
	#ifdef SIGILL
		case SIGILL: { return stringify(SIGILL); }
	#endif

		/*
		This signal is used mainly from within debuggers and program tracers.
		*/
	#ifdef SIGTRAP
		case SIGTRAP: { return stringify(SIGTRAP); }
	#endif

		/*
		The program called the abort() function. This is an emergency stop.
		*/
	#ifdef SIGABRT
		case SIGABRT: { return stringify(SIGABRT); }
	#endif

		/*
		An attempt was made to access memory incorrectly. This can be caused by
		alignment errors in memory access etc.
		*/
	#ifdef SIGBUS
		case SIGBUS: { return stringify(SIGBUS); }
	#endif

		/*
		A floating point exception happened in the program.
		*/
	#ifdef SIGFPE
		case SIGFPE: { return stringify(SIGFPE); }
	#endif

		/*
		The process was explicitly killed by somebody wielding the kill program.
		*/
	#ifdef SIGKILL 
		case SIGKILL: { return stringify(SIGKILL); }
	#endif

		/*
		Left for the programmers to do whatever they want.
		*/
	#ifdef SIGUSR1
		case SIGUSR1: { return stringify(SIGUSR1); }
	#endif

		/*
		An attempt was made to access memory not allocated to the process.
		This is often caused by reading off the end of arrays etc.
		*/
	#ifdef SIGSEGV
		case SIGSEGV: { return stringify(SIGSEGV); }
	#endif

		/*
		Left for the programmers to do whatever they want.
		*/
	#ifdef SIGUSR2
		case SIGUSR2: { return stringify(SIGUSR2); }
	#endif

		/*
		If a process is producing output that is being fed into another process that
		consume it via a pipe (“producer | consumer”) and the consumer
		dies then the producer is sent this signal.
		*/ 
	#ifdef SIGPIPE
		case SIGPIPE: { return stringify(SIGPIPE); }
	#endif

		/*
		A process can request a “wake up call” from the operating system at some
		time in the future by calling the alarm() function. When that time comes
		round the wake up call consists of this signal.
		*/
	#ifdef SIGALRM
		case SIGALRM: { return stringify(SIGALRM); }
	#endif

		/*
		The process was explicitly killed by somebody wielding the kill program.
		*/
	#ifdef SIGTERM
		case SIGTERM: { return stringify(SIGTERM); }
	#endif

		/*
		The process had previously created one or more child processes with the
		fork() function. One or more of these processes has since died.
		*/
	#ifdef SIGCHLD
		case SIGCHLD: { return stringify(SIGCHLD); }
	#endif

		/*
		(To be read in conjunction with SIGSTOP.)
		If a process has been paused by sending it SIGSTOP then sending
		SIGCONT to the process wakes it up again (“continues” it).
		*/
	#ifdef SIGCONT
		case SIGCONT: { return stringify(SIGCONT); }
	#endif

		/*
		(To be read in conjunction with SIGCONT.)
		If a process is sent SIGSTOP it is paused by the operating system. All its
		No. Short name What it means
		state is preserved ready for it to be restarted (by SIGCONT) but it doesn't
		get any more CPU cycles until then.
		*/
	#ifdef SIGSTOP
		case SIGSTOP: { return stringify(SIGSTOP); }
	#endif

		/*
		Essentially the same as SIGSTOP. This is the signal sent when the user hits
		Control+Z on the terminal. (SIGTSTP is short for “terminal stop”) The
		only difference between SIGTSTPand SIGSTOP is that pausing is
		only the default action for SIGTSTP but is the required action for
		SIGSTOP. The process can opt to handle SIGTSTP differently but gets no
		choice regarding SIGSTOP.
		*/
	#ifdef SIGSTP
		case SIGSTP: { return stringify(SIGSTP); }
	#endif

		/*
		The operating system sends this signal to a backgrounded process when it
		tries to read input from its terminal. The typical response is to pause (as per
		SIGSTOP and SIFTSTP) and wait for the SIGCONT that arrives when the
		process is brought back to the foreground.
		*/
	#ifdef SIGTTIN
		case SIGTTIN: { return stringify(SIGTTIN); }
	#endif

		/*
		The operating system sends this signal to a backgrounded process when it
		tries to write output to its terminal. The typical response is as per SIGTTIN.
		*/
	#ifdef SIGTTOU
		case SIGTTOU: { return stringify(SIGTTOU); }
	#endif

		/*
		The operating system sends this signal to a process using a network
		connection when “urgent” out of band data is sent to it.
		*/
	#ifdef SIGURG
		case SIGURG: { return stringify(SIGURG); }
	#endif

		/*
		The operating system sends this signal to a process that has exceeded its
		CPU limit. You can cancel any CPU limit with the shell command
		“ulimit -t unlimited” prior to running make though it is more
		likely that something has gone wrong if you reach the CPU limit in make.
		*/
	#ifdef SIGXCPU 
		case SIGXCPU: { return stringify(SIGXCPU); }
	#endif

		/*
		The operating system sends this signal to a process that has tried to create a
		file above the file size limit. You can cancel any file size limit with the
		shell command “ulimit -f unlimited” prior to running make though it is
		more likely that something has gone wrong if you reach the file size limit
		in make.
		*/
	#ifdef SIGXCFSZ 
		case SIGXFSZ: { return stringify(SIGXFSZ); }
	#endif

		/*
		This is very similar to SIGALRM, but while SIGALRM is sent after a
		certain amount of real time has passed, SIGVTALRM is sent after a certain
		amount of time has been spent running the process.
		*/
	#ifdef SIGVTALRM 
		case SIGVTALRM: { return stringify(SIGVTALRM); }
	#endif

		/*
		This is also very similar to SIGALRM and SIGVTALRM, but while
		SIGALRM is sent after a certain amount of real time has passed, SIGPROF
		is sent after a certain amount of time has been spent running the process
		and running system code on behalf of the process.
		*/
	#ifdef SIGVPROF 
		case SIGPROF: { return stringify(SIGPROF); }
	#endif

		/*
		(Mostly unused these days.) A process used to be sent this signal when one
		of its windows was resized.
		*/
	#ifdef SIGWINCH
		case SIGWINCH: { return stringify(SIGWINCH); }
	#endif

		/*
		(Also known as SIGPOLL.) A process can arrange to have this signal sent
		to it when there is some input ready for it to process or an output channel
		has become ready for writing.
		*/
	#ifdef SIGIO
		case SIGIO: { return stringify(SIGIO); }
	#endif
	#ifdef SIGPOLL
		case SIGPOLL: { return stringify(SIGPOLL); }
	#endif

		/*
		A signal sent to processes by a power management service to indicate that
		power has switched to a short term emergency power supply. The process
		(especially long-running daemons) may care to shut down cleanlt before
		the emergency power fails.
		*/
	#ifdef SIGPWR
		case SIGPWR: { return stringify(SIGPWR); }
	#endif

		/*
		31 SIGSYS Unused
		*/
	#ifdef SIGSYS
		case SIGSYS: { return stringify(SIGSYS); }
	#endif
	}
	return "SIG?";
}

int OS_Signal_GetSignal() {
	return received_termination_signal.load(std::memory_order_relaxed);
}

static void OS_SignalHandler(int signal) {
	if(!received_termination_signal.load(std::memory_order_relaxed)) {
		received_termination_signal.store(signal, std::memory_order_relaxed);
	}

	static uint32_t count = 0;
	++count;
	if(3 <= count) {
		fprintf(stderr, "Received three termination signals. Exiting...\n");

		/*
		The Terminator/Release date October 26, 1984.
		Importantly, this code will never be confused with __LINE__.
		*/
		exit(19841026);
	}
}

#if c_os(windows)
static BOOL WINAPI Win32_CtrlHandler(DWORD fdwCtrlType) {
	switch(fdwCtrlType) {
		case CTRL_C_EVENT:
		case CTRL_BREAK_EVENT: {
			OS_SignalHandler(SIGINT);
			return TRUE;
		}

		default:
		case CTRL_CLOSE_EVENT:
		case CTRL_LOGOFF_EVENT:
		case CTRL_SHUTDOWN_EVENT: {
			OS_SignalHandler(SIGTERM);

			/*
			We have 5 seconds to terminate gracefully.
			*/
			while(!uninstalled.load(std::memory_order_relaxed)) {
				Sleep(1);
			}

			return TRUE;
		}
	}

	return FALSE;
}
#endif

bool OS_Signal_Install() {
	if(!tru(SIG_ERR != signal(SIGINT, OS_SignalHandler))) {
		return false;
	}

	if(!tru(SIG_ERR != signal(SIGTERM, OS_SignalHandler))) {
		return false;
	}

#ifdef SIGPIPE
	if(!tru(SIG_ERR != signal(SIGPIPE, OS_SignalHandler))) {
		return false;
	}
#endif

#if c_os(windows)
	if(!tru(SetConsoleCtrlHandler(cast(PHANDLER_ROUTINE)Win32_CtrlHandler, TRUE))) {
		return false;
	}
#endif

	return true;
}

void OS_Signal_Uninstall() {
	uninstalled.store(true, std::memory_order_relaxed);
}
