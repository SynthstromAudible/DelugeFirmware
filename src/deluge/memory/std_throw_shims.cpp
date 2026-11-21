// Strong definitions of the throw helpers libstdc++ keeps in functexcept.o. libstdc++.a is built with exceptions, so
// its out-of-line code still calls these; defining every one functexcept.o defines keeps that member (and with it
// __cxa_throw and the unwinder) out of the image. Allocation failures go to oom(); the rest are logic errors and
// freeze. The helpers libstdc++ defines in other members (bad_function_call, system_error, future_error, ios_failure,
// regex_error) sit beside code those members also hold, so they are not overridden here.
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
} // namespace std

#endif
