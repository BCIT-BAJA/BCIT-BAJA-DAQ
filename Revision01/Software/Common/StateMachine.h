//

#pragma once

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
	bool success = true;
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
				/*_StateMachine_BeforeTryFallthrough_SanityCheckForPopState(_);*/ \
				SWITCH_FallThroughToNextCase(); \
				_StateMachine_State_Case_Open(__LINE__) \

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
	MyState_B_1,
	MyState_B_2,
	MyState_B_3,
	MyState_C,
};

/*
StateMachine_Demo_Scope | A
StateMachine_Demo | Yield
StateMachine_SubDemoA | A1
StateMachine_Demo | Yield
StateMachine_SubDemoA | A2
StateMachine_Demo | Yield
StateMachine_SubDemoA | A3
StateMachine_Demo_Scope | Completed SubDemoA!
StateMachine_Demo | Yield
StateMachine_Demo_Scope | B
StateMachine_Demo | Yield
StateMachine_SubDemoB | B1
StateMachine_SubDemoB | B2
StateMachine_SubDemoB | B3
StateMachine_Demo_Scope | Completed SubDemoB!
StateMachine_Demo | Yield
StateMachine_Demo_Scope | C
StateMachine_Demo | Yield
*/
#if 1
void StateMachine_SubDemoA(StateMachine* sm) {
	StateMachine_Switch_Open(sm);

	StateMachine_DefaultState(MyState_A_1) {
		printf(MACRO_Function() " | A1\n");
		StateMachine_Yield_ThenGoTo(sm, MyState_A_2);
	}

	StateMachine_State(MyState_A_2) {
		printf(MACRO_Function() " | A2\n");
		StateMachine_Yield_ThenGoTo(sm, MyState_A_3);
	}

	StateMachine_State(MyState_A_3) {
		printf(MACRO_Function() " | A3\n");
		StateMachine_Yield_ThenGoTo(sm, StateMachine_State_Default);
	}

	StateMachine_Switch_Close;
}

void StateMachine_SubDemoB(StateMachine* sm) {
	StateMachine_Switch_Open(sm);

	StateMachine_DefaultState(MyState_B_1) {
		printf(MACRO_Function() " | B1\n");
		StateMachine_Yield_ThenGoTo(sm, MyState_B_2);
	}

	StateMachine_State(MyState_B_2) {
		printf(MACRO_Function() " | B2\n");
		StateMachine_Yield_ThenGoTo(sm, MyState_B_3);
	}

	StateMachine_State(MyState_B_3) {
		printf(MACRO_Function() " | B3\n");
		StateMachine_Yield_ThenGoTo(sm, StateMachine_State_Default);
	}

	StateMachine_Switch_Close;
}

void StateMachine_Demo_Scope(StateMachine* sm) {
	StateMachine_Switch_Open(sm);

	StateMachine_DefaultState(MyState_A) {
		printf(MACRO_Function() " | A\n");
	}
	StateMachine_Yield_ThenTry(sm) {
		StateMachine_SubDemoA(sm);

		// TryOrYield logic:
		{
			StateMachine_StateType child_state = StateMachine_PeekState(sm);
			if(child_state != StateMachine_State_Default) {
				StateMachine_Yield_ThenRetry(sm);
			}
			StateMachine_PopState(sm);
		}

		printf(MACRO_Function() " | Completed SubDemoA!\n");
		StateMachine_Yield_ThenGoTo(sm, MyState_B);
	}

	StateMachine_State(MyState_B) {
		printf(MACRO_Function() " | B\n");
	}
	StateMachine_Yield_ThenTry(sm) {
		StateMachine_SubDemoB(sm);

		// Retry logic:
		{
			StateMachine_StateType child_state = StateMachine_PeekState(sm);
			if(child_state != StateMachine_State_Default) {
				Sleep(1000);
				StateMachine_GoToRetry(sm);
			}
			StateMachine_PopState(sm);
		}

		printf(MACRO_Function() " | Completed SubDemoB!\n");
		StateMachine_Yield_ThenGoTo(sm, MyState_C);
	}

	StateMachine_State(MyState_C) {
		printf(MACRO_Function() " | C\n");
		StateMachine_Yield_ThenGoTo(sm, StateMachine_State_Default);
	}

	StateMachine_Switch_Close;
}

void StateMachine_Demo() {
	printf("\n");
	StateMachine sm;
	do {
		StateMachine_Demo_Scope(&sm);
		printf(MACRO_Function() " | Yield\n");
	} while(sm.scope_at_depth[0].resume_at_line);

	StateMachine_PopState(&sm);
}
#endif
