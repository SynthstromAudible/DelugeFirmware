#include "playback/trigger_clock_input.h"

#include <libdeluge/cv_gate.h>

namespace deluge::playback {

TriggerClockEdges triggerClockEdges;

namespace {
/// The BSP calls this once per rising edge, on its trigger-clock task. `stamp` is the edge's capture stamp,
/// widened to 64 bits; it fits in 32.
extern "C" void onTriggerClockEdge(uint64_t stamp) noexcept {
	triggerClockEdges.push(static_cast<uint32_t>(stamp));
}
} // namespace

void installTriggerClockInput() noexcept {
	deluge_trigger_clock_set_handler(&onTriggerClockEdge);
}

} // namespace deluge::playback
