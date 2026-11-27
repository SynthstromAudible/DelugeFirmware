/// @file
/// @brief The menu's MIDI learn over the ports' fakes, with a model song of the spec's own and no legacy song behind
///        the menu: what learn mode does with a note-on, a CC, a pitch bend and a gold knob on a param, a note-on on a
///        global command, and SHIFT + LEARN's unlearn, each as the learn fake records it.

#include "ui/scenes/bind/ports/learn_sink.h"
#include "ui/scenes/bind/ports/midi_devices.h"
#include "ui/scenes/menu/context.h"
#include "ui/scenes/menu/menu_scene.h"
#include "ui/scenes/menu/nodes/nodes.h"
#include "ui/scenes/menu/nodes/settings.h"

#include "midi/device_id.h"
#include "project/controller_mapping.h"
#include "project/mapping_table.h"
#include "project/midi_command.h"
#include "project/param_address.h"
#include "ui/shell/profile.h"
#include "ui/shell/scene.h"
#include "ui/widgets/core/input.h"

#include "support/fake_menu_shell.h"

#include "cppspec.hpp"
#include "support/printers/learned_binding_printer.h"

#include <cstdint>
#include <optional>
#include <vector>

#include "support/spec_main.h"

using namespace deluge::ui::scenes;
using deluge::project::GlobalCommand;
using deluge::project::MappingTarget;
using deluge::project::OwnerParamTarget;
using deluge::ui::scenes::bind::LearnedBinding;
using deluge::ui::scenes::spec::FakeHostRig;
using deluge::ui::scenes::spec::FakeMenuShell;
using deluge::ui::shell::UiEvent;
using Call = deluge::ui::scenes::bind::test::RecordingLearnSink::Call;

namespace {

namespace nodes = deluge::ui::scenes::menu::nodes;

/// A hosted device, which the MIDI devices fake learns of through `FakeMidiDevices::connect`.
constexpr deluge::midi::DeviceId kHosted = deluge::midi::kFirstHosted;

/// @brief The knob target the synth's low-pass cutoff page learns onto: the synth output's controls, the cutoff's
///        engine param. Spelled from the page and the rig, not from the menu's own learn target.
OwnerParamTarget cutoff_target() {
	return OwnerParamTarget{
	    .owner = {.kind = deluge::project::ControlsAddress::Kind::OUTPUT, .id = FakeHostRig::kSynthOutputId},
	    .param = deluge::project::engine_target(nodes::lpf_freq.bind.param.id->value)};
}

/// @brief The calls of @p kind the learn fake recorded.
std::vector<Call> calls_of(const FakeMenuShell& r, Call::Kind kind) {
	std::vector<Call> out;
	for (const Call& call : r.fakes.host.learn.calls) {
		if (call.kind == kind) {
			out.push_back(call);
		}
	}
	return out;
}

} // namespace

// clang-format off
describe menu_learn_fakes("the menu's MIDI learn over the ports' fakes and a model song", ${
	it("learns a CC onto the focused param as a knob, from the device and the channel it came on", _ {
		FakeMenuShell r;
		r.fakes.host.midi.connect(kHosted, "HOSTED");
		(void)r.open_synth(nodes::lpf_freq);
		const std::optional<OwnerParamTarget> target = menu_scene().learn_target();
		expect(target).to_equal(std::optional{cutoff_target()});
		expect(r.offer_while_learning(UiEvent{.kind = UiEvent::Kind::MidiCc, .channel = 2, .data1 = 74, .data2 = 64,
		                                      .device = kHosted}))
		    .to_be_true();
		const std::vector<Call> knobs = calls_of(r, Call::Kind::LEARN_KNOB);
		expect(knobs.size()).to_equal(size_t{1});
		if (knobs.size() == 1) {
			expect(knobs[0].target).to_equal(std::optional<MappingTarget>{cutoff_target()});
			expect(knobs[0].device.value).to_equal(kHosted.value);
			expect(knobs[0].which_knob).to_equal(74);
			expect(knobs[0].midi_channel).to_equal(2);
		}
		expect(r.fakes.host.learn.calls.size()).to_equal(size_t{1});
	});

	it("learns a pitch bend as knob 128 and a gold knob as a mod knob with no device", _ {
		FakeMenuShell r;
		(void)r.open_synth(nodes::lpf_freq);
		expect(r.offer_while_learning(UiEvent{.kind = UiEvent::Kind::MidiPitchBend, .channel = 5,
		                                      .device = deluge::midi::kDin}))
		    .to_be_true();
		expect(r.offer_while_learning(UiEvent{.kind = UiEvent::Kind::Encoder,
		                                      .encoder = deluge::ui::widgets::core::EncoderRole::GoldUpper, .delta = 1}))
		    .to_be_true();
		const std::vector<Call> knobs = calls_of(r, Call::Kind::LEARN_KNOB);
		expect(knobs.size()).to_equal(size_t{2});
		if (knobs.size() == 2) {
			expect(knobs[0].which_knob).to_equal(128);
			expect(knobs[0].device.value).to_equal(deluge::midi::kDin.value);
			expect(knobs[0].midi_channel).to_equal(5);
			expect(knobs[1].which_knob).to_equal(1);
			expect(knobs[1].device.value).to_equal(uint32_t{0});
			expect(knobs[1].midi_channel).to_equal(255);
		}
	});

	it("takes a note-on on a param in learn mode and learns nothing from it, as no param learns a note", _ {
		FakeMenuShell r;
		(void)r.open_synth(nodes::lpf_freq);
		expect(r.offer_while_learning(UiEvent{.kind = UiEvent::Kind::MidiNoteOn, .channel = 1, .data1 = 60,
		                                      .data2 = 100, .device = deluge::midi::kDin}))
		    .to_be_true();
		expect(r.fakes.host.learn.calls.size()).to_equal(size_t{0});
	});

	it("learns a note-on onto a global command, carrying the device the note came from", _ {
		FakeMenuShell r;
		r.fakes.host.midi.connect(kHosted, "HOSTED");
		(void)r.open_settings(nodes::midi_command_play);
		expect(r.offer_while_learning(UiEvent{.kind = UiEvent::Kind::MidiNoteOn, .channel = 6, .data1 = 48,
		                                      .data2 = 100, .device = kHosted}))
		    .to_be_true();
		const LearnedBinding want{.device = kHosted, .channel_or_zone = 6, .note_or_cc = 48};
		const std::vector<Call> globals = calls_of(r, Call::Kind::LEARN_GLOBAL);
		expect(globals.size()).to_equal(size_t{1});
		if (globals.size() == 1) {
			expect(globals[0].global).to_equal(GlobalCommand::PLAY);
			expect(globals[0].binding).to_equal(std::optional{want});
		}
		expect(calls_of(r, Call::Kind::LEARN).size()).to_equal(size_t{0});
		expect(r.fakes.host.midi.global_command(GlobalCommand::PLAY)).to_equal(std::optional{want});
	});

	it("unlearns the focused param's knob on SHIFT + LEARN as a learn of no command", _ {
		FakeMenuShell r;
		(void)r.open_synth(nodes::lpf_freq);
		const OwnerParamTarget target = cutoff_target();
		expect(menu_scene().learn_target()).to_equal(std::optional{target});
		// The song has a knob on the param, so there is something to forget.
		(void)r.fakes.model.mappings.learn(deluge::project::Mapping{
		    .source = deluge::project::MidiCommand{.channel_or_zone = 2, .note_or_cc = 74}, .target = target});
		r.shift_learn(100);
		const std::vector<Call> learns = calls_of(r, Call::Kind::LEARN);
		expect(learns.size()).to_equal(size_t{1});
		if (learns.size() == 1) {
			expect(learns[0].target).to_equal(std::optional<MappingTarget>{target});
			expect(learns[0].command).not_().to_have_value();
			expect(learns[0].input).not_().to_have_value();
		}
		expect(calls_of(r, Call::Kind::LEARN_KNOB).size()).to_equal(size_t{0});
	});

	it("unlearns a global command on SHIFT + LEARN as a learn of no binding", _ {
		FakeMenuShell r;
		r.fakes.host.midi.globals.insert_or_assign(
		    GlobalCommand::TAP, LearnedBinding{.device = deluge::midi::kDin, .channel_or_zone = 1, .note_or_cc = 40});
		(void)r.open_settings(nodes::midi_command_tap);
		r.shift_learn(100);
		const std::vector<Call> globals = calls_of(r, Call::Kind::LEARN_GLOBAL);
		expect(globals.size()).to_equal(size_t{1});
		if (globals.size() == 1) {
			expect(globals[0].global).to_equal(GlobalCommand::TAP);
			expect(globals[0].binding).not_().to_have_value();
		}
		expect(r.fakes.host.midi.global_command(GlobalCommand::TAP)).not_().to_have_value();
	});
});
// clang-format on

DELUGE_SPEC(menu_learn_fakes_spec, menu_learn_fakes)
