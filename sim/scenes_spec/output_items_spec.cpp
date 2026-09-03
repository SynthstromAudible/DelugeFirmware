/// @file
/// @brief The last two bespoke menu items (`scenes/menu/nodes/output_items.h`): which node carries
///        which item, what each one does when the menu focuses it -- and, at the end of it, that the
///        tree has **no `Kind::Custom` placeholders left at all**.
///
/// Three groups, three different kinds of claim:
///
///   * **The mapping, by identity.** Both nodes carry the item their legacy class became, with the
///     `BrowserId` that class opened; no other node in `nodes::kAllRoots` carries either item; and
///     a deliberately crossed copy of the observed table is asserted *not* to match, so the
///     comparison cannot be passing on an all-null read. Task 6's tenth mutant is the reason this
///     is an identity table and not only a behaviour check.
///   * **The behaviour, against legacy on the same fixture.** `midi::device_definition::Linked` and
///     `audio_clip::SpecificSourceOutputSelector` are both reachable from a spec process, so both
///     are run: the real `midiDeviceLinkedMenu`'s `readCurrentValue`, and the real
///     `specificOutputSelectorMenu`'s `beginSession` / `selectEncoderAction` / `drawPixelsForOled`.
///     Each side is driven from its **own** sentinel -- the model is written, legacy is asked
///     through its globals, the model is written again, and the port is asked through a freshly
///     built `MenuContext` -- so a comparison cannot pass by reading state the other side left
///     behind.
///   * **The milestone.** `validate_tree(kAllRoots, ...)` with strict `ValidateOptions` reports
///     **zero** `PlaceholderCustom`, `kPendingCustomNodes` is 0, and nothing reachable from the
///     roots answers `is_placeholder()`. `ValidateOptions::allow_placeholder_custom` no longer
///     exists, so there is no flag left that could hide a regression here.
///
/// @note The spec may include `gui/` (for the legacy menu items); the library may not. Same rule
///       `tables_1_spec.cpp`, `menu_actions_spec.cpp` and `browser_scene_spec.cpp` run under.

#include "scenes/browser/browser_scene.h"
#include "scenes/l10n_bridge.h"
#include "scenes/menu/context.h"
#include "scenes/menu/menu_scene.h"
#include "scenes/menu/node.h"
#include "scenes/menu/node_id.h"
#include "scenes/menu/nodes/nodes.h"
#include "scenes/menu/nodes/output_items.h"
#include "scenes/menu/nodes/sound_osc.h" // kPending -- the mechanism that must outlive its users
#include "scenes/menu/relevance.h"
#include "scenes/menu/validate.h"
#include "scenes/profile.h"
#include "scenes/registry.h"

#include "notify/notice_queue.h"
#include "shell/profiles/deluge.h"
#include "shell/shell.h"

// Before the legacy item headers: `specific_output_source_selector.h:76` names
// `NonAudioInstrument` without including it, and only compiles inside `gui/ui/menus.cpp` because
// something earlier in that translation unit already had.
#include "model/instrument/non_audio_instrument.h"

#include "gui/l10n/l10n.h"
#include "gui/menu_item/audio_clip/specific_output_source_selector.h"
#include "gui/menu_item/midi/device_definition/linked.h"
#include "hid/display/display.h"
#include "hid/display/oled.h"
#include "model/instrument/midi_instrument.h"
#include "model/song/song.h"
#include "processing/audio_output.h"
#include "storage/storage_manager.h"

#include "support/song_fixture.h"

// Before <cppspec.hpp>: it #defines `it`, which collides with locals named `it` in the project
// headers this drags in (`audio_engine.h` -> `util/container/ordered_pos_vector.h`).
#include "support/card_access.h"

#include "spec/gfx/support/pbm.h"
#include "spec/shell/support/fake_surfaces.h"

#include "cppspec.hpp"

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <set>
#include <string>
#include <vector>

#include "spec/support/spec_main.h"

/// The real legacy singletons. Declared bare at global scope because `gui/ui/menus.cpp` defines
/// them there (`:905` and `:1151`) and `gui/ui/menus.h` does not export either -- the same
/// situation `menu_actions_spec.cpp` records for `performEditorMenu`.
extern deluge::gui::menu_item::midi::device_definition::Linked midiDeviceLinkedMenu;
extern deluge::gui::menu_item::audio_clip::SpecificSourceOutputSelector specificOutputSelectorMenu;

using namespace deluge::scenes;
using deluge::scenes::menu::MenuContext;
using deluge::scenes::menu::Node;
using deluge::ui::shell::BoardProfile;
using deluge::ui::shell::SceneId;
using deluge::ui::shell::SceneKind;
using deluge::ui::shell::Shell;
using deluge::ui::shell::ShellConfig;

namespace {

namespace nodes = deluge::scenes::menu::nodes;

/// Board encoder index (`kDelugeEncoders`, `shell/profiles/deluge.cpp`): 3 = Select.
constexpr uint8_t kSelectEncoder = 3;
/// Matrix coordinates of the select-encoder button (`kMatrix`, `shell/profiles/deluge.cpp`).
constexpr uint8_t kSelectEncButtonX = 4, kSelectEncButtonY = 3;

void select_language() {
	deluge::l10n::chosenLanguage = &deluge::l10n::built_in::english;
	deluge::notify::Notice discard;
	while (deluge::notify::notices().pop(discard)) {}
}

/// @brief The minimum `Display` the legacy items need. `display` is null in a spec process
///        (`hid/display/display.cpp:4`).
struct QuietDisplay final : deluge::hid::Display {
	constexpr size_t getNumBrowserAndMenuLines() override { return 3; }
	void displayPopup(char const*, int8_t, bool, uint8_t, int32_t, PopupType) override {}
	void popupText(char const*, PopupType) override {}
	void popupTextTemporary(char const*, PopupType) override {}
	void cancelPopup() override {}
	void freezeWithError(char const*) override {}
	void displayError(Error) override {}
	void removeWorkingAnimation() override {}
	void displayLoadingAnimationText(char const*, bool, bool) override {}
	void removeLoadingAnimation() override {}
	bool hasPopup() override { return false; }
	bool hasPopupOfType(PopupType) override { return false; }
	void consoleText(char const*) override {}
	void timerRoutine() override {}
};

/// @brief A scratch card carrying one MIDI device definition, so the browser the link arm opens has
///        a real folder to list.
///
/// Built under the host temp directory and removed again: the committed `tests/fixtures/card` has
/// no `MIDI_DEVICES/` at all, and nothing here may write to it (`browser_scene_spec.cpp`'s rule).
struct ScratchCard {
	std::filesystem::path dir;
	spec::SdRoot root;

	static std::filesystem::path make() {
		std::filesystem::path d = std::filesystem::temp_directory_path() / "deluge_output_items_spec";
		std::filesystem::remove_all(d);
		std::filesystem::create_directories(d / "MIDI_DEVICES" / "DEFINITION");
		std::ofstream(d / "MIDI_DEVICES" / "DEFINITION" / "DEVICE1.XML") << "<x/>";
		return d;
	}

	ScratchCard() : dir(make()), root(dir.string()) {}
	~ScratchCard() { std::filesystem::remove_all(dir); }
	ScratchCard(const ScratchCard&) = delete;
	ScratchCard& operator=(const ScratchCard&) = delete;
};

[[noreturn]] void spec_bail(const char* what) {
	std::fprintf(stderr, "output_items_spec: %s\n", what);
	std::abort();
}

/// @brief Refuse to start, or to finish, with the browser singleton still holding a listing.
///
/// **Assert-then-clear, not defensive-clear** (task 5's finding, task 6 §7): scenes are process
/// singletons, so an example that leaked a listing would make the *next* one's refusal
/// indistinguishable from a wiring bug. Each rig's destructor tears its own stack down; this guard
/// says so out loud if one did not. `before_each`/`after_each` get no `self`, so `expect` is
/// unavailable there and a named reason on stderr plus `std::abort()` is what "loudly" means.
struct CleanScenes {
	CleanScenes() { check("a scene singleton was still busy when an example started"); }
	~CleanScenes() { check("an example finished with a scene singleton still busy"); }
	CleanScenes(const CleanScenes&) = delete;
	CleanScenes& operator=(const CleanScenes&) = delete;

private:
	static void check(const char* what) {
		if (browser::browser_scene().listing_in_flight()) {
			spec_bail(what);
		}
	}
};

/// @brief A `Shell` over `FakeSurfaceSet` with the application's own scene registry installed.
struct Rig {
	deluge::ui::shell::SceneRegistry reg = scene_registry();
	uitest::FakeSurfaceSet fake;
	Shell shell;
	uint32_t clock = 0;

	Rig(const Rig&) = delete;
	Rig& operator=(const Rig&) = delete;

	Rig()
	    : fake(deluge::ui::shell::kDelugeBoard, deluge_profile()),
	      shell(fake, deluge_profile(), reg, deluge::ui::shell::Services{}, ShellConfig{}) {}

	/// @brief Tear the stack down, so every scene's `on_exit` always runs.
	~Rig() {
		while (shell.stack().depth() > 0) {
			shell.stack().pop(SceneKind::Null);
		}
	}

	void open(const Node& root, const MenuContext& ctx) {
		select_language();
		(void)menu::assign_node_ids(nodes::kAllRoots);
		menu_scene().open(root, ctx);
		shell.init(SceneId{SceneKind::FirstApp, menu::node_id(root)});
	}

	void tick(uint32_t at) {
		clock = at;
		shell.tick(at);
	}
	void step() { tick(clock + 10); }

	void edges(uint8_t index, int16_t count) {
		shell.on_board_event(DelugeInputEvent{DELUGE_EVENT_ENCODER, index, 0, count}, clock + 5);
	}
	/// One detent of the (detented, 2 edges per detent) select encoder.
	void turn(int32_t detents) { edges(kSelectEncoder, static_cast<int16_t>(detents * 2)); }
	void button(uint8_t x, uint8_t y, bool on) {
		shell.on_board_event(DelugeInputEvent{DELUGE_EVENT_BUTTON, x, y, static_cast<int16_t>(on ? 255 : 0)},
		                     clock + 5);
	}
	void enter() { button(kSelectEncButtonX, kSelectEncButtonY, true); }

	bool on_stack(SceneKind kind) {
		for (const deluge::ui::shell::Scene* s : shell.stack().scenes()) {
			if (s->kind() == kind) {
				return true;
			}
		}
		return false;
	}
};

/// @brief @p parent's visible children in the order the scene lists them
///        (`menu_actions_spec.cpp`'s helper, same two-line rule as `MenuScene::visible_children`).
int32_t visible_index_of(const Node& parent, const Node& child, const MenuContext& ctx) {
	if (parent.dynamic_children != nullptr) {
		return -1; // neither parent here has one; a table edit that adds one must be seen.
	}
	int32_t index = 0;
	for (const Node* kid : parent.children) {
		if (kid->relevant != nullptr && !kid->relevant(ctx)) {
			continue;
		}
		if (kid == &child) {
			return index;
		}
		index++;
	}
	return -1;
}

/// @brief Open @p parent, walk the cursor onto @p child and press select -- through the real
///        lowering, the real `list` widget and the real `enter_child`.
void press_child(Rig& r, const Node& parent, const Node& child, const MenuContext& ctx) {
	const int32_t index = visible_index_of(parent, child, ctx);
	if (index < 0) {
		spec_bail("the child under test is not visible in its parent");
	}
	r.open(parent, ctx);
	r.tick(5);
	for (int32_t i = 0; i < index; i++) {
		r.turn(1);
		r.step();
	}
	r.enter();
	r.step();
}

// ---------------------------------------------------------------------------------------------
// The fixtures

/// @brief An audio clip whose output records from a specific track, in a song with four outputs.
///
/// The order is the discriminator the whole `SpecificOutputItem` group leans on: the walk must skip
/// the audio track itself (`canRecordFrom` refuses `this`) and the MIDI one (it refuses
/// `MIDI_OUT` outright, `processing/audio_output.h:109-111`), so a port that walked raw song
/// indices would land on a different output from legacy at almost every step.
///
/// Indices, after `addOutput(..., atStart=false)` appends each one:
///   0 = the `AudioOutput` being edited (**not** recordable -- itself)
///   1 = a synth                       (recordable)
///   2 = a MIDI instrument             (**not** recordable)
///   3 = a second synth                (recordable)
struct ClipRig {
	spec::AudioSongFixture song = spec::make_audio_clip_song();
	Instrument* synth_a = nullptr;
	Instrument* midi = nullptr;
	Instrument* synth_b = nullptr;

	ClipRig(const ClipRig&) = delete;
	ClipRig& operator=(const ClipRig&) = delete;

	ClipRig() {
		song.output->inputChannel = AudioInputChannel::SPECIFIC_OUTPUT;
		synth_a = add_synth("A");
		midi = add_midi();
		synth_b = add_synth("B");
	}

	Instrument* add_synth(const char* name) {
		ParamManagerForTimeline params;
		auto* synth = static_cast<Instrument*>(StorageManager::createNewInstrument(OutputType::SYNTH, &params));
		synth->dirPath = "SYNTHS";
		synth->name = name;
		song.song->addOutput(synth, /*atStart=*/false);
		return synth;
	}

	Instrument* add_midi() {
		auto* m = static_cast<Instrument*>(StorageManager::createNewNonAudioInstrument(OutputType::MIDI_OUT, 0, 0));
		m->name = "M";
		song.song->addOutput(m, /*atStart=*/false);
		return m;
	}

	MenuContext ctx() { return menu::context_for(*song.song, song.clip, 0, false, false, /*kit_global_fx=*/false); }
};

/// @brief A MIDI clip, for `midi_device_linked`.
struct MidiRig {
	spec::SongFixture song = spec::make_midi_song();

	MidiRig(const MidiRig&) = delete;
	MidiRig& operator=(const MidiRig&) = delete;
	MidiRig() = default;

	MIDIInstrument* instrument() { return static_cast<MIDIInstrument*>(song.instrument); }
	MenuContext ctx() { return menu::context_for(*song.song, song.clip, 0, false, false, /*kit_global_fx=*/false); }
};

// ---------------------------------------------------------------------------------------------
// The mapping, as a value that can be compared whole

/// @brief Which item class a node carries, plus what it was constructed with.
struct Wiring {
	std::string node;
	int kind = -1;    ///< 0 = MidiDeviceLinkedItem, 1 = SpecificOutputItem, -1 = neither
	int browser = -1; ///< `MidiDeviceLinkedItem::browser_arg()`, or -1 for an item with none

	bool operator==(const Wiring&) const = default;
};

Wiring wiring_of(const char* name, const Node& node) {
	Wiring w;
	w.node = name;
	if (const auto* linked = dynamic_cast<const nodes::MidiDeviceLinkedItem*>(node.custom); linked != nullptr) {
		w.kind = 0;
		w.browser = static_cast<int>(linked->browser_arg());
	}
	else if (dynamic_cast<const nodes::SpecificOutputItem*>(node.custom) != nullptr) {
		w.kind = 1;
	}
	return w;
}

std::string format_wiring(const std::vector<Wiring>& ws) {
	std::string out;
	for (const Wiring& w : ws) {
		out += w.node + "={kind=" + std::to_string(w.kind) + ",browser=" + std::to_string(w.browser) + "} ";
	}
	return out;
}

std::vector<Wiring> observed_wiring() {
	return {
	    wiring_of("midi_device_linked", nodes::midi_device_linked),
	    wiring_of("specific_output_selector", nodes::specific_output_selector),
	};
}

/// @brief What each node must carry, derived from the legacy class each item ports.
std::vector<Wiring> expected_wiring() {
	return {
	    // `midi::device_definition::Linked::writeCurrentValue`'s ON arm is
	    // `openUI(&loadMidiDeviceDefinitionUI)` (`midi/device_definition/linked.h:42`), which is
	    // `browser::BrowserId::MidiDeviceDefinition` == 1 (`scenes/browser/browser_scene.h:113`).
	    {"midi_device_linked", 0, 1},
	    // `audio_clip::SpecificSourceOutputSelector` (`audio_clip/specific_output_source_selector.h:26`)
	    // opens nothing at all -- it is a page, so it carries no browser id.
	    {"specific_output_selector", 1, -1},
	};
}

// ---------------------------------------------------------------------------------------------
// The legacy oracle for the output picker

/// @brief `OLED::main`, into which every legacy `renderOLED()` draws, as a PBM
///        (`menu_ranges_spec.cpp`'s helper).
std::string legacy_panel_pbm() {
	const auto& canvas = deluge::hid::display::OLED::main;
	return page_major_to_pbm(canvas.bytes().data(), OLED_MAIN_WIDTH_PIXELS, OLED_MAIN_HEIGHT_PIXELS);
}

/// @brief The rows the port's root frame can actually ink on the 48-row panel.
///
/// The Deluge panel has a five-row bezel (`kDelugeBoard`'s `DelugeDisplayDesc{128, 48, 5}`) and the
/// port's root frame has its origin at `visible_top`, so anything a legacy routine inks above row 5
/// is clipped away here. Every comparison drops those five rows from *both* sides, exactly as
/// `menu_ranges_spec.cpp` and `context_menu_scene_spec.cpp` do.
std::string below_bezel(const std::string& pbm) {
	size_t pos = 0;
	int newlines = 0;
	while (pos < pbm.size() && newlines < 2) {
		if (pbm[pos] == '\n') {
			newlines++;
		}
		pos++;
	}
	std::string out = pbm.substr(0, pos);
	for (int i = 0; i < 5 && pos < pbm.size(); i++) {
		const size_t next = pbm.find('\n', pos);
		pos = next == std::string::npos ? pbm.size() : next + 1;
	}
	out += pbm.substr(pos);
	return out;
}

/// @brief Run the real `specificOutputSelectorMenu` over whatever `currentSong` currently says, and
///        report which output it ended up on.
///
/// Driven from its **own** sentinel: `beginSession` re-resolves `audioOutputBeingEdited` and
/// `numOutputs` from `getCurrentOutput()` / `currentSong` on entry, so nothing the port left behind
/// can be what this reads.
/// @param detents How many select-encoder detents to apply after `beginSession`, one at a time.
Output* legacy_pick(int32_t detents) {
	midiDeviceLinkedMenu.readCurrentValue(); // unrelated; keeps the two globals' state independent
	specificOutputSelectorMenu.beginSession(nullptr);
	const int32_t step = detents >= 0 ? 1 : -1;
	for (int32_t i = 0; i < (detents >= 0 ? detents : -detents); i++) {
		specificOutputSelectorMenu.selectEncoderAction(step);
	}
	return static_cast<AudioOutput*>(getCurrentOutput())->getOutputRecordingFrom();
}

} // namespace

// clang-format off
describe output_items_mapping("scenes::menu the two output items: the mapping", $ {

	it("carries the item its legacy class became, on both nodes", _ {
		expect(format_wiring(observed_wiring())).to_equal(format_wiring(expected_wiring()));

		// ... and the comparison is not vacuous. Two items is a small table, but it is exactly the
		// size at which a positional check looks strong and pins nothing, so a deliberately crossed
		// copy is asserted NOT to match. The crossing swaps the *payload* and leaves each row's node
		// name where it is, because that is the shape a real miswiring has: `midi_device_linked`
		// still called that, carrying the output picker.
		std::vector<Wiring> crossed = observed_wiring();
		std::swap(crossed[0].kind, crossed[1].kind);
		expect(format_wiring(crossed) == format_wiring(expected_wiring())).to_be_false();

		// And the browser id is a real discriminator, not a constant: pointing the link arm at the
		// *sample* browser (`BrowserId::Sample` == 0) must fail the table too.
		std::vector<Wiring> wrong_browser = observed_wiring();
		wrong_browser[0].browser = 0;
		expect(format_wiring(wrong_browser) == format_wiring(expected_wiring())).to_be_false();
	});

	it("puts these items on those two nodes and on nothing else in the tree", _ {
		// By identity over the whole reachable tree, not by a count: an item accidentally attached to
		// a third node somewhere else would leave every assertion above passing.
		select_language();
		const uint16_t assigned = menu::assign_node_ids(nodes::kAllRoots);

		const Node* const kExpected[] = {&nodes::midi_device_linked, &nodes::specific_output_selector};
		std::set<const Node*> want{std::begin(kExpected), std::end(kExpected)};

		std::set<const Node*> found;
		for (uint16_t id = 1; id <= assigned; id++) {
			const Node* n = menu::node_by_id(id);
			if (n == nullptr || n->kind != menu::Kind::Custom) {
				continue;
			}
			if (dynamic_cast<const nodes::MidiDeviceLinkedItem*>(n->custom) != nullptr
			    || dynamic_cast<const nodes::SpecificOutputItem*>(n->custom) != nullptr) {
				found.insert(n);
			}
		}
		const auto ids = [](const std::set<const Node*>& in) {
			std::vector<std::string> parts;
			for (const Node* n : in) {
				parts.push_back(std::to_string(menu::node_id(*n)) + "=" + std::to_string(n->name));
			}
			std::sort(parts.begin(), parts.end());
			std::string out;
			for (const std::string& p : parts) {
				out += p + " ";
			}
			return out;
		};
		expect(ids(found)).to_equal(ids(want));
	});
});

describe output_items_milestone("scenes::menu the tree has no placeholders left", $ {

	it("reports zero PlaceholderCustom with nothing standing the rule down", _ {
		// **The milestone this whole plan exists for.** `ValidateOptions` is default-constructed --
		// there is no `allow_placeholder_custom` any more, so there is no flag that could be hiding
		// this. The shortcut sweep stays on: this is `kAllRoots`, the one caller for which it means
		// anything.
		select_language();
		const auto issues = menu::validate_tree(nodes::kAllRoots, {}, {}, {}, menu::ValidateOptions{});
		size_t placeholders = 0;
		std::string names;
		for (const menu::Issue& i : issues) {
			if (i.kind == menu::Issue::Kind::PlaceholderCustom) {
				placeholders++;
				names += std::to_string(i.detail) + " ";
			}
		}
		expect(names).to_equal(std::string{}); // names the offender rather than only moving a count
		expect(placeholders).to_equal(size_t(0));
		expect(menu::kPendingCustomNodes).to_equal(size_t(0));
	});

	it("walks the tree and finds no node pointing at a stand-in", _ {
		// The same claim from the other end, and not through `validate_tree` at all: every
		// `Kind::Custom` node reachable from the roots has a non-null `custom` that is not a
		// placeholder. Two assertions, not one -- `custom != nullptr && is_placeholder()` is also
		// false for a null pointer, which is the worse bug (`validate.h`'s `NullCustom`).
		select_language();
		const uint16_t assigned = menu::assign_node_ids(nodes::kAllRoots);
		size_t custom_nodes = 0;
		for (uint16_t id = 1; id <= assigned; id++) {
			const Node* n = menu::node_by_id(id);
			if (n == nullptr || n->kind != menu::Kind::Custom) {
				continue;
			}
			custom_nodes++;
			expect(n->custom != nullptr).to_be_true();
			expect(n->custom != nullptr && n->custom->is_placeholder()).to_be_false();
		}
		expect(custom_nodes).to_equal(menu::kShippedCustomNodes);
	});

	it("keeps the placeholder mechanism itself, now with no users", _ {
		// `nodes::kPending` stays, and still answers `is_placeholder()`. That is the point of
		// removing the *option* rather than the *rule*: the next table that needs a stand-in gets
		// one, and `validate_tree` reports it with nothing left to suppress it.
		expect(nodes::kPending.is_placeholder()).to_be_true();
	});
});

describe midi_device_linked_item("scenes::menu midi_device_linked", $ {

	before_each([] {
		static QuietDisplay d;
		if (display == nullptr) {
			display = &d;
		}
	});

	it("agrees with Linked::readCurrentValue about whether a definition is linked", _ {
		// Three model states, each side driven from its own sentinel: the model is written, legacy
		// is asked through `getCurrentOutput()`, and the port is asked through a freshly built
		// `MenuContext`.
		MidiRig rig;
		const char* const kPaths[] = {"", "MIDI_DEVICES/DEFINITION/DEVICE1.XML", ""};
		std::vector<int> legacy;
		std::vector<int> port;
		for (const char* path : kPaths) {
			rig.instrument()->deviceDefinitionFileName = path;
			midiDeviceLinkedMenu.readCurrentValue();
			legacy.push_back(midiDeviceLinkedMenu.getValue() ? 1 : 0);

			rig.instrument()->deviceDefinitionFileName = path;
			port.push_back(nodes::MidiDeviceLinkedItem::linked(rig.ctx()) ? 1 : 0);
		}
		expect(port).to_equal(legacy);
		// Not all one answer, so a constant-returning port could not pass.
		expect(legacy).to_equal(std::vector<int>{0, 1, 0});
	});

	it("says nothing is linked when the output is not a MIDI instrument", _ {
		// Legacy's `readCurrentValue` casts `getCurrentOutput()` unguarded and is safe only because
		// `isRelevant` (`linked.h:51-54`) refused first -- which is `relevance::is_midi_out` on the
		// node. The port answers false rather than reading through the wrong type, and the predicate
		// that keeps it off screen is checked here too.
		ClipRig rig;
		expect(nodes::MidiDeviceLinkedItem::linked(rig.ctx())).to_be_false();
		expect(menu::relevance::is_midi_out(rig.ctx())).to_be_false();
	});

	it("opens the MIDI device-definition browser when nothing is linked yet", _ {
		CleanScenes clean;
		spec::QuietAudio quiet;
		ScratchCard card;
		MidiRig rig;
		rig.instrument()->deviceDefinitionFileName.clear();

		Rig r;
		press_child(r, nodes::midi_device_definition, nodes::midi_device_linked, rig.ctx());
		// Two more ticks: `after_render` files the `Push` at the end of the tick that focused the
		// item, and `BrowserScene` collects its listing in its own `after_render` a tick later.
		r.step();
		r.step();

		expect(r.on_stack(browser::kBrowserSceneKind)).to_be_true();
		// **The wiring under test**: the second descriptor, not the sample one.
		expect(static_cast<int>(browser::browser_scene().browser_id()))
		    .to_equal(static_cast<int>(browser::BrowserId::MidiDeviceDefinition));
		expect(std::string{browser::browser_scene().current_dir()}).to_equal(std::string{"MIDI_DEVICES/DEFINITION"});
	});

	it("clears the linked file and opens nothing when one is already linked", _ {
		CleanScenes clean;
		MidiRig rig;
		rig.instrument()->deviceDefinitionFileName = "MIDI_DEVICES/DEFINITION/DEVICE1.XML";

		Rig r;
		press_child(r, nodes::midi_device_definition, nodes::midi_device_linked, rig.ctx());

		// `Linked::writeCurrentValue`'s OFF arm (`linked.h:45-48`).
		expect(rig.instrument()->deviceDefinitionFileName.empty()).to_be_true();
		// And no browser: this arm opens no UI at all.
		r.step();
		r.step();
		expect(r.on_stack(browser::kBrowserSceneKind)).to_be_false();
		// `Toggle::selectButtonPress` returns `NO_NAVIGATION` (`toggle.h:42`) -- the user is left on
		// the parent list, which the port reaches by descending for one render and ascending out.
		expect(menu_scene().focused() == &nodes::midi_device_definition).to_be_true();
		expect(static_cast<int>(menu_scene().state().depth)).to_equal(0);
	});
});

describe specific_output_item("scenes::menu specific_output_selector", $ {

	before_each([] {
		static QuietDisplay d;
		if (display == nullptr) {
			display = &d;
		}
	});

	it("lands where SpecificSourceOutputSelector::beginSession lands, from an invalid stored source", _ {
		ClipRig rig;
		// Nothing recorded from yet, which is `beginSession`'s repair arm (`:36-41`).
		rig.song.output->setOutputRecordingFrom(nullptr);
		Output* legacy = legacy_pick(0);

		rig.song.output->setOutputRecordingFrom(nullptr);
		Rig r;
		press_child(r, nodes::root_audio_clip, nodes::specific_output_selector, rig.ctx());
		expect(rig.song.output->getOutputRecordingFrom() == legacy).to_be_true();
		// The repair really moved something: index 0 is the audio track itself, which it may not
		// record from, so the first valid target is the synth at index 1.
		expect(legacy == rig.synth_a).to_be_true();
	});

	it("keeps a stored source that is still valid, exactly as beginSession does", _ {
		ClipRig rig;
		rig.song.output->setOutputRecordingFrom(rig.synth_b);
		Output* legacy = legacy_pick(0);

		rig.song.output->setOutputRecordingFrom(rig.synth_b);
		Rig r;
		press_child(r, nodes::root_audio_clip, nodes::specific_output_selector, rig.ctx());
		expect(rig.song.output->getOutputRecordingFrom() == legacy).to_be_true();
		expect(legacy == rig.synth_b).to_be_true();
	});

	it("skips the outputs it cannot record from, exactly as selectEncoderAction does", _ {
		// One detent from the synth at index 1 must land on the synth at index **3**, skipping the
		// MIDI instrument at index 2 -- the discriminator this fixture exists for.
		ClipRig rig;
		rig.song.output->setOutputRecordingFrom(rig.synth_a);
		Output* legacy = legacy_pick(1);

		rig.song.output->setOutputRecordingFrom(rig.synth_a);
		Rig r;
		press_child(r, nodes::root_audio_clip, nodes::specific_output_selector, rig.ctx());
		r.turn(1);
		r.step();
		expect(rig.song.output->getOutputRecordingFrom() == legacy).to_be_true();
		expect(legacy == rig.synth_b).to_be_true();
		expect(legacy != rig.midi).to_be_true();
	});

	it("stops at the end of the list rather than wrapping", _ {
		ClipRig rig;
		rig.song.output->setOutputRecordingFrom(rig.synth_a);
		Output* legacy = legacy_pick(5);

		rig.song.output->setOutputRecordingFrom(rig.synth_a);
		Rig r;
		press_child(r, nodes::root_audio_clip, nodes::specific_output_selector, rig.ctx());
		for (int i = 0; i < 5; i++) {
			r.turn(1);
			r.step();
		}
		expect(rig.song.output->getOutputRecordingFrom() == legacy).to_be_true();
		expect(legacy == rig.synth_b).to_be_true(); // the last recordable one, not past it
	});

	it("stops at the bottom of the list too, rather than falling off it", _ {
		// The mirror of the example above, and it is not symmetry padding: index 0 is the audio
		// track itself, which `canRecordFrom` refuses, so a walk that clamped to "the first index"
		// rather than "the last index it could stop on" would land on an output the track cannot
		// record from -- and `setOutputRecordingFrom` would then store null. Found by mutating the
		// end guard; the forward end could not see it, because index 3 happens to be valid.
		ClipRig rig;
		rig.song.output->setOutputRecordingFrom(rig.synth_a);
		Output* legacy = legacy_pick(-3);

		rig.song.output->setOutputRecordingFrom(rig.synth_a);
		Rig r;
		press_child(r, nodes::root_audio_clip, nodes::specific_output_selector, rig.ctx());
		for (int i = 0; i < 3; i++) {
			r.turn(-1);
			r.step();
		}
		expect(rig.song.output->getOutputRecordingFrom() == legacy).to_be_true();
		expect(legacy == rig.synth_a).to_be_true(); // the first recordable one, not index 0
		expect(rig.song.output->getOutputRecordingFrom() != nullptr).to_be_true();
	});

	it("walks back down again, skipping the same output on the way", _ {
		ClipRig rig;
		rig.song.output->setOutputRecordingFrom(rig.synth_b);
		Output* legacy = legacy_pick(-1);

		rig.song.output->setOutputRecordingFrom(rig.synth_b);
		Rig r;
		press_child(r, nodes::root_audio_clip, nodes::specific_output_selector, rig.ctx());
		r.turn(-1);
		r.step();
		expect(rig.song.output->getOutputRecordingFrom() == legacy).to_be_true();
		expect(legacy == rig.synth_a).to_be_true();
	});

	it("draws what SpecificSourceOutputSelector::drawPixelsForOled draws", _ {
		// The pixel oracle, and it is what pins the layout: legacy's rows are
		// `OLED_MAIN_TOPMOST_PIXEL + 14` and `+ 28` (`:82`, `:92`), and the port's frame origin
		// already carries `OLED_MAIN_TOPMOST_PIXEL` (5, `src/board_config.h:64`), so a port drawing
		// at `ui::oled::output_name`'s rows (12 and 30) would fail here.
		ClipRig rig;
		rig.song.output->setOutputRecordingFrom(rig.synth_a);

		Rig r;
		press_child(r, nodes::root_audio_clip, nodes::specific_output_selector, rig.ctx());
		r.step(); // the frame after the descent, so the item is what the panel shows

		// The oracle runs SECOND and from its own state -- `beginSession` re-resolves everything it
		// draws with. `renderOLED()` is `drawScreenTitle` + `drawPixelsForOled`
		// (`gui/menu_item/menu_item.cpp:37-41`), which is what the port's `draw_title_for` plus its
		// two `text` calls are.
		deluge::hid::display::OLED::main.clear();
		specificOutputSelectorMenu.beginSession(nullptr);
		specificOutputSelectorMenu.renderOLED();
		expect(below_bezel(r.fake.pbm(0))).to_equal(below_bezel(legacy_panel_pbm()));
	});

	it("draws the same empty page when nothing can be recorded from", _ {
		// A song with only the audio track in it: `canRecordFrom` refuses `this`, so there is no
		// valid target at all and both sides take the "No track" arm (`:64-67`) -- a different
		// branch of the renderer from the example above.
		spec::AudioSongFixture f = spec::make_audio_clip_song();
		f.output->inputChannel = AudioInputChannel::SPECIFIC_OUTPUT;
		f.output->setOutputRecordingFrom(nullptr);
		const MenuContext ctx = menu::context_for(*f.song, f.clip, 0, false, false, /*kit_global_fx=*/false);

		Rig r;
		press_child(r, nodes::root_audio_clip, nodes::specific_output_selector, ctx);
		r.step();

		deluge::hid::display::OLED::main.clear();
		specificOutputSelectorMenu.beginSession(nullptr);
		specificOutputSelectorMenu.renderOLED();
		expect(below_bezel(r.fake.pbm(0))).to_equal(below_bezel(legacy_panel_pbm()));
	});

	it("agrees with SpecificSourceOutputSelector::isRelevant about when it is offered", _ {
		// `isRelevant` (`:102-105`) is `inputChannel == SPECIFIC_OUTPUT` on the current audio
		// output; the node carries `relevance::audio_source_is_specific`. Compared over the two
		// answers plus a non-audio output, each side from its own sentinel.
		ClipRig rig;
		std::vector<int> legacy;
		std::vector<int> port;
		for (const AudioInputChannel ch : {AudioInputChannel::SPECIFIC_OUTPUT, AudioInputChannel::LEFT}) {
			rig.song.output->inputChannel = ch;
			legacy.push_back(specificOutputSelectorMenu.isRelevant(nullptr, 0) ? 1 : 0);
			rig.song.output->inputChannel = ch;
			port.push_back(menu::relevance::audio_source_is_specific(rig.ctx()) ? 1 : 0);
		}
		expect(port).to_equal(legacy);
		expect(legacy).to_equal(std::vector<int>{1, 0});

		// And on a MIDI clip -- where legacy's unguarded cast would be reading the wrong type -- the
		// port simply says no.
		MidiRig midi;
		expect(menu::relevance::audio_source_is_specific(midi.ctx())).to_be_false();
	});
});
// clang-format on

/// Four `describe` groups in one TU, so `DELUGE_SPEC` is spelled out with `add_specs`
/// (`sample_items_spec.cpp`'s shape).
int output_items_spec(int argc, char** const argv) {
	return CppSpec::parse(argc, argv)
	               .add_specs(output_items_mapping, output_items_milestone, midi_device_linked_item,
	                          specific_output_item)
	               .exec()
	               .is_success()
	           ? EXIT_SUCCESS
	           : EXIT_FAILURE;
}
