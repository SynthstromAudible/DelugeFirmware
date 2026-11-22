#include "gui/ui/shell_host/shell_host.h"
#include "io/midi/midi_follow.h"

#include <cstdint>
#include <optional>

namespace deluge::gui::shell_host {
namespace {

/// MIDI follow's feedback, over the `midiFollow` engine: each method is one call into the engine that
/// `View::sendMidiFollowFeedback` (`origin/main:src/deluge/gui/views/view.cpp`) makes.
class HostMidiFeedback final : public deluge::ui::scenes::bind::MidiFeedback {
public:
	[[nodiscard]] bool enabled() const override { return midiFollow.isFeedbackEnabled(); }
	[[nodiscard]] bool global_effectable_context() const override { return midiFollow.isGlobalEffectableContext(); }
	[[nodiscard]] std::optional<uint8_t> cc_of(project::OwnerPlugin owner, project::ParamTarget target) const override {
		return midiFollow.ccOf(owner, target);
	}
	void send(uint8_t cc, int32_t knob_pos) override { midiFollow.sendCCForMidiFollowFeedback(cc, knob_pos); }
	void send_all(bool automation) override { midiFollow.sendCCWithoutModelStackForMidiFollowFeedback(automation); }
};

} // namespace

deluge::ui::scenes::bind::MidiFeedback& midi_feedback() {
	static HostMidiFeedback feedback;
	return feedback;
}

} // namespace deluge::gui::shell_host
