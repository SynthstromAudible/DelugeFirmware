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

// libdeluge/scheduler.h: the BSP's clock, read by deluge::runtime::nowUs() (the stall bounds' clock). Time stands
// still in a unit test, so no stall bound expires on its own.
extern "C" double getSystemTime() noexcept {
	return 0.0;
}

// libdeluge/app.h: asks the BSP for an app-loop pass. There is no app loop here.
extern "C" void deluge_app_wake(uint32_t) noexcept {
}

// libdeluge/clock.h: the clock MIDI output times are kept on. Stands still, like getSystemTime() above.
extern "C" uint64_t deluge_clock_now() noexcept {
	return 0;
}
