#include "libdeluge/system.h"
#include <cstdint>

// libdeluge/boot.h: the BSP's boot latch. A unit test never finishes booting, so storage calls take the inline arm.
extern "C" bool deluge_boot_finished(void) noexcept {
	return false;
}

// Host/test build has no preemptive interrupts — code never runs in ISR context.
extern "C" bool deluge_in_interrupt(void) noexcept {
	return false;
}

// Mirrors deluge_in_interrupt above: never in an ISR, so never the audio context.
extern "C" bool deluge_in_audio_context(void) noexcept {
	return false;
}

// Single-threaded tests: nothing can preempt them, so a critical section has nothing to exclude.
extern "C" void ENTER_CRITICAL_SECTION() noexcept {
}
extern "C" void EXIT_CRITICAL_SECTION() noexcept {
}
