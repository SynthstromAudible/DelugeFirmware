//
// Created by Mark Adams on 2024-05-11.
//

#ifndef DELUGE_TIMERS_INTERRUPTS_H
#define DELUGE_TIMERS_INTERRUPTS_H

static inline __attribute__((no_instrument_function)) void DISABLE_ALL_INTERRUPTS() {
}

static inline __attribute__((no_instrument_function)) void ENABLE_INTERRUPTS() {
}

#ifdef __cplusplus
/// Host stand-in for the RAII interrupt guard the firmware uses to make a shared read-modify-write
/// atomic against an ISR. These tests are single-threaded, so there is nothing to mask; the real
/// implementation is ARM assembly and cannot link here.
///
/// Needed because this header shadows src/OSLikeStuff/timers_interrupts/timers_interrupts.h on the
/// 32-bit test include path, so anything the real header declares has to be mirrored here or it simply
/// does not exist for these targets.
struct CriticalSectionGuard {
	CriticalSectionGuard() = default;
	~CriticalSectionGuard() = default;
	CriticalSectionGuard(CriticalSectionGuard const&) = delete;
	CriticalSectionGuard& operator=(CriticalSectionGuard const&) = delete;
};
#endif

#endif // DELUGE_TIMERS_INTERRUPTS_H
