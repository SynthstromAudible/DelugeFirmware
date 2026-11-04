/// @file
/// @brief The menu's contracts that its parts must keep however they are arranged: the order `after_render` drains
///        its mailboxes in, that `owned_input` predicts exactly what the render claims, and that a new visit starts
///        from defaults.

#include "gui/ui/menu_view_of.h"
#include "scenes/menu/action.h"
#include "scenes/menu/bindings.h"
#include "scenes/menu/context.h"
#include "scenes/menu/menu_scene.h"
#include "scenes/menu/node.h"
#include "scenes/menu/node_id.h"
#include "scenes/menu/nodes/nodes.h"
#include "scenes/profile.h"
#include "scenes/registry.h"
#include "scenes/slicing/slicer_scene.h"

#include "notify/notice_queue.h"
#include "shell/profiles/deluge.h"
#include "shell/shell.h"

#include "gui/l10n/l10n.h"
#include "io/midi/midi_device.h"

#include "support/menu_rig.h"
#include "support/song_fixture.h"

#include "spec/shell/support/fake_surfaces.h"
#include "spec/ui/support/frames.h"

#include "cppspec.hpp"

#include <span>
#include <string>
#include <vector>

#include "spec/support/spec_main.h"

using namespace deluge::scenes;
using deluge::scenes::menu::MenuContext;
using deluge::scenes::menu::MenuScene;
using deluge::scenes::menu::MenuState;
using deluge::scenes::menu::Node;
using deluge::ui::shell::BoardProfile;
using deluge::ui::shell::SceneId;
using deluge::ui::shell::SceneKind;
using deluge::ui::shell::Shell;
using deluge::ui::shell::ShellConfig;
using deluge::ui::shell::SurfaceRole;

namespace {

namespace nodes = deluge::scenes::menu::nodes;
using Band = MenuScene::DrainBand;

void select_language() {
	deluge::l10n::chosenLanguage = &deluge::l10n::built_in::english;
	deluge::notify::Notice discard;
	while (deluge::notify::notices().pop(discard)) {}
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
		return menu::context_for(*song.song, deluge::gui::menuViewOf(song.clip), 0, false, false, false,
		                         /*kit_global_fx=*/false);
	}
	/// @brief Open @p root, landing on @p at (the root itself when it is the root).
	void open(const Node& root, const Node& at) {
		(void)menu::assign_node_ids(MenuScene::roots());
		menu_scene().open(root, menu_context());
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
	const deluge::ui::PadMask pads{};
	const deluge::ui::shell::UiContext ui{held, pads, shift, false, 0, deluge_profile()};
	return menu_scene().handle(event, ui);
}

Result button(ButtonId b, bool on) {
	return send(UiEvent{.kind = UiEvent::Kind::Button, .on = on, .button = b});
}

Result midi(UiEvent::Kind kind, bool shift = false) {
	return send(UiEvent{.kind = kind, .channel = 3, .data1 = 64, .data2 = 100, .cable = &spec_cable()}, shift);
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
		st.walk.pending_action = menu::MenuActionRequest{.action = menu::MenuAction::PadBrightnessChanged};
		st.walk.pending_scene = SceneId{slicing::kSlicerSceneKind, 0};
		(void)menu_scene().after_render();

		const std::vector<Band> want{Band::RangeChoice, Band::SourceChoice, Band::ValueCommit, Band::Navigation,
		                             Band::PendingAction, Band::Repaint,     Band::PendingScene};
		const std::span<const Band> got = menu_scene().drain_trace();
		expect(std::vector<Band>(got.begin(), got.end()) == want).to_be_true();
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
				const deluge::ui::InputClaims owned = menu_scene().owned_input();
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
		const deluge::ui::PadMask pads{};
		const deluge::ui::shell::UiContext ui{held, pads, false, false, 0, deluge_profile()};
		(void)menu_scene().handle(
		    deluge::ui::shell::UiEvent{.kind = deluge::ui::shell::UiEvent::Kind::Button,
		                               .button = deluge::ui::shell::ButtonId::Learn, .on = true},
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
		expect(menu_scene().focused() == focused).to_be_true();

		(void)button(ButtonId::Learn, false);
		const Result back_after = button(ButtonId::Back, true);
		r.shell.tick(20);
		expect(!back_after.actions.empty() || menu_scene().state_for_spec().depth != depth).to_be_true();
	});

	it("on a horizontal page, learns onto and unlearns the focused column's param", _ {
		// `Submenu::unlearnAction` and `learnKnob` forward to the current child, the focused column.
		select_language();
		Rig r;
		r.open(nodes::sound_root, nodes::lpf);
		r.shell.tick(0);
		expect(menu_scene().focused() == &nodes::lpf).to_be_true();
		MenuState& st = menu_scene().state_for_spec();
		st.columns[st.depth].focused = 1; // LPF's columns: MODE, FREQUENCY, RESONANCE, MORPH
		expect(menu::MenuLearn::node(r.menu_context(), st) == &nodes::lpf_freq).to_be_true();
		const MenuContext ctx = r.menu_context();
		const std::optional<deluge::project::OwnerParamTarget> column = menu::bindings::learn_target(nodes::lpf_freq, ctx);
		expect(column.has_value()).to_be_true();
		expect(menu_scene().learn_target() == column).to_be_true();
		if (!column) {
			return;
		}
		expect(ctx.sink->learn(*column, deluge::project::MidiCommand{.channel_or_zone = 18, .note_or_cc = 20},
		                       std::nullopt))
		    .to_be_true();
		expect(ctx.model->mappings.find(*column) != nullptr).to_be_true();
		(void)send(UiEvent{.kind = UiEvent::Kind::Button, .on = true, .button = ButtonId::Learn}, true);
		expect(ctx.model->mappings.find(*column) == nullptr).to_be_true();
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
});
// clang-format on

DELUGE_SPEC(menu_contract_spec, menu_contract_drain, menu_contract_claims, menu_contract_visit, menu_contract_learn)
