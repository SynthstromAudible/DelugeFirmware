/// @file
/// @brief The menu's contracts that its parts must keep however they are arranged: the order `after_render` drains
///        its mailboxes in, that `owned_input` predicts exactly what the render claims, and that a new visit starts
///        from defaults.

#include "gui/ui/menu_view_of.h"
#include "gui/ui/shell_host/host_ports.h"
#include "ui/scenes/bind/ports/learn_sink.h"
#include "ui/scenes/menu/action.h"
#include "ui/scenes/menu/bindings.h"
#include "ui/scenes/menu/context.h"
#include "ui/scenes/menu/menu_scene.h"
#include "ui/scenes/menu/node.h"
#include "ui/scenes/menu/node_id.h"
#include "ui/scenes/menu/nodes/nodes.h"
#include "ui/scenes/profile.h"
#include "ui/scenes/registry.h"
#include "ui/scenes/slicing/slicer_scene.h"

#include "base/notify/notice_queue.h"
#include "ui/shell/profiles/deluge.h"
#include "ui/shell/shell.h"

#include "gui/l10n/l10n.h"
#include "io/midi/device_registry.h"
#include "io/midi/midi_device.h"
#include "io/midi/midi_device_manager.h"
#include "model/project_bridge/midi_command.h"

#include "fakes.h"
#include "support/menu_rig.h"
#include "support/popup_capture.h"
#include "support/song_fixture.h"

#include "support/mocks/fake_surfaces.h"
#include "support/printers/mapping_printer.h"
#include "support/printers/param_id_printer.h"
#include "unit/ui/widgets/support/frames.h"

#include "cppspec.hpp"

#include <array>
#include <span>
#include <string>
#include <tuple>
#include <vector>

#include "support/menu_context.h"
#include "support/spec_main.h"

using namespace deluge::ui::scenes;
using deluge::ui::scenes::menu::MenuContext;
using deluge::ui::scenes::menu::MenuScene;
using deluge::ui::scenes::menu::MenuState;
using deluge::ui::scenes::menu::Node;
using deluge::ui::shell::BoardProfile;
using deluge::ui::shell::SceneId;
using deluge::ui::shell::SceneKind;
using deluge::ui::shell::Shell;
using deluge::ui::shell::ShellConfig;
using deluge::ui::shell::SurfaceRole;

namespace {

namespace nodes = deluge::ui::scenes::menu::nodes;
using Band = MenuScene::DrainBand;

void select_language() {
	deluge::l10n::chosenLanguage = &deluge::l10n::built_in::english;
	deluge::base::notify::Notice discard;
	while (deluge::base::notify::notices().pop(discard)) {}
}

struct Rig {
	spec::SongFixture song = spec::make_synth_song();
	const BoardProfile& profile = deluge_profile();
	deluge::ui::shell::SceneRegistry reg = scene_registry();
	uitest::FakeSurfaceSet fake{deluge::ui::shell::kDelugeBoard, deluge_profile()};
	Shell shell{fake, profile, reg, deluge::ui::shell::Services{}, ShellConfig{}};

	Rig() = default;
	Rig(const Rig&) = delete;
	Rig& operator=(const Rig&) = delete;
	~Rig() { spec::reset_menu_singleton(shell); }

	MenuContext menu_context() const {
		return spec::ctx_of(*song.song, deluge::gui::menuViewOf(song.clip), 0, false, false, false,
		                    /*kit_global_fx=*/false);
	}
	/// @brief Open @p root, landing on @p at (the root itself when it is the root).
	void open(const Node& root, const Node& at) { open(root, at, menu_context()); }
	/// @brief Open @p root on @p ctx, landing on @p at.
	void open(const Node& root, const Node& at, const MenuContext& ctx) {
		(void)menu::assign_node_ids(MenuScene::roots());
		menu_scene().open(root, ctx);
		shell.init(SceneId{SceneKind::FirstApp, menu::node_id(at)});
	}
};

/// @brief A cable this spec owns: `MIDICable` is abstract and no shipped subclass builds without a backend.
struct SpecCable final : ::MIDICable {
	void writeToFlash(uint8_t*) override {}
	char const* getDisplayName() override { return "contract"; }
	void sendMessage(MIDIMessage) override {}
	void sendSysex(const uint8_t*, int32_t) override {}
	[[nodiscard]] size_t sendBufferSpace() const override { return 0; }

protected:
	void writeReferenceAttributesToFile(Serializer&) override {}
};

SpecCable& spec_cable() {
	static SpecCable instance;
	return instance;
}

using deluge::ui::shell::ButtonId;
using deluge::ui::shell::Result;
using deluge::ui::shell::UiEvent;

/// @brief Hand the menu one event, with Shift held or not and nothing else held.
Result send(const UiEvent& event, bool shift = false) {
	const deluge::ui::shell::ButtonSet held{};
	const deluge::ui::widgets::core::PadMask pads{};
	const deluge::ui::shell::UiContext ui{held, pads, shift, false, 0, deluge_profile()};
	return menu_scene().handle(event, ui);
}

Result button(ButtonId b, bool on) {
	return send(UiEvent{.kind = UiEvent::Kind::Button, .on = on, .button = b});
}

Result midi(UiEvent::Kind kind, bool shift = false) {
	return send(
	    UiEvent{.kind = kind, .channel = 3, .data1 = 64, .data2 = 100, .device = deluge::midi::id_of(spec_cable())},
	    shift);
}

Result gold(deluge::ui::widgets::core::EncoderRole knob) {
	return send(UiEvent{.kind = UiEvent::Kind::Encoder, .encoder = knob, .delta = 1});
}

/// @brief The param mod knob @p knob moves in knob mode @p mode on @p owner, from the song's model; none if none.
std::optional<deluge::project::ParamTarget> knob_param(deluge::project::ControlsAddress owner, uint8_t mode,
                                                       uint8_t knob) {
	const std::vector<deluge::project::ControllerMapping>* controls = currentSong->project.resolveControls(owner);
	if (controls == nullptr) {
		return std::nullopt;
	}
	for (const deluge::project::ControllerMapping& m : *controls) {
		if (m.control == deluge::project::ModKnobControl{.button = mode, .knob = knob}) {
			return m.target;
		}
	}
	return std::nullopt;
}

} // namespace

// clang-format off
describe menu_contract_drain("scenes::MenuScene::after_render -- the drain order", $ {
	it("drains context changes, then the commit, navigation, the action, the repaint and the pushed scene", _ {
		select_language();
		Rig r;
		r.open(nodes::sound_root, nodes::sound_root);
		r.shell.tick(0);

		MenuState& st = menu_scene().state_for_spec();
		st.walk.range_choice = 0;
		st.walk.source_choice = 0;
		st.tick.value_changed = true;
		st.tick.value_node = &nodes::lpf_freq;
		st.tick.render_resp.entered = true;
		st.walk.pending_action = menu::MenuActionRequest{.action = menu::MenuAction::OpenPerformanceView};
		st.walk.pending_scene = SceneId{slicing::kSlicerSceneKind, 0};
		(void)menu_scene().after_render();

		const std::vector<Band> want{Band::RangeChoice, Band::SourceChoice, Band::ValueCommit, Band::Navigation,
		                             Band::PendingAction, Band::Repaint,     Band::PendingScene};
		const std::span<const Band> got = menu_scene().drain_trace();
		expect(std::vector<Band>(got.begin(), got.end())).to_equal(want);
		(void)menu_scene().take_action(); // the drained request is not left for a later group to perform
	});
});

describe menu_contract_claims("scenes::MenuScene::owned_input -- the render's claims, predicted", $ {
	it("predicts exactly what the render claims, on every root and every page directly under one", _ {
		select_language();
		std::vector<std::string> mismatches;
		for (const Node* root : MenuScene::roots()) {
			std::vector<const Node*> pages{root};
			for (const Node* child : root->children) {
				pages.push_back(child);
			}
			for (const Node* page : pages) {
				Rig r;
				r.open(*root, *page);
				deluge::ui::test::Rig48 rig;
				rig.begin_tick(0);
				const deluge::ui::widgets::core::InputClaims owned = menu_scene().owned_input();
				menu_scene().render_oled(SurfaceRole::Main, rig.frame);
				if (!spec::owns_matches_claims(owned, rig.ctx.claims)) {
					mismatches.push_back(std::to_string(menu::node_id(*root)) + "/" +
					                     std::to_string(menu::node_id(*page)));
				}
			}
		}
		std::string joined;
		for (const std::string& m : mismatches) {
			joined += m + " ";
		}
		expect(joined).to_equal(std::string{});
	});
});

describe menu_contract_visit("scenes::MenuScene -- a visit's state", $ {
	it("keeps the visit when on_enter refuses the id it is given", _ {
		// A refused `on_enter` rolls the move back: the menu stays the visit it was, learn mode and all.
		select_language();
		Rig r;
		r.open(nodes::sound_root, nodes::lpf_freq);
		r.shell.tick(0);
		const deluge::ui::shell::ButtonSet held{};
		const deluge::ui::widgets::core::PadMask pads{};
		const deluge::ui::shell::UiContext ui{held, pads, false, false, 0, deluge_profile()};
		(void)menu_scene().handle(
		    deluge::ui::shell::UiEvent{.kind = deluge::ui::shell::UiEvent::Kind::Button, .on = true,
		                               .button = deluge::ui::shell::ButtonId::Learn},
		    ui);
		expect(menu_scene().select_release_blocked()).to_be_true(); // learn mode
		expect(menu_scene().on_enter(SceneId{SceneKind::FirstApp, 0xFFFF})).to_be_false();
		expect(menu_scene().select_release_blocked()).to_be_true();
	});

	it("starts a reopened visit clean: a close asked for by the last visit does not close this one", _ {
		select_language();
		Rig r;
		r.open(nodes::sound_root, nodes::sound_root);
		r.shell.tick(0);
		// A scene opener's scene returning at the root goes up below depth 0, which asks the menu to close on its next
		// after_render.
		menu_scene().state_for_spec().walk.go_up_on_resume = true;
		menu_scene().on_resume();
		while (r.shell.stack().depth() > 0) {
			r.shell.stack().pop(SceneKind::Null); // closed some other way before it did
		}

		r.open(nodes::sound_root, nodes::sound_root);
		r.shell.tick(10);
		r.shell.tick(20);
		expect(r.shell.stack().depth()).to_equal(size_t(1));
	});
});

describe menu_contract_learn("scenes::MenuScene -- MIDI learn offers", $ {
	it("uses no MIDI message outside learn mode", _ {
		select_language();
		Rig r;
		r.open(nodes::sound_root, nodes::lpf_freq);
		r.shell.tick(0);
		for (const UiEvent::Kind k : {UiEvent::Kind::MidiNoteOn, UiEvent::Kind::MidiProgramChange,
		                              UiEvent::Kind::MidiCc, UiEvent::Kind::MidiPitchBend}) {
			expect(midi(k).handled).to_be_false();
		}
	});

	it("in learn mode uses a note-on even on an item that learns nothing from it", _ {
		select_language();
		Rig r;
		r.open(nodes::sound_root, nodes::lpf_freq);
		r.shell.tick(0);
		(void)button(ButtonId::Learn, true);
		expect(menu_scene().select_release_blocked()).to_be_true(); // learn mode
		expect(midi(UiEvent::Kind::MidiNoteOn).handled).to_be_true();
		expect(midi(UiEvent::Kind::MidiNoteOn, true).handled).to_be_true();
	});

	it("in learn mode uses a CC and a program change whatever Shift does, and a pitch bend only with Shift up", _ {
		select_language();
		Rig r;
		r.open(nodes::sound_root, nodes::lpf_freq);
		r.shell.tick(0);
		(void)button(ButtonId::Learn, true);
		expect(midi(UiEvent::Kind::MidiCc).handled).to_be_true();
		expect(midi(UiEvent::Kind::MidiCc, true).handled).to_be_true();
		expect(midi(UiEvent::Kind::MidiProgramChange).handled).to_be_true();
		expect(midi(UiEvent::Kind::MidiProgramChange, true).handled).to_be_true();
		expect(midi(UiEvent::Kind::MidiPitchBend).handled).to_be_true();
		expect(midi(UiEvent::Kind::MidiPitchBend, true).handled).to_be_false();
	});

	it("leaves learn mode on LEARN's release, and the next note-on plays", _ {
		select_language();
		Rig r;
		r.open(nodes::sound_root, nodes::lpf_freq);
		r.shell.tick(0);
		(void)button(ButtonId::Learn, true);
		(void)button(ButtonId::Learn, false);
		expect(midi(UiEvent::Kind::MidiNoteOn).handled).to_be_false();
	});

	it("ignores Back while learning, and goes back once LEARN is up", _ {
		select_language();
		Rig r;
		r.open(nodes::sound_root, nodes::lpf_freq);
		r.shell.tick(0);
		const uint8_t depth = menu_scene().state_for_spec().depth;
		const Node* const focused = menu_scene().focused();
		(void)button(ButtonId::Learn, true);
		const Result back = button(ButtonId::Back, true);
		(void)button(ButtonId::Back, false);
		r.shell.tick(10);
		expect(back.handled).to_be_true();
		expect(back.actions.empty()).to_be_true();
		expect(menu_scene().state_for_spec().depth).to_equal(depth);
		expect(menu_scene().focused()).to_equal(focused);

		(void)button(ButtonId::Learn, false);
		const Result back_after = button(ButtonId::Back, true);
		r.shell.tick(20);
		expect(std::tuple{back_after.actions.empty(), menu_scene().state_for_spec().depth})
		    .not_()
		    .to_equal(std::tuple{true, depth}, "Back neither acted nor changed the depth");
	});

	it("on a horizontal page, learns onto and unlearns the focused column's param", _ {
		// `Submenu::unlearnAction` and `learnKnob` forward to the current child, the focused column.
		select_language();
		Rig r;
		r.open(nodes::sound_root, nodes::lpf);
		r.shell.tick(0);
		expect(menu_scene().focused()).to_equal(&nodes::lpf);
		MenuState& st = menu_scene().state_for_spec();
		st.columns[st.depth].focused = 1; // LPF's columns: MODE, FREQUENCY, RESONANCE, MORPH
		expect(menu::MenuLearn::node(r.menu_context(), st)).to_equal(&nodes::lpf_freq);
		const MenuContext ctx = r.menu_context();
		const std::optional<deluge::project::OwnerParamTarget> column = menu::bindings::learn_target(nodes::lpf_freq, ctx);
		expect(column.has_value()).to_be_true();
		expect(menu_scene().learn_target()).to_equal(column);
		if (!column) {
			return;
		}
		expect(ctx.ports.learn->learn(*column, deluge::project::MidiCommand{.channel_or_zone = 18, .note_or_cc = 20},
		                              std::nullopt))
		    .to_be_true();
		expect(ctx.model->mappings.find(*column)).not_().to_be_null();
		(void)send(UiEvent{.kind = UiEvent::Kind::Button, .on = true, .button = ButtonId::Learn}, true);
		expect(ctx.model->mappings.find(*column)).to_be_null();
	});

	it("a gold turn while learning makes that knob move the param, and pops LEARNED", _ {
		// `SoundEditor::modEncoderAction`'s learn arm -> `MenuItemWithCCLearning::learnKnob(nullptr, which, mode, 255)`.
		select_language();
		Rig r;
		r.open(nodes::sound_root, nodes::lpf_res);
		r.shell.tick(0);
		const std::optional<deluge::project::OwnerParamTarget> target = menu_scene().learn_target();
		expect(target.has_value()).to_be_true();
		if (!target) {
			return;
		}
		const uint8_t mode = r.song.clip->output->modKnobMode;
		expect(knob_param(target->owner, mode, 1)).not_().to_equal(std::optional{target->param});
		deluge::ui::scenes::spec::PopupCapture popups;
		(void)button(ButtonId::Learn, true);
		expect(gold(deluge::ui::widgets::core::EncoderRole::GoldUpper).handled).to_be_true();
		(void)button(ButtonId::Learn, false);
		expect(knob_param(target->owner, mode, 1)).to_equal(std::optional{target->param});
		expect(popups.seen())
		    .to_equal(std::vector<std::string>{"popup: " + std::string(deluge::l10n::get(deluge::l10n::String::STRING_FOR_LEARNED))});
	});

	it("a CC and a pitch bend while learning learn a MIDI knob onto the param", _ {
		// `MenuItem::learnCC` is `learnKnob(&cable, cc, 0, channel)`; `SoundEditor`'s pitch bend is
		// `learnKnob(&cable, 128, 0, channel)`.
		select_language();
		Rig r;
		r.open(nodes::sound_root, nodes::lpf_res);
		r.shell.tick(0);
		const std::optional<deluge::project::OwnerParamTarget> target = menu_scene().learn_target();
		const MenuContext ctx = r.menu_context();
		expect(target.has_value()).to_be_true();
		if (!target) {
			return;
		}
		(void)button(ButtonId::Learn, true);
		// From the DIN ports and from the first upstream USB port: a fixed port's handle comes back as that port's
		// reference in the learned mapping.
		expect(send(UiEvent{.kind = UiEvent::Kind::MidiCc, .channel = 2, .data1 = 74, .data2 = 64,
		                    .device = deluge::midi::kDin})
		           .handled)
		    .to_be_true();
		const deluge::project::Mapping* cc = ctx.model->mappings.find(*target);
		expect(cc).not_().to_be_null();
		if (cc != nullptr) {
			expect(int(cc->source.channel_or_zone)).to_equal(2);
			expect(int(cc->source.note_or_cc)).to_equal(74);
			expect(cc->source.device.has_value()).to_be_true();
			if (cc->source.device) {
				expect(int(cc->source.device->port)).to_equal(int(deluge::project::MidiDeviceRef::Port::DIN));
			}
		}
		(void)button(ButtonId::Learn, false);
		(void)send(UiEvent{.kind = UiEvent::Kind::Button, .on = true, .button = ButtonId::Learn}, true); // unlearn
		expect(ctx.model->mappings.find(*target)).to_be_null();

		(void)button(ButtonId::Learn, true);
		expect(send(UiEvent{.kind = UiEvent::Kind::MidiPitchBend, .channel = 5, .data1 = 0, .data2 = 64,
		                    .device = deluge::midi::kUpstreamUsb1})
		           .handled)
		    .to_be_true();
		const deluge::project::Mapping* bend = ctx.model->mappings.find(*target);
		expect(bend).not_().to_be_null();
		if (bend != nullptr) {
			expect(int(bend->source.channel_or_zone)).to_equal(5);
			expect(int(bend->source.note_or_cc)).to_equal(128);
			expect(bend->source.device.has_value()).to_be_true();
			if (bend->source.device) {
				expect(int(bend->source.device->port))
				    .to_equal(int(deluge::project::MidiDeviceRef::Port::UPSTREAM_USB_1));
			}
		}
		(void)button(ButtonId::Learn, false);
	});

	it("hands the learn port no device for a gold knob's learn, and the message's device for a MIDI knob's", _ {
		select_language();
		Rig r;
		deluge::ui::scenes::bind::test::RecordingLearnSink recording;
		MenuContext ctx = r.menu_context();
		ctx.ports.learn = &recording;
		r.open(nodes::sound_root, nodes::lpf_res, ctx);
		r.shell.tick(0);
		expect(menu_scene().learn_target().has_value()).to_be_true();
		const deluge::midi::DeviceId device = deluge::midi::id_of(spec_cable());

		(void)button(ButtonId::Learn, true);
		expect(gold(deluge::ui::widgets::core::EncoderRole::GoldUpper).handled).to_be_true();
		expect(send(UiEvent{.kind = UiEvent::Kind::MidiCc, .channel = 2, .data1 = 74, .data2 = 64, .device = device})
		           .handled)
		    .to_be_true();
		(void)button(ButtonId::Learn, false);

		using Call = deluge::ui::scenes::bind::test::RecordingLearnSink::Call;
		std::vector<Call> knobs;
		for (const Call& call : recording.calls) {
			if (call.kind == Call::Kind::LEARN_KNOB) {
				knobs.push_back(call);
			}
		}
		expect(knobs.size()).to_equal(size_t{2});
		if (knobs.size() != 2) {
			return;
		}
		expect(knobs[0].device.value).to_equal(deluge::midi::DeviceId{}.value); // the gold knob
		expect(knobs[0].which_knob).to_equal(int32_t{1});
		expect(knobs[1].device.value).to_equal(device.value); // the CC
		expect(knobs[1].which_knob).to_equal(int32_t{74});
		expect(knobs[1].midi_channel).to_equal(int32_t{2});
	});

	it("the app's learn port binds a MIDI knob to the cable its device names, and a mod knob to none", _ {
		// The host's `LearnSink::learn_knob`, called directly: a fixed port's handle must reach the knob's learn as
		// that port's cable, which the learned mapping names and resolves back to.
		select_language();
		Rig r;
		r.open(nodes::sound_root, nodes::lpf_res);
		r.shell.tick(0);
		const std::optional<deluge::project::OwnerParamTarget> target = menu_scene().learn_target();
		const MenuContext ctx = r.menu_context();
		deluge::ui::scenes::bind::LearnSink* const learns = deluge::gui::shell_host::host_ports().learn;
		expect(target.has_value()).to_be_true();
		expect(learns).not_().to_be_null();
		if (!target || learns == nullptr) {
			return;
		}
		const std::array<std::pair<deluge::midi::DeviceId, ::MIDICable*>, 2> fixed{{
		    {deluge::midi::kDin, &MIDIDeviceManager::dinMIDIPorts},
		    {deluge::midi::kUpstreamUsb2, &MIDIDeviceManager::upstreamUSBMIDICable2},
		}};
		for (const auto& [device, cable] : fixed) {
			expect(learns->learn_knob(*target, device, 74, 3)).to_be_true();
			const deluge::project::Mapping* learned = ctx.model->mappings.find(*target);
			expect(learned).not_().to_be_null();
			if (learned == nullptr) {
				return;
			}
			expect(int(learned->source.channel_or_zone)).to_equal(3);
			expect(int(learned->source.note_or_cc)).to_equal(74);
			expect(deluge::bridge::learned_of(learned->source).cable).to_equal(cable);
			expect(learns->learn(*target, std::nullopt, std::nullopt)).to_be_true(); // unlearn for the next
			expect(ctx.model->mappings.find(*target)).to_be_null();
		}

		// A mod knob: no device, a channel of 16 or more, and a knob that did not turn the param now turns it.
		const uint8_t mode = r.song.clip->output->modKnobMode;
		const uint8_t knob = knob_param(target->owner, mode, 0) == std::optional{target->param} ? 1 : 0;
		expect(knob_param(target->owner, mode, knob)).not_().to_equal(std::optional{target->param});
		expect(learns->learn_knob(*target, deluge::midi::DeviceId{}, knob, 255)).to_be_true();
		expect(knob_param(target->owner, mode, knob)).to_equal(std::optional{target->param});
		expect(ctx.model->mappings.find(*target)).to_be_null(); // a mod knob is not a MIDI mapping
	});

	it("the app's learn port binds a MIDI knob and a mod knob to a sound's arp param, its gate and its rate", _ {
		// A sound's arp params are its arp's (ParamSpace::ARP): a MIDI knob learns into the song's table and a mod knob
		// into the sound's controls, as main learns them on its arp items.
		select_language();
		for (const Node* item : {&nodes::arp_gate, &nodes::arp_rate}) {
			Rig r;
			r.open(nodes::sound_root, *item);
			r.shell.tick(0);
			const std::optional<deluge::project::OwnerParamTarget> target = menu_scene().learn_target();
			const MenuContext ctx = r.menu_context();
			deluge::ui::scenes::bind::LearnSink* const learns = deluge::gui::shell_host::host_ports().learn;
			expect(target.has_value()).to_be_true();
			expect(learns).not_().to_be_null();
			if (!target || learns == nullptr) {
				return;
			}
			expect(target->param.space).to_equal(deluge::project::ParamSpace::ARP);
			expect(learns->learn_knob(*target, deluge::midi::kDin, 75, 3)).to_be_true();
			const deluge::project::Mapping* learned = ctx.model->mappings.find(*target);
			expect(learned).not_().to_be_null();
			if (learned != nullptr) {
				expect(int(learned->source.note_or_cc)).to_equal(75);
			}
			expect(learns->learn(*target, std::nullopt, std::nullopt)).to_be_true();

			const uint8_t mode = r.song.clip->output->modKnobMode;
			expect(learns->learn_knob(*target, deluge::midi::DeviceId{}, 1, 255)).to_be_true();
			expect(knob_param(target->owner, mode, 1)).to_equal(std::optional{target->param});
		}
	});

	it("outside learn mode leaves a gold turn to the knobs", _ {
		select_language();
		Rig r;
		r.open(nodes::sound_root, nodes::lpf_res);
		r.shell.tick(0);
		expect(gold(deluge::ui::widgets::core::EncoderRole::GoldUpper).handled).to_be_false();
	});

	it("is not learning when input comes back to it from a scene above", _ {
		// LEARN's release went to the scene above, so the menu never saw it.
		select_language();
		Rig r;
		r.open(nodes::sound_root, nodes::lpf_freq);
		r.shell.tick(0);
		(void)button(ButtonId::Learn, true);
		menu_scene().on_resume();
		expect(menu_scene().select_release_blocked()).to_be_false();
		expect(midi(UiEvent::Kind::MidiNoteOn).handled).to_be_false();
	});

	it("with kit affect-entire, a gold turn while learning is used and learns nothing", _ {
		// `SoundEditor::modEncoderAction`: "can't do it if it's a Kit and affect-entire is on".
		select_language();
		Rig r;
		spec::SongFixture kit = spec::make_kit_song();
		const MenuContext ctx = spec::ctx_of(*kit.song, deluge::gui::menuViewOf(kit.clip), 0, false, false, false,
		                                     /*kit_global_fx=*/true);
		(void)menu::assign_node_ids(MenuScene::roots());
		menu_scene().open(nodes::sound_root, ctx);
		r.shell.init(SceneId{SceneKind::FirstApp, menu::node_id(nodes::lpf_res)});
		r.shell.tick(0);
		const std::optional<deluge::project::OwnerParamTarget> target = menu_scene().learn_target();
		expect(target.has_value()).to_be_true();
		if (!target) {
			return;
		}
		const uint8_t mode = kit.clip->output->modKnobMode;
		const std::optional<deluge::project::ParamTarget> before = knob_param(target->owner, mode, 1);
		deluge::ui::scenes::spec::PopupCapture popups;
		(void)button(ButtonId::Learn, true);
		expect(menu_scene().select_release_blocked()).to_be_true(); // learn mode
		expect(gold(deluge::ui::widgets::core::EncoderRole::GoldUpper).handled).to_be_true();
		(void)button(ButtonId::Learn, false);
		expect(knob_param(target->owner, mode, 1)).to_equal(before);
		expect(popups.seen()).to_equal(std::vector<std::string>{});
	});
});
// clang-format on

DELUGE_SPEC(menu_contract_spec, menu_contract_drain, menu_contract_claims, menu_contract_visit, menu_contract_learn)
