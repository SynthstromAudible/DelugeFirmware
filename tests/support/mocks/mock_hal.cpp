#include "libdeluge/system.h"
#include <cstdint>

// The BSP's C ABI as a spec binary with no board sees it. Interrupt masking and the ISR queries are
// mock_critical_section.cpp's; link both.

// libdeluge/boot.h: the BSP's boot latch. A spec never finishes booting, so storage calls take the inline arm.
extern "C" bool deluge_boot_finished(void) noexcept {
	return false;
}

// memory/heaps.h, read by the structural-edit probe: no spec renders, so nothing runs on the render tier.
namespace deluge::memory {
bool onRenderTier() {
	return false;
}
} // namespace deluge::memory

// libdeluge/scheduler.h: the BSP's clock, read by deluge::runtime::nowUs() (the stall bounds' clock). Time stands
// still in a spec, so no stall bound expires on its own.
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
// No cycle counter: the voice budget reads that as nothing measured.
extern "C" uint32_t deluge_cycle_count() noexcept {
	return 0;
}
extern "C" uint32_t deluge_work_cycle_count() noexcept {
	return 0;
}
extern "C" uint32_t deluge_cycle_hz() noexcept {
	return 0;
}
