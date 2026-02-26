//

#pragma once

//
// todo: here's the idea: use Duff's Device to declare a (recursive/depth enabled) state machine.
// this helps by coupling code sections & their labels together in a neat, clear way.
// this allows for standardized state machine code, with clear checkpoints, and the ability to easily jump to any checkpoint from any other checkpoint, even from inside nested loops, etc.
// typical state machine code is often littered with gotos, and it's hard to keep track of which goto goes where, and if it is even valid to jump to a particular label from a particular location. By using Duff's Device, we can declare all the states in one place, and then use a simple switch statement to jump to any state from any other state, without worrying about the validity of the jump. This also allows for easy nesting of states, and the ability to easily add new states without having to worry about the existing code structure.
// as well as standarized way of printing logs, and handling errors, etc. For example, we can have a standard way of logging the current state, and the ability to easily jump to a particular state if an error occurs, or if a particular condition is met. This also allows for easy debugging, as we can easily see which state we are in, and how we got there, without having to trace through a bunch of gotos and labels.
// it also potentially allows for stringified states, and a standard way of logging state transitions, etc. This is especially useful for complex state machines with many states and transitions, as it allows for a clear and organized structure, and makes it easier to understand the flow of the state machine. It also allows for easy modification and extension of the state machine, as new states can be added without having to worry about the existing code structure.
// this mechanism requires persistent data though.
// one really good reason for standardizing it intoa  library is the fact that when invoking a recursive state, it potentially requires resuming the host function exactly before the function call (?) (OR NOT) ie Checkpoint_Invoke() or similar... :)
// also, consider using timeouts for transitions too.
//
// FSM_*
//       ie, Checkpoint(State_Init);
//       ie, Checkpoint(State_Scan);
//           if(!scan_ok) {
//                Checkpoint_StartAtNextTime(State_Init);
//           }
//                Checkpoint_GoTo(State_Open);
//
//       ie, Checkpoint(State_Open);
//       ie, Checkpoint(State_Communicate);

#include "PCH.h"
#include "Basic.h"
#include "Assure.h"

Enum(StateMachine_StateType, uint32_t) {
	StateMachine_State_Default = 0,
};

Struct(StateMachine_Scope) {
	/*
	open_at_line is a sanity check to make sure
	that two state machines do not attempt to use the same resume_at_line,
	and that the Peek(), Pop() are used properly to clean up temporary sub states.
	*/
	uint32_t open_at_line = 0;
	uint32_t resume_at_line = 0;
};

Struct(StateMachine) {
	uint32_t depth_nth = 0;
	std::bitset<32> scope_open_mask = 0;
	StateMachine_Scope scope_at_depth[32] = { 0 };
};

StateMachine_Scope* _StateMachine_OpenScope(StateMachine* _, int open_at_line) {
	Assure(_);

	uint32_t candidate_depth_i = _->depth_nth;

	/* here we can check if some RECURSE FLAG was set! */

	/* check if we have room to push this candidate */
	if(!Assure_True(candidate_depth_i < Array_CountOf(_->scope_at_depth))) {
		return null;
	}

	/* set the open mask and return */
	StateMachine_Scope* scope = &_->scope_at_depth[candidate_depth_i];

	/* make sure the last state machine closed its scope */
	Assure(!_->scope_open_mask.test(candidate_depth_i));

	/* check that:
	    (a) the new scope is unassociated.
	    (b) the new scope is associated with this particular open/close pair.
	*/
	if(!Assure_True(!scope->open_at_line || scope->open_at_line == open_at_line)) {
		return null;
	}
	scope->open_at_line = open_at_line;
	// scope->resume_at_line;

	/* okay, the candidate passed our checks. */
	++_->depth_nth;
	_->scope_open_mask.set(candidate_depth_i);
	return scope;
}

void _StateMachine_CloseScope(StateMachine* _) {
	Assure(_);

	/* the depth handle must be "open", not null */
	if(!Assure_True(0 < _->depth_nth && _->depth_nth <= Array_CountOf(_->scope_at_depth))) {
		return;
	}

	uint32_t close_depth_i = (_->depth_nth - 1);
	Assure(_->scope_open_mask.test(close_depth_i));
	_->scope_open_mask.reset(close_depth_i);
	--(_->depth_nth);
}

StateMachine_StateType StateMachine_PeekState(StateMachine* _, bool* out_success = null) {
	Assure(_);

	bool success = true;
	Defer(
		if(out_success) {
			*out_success = success;
		}
	);

	uint32_t child_scope_i = _->depth_nth;

	success &= Assure_True(0 <= child_scope_i && child_scope_i < Array_CountOf(_->scope_at_depth));
	if(!success) {
		return StateMachine_State_Default;
	}
	StateMachine_Scope* child_scope = &_->scope_at_depth[child_scope_i];

	success &= Assure_True(!_->scope_open_mask.test(child_scope_i));
	success &= Assure_True(child_scope->open_at_line);

	if(!success) {
		return StateMachine_State_Default;
	}

	return child_scope->resume_at_line;
}

StateMachine_StateType StateMachine_PopState(StateMachine* _, bool* out_success = null) {
	bool success;
	Defer(
		if(out_success) {
			*out_success = success;
		}
	);

	StateMachine_StateType ret = StateMachine_PeekState(_, &success);

	if(success) {
		uint32_t child_scope_i = _->depth_nth;
		StateMachine_Scope* child_scope = &_->scope_at_depth[child_scope_i];
		child_scope->open_at_line = 0;
		child_scope->resume_at_line = StateMachine_State_Default;
	}

	return ret;
}

void _StateMachine_Scope_HandleUnknownCase(StateMachine_Scope* _) {
	/* (the assure will fail) */
	Assure(_->resume_at_line == StateMachine_State_Default
		, "%d.%d"
		, _->open_at_line
		, _->resume_at_line
	);
	_->resume_at_line = StateMachine_State_Default;
}

#if 0
void _StateMachine_Scope_HandleZeroNullCase(StateMachine_Scope* _, StateMachine_StateType state_default) {
	Assure(_);
	Assure(state_default != StateMachine_State_Default);
	_->resume_at_line = state_default;
}
#endif

void _StateMachine_Scope_SetResumeState_WithDebugInfo(StateMachine_Scope* _, StateMachine_StateType state_next, const char* scope_id, const char* resume_id) {
	Unused(scope_id);
	Unused(resume_id);

	Assure(_);
	// (consider saving the last state for debugging purposes)
	_->resume_at_line = state_next;
}

// todo: ifdef debug
// todo: __PRETTY_FUNCTION__, etc
#define _StateMachine_Scope_SetResumeState(_, state_next, scope_id, resume_id) \
	_StateMachine_Scope_SetResumeState_WithDebugInfo(_ \
		, state_next \
		, scope_id \
		, resume_id \
	) \

#define StateMachine_Switch_Open(_) \
do { \
	StateMachine* _stack = _; \
	StateMachine_Scope* _scope = _StateMachine_OpenScope(_stack, __LINE__); \
	if(_scope) { \
		/* thanks to the Defer, the "return" keyword is safe to use */ \
		Defer(_StateMachine_CloseScope(_stack)); \
		bool _continue = true; \
	_while_##_:; \
		if(0) { goto _while_##_; /* ignore unreferenced label warning */ } \
		while(_continue) { \
			_continue = false; \
			switch(_scope->resume_at_line) { \

#define _StateMachine_State_Case_Open(_STATE) \
				case _STATE:; \
				do /* force the user to open a scope brace */ \

#define _StateMachine_State_Case_Close() \
				while(0); /* close the last scope brace (or nop) */ \

#define StateMachine_DefaultState(_STATE) \
				_StateMachine_State_Case_Close(); \
				break; /* do NOT allow fallthrough */ \
				default: _StateMachine_Scope_HandleUnknownCase(_scope); \
				SWITCH_FallThroughToNextCase(); \
				_StateMachine_State_Case_Open(_STATE) \

#define StateMachine_State(_STATE) \
				_StateMachine_State_Case_Close(); \
				break; /* do NOT allow fallthrough */ \
				SWITCH_FallThroughToNextCase(); \
				_StateMachine_State_Case_Open(_STATE) \

#define StateMachine_Try(_) \
				_StateMachine_State_Case_Close(); \
				_StateMachine_Scope_SetResumeState(_scope, __LINE__, #_, "Try"); \
				SWITCH_FallThroughToNextCase(); \
				_StateMachine_State_Case_Open(__LINE__) \

// todo: for naming, it makes sense to make states have various "stages"
// Yield_ThenTry is NOT allowed inside execution blocks. why? because it defines a case statement.
// Yield_ThenRetry is the expected way to yield inside execution blocks.
#define StateMachine_Yield_ThenTry(_) \
				_StateMachine_State_Case_Close(); \
				_StateMachine_Scope_SetResumeState(_scope, __LINE__, #_, "Yield_ThenTry"); \
				goto _while_##_; \
				case __LINE__: \
				do /* force the user to open a scope brace */ \

#define StateMachine_Yield_ThenRetry(_) \
				_StateMachine_Scope_SetResumeState(_scope, _scope->resume_at_line, #_, "Yield_ThenRetry"); \
				goto _while_##_; \

// (perhaps consider renaming the DebugInfo to be Yield_ThenGoTo)
#define StateMachine_Yield_ThenGoTo(_, _STATE) \
				_StateMachine_State_Case_Close(); \
				_StateMachine_Scope_SetResumeState(_scope, _STATE, #_, "Yield_ThenGoTo"); \
				goto _while_##_; \

#define StateMachine_GoToRetry(_) \
				_StateMachine_State_Case_Close(); /* this call is allowed on switch scope */ \
				Assure(_ == _stack); /* check the id matches */ \
				/*_StateMachine_Scope_SetResumeState(_scope, _STATE, #_, #_STATE);*/ \
				_continue = true; \
				goto _while_##_; \

#define StateMachine_GoTo(_, _STATE) \
				_StateMachine_State_Case_Close(); /* this call is allowed on switch scope */ \
				Assure(_ == _stack); /* check the id matches */ \
				_StateMachine_Scope_SetResumeState(_scope, _STATE, #_, #_STATE); \
				_continue = true; \
				goto _while_##_; \

#define StateMachine_Switch_Close \
				_StateMachine_State_Case_Close(); \
			} \
		} \
	} \
} while(0) \

Enum(MyState, StateMachine_StateType) {
	MyState_A = 0,
	MyState_A_1,
	MyState_A_2,
	MyState_A_3,
	MyState_B,
	MyState_C,
};

void StateMachine_SubDemoAN(StateMachine* sm) {
	StateMachine_Switch_Open(sm);

	StateMachine_State(MyState_A_3) {
		printf(MACRO_Function() " | A3\n");
		StateMachine_Yield_ThenGoTo(sm, StateMachine_State_Default);
	}

	StateMachine_State(MyState_A_2) {
		printf(MACRO_Function() " | A2\n");
		StateMachine_GoTo(sm, MyState_A_3);
	}

	StateMachine_DefaultState(MyState_A_1) {
		printf(MACRO_Function() " | A1\n");
		StateMachine_Yield_ThenGoTo(sm, MyState_A_2);
	}

	StateMachine_Switch_Close;
}

void StateMachine_Demo_Scope(StateMachine* sm) {
	// it is by design that you cannot share data between state brace scopes.
	// try-yield must be scoped appropriately for this, as in a new scope!
	//
	// todo: nested statemachines are basically a version of the 
	// the only way to nest statemachines is inside a Try block:
	// Try {
	//  NestedSM(sm);
	//  RetryNow
	//  RetryLater
	// }
	// Paradigm. Ie, a nested call is like
	// Try {
	//    status = Invoke the nested state machine call
	//    if(status paused) RetryNow;
	//    if(status paused) RetryLater;
	//    if(status unavailable) RetryNow;
	//    if(status unavailable) RetryLater;
	//    RetryBreak;
	// }
	// 
	// The Open { } Block must check that a Try { } has opened
	// (or it is at depth 0)
	//
	// The Open { } and Try { } Must check that no prior submachine state is overwritten,
	// 
	// The Try { } cannot be nested. it will cause a compiler error
	// because cases cannot nest.
	// The Try { } creates a case and sets the _resume_at_line.
	// It checks that the next depth is actually _Nul!!! to ensure that it will not get overwritten!!
	//
	// Callers can figure out a yield becuase the _state != _resume_at_line
	// It is assumed that a state machine has "finished" and (can be reset) if it is set to _Nul (0) and yielded, then it will resume in its DefaultState again.
	//
	// one question is how to skip the Try { } ??
	// ie put a checkpoint below if succeeedde ?
	// i think the answer lies in a double goto situation:
	// 
	// the try will not work nested inside a parent state, though.
	// 
	// todo: the only way SERIAL/Recursive makes any sense, is if A pauses if A.1 pauses. Otherwise you might as well use two separate StateMachine instances, right? Maybe for instance, you can check A.1's state in A, etc.
	//
	// ideally Open AND Close Sanity check when two sub machine are called. you can't do that, you have to wrap it inside an Invoke. Maybe, unless one actually exits with state NUL. ( ie, completed )

	// todo: RIGHT now there is a bug. If you call two subdemos, they do not get saved, they just get cleared... because there isn't parallel execution unless that data is saved somewhere, right? not overwritten?
	// anyway, this gets confusing regardless, the nesting of state machines... and what is parallel vs serial vs pause / resume .. ?

	// todo: the problem with this design is that it doesn't work.
	//       what we actually want with depth is to resume a the correct
	//       execution point. the stack based design doesn't make that much sense
	//       when there are *pause/resume* dynamics involved.
	//       for instance, calling A.SubDemo, it pauses, returns. We could detect that "Close" did not complete, and so chain the Close down the line, right?

	// it's basically not obvious if state machines
	// are serial or parallel. we can assume they are serial 
	// when it is the same object, parallel with multiple StateMachines.
	// then PauseResume actually works with the stack design, because we can detect that the "Close" did not complete, and so chain the Close down the line, right? (ie, if we are in a sub state machine, and we return from it, we can check if the scope was closed or not. if it was not closed, we can assume that we need to close it before resuming the parent state machine. this way, we can have nested state machines with pause/resume dynamics, without having to worry about the execution point of the parent state machine. this also allows for easy nesting of state machines, and the ability to easily add new states without having to worry about the existing code structure.)
	// my question is right, is it always true that we can clear the sub state machine?

	// here we do a depth check to make sure 
	// we're not stepping on another state machine's data.
	StateMachine_Switch_Open(sm);

	StateMachine_DefaultState(MyState_A) {
		printf(MACRO_Function() " | A\n");
	}

	// the differences between a try and a typical state
	// block is that it won't override the parent_state
	// field, so it's easier to print what the current state is.
	// so for instance, you can print MyState_A.Try(Something())
	// as a pretty string.
	// it also makes it clear in the code that it is a sub-stage of a state,
	// that may loop around again to that point.
	// it also signals to the next Open that 
	// recursion is allowed in this block, that yields will
	// recurse properly. so in that sense,
	//
	// it's important to note that the fallthrough "checkpoint" state
	// is different than the resume state. ie, you wouldn't want to
	// reset the sub state machine each time we resume here from the
	// yield, right?
	#if 0
	StateMachine_TryOrYield(StateMachine_SubDemoAN(sm));
	#endif
	// the sub state machine completed successfully,
	// no need to yield, continue.

	// todo: should you be allowed to yield at depth_nth = 0?
	// this should work, ie, resume and fallthrough.

	// in this design, yield and retry IS THE DEFAULT fallthrough behaviour.
	// however, it makes sense for Try blocks to accept fallthrough behaviour,
	// rather than jumping back to retrying the previous States!
	// this is the big difference between Trys and States, States will force a yield,
	// if the current state doesn't match.

	#if 0
	StateMachine_Try(sm) {
		printf(MACRO_Function() " | Before the Yield!\n");
	}

	StateMachine_Yield_ThenTry(sm) {
		printf(MACRO_Function() " | Back from the yield!\n");
	}

	StateMachine_Yield_ThenGoTo(sm, MyState_A);

	StateMachine_Try(sm) {
		printf(MACRO_Function() " | Try, try again!\n");
		for(int a = 0; a < 10; ++a) {
			// this still works
			StateMachine_Yield_ThenRetry(sm);
		}
	}
	#endif

	#if 0
	// it is understood that Try blocks are FLAT "sub states" or "code checkpoints"
	// here we do a depth check to make sure 
	// we're not stepping on another state machine's data.
	StateMachine_Try(SubState_EnumIsOptional) {
		result = DoSomething();
		if(result > 0) StateMachine_GoToRetry();
		if(result < 0) StateMachine_Yield_ThenRetry();
		// ie, set state to _Nul !
		// we're finished here, no need for the caller to yield!
		StateMachine_GoToClose();
	}
	#endif

	// importantely, our fallthrough case must sanity-check that the peekstate() == nul.
	// the resume case doesn't need to sanity check this, it just runs the following block.
	StateMachine_Try(sm) {
		StateMachine_SubDemoAN(sm);

		// the sub state machine has CLOSED. It must have CLOSED
		// because otherwise we wouldn't be executing here.
		// therefore we can assume that it has left its stateline index
		// as a status flag us, (the caller) can check.
		//
		// importantly, we should have the ability to:
		// - "peek" this return value.
		// - "pop" this return value (an reset it)

		// TryOrYield logic:
		StateMachine_StateType child_state = StateMachine_PeekState(sm);
		if(child_state != StateMachine_State_Default) {
			StateMachine_Yield_ThenRetry(sm);
		}
		printf(MACRO_Function() " | Completed SubDemoAN!\n");
		// StateMachine_PopState(sm);

		#if 0
		// "Try Or Retry" logic:
		StateMachine_StateType child_state = StateMachine_Try_PeekState();
		if(child_state != StateMachine_State_Nul) {
			// (Do some optimistic action inline here)
			Sleep(100);

			// immediately retry the state machine call.
			StateMachine_GoToRetry();
		}

		// "Ignore" logic:
		StateMachine_StateType child_state = StateMachine_Try_PopState();
		// do something with the child_state code here...
		Unused(child_state);
		#endif

		// StateMachine_SubDemoAN(sm)
		// handle the state machine manually here.
		// the state machine did not succeed.
	}
	StateMachine_Yield_ThenGoTo(sm, MyState_B);

	// the sub state machine completed successfully,
	// no need to catch / yield, continue.

	#if 0
	StateMachine_TryOrYield(StateMachine_SubDemoAN(sm));
	#endif

	#if 0
		StateMachine_SubDemoAA(sm);
		StateMachine_GoTo(sm, MyState_B);
	#endif

		// any straight, unresuming code goes here
	#if 0
		StateMachine_GoToRetry(); // loops back
		// yields, with the expectation that the caller will
		// not step on our state data.
		StateMachine_Yield_ThenGoToRetry(); 
		// reset the state machine, we're done here
		StateMachine_Yield_ThenGoToDefault();
		//
		StateMachine_Yield_ThenGoTo(...); 
	#endif

	StateMachine_State(MyState_B) {
		printf(MACRO_Function() " | B\n");
	}
	StateMachine_Yield_ThenGoTo(sm, MyState_C);

	StateMachine_State(MyState_C) {
		printf(MACRO_Function() " | C\n");
	}
	StateMachine_Yield_ThenGoTo(sm, StateMachine_State_Default);

	StateMachine_Switch_Close;
}

void StateMachine_Demo() {
	StateMachine sm;
	do {
		StateMachine_Demo_Scope(&sm);
		printf(MACRO_Function() " | Yield\n");
	} while(sm.scope_at_depth[0].resume_at_line);
}
