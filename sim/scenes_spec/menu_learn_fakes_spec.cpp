/// @file
/// @brief The menu's MIDI learn over the ports' fakes, with a model song of the spec's own and no legacy song behind
///        the menu: what learn mode does with a note-on, a CC, a pitch bend and a gold knob on a param, a note-on on a
///        global command, and SHIFT + LEARN's unlearn, each as the learn fake records it.

#include "scenes/bind/ports/learn_sink.h"
#include "scenes/bind/ports/midi_devices.h"
#include "scenes/menu/context.h"
#include "scenes/menu/menu_scene.h"
#include "scenes/menu/nodes/nodes.h"
#include "scenes/menu/nodes/settings.h"

#include "deluge-project/controller_mapping.h"
#include "deluge-project/mapping_table.h"
#include "deluge-project/midi_command.h"
#include "deluge-project/param_address.h"
#include "midi/device_id.h"
#include "shell/profile.h"
#include "shell/scene.h"
#include "ui/core/input.h"

#include "support/fake_menu_shell.h"

#include "cppspec.hpp"
#include "support/printers/learned_binding_printer.h"

#include <cstdint>
#include <optional>
#include <vector>

#include "support/spec_main.h"

using namespace deluge::scenes;
using deluge::project::GlobalCommand;
using deluge::project::MappingTarget;
using deluge::project::OwnerParamTarget;
using deluge::scenes::bind::LearnedBinding;
using deluge::scenes::spec::FakeHostRig;
using deluge::scenes::spec::FakeMenuShell;
using deluge::ui::shell::ButtonId;
using deluge::ui::shell::UiEvent;
using Call = deluge::scenes::bind::test::RecordingLearnSink::Call;

namespace {

namespace nodes = deluge::scenes::menu::nodes;

/// A hosted device, which the MIDI devices fake learns of in `connect_hosted`.
constexpr deluge::midi::DeviceId kHosted = deluge::midi::kFirstHosted;

/// @brief The MIDI devices fake knowing the hosted device as well as DIN.
void connect_hosted(FakeMenuShell& r) {
	r.fakes.host.midi.list.push_back(kHosted);
	r.fakes.host.midi.names.insert_or_assign(kHosted.value, "HOSTED");
	r.fakes.host.midi.kept.insert_or_assign(kHosted.value, deluge::scenes::bind::MidiDeviceSettings{});
}

/// @brief LEARN held while @p event arrives, as the menu takes a MIDI message in learn mode.
/// @return Whether the menu used the message.
bool offer_while_learning(const UiEvent& event) {
	const deluge::ui::shell::ButtonSet held{};
	const deluge::ui::PadMask pads{};
	const deluge::ui::shell::UiContext ui{held, pads, false, false, 0, deluge_profile()};
	(void)menu_scene().handle(UiEvent{.kind = UiEvent::Kind::Button, .on = true, .button = ButtonId::Learn}, ui);
	const bool used = menu_scene().handle(event, ui).handled;
	(void)menu_scene().handle(UiEvent{.kind = UiEvent::Kind::Button, .on = false, .button = ButtonId::Learn}, ui);
	return used;
}

/// @brief The knob target the synth's low-pass cutoff page learns onto: the synth output's controls, the cutoff's
///        engine param. Spelled from the page and the rig, not from the menu's own learn target.
OwnerParamTarget cutoff_target() {
	return OwnerParamTarget{
	    .owner = {.kind = deluge::project::ControlsAddress::Kind::OUTPUT, .id = FakeHostRig::kSynthOutputId},
	    .param = deluge::project::engine_target(nodes::lpf_freq.bind.param.index)};
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
		connect_hosted(r);
		(void)r.open_synth(nodes::lpf_freq);
		const std::optional<OwnerParamTarget> target = menu_scene().learn_target();
		expect(target).to_equal(std::optional{cutoff_target()});
		expect(offer_while_learning(UiEvent{.kind = UiEvent::Kind::MidiCc, .channel = 2, .data1 = 74, .data2 = 64,
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
		expect(offer_while_learning(UiEvent{.kind = UiEvent::Kind::MidiPitchBend, .channel = 5,
		                                    .device = deluge::midi::kDin}))
		    .to_be_true();
		expect(offer_while_learning(UiEvent{.kind = UiEvent::Kind::Encoder,
		                                    .encoder = deluge::ui::EncoderRole::GoldUpper, .delta = 1}))
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
		expect(offer_while_learning(UiEvent{.kind = UiEvent::Kind::MidiNoteOn, .channel = 1, .data1 = 60,
		                                    .data2 = 100, .device = deluge::midi::kDin}))
		    .to_be_true();
		expect(r.fakes.host.learn.calls.size()).to_equal(size_t{0});
	});

	it("learns a note-on onto a global command, carrying the device the note came from", _ {
		FakeMenuShell r;
		connect_hosted(r);
		(void)r.open_settings(nodes::midi_command_play);
		expect(offer_while_learning(UiEvent{.kind = UiEvent::Kind::MidiNoteOn, .channel = 6, .data1 = 48,
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
