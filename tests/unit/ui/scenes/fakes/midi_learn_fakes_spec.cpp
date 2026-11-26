/// @file
/// @brief MIDI learn (`services/midi_learn.cpp`) over the ports' fakes: what it learns through the learn port, from
///        which device, when it saves the settings, and what it does with no ports installed.

#include "ui/scenes/services/midi_learn.h"
#include "ui/scenes/services/view_state.h"

#include "midi/device_id.h"
#include "project/clip_address.h"
#include "project/device_settings.h"
#include "project/mapping_table.h"
#include "project/midi_command.h"
#include "project/song.h"
#include "project/track_kind.h"
#include "ui/shell/profile.h"
#include "ui/shell/profiles/deluge.h"
#include "ui/shell/scene.h"
#include "ui/shell/scene_stack.h"
#include "ui/widgets/core/input.h"
#include "ui/widgets/core/led_frame.h"

#include "fakes.h"

#include "cppspec.hpp"
#include "support/printers/learn_input_printer.h"
#include "support/printers/learned_binding_printer.h"
#include "support/printers/mapping_printer.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <utility>

#include "support/spec_main.h"

using deluge::project::GlobalCommand;
using deluge::project::MidiCommand;
using deluge::project::MidiDeviceRef;
using deluge::ui::scenes::bind::LearnedBinding;
using deluge::ui::scenes::bind::Ports;
using deluge::ui::scenes::bind::test::FakeHost;
using deluge::ui::scenes::bind::test::RecordingDeviceActions;
using deluge::ui::scenes::bind::test::RecordingLearnSink;
using deluge::ui::scenes::services::LearnTarget;
using deluge::ui::shell::ButtonId;
using deluge::ui::shell::Result;
using deluge::ui::shell::Scene;
using deluge::ui::shell::SceneKind;
using deluge::ui::shell::SceneRegistry;
using deluge::ui::shell::SceneStack;
using deluge::ui::shell::UiContext;
using deluge::ui::shell::UiEvent;
using Call = RecordingLearnSink::Call;
using LearnService = deluge::ui::scenes::services::MidiLearn;

namespace {

/// A device the MIDI devices fake does not know.
constexpr deluge::midi::DeviceId kUnknown{0xFFFF'FFF0};

/// The root scene's slot in the rig's registry: its kind's index.
constexpr size_t kRootSlot = std::to_underlying(SceneKind::FirstApp);

/// @brief The root view, which the learn's button arms are gated on.
class RootScene final : public Scene {
public:
	[[nodiscard]] SceneKind kind() const override { return SceneKind::FirstApp; }
	[[nodiscard]] bool is_root() const override { return true; }
	Result handle(const UiEvent&, const UiContext&) override { return Result::unhandled(); }
};

/// @brief A `MidiLearn` over a root scene, with the fakes' ports and an empty song record installed.
struct Rig {
	RootScene root;
	std::array<Scene*, kRootSlot + 1> table{};
	SceneRegistry reg{};
	SceneStack stack{reg};
	LearnService ml{stack};
	FakeHost host;
	const Ports ports = host.ports();
	deluge::project::Song model;
	deluge::ui::shell::ButtonSet held{};
	deluge::ui::widgets::core::PadMask pads{};

	Rig() {
		table[kRootSlot] = &root;
		reg.by_kind = std::span<Scene* const>{table};
		(void)stack.push({SceneKind::FirstApp});
		ml.set_ports(&ports);
		ml.set_model(&model);
	}
	Rig(const Rig&) = delete;
	Rig& operator=(const Rig&) = delete;
	~Rig() {
		while (stack.depth() > 0) {
			stack.pop(SceneKind::Null);
		}
	}

	/// @brief Route @p event to the learn.
	Result send(const UiEvent& event) {
		return ml.handle(event, UiContext{held, pads, false, false, 0, deluge::ui::shell::deluge_profile()});
	}
	/// @brief Press or release @p button.
	Result button(ButtonId button, bool on) {
		return send(UiEvent{.kind = UiEvent::Kind::Button, .on = on, .button = button});
	}
	/// @brief A note-on from @p device.
	Result note_on(deluge::midi::DeviceId device, uint8_t channel, uint8_t note) {
		return send(UiEvent{
		    .kind = UiEvent::Kind::MidiNoteOn, .channel = channel, .data1 = note, .data2 = 100, .device = device});
	}
	/// @brief A CC from @p device.
	Result cc(deluge::midi::DeviceId device, uint8_t channel, uint8_t number, uint8_t value) {
		return send(UiEvent{
		    .kind = UiEvent::Kind::MidiCc, .channel = channel, .data1 = number, .data2 = value, .device = device});
	}
};

/// @brief The state @p frame declares for @p button's LED, or none.
std::optional<deluge::ui::widgets::core::LedState> led_of(const deluge::ui::widgets::core::LedFrame& frame,
                                                          ButtonId button) {
	for (const auto& [id, state] : frame.entries()) {
		if (static_cast<uint8_t>(id) == static_cast<uint8_t>(button)) {
			return state;
		}
	}
	return std::nullopt;
}

} // namespace

// clang-format off
describe midi_learn_fakes("scenes::services::MidiLearn -- learns through its installed ports", ${
	it("learns a note-on from DIN onto a held clip's mute, naming DIN by its song reference", _ {
		Rig rig;
		expect(rig.button(ButtonId::Learn, true).handled).to_be_true();
		expect(rig.ml.active()).to_be_true();
		const LearnTarget clip = LearnTarget::clip(deluge::project::ClipAddress{.clip_id = 7});
		rig.ml.thing_pressed(clip, true);
		expect(rig.note_on(deluge::midi::kDin, 3, 60).handled).to_be_true();
		expect(rig.host.learn.calls.size()).to_equal(size_t{1});
		if (rig.host.learn.calls.size() == 1) {
			const Call& call = rig.host.learn.calls[0];
			expect(call.kind).to_equal(Call::Kind::LEARN);
			expect(call.target).to_equal(std::optional<deluge::project::MappingTarget>{deluge::project::ClipMuteTarget{7}});
			expect(call.command).to_equal(std::optional{MidiCommand{
			    .channel_or_zone = 3, .note_or_cc = 60, .device = rig.host.midi.refs[deluge::midi::kDin.value]}});
		}
	});
	it("learns PLAY to a note-on's device, channel and note, and unlearns it on a release with nothing learned", _ {
		Rig rig;
		(void)rig.button(ButtonId::Learn, true);
		expect(rig.button(ButtonId::Play, true).handled).to_be_true();
		(void)rig.note_on(deluge::midi::kDin, 2, 64);
		expect(rig.button(ButtonId::Play, false).handled).to_be_true();
		expect(rig.button(ButtonId::Play, true).handled).to_be_true();
		expect(rig.button(ButtonId::Play, false).handled).to_be_true();
		expect(rig.host.learn.calls.size()).to_equal(size_t{2});
		if (rig.host.learn.calls.size() == 2) {
			expect(rig.host.learn.calls[0].kind).to_equal(Call::Kind::LEARN_GLOBAL);
			expect(rig.host.learn.calls[0].global).to_equal(GlobalCommand::PLAY);
			expect(rig.host.learn.calls[0].binding)
			    .to_equal(std::optional{LearnedBinding{.device = deluge::midi::kDin, .channel_or_zone = 2, .note_or_cc = 64}});
			expect(rig.host.learn.calls[1].kind).to_equal(Call::Kind::LEARN_GLOBAL);
			expect(rig.host.learn.calls[1].global).to_equal(GlobalCommand::PLAY);
			expect(rig.host.learn.calls[1].binding.has_value()).to_be_false();
		}
	});
	it("saves the settings when LEARN is released after a global learn, unless the audio routine is running", _ {
		Rig rig;
		(void)rig.button(ButtonId::Learn, true);
		(void)rig.button(ButtonId::TapTempo, true);
		(void)rig.note_on(deluge::midi::kDin, 1, 40);
		(void)rig.button(ButtonId::TapTempo, false);
		(void)rig.button(ButtonId::Learn, false);
		expect(rig.host.device_actions.calls.size()).to_equal(size_t{1});
		if (rig.host.device_actions.calls.size() == 1) {
			expect(rig.host.device_actions.calls[0]).to_equal(RecordingDeviceActions::Kind::SAVE_SETTINGS);
		}

		Rig busy;
		busy.host.audio.busy = true;
		(void)busy.button(ButtonId::Learn, true);
		(void)busy.button(ButtonId::TapTempo, true);
		(void)busy.note_on(deluge::midi::kDin, 1, 40);
		(void)busy.button(ButtonId::TapTempo, false);
		(void)busy.button(ButtonId::Learn, false);
		expect(busy.host.learn.calls.size()).to_equal(size_t{1});
		expect(busy.host.device_actions.calls.empty()).to_be_true();
	});
	it("flashes the PLAY LED while learning when PLAY's command is learned", _ {
		Rig rig;
		(void)rig.button(ButtonId::Learn, true);
		deluge::ui::widgets::core::LedFrame unlearned;
		rig.ml.leds(unlearned);
		expect(led_of(unlearned, ButtonId::Play).has_value()).to_be_false();

		rig.host.midi.globals[GlobalCommand::PLAY] =
		    LearnedBinding{.device = deluge::midi::kDin, .channel_or_zone = 0, .note_or_cc = 36};
		deluge::ui::widgets::core::LedFrame learned;
		rig.ml.leds(learned);
		const std::optional<deluge::ui::widgets::core::LedState> play = led_of(learned, ButtonId::Play);
		expect(play.has_value()).to_be_true();
		if (play) {
			expect(rig.ml.flash_on()).to_be_true();
			expect(play->kind).to_equal(deluge::ui::widgets::core::LedState::Kind::On);
		}
		expect(led_of(learned, ButtonId::Record).has_value()).to_be_false();
	});
	it("learns PLAY from a device the MIDI devices port does not know as a binding with no device", _ {
		Rig rig;
		(void)rig.button(ButtonId::Learn, true);
		(void)rig.button(ButtonId::Play, true);
		rig.ml.note_on_received(kUnknown, 4, 50, 100);
		expect(rig.host.learn.calls.size()).to_equal(size_t{1});
		if (rig.host.learn.calls.size() == 1) {
			expect(rig.host.learn.calls[0].kind).to_equal(Call::Kind::LEARN_GLOBAL);
			expect(rig.host.learn.calls[0].global).to_equal(GlobalCommand::PLAY);
			expect(rig.host.learn.calls[0].binding)
			    .to_equal(std::optional{LearnedBinding{.channel_or_zone = 4, .note_or_cc = 50}});
		}
	});
	it("leaves a note-on from a device it does not know unhandled, and learns nothing", _ {
		Rig rig;
		(void)rig.button(ButtonId::Learn, true);
		rig.ml.thing_pressed(LearnTarget::clip(deluge::project::ClipAddress{.clip_id = 7}), true);
		expect(rig.note_on(kUnknown, 3, 60).handled).to_be_false();
		expect(rig.host.learn.calls.empty()).to_be_true();
	});
	it("with no ports installed, knows no device: learns nothing and leaves MIDI unhandled", _ {
		Rig rig;
		rig.ml.set_ports(nullptr);
		expect(rig.button(ButtonId::Learn, true).handled).to_be_true();
		rig.ml.thing_pressed(LearnTarget::clip(deluge::project::ClipAddress{.clip_id = 7}), true);
		expect(rig.note_on(deluge::midi::kDin, 3, 60).handled).to_be_false();
		rig.ml.note_on_received(deluge::midi::kDin, 3, 60, 100);
		rig.ml.thing_pressed(LearnTarget::clip(deluge::project::ClipAddress{.clip_id = 7}), false);
		(void)rig.button(ButtonId::Learn, false);
		expect(rig.host.learn.calls.empty()).to_be_true();
		expect(rig.host.device_actions.calls.empty()).to_be_true();
	});
	it("learns a CC's channel as a MIDI track's input when the track holds none, naming DIN", _ {
		Rig rig;
		const deluge::ui::scenes::services::MenuView was = deluge::ui::scenes::services::view_state().current_view();
		deluge::ui::scenes::services::view_state().set_current_view(
		    {.clip = deluge::project::ClipAddress{.clip_id = 3}, .kind = deluge::project::TrackKind::MIDI_OUT});
		(void)rig.button(ButtonId::Learn, true);
		rig.ml.thing_pressed(LearnTarget::instrument_input(9), true);
		expect(rig.cc(deluge::midi::kDin, 5, 74, 64).handled).to_be_true();
		deluge::ui::scenes::services::view_state().set_current_view(was);
		expect(rig.host.learn.calls.size()).to_equal(size_t{1});
		if (rig.host.learn.calls.size() == 1) {
			const Call& call = rig.host.learn.calls[0];
			expect(call.kind).to_equal(Call::Kind::LEARN);
			expect(call.target)
			    .to_equal(std::optional<deluge::project::MappingTarget>{deluge::project::TrackInputTarget{9}});
			expect(call.command).to_equal(std::optional{MidiCommand{
			    .channel_or_zone = 5, .device = MidiDeviceRef{.port = MidiDeviceRef::Port::DIN}}});
		}
	});
	it("learns no CC channel for a MIDI track whose input already holds a channel", _ {
		Rig rig;
		(void)rig.model.mappings.learn(deluge::project::Mapping{
		    .source = MidiCommand{.channel_or_zone = 2, .note_or_cc = 60}, .target = deluge::project::TrackInputTarget{9}});
		const deluge::ui::scenes::services::MenuView was = deluge::ui::scenes::services::view_state().current_view();
		deluge::ui::scenes::services::view_state().set_current_view(
		    {.clip = deluge::project::ClipAddress{.clip_id = 3}, .kind = deluge::project::TrackKind::MIDI_OUT});
		(void)rig.button(ButtonId::Learn, true);
		rig.ml.thing_pressed(LearnTarget::instrument_input(9), true);
		expect(rig.cc(deluge::midi::kDin, 5, 74, 64).handled).to_be_true();
		deluge::ui::scenes::services::view_state().set_current_view(was);
		expect(rig.host.learn.calls.empty()).to_be_true();
	});
	it("keeps the held note of a MIDI track's input that holds no channel when a CC learns its channel", _ {
		Rig rig;
		(void)rig.model.mappings.learn(deluge::project::Mapping{
		    .source = MidiCommand{.note_or_cc = 61}, .target = deluge::project::TrackInputTarget{9}});
		const deluge::ui::scenes::services::MenuView was = deluge::ui::scenes::services::view_state().current_view();
		deluge::ui::scenes::services::view_state().set_current_view(
		    {.clip = deluge::project::ClipAddress{.clip_id = 3}, .kind = deluge::project::TrackKind::MIDI_OUT});
		(void)rig.button(ButtonId::Learn, true);
		rig.ml.thing_pressed(LearnTarget::instrument_input(9), true);
		expect(rig.cc(deluge::midi::kDin, 5, 74, 64).handled).to_be_true();
		deluge::ui::scenes::services::view_state().set_current_view(was);
		expect(rig.host.learn.calls.size()).to_equal(size_t{1});
		if (rig.host.learn.calls.size() == 1) {
			expect(rig.host.learn.calls[0].command).to_equal(std::optional{MidiCommand{
			    .channel_or_zone = 5, .note_or_cc = 61, .device = MidiDeviceRef{.port = MidiDeviceRef::Port::DIN}}});
		}
	});
});
// clang-format on

DELUGE_SPEC(midi_learn_fakes_spec, midi_learn_fakes)
