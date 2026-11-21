// Strong definitions of the libstdc++ runtime entry points that would otherwise bring in its exception machinery
// (__cxa_throw, std::terminate's catch, the personality routine) and with it libgcc's unwinder. libstdc++.a is built
// with exceptions, so its out-of-line code still calls these. Each archive member replaced here is defined whole --
// every symbol it holds that the firmware reaches -- so the member is never pulled in and nothing is defined twice:
//  - functexcept.o: the std::__throw_* helpers. Allocation failures go to oom(); the rest are logic errors and freeze.
//  - functional.o: std::__throw_bad_function_call (an empty std::function called). std::bad_function_call itself is
//    never constructed without exceptions, so its members are not needed.
//  - pure.o: __cxa_pure_virtual and __cxa_deleted_virtual, whose own versions call std::terminate().
// The helpers libstdc++ defines beside other code (system_error, future_error, ios_failure, regex_error) are not
// reached by the firmware and are left alone.
#if defined(__arm__) && !defined(DELUGE_HOST)

#include "foundation/oom.h"
#include "foundation/panic.h"

#include <bits/functexcept.h>

namespace {
[[noreturn]] void stdFault() {
	FREEZE_WITH_ERROR("STDX");
	__builtin_trap();
}
} // namespace

namespace std {
void __throw_bad_alloc() {
	deluge::oom();
}
void __throw_bad_array_new_length() {
	deluge::oom();
}
void __throw_length_error(const char*) {
	deluge::oom();
}
void __throw_bad_exception() {
	stdFault();
}
void __throw_bad_cast() {
	stdFault();
}
void __throw_bad_typeid() {
	stdFault();
}
void __throw_logic_error(const char*) {
	stdFault();
}
void __throw_domain_error(const char*) {
	stdFault();
}
void __throw_invalid_argument(const char*) {
	stdFault();
}
void __throw_out_of_range(const char*) {
	stdFault();
}
void __throw_out_of_range_fmt(const char*, ...) {
	stdFault();
}
void __throw_runtime_error(const char*) {
	stdFault();
}
void __throw_range_error(const char*) {
	stdFault();
}
void __throw_overflow_error(const char*) {
	stdFault();
}
void __throw_underflow_error(const char*) {
	stdFault();
}
void __throw_bad_function_call() {
	FREEZE_WITH_ERROR("FUNC");
	__builtin_trap();
}
} // namespace std

extern "C" [[noreturn]] void __cxa_pure_virtual() {
	FREEZE_WITH_ERROR("PURE");
	__builtin_trap();
}

extern "C" [[noreturn]] void __cxa_deleted_virtual() {
	FREEZE_WITH_ERROR("PURE");
	__builtin_trap();
}

#endif
