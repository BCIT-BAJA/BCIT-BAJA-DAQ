//

#pragma once

#include "PCH.h"
#include "Compile.h"

/*
1. The "Action-Oriented" Scheme (Recommended)
This is best for Audit_True macros because it tells you exactly what the code will do next.
    PASSIVE (Lite): Just record to TLS. Don't interrupt the flow.
    ADVISORY (Warn): Record to TLS + log to file/console. "Something is weird."
    TRANSIENT (Retry): Record + Log + trigger a retry-counter logic.
    CRITICAL (Halt): Record + Log + MessageBox + Block thread.
    FATAL (Abort): Record + Log + MessageBox + Process Exit.
2. The "Syslog" Classic (Standard)
If you want to follow the naming conventions used by Linux/Unix and professional Windows services.
    DEBUG: Silent recording for developer inspection.
    INFO: Normal operational milestones.
    NOTICE: Significant but normal events.
    WARNING: Error handled, but might lead to problems.
    ERROR: Operation failed, but the handle is still alive.
    CRITICAL: Hardware failure or lost connection.
    ALERT: Action must be taken immediately (The MessageBox tier).
    EMERGENCY: System is unusable.
3. The "Serial Protocol" Scheme
Tailored specifically for your Win32 Comm project.
    SILENT: Background GetLastError recording.
    TRACE: Record the recursive call stack for this specific failure.
    GLITCH: Non-fatal hardware errors (Frame/Parity/Overrun).
    STALL: Timeouts or pending I/O that requires a wait.
    RECAP: Close handle and attempt a re-initialization.
    PANIC: Full UI intervention required.
*/

// so, what we need is a generic ThreadLocal Error Tracing Mechanism that allows errors to be recursively chained.
// although for now, we can just do a single level for simplicity's sake. Ie TraceError() has some sort of clever scope mechanism using the period keyword, etc.
// for now though, so as not to confuse between Assure and this new tracing mechanism, we're going to call it simply Trace()

// todo: consider stripping the actual #c & __FILE__, but keep __LINE__ and __VA_ARGS__ in release builds !
// todo: check the platform here to minimize codesize bloat.
//       just use full compiler strings on Windows.

#define _AssertMetadata_DeclareArguments \
	uint32_t metadata_file_line, \
	const char* metadata_condition_str, \
	const char* metadata_file_str, \
	const char* metadata_function_signature_str \

#define _AssertMetadata_ProvideArguments(c) \
	__LINE__, \
	#c, \
	__FILE__, \
	MACRO_FunctionSignature() \

bool _Assert_OnConditionFalse(_AssertMetadata_DeclareArguments, const char* fmt, ...) c_fmt(2);

#define _Assert_True(c, ...) ( \
	(bool)((c) || _Assert_OnConditionFalse( \
		_AssertMetadata_ProvideArguments(c) \
		, "" __VA_ARGS__ \
	)) \
)

/* an "Assert" is completely compiled out in Release builds */
#if c_config(release)
#define Assert_True(c, ...) (void)0
#else
#define Assert_True(c, ...) do { _Assert_True(c, __VA_ARGS__); } while(0)
#endif

/* a "Test" will still execute in Release builds, (and Assert in debug builds) */
#if c_config(release)
#define Test_True(c, ...) (c)
#else
#define Test_True(c, ...) _Assert_True(c, __VA_ARGS__)
#endif

// Audits require a strictness level, to maybe produce a message box, or not.

Struct(AuditMetadata) {
	uint32_t file_line = 0;
	const char* condition_str = null;
#if c_config(debug)
	// todo: it would be really helpful to put this into a separate Struct name. Ie .Debug. ... to not confuse users.
	const char* debug_file_str = null;
	const char* debug_function_signature_str = null;
#endif
};

#if c_config(debug)
#define _Auditor_Metadata_DeclareArguments \
	uint32_t metadata_file_line, \
	const char* metadata_condition_str, \
	const char* metadata_file_str, \
	const char* metadata_function_signature_str \

#else
#define _Auditor_Metadata_DeclareArguments \
	uint32_t metadata_file_line, \
	const char* metadata_condition_str \

#endif

#define _Auditor_Metadata_ProvideArguments(c) \
	__LINE__, \
	#c, \
	__FILE__, \
	MACRO_FunctionSignature() \

#define _Audit_True(ConditionTypeCheckerFunction, c, ...) (bool)( \
	(true \
		&& _Audit_Open() \
		&& ConditionTypeCheckerFunction(c) \
		&& _Audit_Close_OnConditionTrue() \
	) \
	|| _Audit_Close_OnConditionFalse(_Auditor_Metadata_ProvideArguments(c), "" __VA_ARGS__) \
) \

// now we can ensure that throws are caught, forcing the caller to Pop() at the correct scope?
// and the point of throw / catch. (constructor)

// this at LEAST makes it easier to detect a forgotten Pop(), even if the audit succeeds.

// if this fails the caller is expected to Pop() !!!
#define Audit_True(c, ...) _Audit_True(, c, __VA_ARGS__)

// if this fails the caller is expected to Pop() !!!
// the only point of the Audit object is to ensure that the user doesn't forget to add logic to Pop an expected yielded false condition!
#define Audit_AuditFailed(c, ...) (!_Audit_True(_Audit_ConditionTypeChecker, c, __VA_ARGS__))

#define Audit_ReturnIfUntrue(c, ...) \
	if(!_Audit_True(, c, __VA_ARGS__)) { \
		/* todo, in debug builds, maybe Log() this!! */; \
		return Audit(); \
	} \

#define Audit_ReturnIfAuditFailed(c, ...) \
	if(!_Audit_True(_Audit_ConditionTypeChecker, c, __VA_ARGS__)) { \
		/* todo, in debug builds, maybe Log() this!! */; \
		return Audit(); \
	} \

typedef uint64_t AuditError;
#define AuditError_None 0

extern ThreadLocal AuditError t_audit_error;
Inline void Audit_SetLastError(AuditError e) {
	t_audit_error = e;
}
Inline AuditError Audit_GetLastError() {
	return t_audit_error;
}

Struct(AuditScope) {
	AuditError audit_error = 0;
	OS_ErrorType os_error = 0;
	std::string format_error_str;
	AuditMetadata metadata;
};

#define Auditor_MaxScopeDepth 32

Struct(Auditor) {
	uint32_t depth_nth = 0;
	std::bitset<Auditor_MaxScopeDepth> scope_open_mask = 0; // scope_i is open
	std::bitset<Auditor_MaxScopeDepth> scope_unpoppedfail_mask = 0; // scope_i contains unpopped-fail.
	AuditScope scope_at_depth[Auditor_MaxScopeDepth];
};

Struct(AuditPeek) {
	AuditError audit_error = 0;
	OS_ErrorType os_error = 0;
};

Struct(AuditStack) {
	uint32_t auditor_depth_i = 0;
	uint32_t scopes_n = 0;
	AuditScope scopes[Auditor_MaxScopeDepth];
};

extern ThreadLocal Auditor t_auditor;

// this forces the user to construct
Struct(Audit) {
	bool success;

	Audit(/* error code? */) {
		Auditor* _ = &t_auditor;
		const uint32_t host_scope_i = (_->depth_nth);
		Assert_True(host_scope_i < Array_CountOf(_->scope_at_depth));

		this->success = (!_->scope_unpoppedfail_mask.test(host_scope_i));
	}

	#if 0
	Audit(AuditError e) {
		if(e) {
			this->success = false;
			return;
		}

		Auditor* _ = &t_auditor;
		const uint32_t host_scope_i = (_->depth_nth);
		Assert_True(host_scope_i < Array_CountOf(_->scope_at_depth));

		this->success = (!_->scope_unpoppedfail_mask.test(host_scope_i));
	}
	#endif
};

// Inline bool _Audit_ConditionTypeChecker(bool b) { return b; }
Inline bool _Audit_ConditionTypeChecker(Audit a) { return a.success; }

Inline bool _Audit_Open() {
	Auditor* _ = &t_auditor;

	// todo: we need to check if there is a host Audit??

	if(!Test_True((_->depth_nth + 1) <= Array_CountOf(_->scope_at_depth)
		, "You exceeded the maximum Audit Depth of %u"
		, Array_CountOf(_->scope_at_depth)
	)) {
		return true;
	}

	uint32_t candidate_depth_i = (_->depth_nth)++;

	bool reset_host_and_children = false;
	reset_host_and_children |= Test_True(!_->scope_open_mask.test(candidate_depth_i));

	{
		auto& is_unpoppedfail =_->scope_unpoppedfail_mask;
		reset_host_and_children |= Test_True(!is_unpoppedfail.test(candidate_depth_i)
			, "You forgot to Pop the last failed audit."
		);

		if(reset_host_and_children) {
			for(uint32_t d_i = candidate_depth_i; d_i < Array_CountOf(_->scope_at_depth); ++d_i) {
				_->scope_open_mask.reset(d_i);
				is_unpoppedfail.reset(d_i);
			}
		}
	}

	_->scope_open_mask.set(candidate_depth_i);

	return true;
}

Inline bool _Audit_Close_OnConditionTrue() {
	Auditor* _ = &t_auditor;

	if(!(Test_True(0 < _->depth_nth && _->depth_nth <= Array_CountOf(_->scope_at_depth)))) {
		return true;
	}

	uint32_t current_depth_i = --(_->depth_nth);

	Assert_True(_->scope_open_mask.test(current_depth_i));
	_->scope_open_mask.reset(current_depth_i);
	_->scope_unpoppedfail_mask.reset(current_depth_i);

	return true;
}

bool _Audit_Close_OnConditionFalse(_Auditor_Metadata_DeclareArguments, const char* format, ...) c_fmt(2);

AuditPeek Audit_PeekChild();
void Audit_Pop(AuditStack* out = null);
void Audit_Push(const AuditStack* in);

static bool Audit_DemoSub() {
	Audit_SetLastError(0xDeadBeef);
	if(!Audit_True(false, "" MACRO_FunctionSignature())) {
		AuditStack stack;
		Audit_Pop(&stack);
		Audit_Push(&stack);
		Audit_SetLastError(123);
		return false;
	}
	COMPILER_Unreachable;
	return true;
}

static bool Audit_Demo_A() {
	if(!Audit_True(false, "oh, %s", "shit!")) {
		// since the audit failed, we are guaranteed to have >= 1 Audit Scope(s) that haven't closed.
		// if we simply return false here, we leave this Audit "hanging"
		return false;
	}
	COMPILER_Unreachable;
}

static bool Audit_Demo_B() {
	if(!Audit_True(false, "oh, %s", "shit!")) {
		/* (pretend that we handle the error here) */
		// the next Audit should fail, since we forgot to Drop the stack before auditing anew!
		Audit_True(true); 
		return true;
	}
	COMPILER_Unreachable;
}

static bool Audit_Demo_C() {
	if(!Audit_True(false, "oh, %s", "shit!")) {
		Audit_Pop();
		// if we return true here, then the unclosed scopes have effectively been ignored.
		// before we call any proceeding Audit_True()
		return Audit_True(true);
	}
	COMPILER_Unreachable;
}

static bool Audit_Demo_D() {
	if(!Audit_True(false, "" MACRO_FunctionSignature())) {
		AuditStack audit;
		Audit_Pop(&audit);
		printf("%s\n", audit.scopes[0].format_error_str.c_str());
		return true;
	}
	COMPILER_Unreachable;
}

static bool Audit_Demo_E() {
	if(!Audit_True(Audit_DemoSub(), "" MACRO_FunctionSignature())) {
		if(Test_True(Audit_PeekChild().audit_error == 123)) {
			Audit_SetLastError(456);
		}

		AuditStack stack;
		Audit_Pop(&stack);
		stack.auditor_depth_i;
		stack.scopes_n;
		stack.scopes[0].os_error;
		printf("%s\n", stack.scopes[0].format_error_str.c_str());

		if(!Audit_True(false, "(" MACRO_FunctionSignature() ")")) {
			Audit_Pop();
		}

		Audit_Push(&stack);
		Audit_Pop(&stack);
		Audit_Push(&stack);
		Audit_Pop(&stack);
		Audit_Push(&stack);
		return false;
	}

	COMPILER_Unreachable;
}

static bool Audit_Demo_F() {
	;
}

static void Audit_Demo() {
#if 0
	// AuditStack stack; Audit_Push(&stack); // should fail...

	Audit_Demo_C();
	Audit_Demo_D();
	Audit_Demo_C();

	// Audit_Demo_E(); // this should fail...

	if(!Audit_True(Audit_Demo_E(), "" MACRO_FunctionSignature())) {
		AuditStack stack;
		Audit_Pop(&stack);
		stack.scopes[0].audit_error;
		stack.scopes[0].os_error;
		printf("%s\n", stack.scopes[0].format_error_str.c_str());
	}

	// Audit_PeekChild(); // this should fail...
	// Audit_Pop(); // this should fail...

	// Audit_Demo_A(); // this should fail
	// Audit_Demo_B(); // this should fail
#endif
}



