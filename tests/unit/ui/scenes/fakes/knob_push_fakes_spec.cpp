/// @file
/// @brief A gold knob's push and a mod button's popup (`services/knob_push.cpp`) over the ports' fakes: the owner's
///        effects and the alternative delay knobs come through the ports the gesture names, and with none the push
///        reads a new chain's effects and the normal delay knobs.

#include "ui/scenes/services/knob_push.h"

#include "board_controls.hpp"
#include "effects/state.h"
#include "gui/l10n/l10n.h"
#include "instruments/classic/deluge_synth.h"
#include "instruments/classic/params.h"
#include "instruments/classic/sound_state.h"
#include "instruments/classic/state_access.h"
#include "project/controller_mapping.h"
#include "project/instrument_slot.h"
#include "project/song.h"
#include "ui/scenes/bind/knob_focus.h"
#include "ui/scenes/bind/ports/ports.h"

#include "fakes.h"

#include "cppspec.hpp"

#include <cstdint>
#include <string>

#include "support/spec_main.h"

using deluge::ui::scenes::bind::KnobFocus;
using deluge::ui::scenes::bind::Ports;
using deluge::ui::scenes::bind::SoundFocus;
using deluge::ui::scenes::bind::test::FakeHost;
using deluge::ui::scenes::services::KnobPush;
using deluge::ui::scenes::services::PushInputs;

namespace {

/// The synth's durable output id; any id works, since the rig's song holds no other output.
constexpr uint32_t kSynthOutputId = 4;
/// The synth's clip.
constexpr deluge::project::ClipAddress kSynthClip{35};
/// The synth's slot: its output's own instrument.
constexpr deluge::project::SlotAddress kSynthSlot{deluge::project::SlotAddress::Kind::OUTPUT, kSynthOutputId};
/// The delay's mod button, whose knobs a new sound maps to the delay's rate and feedback.
constexpr int32_t kDelayButton = 3;
/// The delay's upper knob: its rate, whose push toggles the ping pong.
constexpr int32_t kUpperKnob = 1;

/// @brief A song record holding one synth on the delay's mode, and a fake host whose sound port holds the synth's
///        state with its delay set apart from a new chain's: analog, and no ping pong.
struct Rig {
	FakeHost host;
	deluge::project::Song model;
	Ports ports = host.ports();
	KnobFocus focus{.owner = SoundFocus{.slot = kSynthSlot, .clip = kSynthClip}};

	Rig() {
		deluge::instruments::classic::SoundState state{};
		deluge::effects::EffectStates& fx = deluge::instruments::classic::sound_effects(state);
		fx.delay.type = deluge::effects::DelayType::ANALOG;
		fx.delay.ping_pong = false;
		host.sound.states[kSynthSlot.id] = state;
		deluge::project::DocOutput& output = model.output_structure.getOrCreate(kSynthOutputId);
		output.instrument = deluge::project::InstrumentSlot{
		    .plugin = "deluge.instrument",
		    .state = deluge::plugin::StateHandle::of<deluge::instruments::classic::DelugeSynth>(state)};
		output.mod_knob_mode = kDelayButton;
		// A new sound's delay knobs: the rate on the upper knob, the feedback on the lower.
		output.controls = {
		    {.control = {.button = kDelayButton, .knob = kUpperKnob},
		     .target = deluge::project::engine_target(deluge::instruments::classic::param::kDelayRate.value)},
		    {.control = {.button = kDelayButton, .knob = 0},
		     .target = deluge::project::engine_target(deluge::instruments::classic::param::kDelayFeedback.value)}};
	}
	Rig(const Rig&) = delete;
	Rig& operator=(const Rig&) = delete;

	/// @brief The gesture's context on the synth's clip, the delay's button held, reading through @p through.
	[[nodiscard]] PushInputs gesture(const Ports* through) const {
		return {.button_held = true, .clip_context = true, .board = kDelugeControls, .ports = through};
	}
};

} // namespace

// clang-format off
describe knob_push_fakes("a gold knob's push and a mod button's popup over the ports' fakes", ${
	// The app chooses its language at boot; the popups' names are the built-in English table's.
	before_all([] { deluge::l10n::chosenLanguage = &deluge::l10n::built_in::english; });

	it("reads the owner's effects through the sound port the gesture names", _ {
		Rig rig;
		const KnobPush push =
		    deluge::ui::scenes::services::knob_push(rig.model, rig.focus, kUpperKnob, true, rig.gesture(&rig.ports));
		// The fake's analog delay with no ping pong, toggled on.
		expect(push.popup).to_equal(std::string{"Ping pong: Enabled\nAnalog delay"});
	});

	it("reads a new chain's effects with no ports", _ {
		Rig rig;
		const KnobPush push =
		    deluge::ui::scenes::services::knob_push(rig.model, rig.focus, kUpperKnob, true, rig.gesture(nullptr));
		// A new chain's delay, off with ping pong, toggled off.
		expect(push.popup).to_equal(std::string{"Ping pong: Disabled\nDelay off"});
	});

	it("names the alternative delay page when the controls port has the alternative delay knobs on", _ {
		Rig rig;
		rig.host.controls.alternative_delay = true;
		const std::string popup =
		    deluge::ui::scenes::services::mod_button_popup(rig.model, rig.focus, kDelayButton, rig.gesture(&rig.ports));
		expect(popup).to_start_with(std::string{"Type: "});
	});

	it("names the normal delay page with no ports", _ {
		Rig rig;
		rig.host.controls.alternative_delay = true;
		const std::string popup =
		    deluge::ui::scenes::services::mod_button_popup(rig.model, rig.focus, kDelayButton, rig.gesture(nullptr));
		expect(popup).to_equal(std::string{"Ping pong: Enabled\nDelay off"});
	});
});
// clang-format on

DELUGE_SPEC(knob_push_fakes_spec, knob_push_fakes)
