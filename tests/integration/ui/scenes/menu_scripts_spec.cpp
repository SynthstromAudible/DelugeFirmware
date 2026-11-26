/// @file
/// @brief Level-2 acceptance scripts, driven through the real `Shell` over
///        `uitest::FakeSurfaceSet` (`tests/unit/ui/shell/support/script_runner.h`) against the REAL
///        declarative tables (`src/ui/scenes/menu/nodes/`) -- not a synthetic tree.
///
/// Three groups of script live here.
///
/// **Seven scripts** run the same generic recipe -- a Select-encoder detent, a select-encoder
/// press/release, a GoldUpper detent, a Back press/release, snapshotting after every step -- against
/// a real node `RunOptions::initial_scene` opens directly (`SceneId{FirstApp, node_id(target)}`).
/// What that recipe visibly DOES depends entirely on the target's own `Kind`, which is the point:
/// one interpreter, seven different real screens.
///
/// **Twelve scripts** are each shaped by their subject rather than by a shared recipe, because the
/// subjects are not all one kind of leaf:
///
// clang-format off
/// | script | subject |
/// |---|---|
/// | `menu_sync_level` | `Kind::SyncLevel`'s renderer |
/// | `menu_chain` | `Node::select_chain` on a value-leaf target |
/// | `rename` | `nodes::EditNameItem` -> `rename::RenameScene` |
/// | `menu_reset` | the five reset `nodes::ContextMenuItem`s, and `go_up_on_resume` |
/// | `context_menu` | `context_menu::ContextMenuScene`'s multi-option descriptor |
/// | `menu_midi_learn` | `nodes::MidiCommandItem`, and the LEARN blink in `.state` |
/// | `menu_patch_cables` | `nodes::PatchCablesItem` and the chain into a strength page |
/// | `menu_cv_selection` | `nodes::CvSelectionItem`, a runtime list, and its `source_index` write |
/// | `menu_action` | `nodes::ActionItem`: file a `MenuAction`, ascend straight back out |
/// | `menu_perform_editor` | `nodes::PerformEditorItem`: the action item that is also a page |
/// | `menu_range` | `Kind::Range`'s two-edge editor |
/// | `menu_scales` | `nodes::ActiveScalesItem`, the preset-scale checklist |
// clang-format on
///
/// Three of the twelve deliberately carry a script *step* with no snapshot after it. That is not an
/// omission: the frame would have been byte-identical to the one before it, and a golden whose whole
/// content repeats its predecessor can never fail for the right reason. Each of those steps is
/// asserted in C++ instead, against the state the pixels could not have shown anyway; the script
/// files say which and why.
///
/// **Why one node id resolves correctly across every script.** `MenuScene::on_enter` re-targets the
/// path from `SceneId::arg` (`node_by_id`) but never touches `ctx_` (`MenuScene::on_enter`), so
/// each `it` block calls `menu_scene().open(target, ctx)` itself first -- purely to install `ctx_`
/// for its own fixture on the process-wide singleton -- before handing `expect_script_goldens` a
/// `RunOptions::initial_scene` that re-targets the path a second time via the id. `assign_node_ids`
/// is called fresh in every block, over exactly the roots that block's own target is reachable from
/// (it is idempotent over one root set, but a later call over a DIFFERENT set is what makes ids
/// valid for that set -- see `node_id.h`), so blocks never depend on execution order.
///
/// **The range-permission script is the one target with no real host.** Every `range_specific` node
/// in the committed tables (`nodes::kRangeSpecificArray`, `nodes/nodes.h`) is a value column of a
/// `Layout::Horizontal` page (`source0`/`source1`), and a horizontal page's `columns` widget claims
/// `entered` only for a `Kind::Submenu` column (`ui/widgets/oled/columns.cpp`) -- so `enter_child` (the only
/// caller of `permission_for`) is never reached through the real committed tree. To still exercise
/// the REAL `source0_transpose` node's permission gate, this file declares one small vertical
/// wrapper submenu naming it as a real child alongside `nodes::lpf_mode` -- the wrapper is
/// synthetic, the leaf it gates is not.
///
/// **Four scripts** cover the per-device MIDI page and the three scenes the sample items open, and
/// three of the four are the *sequel to that last paragraph*, because the same Custom-column gap
/// governs them:
///
// clang-format off
/// | script | subject |
/// |---|---|
/// | `menu_midi_device` | the per-device MIDI page and its MPE chain (`nodes/midi_device.h`) |
/// | `browser` | `browser::BrowserScene` over a real card: list, descend, search, ascend, close |
/// | `marker` | `marker::MarkerScene`: the pad waveform, the four-phase blink, the two ways to move a marker |
/// | `recorder` | `recorder::RecorderScene`: a real `SampleRecorder` armed, and the whole close path |
// clang-format on
///
/// **How the three scene scripts get in, and why it is not a workaround.** Every one of the nine
/// `nodes/sample.h` items is declared on a `Layout::Horizontal` page (`source0`, `audio_clip_sample`,
/// `recorder`), so the paragraph above applies to them verbatim: no select press in the committed
/// tree descends into any of them. Unlike `source0_transpose`, though, these three DO have a real
/// door -- the shortcut-pad jump, whose table names all of them (`menu/shortcuts.cpp`, legacy's
/// own `paramShortcutsForSounds` cells). So these scripts use a real shift+pad press rather than a
/// synthetic wrapper submenu, and what they exercise is the path a user actually has.
///
/// Two consequences worth knowing before reading their goldens:
///
///   * the jump re-roots `MenuState::path` **onto the item itself** (`MenuScene::handle`'s
///     `Replace`), so the item is at depth 0 and `go_up_on_resume`'s ascend out of it closes the
///     menu, as legacy's `goUpOneLevel` does at depth 0 (`exitCompletely()`). Each script's last
///     frame is therefore the view under the menu;
///   * a gold-knob turn acts on every detent, as `main`'s `interpretEncoders` does.
///
/// @par The twelve gold-turn scripts install the real three, UNSEEDED -- and what that does not settle
///
/// **Twelve of the 24 `expect_script_goldens` calls here install `services::ModKnobs`,
/// `GlobalControls` and `MidiLearn` through `support/real_services.h`, with `svc.song` set and
/// `svc.mod_target` deliberately LEFT NULL.** The other twelve still pass
/// `deluge::ui::shell::Services{}` -- three `NullService`s -- because they drive no gold turn and
/// the install would move their goldens for nothing.
///
/// The install costs: **57 `.state` files, 114 insertions, 0 deletions, 0 `.pbm` files, 0 value
/// assertions**. Every inserted line is one of exactly two:
///
///     indicator lower 0 0 0 0
///     indicator upper 0 0 0 0
///
/// -- `ModKnobs::leds` declaring both mod-knob rings on every tick, all four quadrants dark because
/// there is no target to read a level from. `led Mod1 on` does **not** appear; that comes from
/// seeding, and seeding is still owed. `Services{}` is three nulls, which is *further* from the
/// application than an unseeded `ModKnobs`, so this is not a choice between a right fixture and a
/// wrong one -- it is very wrong versus one seeded step from right, and 114 lines of a single
/// repeated pair is not the ambiguous two-cause diff that the one-cause-per-golden-change rule
/// exists to prevent.
///
/// **Still owed, as a named edit:** seed `svc.mod_target` and re-record (`led Mod1 on` on the frames that install the
/// services).

#include "gui/ui/menu_view_of.h"
#include "gui/ui/shell_host/shell_host.h"
#include "support/peak_seed.h"
#include "ui/scenes/browser/browser_scene.h"
#include "ui/scenes/context_menu/context_menu_scene.h"
#include "ui/scenes/marker/marker_scene.h"
#include "ui/scenes/menu/action.h"
#include "ui/scenes/menu/bindings.h"
#include "ui/scenes/menu/context.h"
#include "ui/scenes/menu/menu_scene.h"
#include "ui/scenes/menu/node.h"
#include "ui/scenes/menu/node_id.h"
#include "ui/scenes/menu/nodes/nodes.h"
#include "ui/scenes/menu/nodes/patch_cables.h"
#include "ui/scenes/menu/nodes/scales.h"
#include "ui/scenes/profile.h"
#include "ui/scenes/recorder/recorder_scene.h"
#include "ui/scenes/registry.h"
#include "ui/scenes/rename/rename_scene.h"
#include "ui/scenes/services/view_state.h"

#include "base/notify/dirty_set.h"
#include "base/notify/notice_queue.h"
#include "ui/shell/profiles/deluge.h"
#include "ui/shell/shell.h"

#include "gui/l10n/l10n.h"
#include "gui/l10n/strings.h"
#include "gui/views/performance_view.h"
#include "hid/display/display.h"
#include "io/midi/device_registry.h"
#include "io/midi/midi_device.h"
#include "io/midi/midi_device_manager.h"
#include "io/midi/midi_engine.h"
#include "model/clip/clip.h"
#include "model/edits/setting_edit.h"
#include "model/project_bridge/slot_bridge.h"
#include "model/sample/sample.h"
#include "model/sample/sample_recorder.h"
#include "model/song/song.h"
#include "modulation/patch/patch_cable.h"
#include "modulation/patch/patch_cable_set.h"
#include "processing/sound/sound.h"
#include "processing/sound/sound_instrument.h"
#include "processing/sound/source_resources.h"
#include "storage/cluster/cluster.h"
#include "storage/flash_storage.h"
#include "storage/multi_range/multisample_range.h"
#include "synth/modulation/cable.h"

#include "support/menu_engine.h"
#include "support/no_notification_sink.h"
#include "support/song_fixture.h"

// Before <cppspec.hpp>: it #defines `it`, which collides with every local named `it` in the project
// headers this drags in (`audio_engine.h` -> `util/container/ordered_pos_vector.h`). Same placement
// rule `browser_scene_spec.cpp`, `marker_scene_spec.cpp` and `recorder_scene_spec.cpp` run under.
#include "support/card_access.h"

// Before `unit/ui/shell/support/script_runner.h`, which is what actually drags `<cppspec.hpp>` in -- so "before
// cppspec" is not enough here, it has to be before this line. `real_services.h` reaches
// `ui/scenes/overlay/param_popup_scene.h`, whose `push` takes a parameter named `context`, and CppSpec `#define`s
// `context` to `self.context`. The same placement rule as `card_access.h` above, for the same kind of collision on a
// different keyword.
#include "support/real_services.h"

#include "unit/ui/shell/support/script_runner.h"

#include "cppspec.hpp"
#include "support/record_state.h"

#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <string>

#include "support/menu_context.h"
#include "support/spec_main.h"

using namespace deluge;

using namespace deluge::ui::scenes;
using deluge::l10n::String;
using deluge::ui::scenes::menu::MenuAction;
using deluge::ui::scenes::menu::MenuActionRequest;
using deluge::ui::scenes::menu::MenuContext;
using deluge::ui::scenes::menu::Node;
namespace bindings = deluge::ui::scenes::menu::bindings;
namespace nodes = deluge::ui::scenes::menu::nodes;
namespace context_menu = deluge::ui::scenes::context_menu;
namespace browser = deluge::ui::scenes::browser;
namespace marker = deluge::ui::scenes::marker;
namespace recorder = deluge::ui::scenes::recorder;

namespace {

/// @brief An `l10n::String` as the plain `uint16_t` a `Node` carries -- the same helper every
///        `nodes/*.cpp` and `menu_scene_spec.cpp` define locally rather than export.
constexpr uint16_t sid(String s) {
	return static_cast<uint16_t>(s);
}

/// @brief Reset the two process-global things a menu touches, and the singleton's own state.
///
/// Mirrors `menu_scene_spec.cpp`'s `select_language`, plus draining `dirty()`: a script run reads
/// `after_render`'s repaint requests through it, and a fact an earlier `it` left behind must not
/// leak into this one's first tick.
void reset_globals() {
	menu_scene().reset_process_state(); // each example starts with the rows a boot has
	deluge::l10n::chosenLanguage = &deluge::l10n::built_in::english;
	deluge::base::notify::Notice discard;
	while (deluge::base::notify::notices().pop(discard)) {}
	deluge::base::notify::dirty().drain();
}

/// @brief The synthetic vertical wrapper the range-permission script opens: two REAL leaves,
///        `lpf_mode` (an ordinary Enum, so `Select +1` off the default cursor lands on row 1) and
///        `source0_transpose` (the real range-dependent Decimal `permission_for` gates).
constexpr const Node* kRangePermChildren[] = {&nodes::lpf_mode, &nodes::source0_transpose};
constexpr Node range_perm_root =
    deluge::ui::scenes::menu::submenu(sid(String::STRING_FOR_TRANSPOSE), kRangePermChildren);
constexpr const Node* kRangePermRoots[] = {&range_perm_root};

/// @brief `menu_scene().state().list[0].cursor` -- the same accessor `menu_scene_spec.cpp` uses.
int cursor0() {
	return menu_scene().state().list[0].cursor;
}

/// @brief The cursor of the row list at whatever depth the menu is currently at.
///
/// `cursor0()` is right only while the target sits at depth 0. Several scripts open nodes
/// levels down and some of them descend further, so the depth has to be read rather than assumed.
int cursor_now() {
	const deluge::ui::scenes::menu::MenuState& st = menu_scene().state();
	return st.depth < deluge::ui::scenes::menu::kMenuDepth ? st.list[st.depth].cursor : -1;
}

/// @brief The `RunOptions` every block here shares, pointed at @p node.
///
/// Twelve blocks repeating the same five-field aggregate is twelve chances to get the profile or the
/// board wrong in one of them; this makes the only thing that varies -- which node the run opens on
/// -- the only thing spelled at the call site.
/// @param node The node whose `node_id` becomes `SceneId::arg`.
/// @return The options.
uitest::RunOptions opts_for(const Node& node) {
	return uitest::RunOptions{
	    .tick_ms = 10,
	    .board = &deluge::ui::shell::kDelugeBoard,
	    .profile = &deluge::ui::scenes::deluge_profile(),
	    .initial_scene = deluge::ui::shell::SceneId{deluge::ui::shell::SceneKind::FirstApp, menu::node_id(node)},
	};
}

/// @brief A `MIDICable` with a fixed display name, so a learned readout is a stable golden.
///
/// The same shape `tables_3_spec.cpp` keeps for the same reason; it is spec-local there and here
/// because `MIDICable` is abstract and neither file wants to export a test double.
struct FakeCable final : ::MIDICable {
	void writeToFlash(uint8_t*) override {}
	char const* getDisplayName() override { return "SPEC CABLE"; }
	void sendMessage(MIDIMessage) override {}
	void sendSysex(const uint8_t*, int32_t) override {}
	[[nodiscard]] size_t sendBufferSpace() const override { return 0; }

protected:
	void writeReferenceAttributesToFile(Serializer&) override {}
};

FakeCable& fake_cable() {
	static FakeCable c;
	return c;
}

/// @brief Save and restore every `midiEngine.globalMIDICommands` slot around the learn script.
///
/// The whole array, not the one slot the script touches: an item that cleared the *wrong* command is
/// exactly what a twelve-way mapping has to catch, and restoring only one slot would carry that
/// mistake into the next `it` instead of failing here. (`tables_3_spec.cpp` keeps the same helper.)
struct CommandSlots {
	LearnedMIDI slots[kNumGlobalMIDICommands];

	static CommandSlots capture() {
		CommandSlots c;
		for (int32_t i = 0; i < kNumGlobalMIDICommands; i++) {
			c.slots[i] = midiEngine.globalMIDICommands[i];
		}
		return c;
	}
	void restore() const {
		for (int32_t i = 0; i < kNumGlobalMIDICommands; i++) {
			midiEngine.globalMIDICommands[i] = slots[i];
		}
	}
	static void clear_all() {
		for (int32_t i = 0; i < kNumGlobalMIDICommands; i++) {
			midiEngine.globalMIDICommands[i].clear();
		}
	}
};

/// @brief Save and restore one `FlashStorage::defaultRanges` entry around a `Kind::Range` script.
///
/// `nodes::default_tempo` edits process-wide flash-backed state (its DEVICE rows, which the host keeps in
/// `FlashStorage::defaultRanges`), and the script deliberately moves both ends of it. Every other spec in
/// this executable reads the same array.
struct SavedRange {
	int32_t index;
	int32_t lower;
	int32_t upper;

	static SavedRange capture(int32_t index) {
		return SavedRange{index, FlashStorage::defaultRanges[index].lower, FlashStorage::defaultRanges[index].upper};
	}
	void restore() const {
		FlashStorage::defaultRanges[index].lower = lower;
		FlashStorage::defaultRanges[index].upper = upper;
	}
};

// ---------------------------------------------------------------------------------------------
// The per-device MIDI page and the three scene scripts, and the fixtures they need.
//
// The browser and the marker editor are reached here through the **shortcut-pad jump**, and that is
// not a shortcut in the convenience sense: `file0_selector` and `sample0_start` are declared only on
// `Layout::Horizontal` pages (`source0`, `audio_clip_sample`), and a horizontal page's `columns`
// widget claims `entered` only for a `Kind::Submenu` column (`ui/widgets/oled/columns.cpp`) -- so no select
// press in the committed tree descends into either. The shortcut table does
// (`menu/shortcuts.cpp`, legacy's own `paramShortcutsForSounds` cells), which makes it the only
// door those two have -- the same Custom-column navigation gap `menu_range_permission`'s synthetic
// wrapper already records one level up, met here with a real path instead of a synthetic one.
//
// **The recorder is the exception.**
// `sample0_recorder` is also a child of the *vertical* `sound_actions` (`nodes/sound_actions.h`)
// and `drum_actions` (`nodes/roots.h`), and `ui::widgets::oled::list` sets `entered` for any selectable
// row -- so SOUND -> ACTIONS -> RECORD TO SAMPLE is two ordinary select
// presses. `recorder.txt` drives that door first and the shortcut second, in one run, because the
// two do not end the same way: the ordinary route leaves the item at depth 2 where
// `MenuWalkState::go_up_on_resume` ascends to the ACTIONS list, and the jump re-roots onto the item at
// depth 0, where going up closes the menu.

/// @brief The minimum `Display` the model reaches for.
///
/// `display` is null in a spec process (`hid/display/display.cpp`), and both
/// `AudioEngine::getNewRecorder`'s failure paths and `Kit::makeDrumNameUnique` go through it.
/// `recorder_scene_spec.cpp` and `marker_scene_spec.cpp` each keep the identical stub for the same
/// reason; it is spec-local in all three because `hid::Display` is abstract.
struct ScriptDisplay final : deluge::hid::Display {
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

/// @brief Install `ScriptDisplay` once, for the scripts that need one.
void install_display() {
	static ScriptDisplay d;
	if (display == nullptr) {
		display = &d;
	}
}

/// @name The sample fixture, lifted from `marker_scene_spec.cpp` for its reason.
///
/// A spec process has no mounted card holding the file this `Sample` claims to be. It does not have to: the
/// fixture seeds the sample's peak index (`support/peak_seed.h`), so every column is answered from the index.
/// @{
constexpr size_t kClusterSize = 32768;
constexpr int32_t kBytesPerFrame = 2; // 16-bit mono
constexpr uint64_t kSampleLength = static_cast<uint64_t>(kClusterSize) * 32 / kBytesPerFrame;
constexpr int32_t kNumClusters = 32;

/// The per-cluster peak: a widening shape, so the golden shows something recognisable rather than a
/// flat band.
int8_t cluster_peak(int32_t i) {
	return static_cast<int8_t>(16 + 3 * i);
}

Sample* make_sample() {
	Cluster::set_size(kClusterSize);
	// Placed in the deluge heap and never freed -- `song_fixture.h`'s own rule, and for its reason.
	void* memory = deluge::memory::alloc_fast(sizeof(Sample));
	auto* s = new (memory) Sample();
	s->byteDepth = 2;
	s->numChannels = 1;
	s->sampleRate = 44100;
	s->audioDataStartPosBytes = 0;
	s->audioDataLengthBytes = kSampleLength * kBytesPerFrame;
	s->lengthInSamples = kSampleLength;
	peak_seed::seed_peaks(*s, [&](int32_t i) {
		return std::pair<int8_t, int8_t>{static_cast<int8_t>(static_cast<int8_t>(-cluster_peak(i))),
		                                 static_cast<int8_t>(cluster_peak(i))};
	});
	return s;
}
/// @}

/// @brief Point @p song's source 0 at a sample source with exactly one `MultisampleRange`.
///
/// **Exactly one** matters twice: `context_for` resolves `MenuContext::range` only for a
/// single-range source (`menu/context.cpp`), and `permission_for` answers `MustSelectRange` for a
/// range-dependent item on a multi-range source with nothing selected -- which every one of these
/// three items is. One range is what makes the shortcut jump land on the item rather than on the
/// range picker.
/// @param song   The fixture.
/// @param sample The sample to hang on the range, or null to leave the holder empty.
/// @return The range, so a caller can read the marker positions back.
MultisampleRange* install_sample_source(spec::SongFixture& song, Sample* sample) {
	auto* sound = song.instrument->toModControllable()->effectsOwner()->sound;
	SourceResources& source = sound->sourceResources[0];
	deluge::test::set(*sound->state_, "osc1/type", "sample");
	deluge::test::set(*sound->state_, "osc1/loopMode", "1"); // ONCE
	deluge::test::set(*sound->state_, "osc1/reversed", "0");
	sound->deriveConfig();
	auto* range = static_cast<MultisampleRange*>(sound->getOrCreateFirstRange(0));
	if (sample != nullptr) {
		range->sampleHolder.audioFile = sample;
		range->sampleHolder.startPos = 0;
		range->sampleHolder.endPos = kSampleLength;
		range->sampleHolder.loopStartPos = 0;
		range->sampleHolder.loopEndPos = 0;
		range->sampleHolder.loopLocked = false;
	}
	// Set field by field: record it as a load does, so the record the scenes read is the source's truth.
	deluge::bridge::recordLoadedSound(*sound, *song.song);
	return range;
}

/// @brief The card these examples list: `tests/fixtures/scenes_spec/menu_scripts`, installed for this file before boot
///        (`tests/integration/checks/scenes_spec_cards.txt`). Constructing one checks it is the installed card.
struct ScratchCard {
	std::filesystem::path dir = std::filesystem::path(DELUGE_REPO_ROOT) / "tests/fixtures/scenes_spec/menu_scripts";
	spec::SdRoot root{dir.string()};
};

/// @brief Save and restore everything the MIDI-device script touches on the two shipped cables.
///
/// Both are process-global objects (`MIDIDeviceManager::dinMIDIPorts`,
/// `::upstreamUSBMIDICable1`), and the script deliberately *writes* one of them -- so the whole
/// observable surface is captured, not just the field the script means to move. `midi_device_spec`
/// keeps the same shape for the same reason: a hook wired to the wrong port or the wrong cable has
/// to be caught here rather than carried into the next block.
struct SavedCables {
	struct One {
		uint8_t flags;
		uint8_t lower[2];
		uint8_t upper[2];
	};
	One a;
	One b;

	static One of(const MIDICable& c) {
		return One{c.connectionFlags,
		           {c.ports[0].mpeLowerZoneLastMemberChannel, c.ports[1].mpeLowerZoneLastMemberChannel},
		           {c.ports[0].mpeUpperZoneLastMemberChannel, c.ports[1].mpeUpperZoneLastMemberChannel}};
	}
	static void put(MIDICable& c, const One& s) {
		c.connectionFlags = s.flags;
		for (int32_t p = 0; p < 2; p++) {
			c.ports[p].mpeLowerZoneLastMemberChannel = s.lower[p];
			c.ports[p].mpeUpperZoneLastMemberChannel = s.upper[p];
		}
	}
	static SavedCables capture() {
		return SavedCables{of(MIDIDeviceManager::dinMIDIPorts), of(MIDIDeviceManager::upstreamUSBMIDICable1)};
	}
	void restore() const {
		put(MIDIDeviceManager::dinMIDIPorts, a);
		put(MIDIDeviceManager::upstreamUSBMIDICable1, b);
	}
};

/// Writes @p menu_value to the cable @p learn names through the app's sink, as the strength page writes it; a new
/// cable takes its source's default polarity.
bool seed_cable(const menu::MenuContext& ctx, deluge::synth::CableTarget learn, int32_t menu_value) {
	const std::optional<deluge::ui::scenes::bind::WriteRequest> request =
	    menu::nodes::cable_request(ctx, learn, menu_value, PatchCable::getDefaultPolarity(learn.bottom_source()));
	return request.has_value() && ctx.ports.writes->write(*request);
}

} // namespace

// clang-format off
describe menu_scripts("scenes::menu per-kind level-2 acceptance scripts", $ {
	it("Int: nodes::saturation renders and edits through the real Shell", _ {
		reset_globals();
		spec::SongFixture song = spec::make_synth_song();
		const MenuContext ctx = spec::ctx_of(*song.song, deluge::gui::menuViewOf(song.clip), 0, false, false, false, false);
		(void)menu::assign_node_ids(nodes::kAllRoots);
		menu_scene().open(nodes::saturation, ctx);

		const int32_t before = bindings::read(nodes::saturation, ctx);

		uitest::RunOptions opts{
		    .tick_ms = 10,
		    .board = &deluge::ui::shell::kDelugeBoard,
		    .profile = &deluge::ui::scenes::deluge_profile(),
		    .initial_scene = deluge::ui::shell::SceneId{deluge::ui::shell::SceneKind::FirstApp,
		                                               menu::node_id(nodes::saturation)},
		};
		// The real three, UNSEEDED -- `mod_target` deliberately left null. See this file's
		// `@par Every script here installs the real three, unseeded`.
		spec::RealServices svc;
		svc.song = song.song;
		opts.after_construct = svc.installer();
		const uitest::RunResult result =
		    uitest::expect_script_goldens(self, "menu_int", ui::scenes::scene_registry(), svc.slots(), opts);
		// `idle`, `after_select_edit` and `after_back`.
		expect(result.snapshots).to_equal(uint32_t{3});

		// Q7: the `.state` pins WHICH node is open; it says nothing about its label or value, so
		// both are asserted here directly. The name check is against the EXPECTED `l10n::String` id,
		// not `nodes::saturation.name` -- comparing a node's own field against itself cannot fail
		// once the pointer-identity check just above already holds; this is the independent
		// value that has to be pinned.
		expect(menu_scene().focused()).to_equal(&nodes::saturation);
		expect(static_cast<uint32_t>(menu_scene().focused()->name)).to_equal(static_cast<uint32_t>(sid(String::STRING_FOR_SATURATION)));
		// `int_value` edits on the select encoder, not the gold knobs (`ui/widgets/oled/values.cpp`'s
		// `select_delta`): the script's `100 encoder Select +1` step -- once a claimed-and-ignored
		// no-op -- is what moves the value, by exactly 1 select detent * the
		// node's own `IntRange{0,15,1}.step` (1). A closing `GoldUpper` step would test nothing (gold
		// reaches nobody: `int_value` claims no gold input, and the unseeded `ModKnobs` behind it has
		// no target to write through) -- so that step is `encoder Select +2` too, a second select
		// edit on the same leaf, landing at `1 + 2 = 3`.
		expect(bindings::read(nodes::saturation, ctx)).to_equal(std::min(before + 3, 15));
	});

	it("Decimal: nodes::source0_transpose renders and edits through the real Shell", _ {
		reset_globals();
		spec::SongFixture song = spec::make_synth_song();
		const MenuContext ctx = spec::ctx_of(*song.song, deluge::gui::menuViewOf(song.clip), 0, false, false, false, false);
		(void)menu::assign_node_ids(nodes::kAllRoots);
		menu_scene().open(nodes::source0_transpose, ctx);

		const int32_t before = bindings::read(nodes::source0_transpose, ctx);

		uitest::RunOptions opts{
		    .tick_ms = 10,
		    .board = &deluge::ui::shell::kDelugeBoard,
		    .profile = &deluge::ui::scenes::deluge_profile(),
		    .initial_scene = deluge::ui::shell::SceneId{deluge::ui::shell::SceneKind::FirstApp,
		                                               menu::node_id(nodes::source0_transpose)},
		};
		// The real three, UNSEEDED -- `mod_target` deliberately left null. See this file's
		// `@par Every script here installs the real three, unseeded`.
		spec::RealServices svc;
		svc.song = song.song;
		opts.after_construct = svc.installer();
		const uitest::RunResult result = uitest::expect_script_goldens(
		    self, "menu_decimal", ui::scenes::scene_registry(), svc.slots(), opts);
		// Five, not six: the script's first GoldUpper turn lands on a page that reads no edit
		// encoder, so its snapshot was byte-identical to `after_select_press` and is not recorded -- the
		// *step* stays, because the "unchanged" assertion below is about it.
		expect(result.snapshots).to_equal(uint32_t{5});

		// `source0_transpose` is one of the 53 `PatchedParam` nodes, so its select press opens the source picker rather
		// than ascending (`PatchedParam::selectButtonPress` in
		// `origin/main:src/deluge/gui/menu_item/patched_param.cpp`). The script's first GoldUpper turn therefore lands
		// on that picker and edits nothing; the Back brings the leaf back and the script's closing select turn is the
		// one that moves the value a second time.
		expect(menu_scene().focused()).to_equal(&nodes::source0_transpose);
		expect(static_cast<uint32_t>(menu_scene().focused()->name)).to_equal(static_cast<uint32_t>(sid(String::STRING_FOR_TRANSPOSE)));
		// `decimal_value` edits on the select encoder (`ui/widgets/oled/values.cpp`'s `select_delta`), by `10^edit_pos` per
		// detent. `edit_pos` starts on 2 (`Decimal::getDefaultEditPos`
		// (`origin/main:src/deluge/gui/menu_item/decimal.h`)), so the script's `Select +1` moves the value 1.00 and its
		// closing `Select +2` moves it 2.00, landing at `before + 300`. The `GoldUpper` turn between them reaches
		// nobody: it lands on the source picker the select press opens, which reads no edit encoder.
		expect(bindings::read(nodes::source0_transpose, ctx)).to_equal(std::min(before + 300, 9600));
		// `after_select_turn`'s pixel diff against `idle` is a real effect of the Select turn, not only
		// the cursor's blink. `edit_pos` moves only on the "Horizontal" encoder, which this script never
		// touches, so it is still 2 after both select turns.
		expect(static_cast<int>(menu_scene().state().decimal_pos)).to_equal(2);
	});

	it("Enum: nodes::lpf_mode renders and edits through the real Shell", _ {
		reset_globals();
		spec::SongFixture song = spec::make_synth_song();
		const MenuContext ctx = spec::ctx_of(*song.song, deluge::gui::menuViewOf(song.clip), 0, false, false, false, false);
		(void)menu::assign_node_ids(nodes::kAllRoots);
		menu_scene().open(nodes::lpf_mode, ctx);

		const int32_t before = bindings::read(nodes::lpf_mode, ctx); // TRANSISTOR_24DB == 1

		uitest::RunOptions opts{
		    .tick_ms = 10,
		    .board = &deluge::ui::shell::kDelugeBoard,
		    .profile = &deluge::ui::scenes::deluge_profile(),
		    .initial_scene = deluge::ui::shell::SceneId{deluge::ui::shell::SceneKind::FirstApp,
		                                               menu::node_id(nodes::lpf_mode)},
		};
		// The real three, UNSEEDED -- `mod_target` deliberately left null. See this file's
		// `@par Every script here installs the real three, unseeded`.
		spec::RealServices svc;
		svc.song = song.song;
		opts.after_construct = svc.installer();
		const uitest::RunResult result =
		    uitest::expect_script_goldens(self, "menu_enum", ui::scenes::scene_registry(), svc.slots(), opts);
		// Three, not five: the Select-press and GoldUpper-turn snapshots are not recorded -- both
		// would be byte-identical to `after_select_turn`, for the two reasons the script file's own
		// comments name. The steps stay; the final assertion below (`before + 1`, not `+2`) is their
		// proof: either one moving the value too would show up in that total.
		expect(result.snapshots).to_equal(uint32_t{3});

		expect(menu_scene().focused()).to_equal(&nodes::lpf_mode);
		expect(static_cast<uint32_t>(menu_scene().focused()->name)).to_equal(static_cast<uint32_t>(sid(String::STRING_FOR_MODE)));
		// `enum_value` owns the Select turn itself: one detent moves the option index by one, and it
		// commits on the same tick (`Response::changed`). The later SelectEnc press/release and
		// GoldUpper turn (unclaimed by this widget) do not move it again.
		expect(bindings::read(nodes::lpf_mode, ctx)).to_equal(before + 1);
	});

	it("Toggle: nodes::osc_sync renders and edits through the real Shell", _ {
		reset_globals();
		spec::SongFixture song = spec::make_synth_song();
		const MenuContext ctx = spec::ctx_of(*song.song, deluge::gui::menuViewOf(song.clip), 0, false, false, false, false);
		(void)menu::assign_node_ids(nodes::kAllRoots);
		menu_scene().open(nodes::osc_sync, ctx);

		expect(bindings::read(nodes::osc_sync, ctx)).to_equal(int32_t{0}); // off by default

		uitest::RunOptions opts{
		    .tick_ms = 10,
		    .board = &deluge::ui::shell::kDelugeBoard,
		    .profile = &deluge::ui::scenes::deluge_profile(),
		    .initial_scene = deluge::ui::shell::SceneId{deluge::ui::shell::SceneKind::FirstApp,
		                                               menu::node_id(nodes::osc_sync)},
		};
		// The real three, UNSEEDED -- `mod_target` deliberately left null. See this file's
		// `@par Every script here installs the real three, unseeded`.
		spec::RealServices svc;
		svc.song = song.song;
		opts.after_construct = svc.installer();
		const uitest::RunResult result = uitest::expect_script_goldens(
		    self, "menu_toggle", ui::scenes::scene_registry(), svc.slots(), opts);
		// Three, not five: the Select-press and GoldUpper-turn snapshots are not recorded, for the
		// same two reasons as `menu_enum` above. The steps stay; the final assertion below (flipped
		// exactly once, not twice) is their proof.
		expect(result.snapshots).to_equal(uint32_t{3});

		expect(menu_scene().focused()).to_equal(&nodes::osc_sync);
		expect(static_cast<uint32_t>(menu_scene().focused()->name)).to_equal(static_cast<uint32_t>(sid(String::STRING_FOR_OSCILLATOR_SYNC)));
		// `toggle`'s turn selects the row in the turn's direction: one detent toward ON flips it.
		// The later SelectEnc press reports `entered` and leaves `changed` false (`ui/widgets/oled/values.h`),
		// and GoldUpper is unclaimed by this widget, so the value stays flipped, not flipped twice.
		expect(bindings::read(nodes::osc_sync, ctx)).to_equal(int32_t{1});
	});

	it("Submenu (horizontal): nodes::lpf renders and edits its focused column through the real Shell", _ {
		// The script reads the shell's own banner, not the guest's.
		const spec::NoNotificationSink no_sink;
		// The select turn edits the focused column and never moves focus (`HorizontalMenu::selectEncoderAction`,
		// `origin/main:src/deluge/gui/menu_item/horizontal_menu.cpp`); KIT focuses column 1 directly (`buttonAction`'s
		// `select_map`); the gold knob does not touch the menu at all, falling through to the real (unseeded)
		// `ModKnobs`; and the select press on FREQ (a `PatchedParam` with a patch-chain) follows the chain rather than
		// raising a notification and staying.
		reset_globals();
		spec::SongFixture song = spec::make_synth_song();
		const MenuContext ctx = spec::ctx_of(*song.song, deluge::gui::menuViewOf(song.clip), 0, false, false, false, false);
		(void)menu::assign_node_ids(nodes::kAllRoots);
		menu_scene().open(nodes::lpf, ctx);

		const int32_t before_mode = bindings::read(nodes::lpf_mode, ctx);
		const int32_t before_freq = bindings::read(nodes::lpf_freq, ctx);

		uitest::RunOptions opts{
		    .tick_ms = 10,
		    .board = &deluge::ui::shell::kDelugeBoard,
		    .profile = &deluge::ui::scenes::deluge_profile(),
		    .initial_scene = deluge::ui::shell::SceneId{deluge::ui::shell::SceneKind::FirstApp, menu::node_id(nodes::lpf)},
		};
		// The real three, UNSEEDED -- `mod_target` deliberately left null. See this file's
		// `@par Every script here installs the real three, unseeded`.
		spec::RealServices svc;
		svc.song = song.song;
		opts.after_construct = svc.installer();
		const uitest::RunResult result = uitest::expect_script_goldens(
		    self, "menu_submenu_horizontal", ui::scenes::scene_registry(), svc.slots(), opts);
		expect(result.snapshots).to_equal(uint32_t{5});

		// The select press on FREQ followed its patch-chain into the source-selection page, so the
		// path is one level deeper than the page itself; `after_back` returns to it with FREQ still
		// the focused column.
		expect(menu_scene().focused()).to_equal(&nodes::lpf);
		expect(static_cast<uint32_t>(menu_scene().focused()->name)).to_equal(static_cast<uint32_t>(sid(String::STRING_FOR_LPF)));
		expect(static_cast<int>(menu_scene().state().columns[0].focused)).to_equal(1); // FREQ

		// The select turn edited MODE by one -- clamped, matching `Enumeration::selectEncoderAction`'s
		// non-wrapping clamp -- and never touched FREQ.
		const int32_t last_mode_index = bindings::range(nodes::lpf_mode, ctx).max;
		expect(bindings::read(nodes::lpf_mode, ctx)).to_equal(std::min(before_mode + 1, last_mode_index));
		// GoldUpper reached the real, unseeded `ModKnobs` instead of the menu -- no target to write
		// through, so FREQ never moved.
		expect(bindings::read(nodes::lpf_freq, ctx)).to_equal(before_freq);
	});

	it("Shortcut: a real shift+pad jump lands on the LPF page focused on nodes::lpf_freq, then the real Shell edits it", _ {
		reset_globals();
		spec::SongFixture song = spec::make_synth_song();
		const MenuContext ctx = spec::ctx_of(*song.song, deluge::gui::menuViewOf(song.clip), 0, false, false, false, false);
		(void)menu::assign_node_ids(nodes::kAllRoots);
		menu_scene().open(nodes::sound_root, ctx);

		const int32_t before = bindings::read(nodes::lpf_freq, ctx);

		uitest::RunOptions opts{
		    .tick_ms = 10,
		    .board = &deluge::ui::shell::kDelugeBoard,
		    .profile = &deluge::ui::scenes::deluge_profile(),
		    .initial_scene = deluge::ui::shell::SceneId{deluge::ui::shell::SceneKind::FirstApp,
		                                               menu::node_id(nodes::sound_root)},
		};
		// The real three, UNSEEDED -- `mod_target` deliberately left null. See this file's
		// `@par Every script here installs the real three, unseeded`.
		spec::RealServices svc;
		svc.song = song.song;
		opts.after_construct = svc.installer();
		const uitest::RunResult result = uitest::expect_script_goldens(
		    self, "menu_shortcut", ui::scenes::scene_registry(), svc.slots(), opts);
		// Five: `lpf_freq` is a `PatchedParam` node too, so the script gained a second GoldUpper turn
		// after the Back -- and the first one's snapshot would be empty, same as `menu_decimal` above. The
		// `after_select_turn` snapshot is not recorded either (byte-identical to `after_shortcut`, for
		// the reason `render_leaf_decimal` claims-and-ignores the select encoder) -- see the script
		// file's own notes for both.
		expect(result.snapshots).to_equal(uint32_t{5});

		// The real pad (8, 7) of `paramShortcutsForSounds` -- `&lpfFreqMenu` -- proven in `menu_scene_spec.cpp`'s own
		// shortcut tests. `setup()` opens it on the LPF page, focused on it (`SoundEditor::maybeGetParentMenu`
		// (`origin/main:src/deluge/gui/ui/sound_editor.cpp`), `parent->focusChild`).
		expect(menu_scene().focused()).to_equal(&nodes::filters_group);
		expect(menu_scene().focused_value()).to_equal(&nodes::lpf_freq);
		expect(static_cast<uint32_t>(menu_scene().focused_value()->name))
		    .to_equal(static_cast<uint32_t>(sid(String::STRING_FOR_FREQUENCY)));
		// `int_value` edits on the select encoder (`ui/widgets/oled/values.cpp`'s `select_delta`), so the
		// script's `200 encoder Select +1` step -- once claimed-and-ignored here too -- is what moves
		// the value first, by 1 select detent * the node's own step (1). The `GoldUpper` turn that
		// follows still lands on the source picker the select press opens and still does nothing
		// there. A closing `GoldUpper` turn (back
		// on this leaf) would test nothing either, since `int_value` claims no gold input at
		// all -- so that step is `encoder Select +2`, landing at `1 + 2 = 3`.
		expect(bindings::read(nodes::lpf_freq, ctx)).to_equal(std::min(before + 3, 50));
	});

	it("Range permission: entering the real source0_transpose opens the real range picker first", _ {
		reset_globals();
		spec::SongFixture song = spec::make_synth_song();
		auto* sound = static_cast<SoundInstrument*>(song.instrument);
		// `permission_for`'s `MustSelectRange` arm (`context.cpp`): more than one `MultiRange` on the
		// source and none selected yet -- the exact setup `menu_scene_spec.cpp`'s own range-permission
		// test uses, here against the REAL `nodes::source0_transpose` instead of a synthetic param.
		sound->sourceResources[0].ranges.insertMultiRange(0);
		sound->sourceResources[0].ranges.insertMultiRange(1);
		deluge::edits::recordRanges(*sound, 0); // recorded, as a gesture records them
		const MenuContext ctx = spec::ctx_of(*song.song, deluge::gui::menuViewOf(song.clip), 0, false, false, false, false);
		(void)menu::assign_node_ids(kRangePermRoots);
		menu_scene().open(range_perm_root, ctx);

		const int32_t before = bindings::read(nodes::source0_transpose, ctx);

		uitest::RunOptions opts{
		    .tick_ms = 10,
		    .board = &deluge::ui::shell::kDelugeBoard,
		    .profile = &deluge::ui::scenes::deluge_profile(),
		    .initial_scene = deluge::ui::shell::SceneId{deluge::ui::shell::SceneKind::FirstApp,
		                                               menu::node_id(range_perm_root)},
		};
		// The real three, UNSEEDED -- `mod_target` deliberately left null. See this file's
		// `@par Every script here installs the real three, unseeded`.
		spec::RealServices svc;
		svc.song = song.song;
		opts.after_construct = svc.installer();
		const uitest::RunResult result = uitest::expect_script_goldens(
		    self, "menu_range_permission", ui::scenes::scene_registry(), svc.slots(), opts);
		expect(result.snapshots).to_equal(uint32_t{4});

		// The script ends with a Back, so the wrapper is on screen again -- but `after_select_press`
		// is the REAL range picker (its two rows, named by `MultiRange::getText`), not the
		// refused-entry no-op. (No GoldUpper snapshot follows it: the picker reads no edit encoder, so
		// it would be byte-identical.)
		expect(menu_scene().focused()).to_equal(&range_perm_root);
		expect(static_cast<uint32_t>(menu_scene().focused()->name)).to_equal(static_cast<uint32_t>(sid(String::STRING_FOR_TRANSPOSE)));
		expect(cursor0()).to_equal(1); // the Select detent landed the cursor on row 1
		// The substitution happened and is still recorded: `enter_child` set both, and `ascend` has no
		// business dropping them -- the item is still the one the picker was standing in for.
		expect(menu_scene().state().walk.range_picker_pending).to_be_true();
		expect(menu_scene().state().walk.pending_after_range).to_equal(&nodes::source0_transpose);
		// And the picker really ran `MultiRange::beginSession` on this source: nothing was remembered,
		// so it took `numRanges >> 1` (`origin/main:src/deluge/gui/menu_item/multi_range.cpp`
		// `MultiRange::beginSession`), which the picker writes back to `SourceResources::defaultRangeI`.
		expect(static_cast<SoundInstrument*>(song.instrument)->sourceResources[0].defaultRangeI).to_equal(1);
		// The leaf itself was never opened, so its model value is untouched.
		expect(bindings::read(nodes::source0_transpose, ctx)).to_equal(before);
	});

	// -----------------------------------------------------------------------------------------
	// One block per row of this file's own table.

	it("SyncLevel: nodes::delay_sync draws legacy's note-length string, and only the SELECT encoder moves it", _ {
		reset_globals();
		spec::SongFixture song = spec::make_synth_song();
		const MenuContext ctx = spec::ctx_of(*song.song, deluge::gui::menuViewOf(song.clip), 0, false, false, false, false);
		(void)menu::assign_node_ids(nodes::kAllRoots);
		menu_scene().open(nodes::delay_sync, ctx);
		// Value 0 is main's literal "OFF" arm (`origin/main:src/deluge/gui/menu_item/sync_level.cpp`
		// `SyncLevel::drawPixelsForOled`), which is what `idle` records.
		bindings::write(nodes::delay_sync, ctx, 0);

		const deluge::ui::scenes::menu::IntRange sync_range = bindings::range(nodes::delay_sync, ctx);

		uitest::RunOptions opts = opts_for(nodes::delay_sync);
		// The real three, UNSEEDED -- `mod_target` deliberately left null. See this file's
		// `@par Every script here installs the real three, unseeded`.
		spec::RealServices svc;
		svc.song = song.song;
		opts.after_construct = svc.installer();
		const uitest::RunResult result = uitest::expect_script_goldens(
		    self, "menu_sync_level", ui::scenes::scene_registry(), svc.slots(), opts);
		expect(result.snapshots).to_equal(uint32_t{3});

		// `SyncLevel` (`origin/main:src/deluge/gui/menu_item/sync_level.h`) derives from `Enumeration`, not `Integer`,
		// and `Enumeration::selectEncoderAction` (`origin/main:src/deluge/gui/menu_item/enumeration.cpp`) clamps rather
		// than wraps. So the +40 detents stop at the top of the node's own range instead of coming back round to OFF.
		//
		// **And this is what backs the missing gold-turn snapshot.** The script's last
		// step is `GoldUpper -4`, after the clamp: `render_leaf_sync_level` never calls `f.edit()`, so
		// the value must still be sitting on `range.max`. A gold knob that was live would have dragged
		// it 8 steps down off the clamp and failed here. (The gold step comes after the clamp on purpose: placed between
		// the two select turns, the following clamp would make this assertion true whatever the gold
		// turn had done.)
		expect(bindings::read(nodes::delay_sync, ctx)).to_equal(sync_range.max);
		expect(sync_range.max).to_be_greater_than(int32_t{4}); // the -4 really could have shown, had it acted
		expect(menu_scene().focused()).to_equal(&nodes::delay_sync);
	});

	it("select_chain: a select press on nodes::unison_count opens nodes::unison_spread, and edits it", _ {
		reset_globals();
		spec::SongFixture song = spec::make_synth_song();
		const MenuContext ctx = spec::ctx_of(*song.song, deluge::gui::menuViewOf(song.clip), 0, false, false, false, false);
		(void)menu::assign_node_ids(nodes::kAllRoots);
		menu_scene().open(nodes::unison_count, ctx);

		const int32_t count_before = bindings::read(nodes::unison_count, ctx);
		const int32_t spread_before = bindings::read(nodes::unison_spread, ctx);
		const deluge::ui::scenes::menu::IntRange count_range = bindings::range(nodes::unison_count, ctx);
		const deluge::ui::scenes::menu::IntRange spread_range = bindings::range(nodes::unison_spread, ctx);

		uitest::RunOptions opts = opts_for(nodes::unison_count);
		// The real three, UNSEEDED -- `mod_target` deliberately left null. See this file's
		// `@par Every script here installs the real three, unseeded`.
		spec::RealServices svc;
		svc.song = song.song;
		opts.after_construct = svc.installer();
		const uitest::RunResult result = uitest::expect_script_goldens(
		    self, "menu_chain", ui::scenes::scene_registry(), svc.slots(), opts);
		expect(result.snapshots).to_equal(uint32_t{6});

		// `CountToStereoSpread::selectButtonPress` (`origin/main:src/deluge/gui/menu_item/unison/count.h`) navigates
		// *into* the spread, so the press must not have ascended -- and the second select turn, after
		// the Back, must have landed on the count again.
		expect(menu_scene().focused()).to_equal(&nodes::unison_count);
		// `int_value` edits on the select encoder, not the gold knobs (`ui/widgets/oled/values.cpp`'s
		// `select_delta`); a `GoldUpper` turn here would test nothing, since `int_value` claims no
		// gold input. The script's three turns (on the count, on the spread the select press opens, on
		// the count again after the Back) are `encoder Select` turns, sized at `+/-4` (a select
		// detent is not doubled by `edges_per_detent` the way an `Accelerating` gold knob's magnitude
		// is) -- so the select turns land the count at the clamp.
		const int32_t after_first = std::min(count_before + 4, count_range.max);
		expect(after_first).to_equal(count_range.max); // the clamp really did happen, so -4 is visible
		expect(bindings::read(nodes::unison_count, ctx)).to_equal(std::max(after_first - 4, count_range.min));
		expect(bindings::read(nodes::unison_spread, ctx)).to_equal(std::min(spread_before + 4, spread_range.max));
	});

	it("Range: nodes::default_tempo picks an edge with the horizontal encoder; its gold turns move neither end", _ {
		reset_globals();
		spec::SongFixture song = spec::make_synth_song();
		const MenuContext ctx = spec::ctx_of(*song.song, deluge::gui::menuViewOf(nullptr), 0, false, false, false, false);
		(void)menu::assign_node_ids(nodes::kAllRoots);
		// Process-wide flash-backed state (the `defaults/tempo/*` DEVICE rows), so it is
		// saved and put back. Set BEFORE `open()`, which is what seeds `MenuState::edit_int` /
		// `edit_upper` from the model (`ValueSession::enter_leaf`'s `Kind::Range` arm). A split pair well
		// inside the rows' 60..240, so the two ends are visibly distinct in `idle`,
		// neither detent below clamps, and `focus_left_when_split` has a split to act on.
		const SavedRange saved = SavedRange::capture(FlashStorage::DEFAULT_RANGE_TEMPO);
		FlashStorage::defaultRanges[FlashStorage::DEFAULT_RANGE_TEMPO].lower = 100;
		FlashStorage::defaultRanges[FlashStorage::DEFAULT_RANGE_TEMPO].upper = 200;
		menu_scene().open(nodes::default_tempo, ctx);

		uitest::RunOptions opts = opts_for(nodes::default_tempo);
		// The real three, UNSEEDED -- `mod_target` deliberately left null. See this file's
		// `@par Every script here installs the real three, unseeded`.
		spec::RealServices svc;
		svc.song = song.song;
		opts.after_construct = svc.installer();
		const uitest::RunResult result = uitest::expect_script_goldens(
		    self, "menu_range", ui::scenes::scene_registry(), svc.slots(), opts);
		expect(result.snapshots).to_equal(uint32_t{5});

		// `range_value` edits the selected edge on the select encoder, not the gold knobs
		// (`ui/widgets/oled/values.cpp`'s `select_delta`, and its doc's note on the two ported
		// `selectEncoderAction`s). This script only ever turns `GoldUpper`, so neither edge moves:
		// `range_value` claims none of those turns, and the unseeded `ModKnobs` behind them (`@par
		// Every script here installs the real three, unseeded`, above) has no target to write
		// through. The horizontal encoder still walks the edge -- that half of
		// this script's own name holds -- it is only the edit half that goes quiet.
		expect(FlashStorage::defaultRanges[FlashStorage::DEFAULT_RANGE_TEMPO].lower).to_equal(int32_t{100});
		expect(FlashStorage::defaultRanges[FlashStorage::DEFAULT_RANGE_TEMPO].upper).to_equal(int32_t{200});
		saved.restore();
	});

	it("ActiveScales: a select press toggles the row's box and stays on the page", _ {
		reset_globals();
		spec::SongFixture song = spec::make_synth_song();
		const MenuContext ctx = spec::ctx_of(*song.song, deluge::gui::menuViewOf(nullptr), 0, false, /*session_or_arranger_root=*/true,
		                                     /*session_view_root=*/true, false);
		(void)menu::assign_node_ids(nodes::kAllRoots);
		// Entered from the root, not opened directly: at depth 0 an unwanted `ascend()` is a silent
		// no-op, so "the item consumed the select press" would be indistinguishable from "it did not".
		menu_scene().open(nodes::root_song, ctx);

		expect(nodes::kSongActiveScalesItem.is_disabled(ctx, 2)).to_be_false();

		const uitest::RunResult result = uitest::expect_script_goldens(
		    self, "menu_scales", ui::scenes::scene_registry(), deluge::ui::shell::Services{}, opts_for(nodes::root_song));
		expect(result.snapshots).to_equal(uint32_t{7});

		// Two presses on row 2: disabled, then enabled again. The *pair* is what makes the goldens
		// mean something -- one press alone could not tell a flip from a one-way write.
		expect(nodes::kSongActiveScalesItem.is_disabled(ctx, 2)).to_be_false();
		// Nothing else moved: `commit` writes only the cursor's own bit.
		expect(nodes::kSongActiveScalesItem.is_disabled(ctx, 0)).to_be_false();
		expect(nodes::kSongActiveScalesItem.is_disabled(ctx, 3)).to_be_false();
		// `selectButtonPress` returns NO_NAVIGATION (`origin/main:src/deluge/gui/menu_item/active_scales.cpp`), so
		// the two presses must not have ascended; the Back at the end is what leaves, and it leaves to
		// the root with its own cursor still on the row the descent came from.
		expect(menu_scene().focused()).to_equal(&nodes::root_song);
		expect(cursor_now()).to_equal(4);
	});

	it("ContextMenuItem: a reset item pushes the context menu, and the menu goes up a level when it closes", _ {
		reset_globals();
		spec::SongFixture song = spec::make_synth_song();
		const MenuContext ctx = spec::ctx_of(*song.song, deluge::gui::menuViewOf(nullptr), 0, false, false, false, false);
		(void)menu::assign_node_ids(nodes::kAllRoots);
		menu_scene().open(nodes::reset_settings, ctx);

		const uitest::RunResult result = uitest::expect_script_goldens(
		    self, "menu_reset", ui::scenes::scene_registry(), deluge::ui::shell::Services{}, opts_for(nodes::reset_settings));
		expect(result.snapshots).to_equal(uint32_t{3});

		// Row 0 of `kResetSettingsChildren` is `reset_flash`, so that is the descriptor the push
		// named. The scene keeps `id_` after it closes, which is what lets this be checked at all.
		expect(context_menu::context_menu_scene().descriptor_id()).to_equal(context_menu::DescriptorId::ResetFlash);
		// The Select detent inside the window moved nothing: `ResetSettings::getOptions()` is a single
		// `STRING_FOR_OK` (not a NO/YES pair), and `step()` clamps.
		expect(static_cast<int>(context_menu::context_menu_scene().selection().option)).to_equal(0);
		// `MenuWalkState::go_up_on_resume` is the scenes menu's own ascend: main's
		// `reset_settings::Reset::selectButtonPress` (`origin/main:src/deluge/gui/menu_item/reset_settings/reset.h`)
		// never descends into the item and sets no `shouldGoUpOneLevelOnBegin`. The resume ascends OUT of the item the
		// press descended into, so the list is on screen again rather than the item that opened the window.
		// (`reset_settings` is this run's path root, so that ascend lands back on it -- one level up from
		// `reset_flash`.)
		expect(menu_scene().focused()).to_equal(&nodes::reset_settings);
	});

	it("ContextMenuScene: the audio-source descriptor opens on its own initial row and walks", _ {
		reset_globals();
		spec::AudioSongFixture song = spec::make_audio_clip_song();
		const MenuContext ctx = spec::ctx_of(*song.song, deluge::gui::menuViewOf(song.clip), 0, false, false, false, false);
		(void)menu::assign_node_ids(nodes::kAllRoots);
		menu_scene().open(nodes::root_audio_clip, ctx);

		const uitest::RunResult result =
		    uitest::expect_script_goldens(self, "context_menu", ui::scenes::scene_registry(),
		                                  deluge::ui::shell::Services{}, opts_for(nodes::root_audio_clip));
		expect(result.snapshots).to_equal(uint32_t{5});

		expect(context_menu::context_menu_scene().descriptor_id()).to_equal(context_menu::DescriptorId::AudioSource);
		// `initial_audio_source` (`ui/scenes/context_menu/context_menu_scene.cpp`, as
		// `AudioInputSelector::setupAndCheckAvailability` in `gui/context_menu/audio_input_selector.cpp` does) opened
		// it on the output's own `inputChannel`, and the two Select detents walked from there -- `step()`'s clamp is at
		// 7, which this does not reach.
		expect(static_cast<int>(context_menu::context_menu_scene().selection().option)).to_equal(3);
		// Backed out without accepting, so the menu is back where the press left it.
		expect(menu_scene().focused()).to_equal(&nodes::root_audio_clip);
		expect(cursor_now()).to_equal(1);
	});

	it("RenameScene: the menu pushes it, the cursor blinks, a held backspace repeats, and enter commits", _ {
		reset_globals();
		spec::SongFixture song = spec::make_synth_song();
		const MenuContext ctx = spec::ctx_of(*song.song, deluge::gui::menuViewOf(song.clip), 0, false, false, false, false);
		(void)menu::assign_node_ids(nodes::kAllRoots);
		menu_scene().open(nodes::edit_name, ctx);
		song.clip->name = "";

		const uitest::RunResult result = uitest::expect_script_goldens(
		    self, "rename", ui::scenes::scene_registry(), deluge::ui::shell::Services{}, opts_for(nodes::edit_name));
		expect(result.snapshots).to_equal(uint32_t{12});

		// Typed "ABAD", then a held backspace: the press itself deletes one
		// (`QwertyUI::padAction`), the first repeat fires 500 ms later (its `UI_SPECIFIC` timer) and the
		// second 80 ms after that, before the pad is released at 1540 ms.
		//
		// Three deletions, not two. One repeat proves the hold is armed; it says
		// nothing about `kBackspaceRepeatMs`, the 80 ms steady state -- and that interval's only
		// other cover is `rename_scene_spec.cpp`, whose hand-placed ticks all land on grid passes and
		// which is therefore blind to a repeat that fires once and stalls. A regression that fired once and
		// then stalled would have passed both. "A" is the discriminating answer at both ends: a
		// missing arming delay would have emptied the buffer, a missing cadence would have left "AB".
		expect(std::string{song.clip->name}).to_equal(std::string{"A"});
		// The enter pad committed and popped the editor, so the menu owns the screen again -- one level
		// up from the item that opened it, `EditNameItem` being a `pending_scene` opener like the five
		// reset items.
		expect(menu_scene().focused()).to_equal(&nodes::edit_name);
	});

	it("MidiCommandItem: the learned readout, the LEARN blink, and a select turn that unlearns", _ {
		reset_globals();
		spec::SongFixture song = spec::make_synth_song();
		const MenuContext ctx = spec::ctx_of(*song.song, deluge::gui::menuViewOf(nullptr), 0, false, false, false, false);
		(void)menu::assign_node_ids(nodes::kAllRoots);
		const CommandSlots saved = CommandSlots::capture();
		CommandSlots::clear_all();
		// Focus the item to learn onto it -- its own learn hook, as `MenuLearn::offer` reaches it -- and then
		// re-root on the list the script actually walks. The slot itself is
		// `midiEngine.globalMIDICommands`, a global, so the binding survives the re-rooting.
		menu_scene().open(nodes::midi_command_play, ctx);
		// Channel 3 + `IS_A_CC`, CC 42 -- the same binding `tables_3_spec`'s learned-readout golden
		// uses, so the three-line shape in `entered` is the one that file already pins at unit level.
		const deluge::ui::scenes::menu::Node* play = menu_scene().focused();
		expect(play).not_().to_be_null();
		expect(play == nullptr ? nullptr : play->custom).not_().to_be_null();
		if (play != nullptr && play->custom != nullptr) {
			expect(play->custom->learn_cc(ctx, deluge::midi::id_of(fake_cable()), 3, 42, 127)).to_be_true();
		}
		menu_scene().open(nodes::midi_commands, ctx);

		const uitest::RunResult result =
		    uitest::expect_script_goldens(self, "menu_midi_learn", ui::scenes::scene_registry(),
		                                  deluge::ui::shell::Services{}, opts_for(nodes::midi_commands));
		expect(result.snapshots).to_equal(uint32_t{4});

		// `Command::selectEncoderAction` (`origin/main:src/deluge/gui/menu_item/midi/command.cpp`) clears on ANY
		// motion. The script turns once each way; the second turn is the step with no snapshot, and this is what stands
		// in for it -- the slot is still empty, not re-learned by the opposite direction.
		expect(midiEngine.globalMIDICommands[static_cast<int32_t>(GlobalMIDICommand::PLAY)].containsSomething())
		    .to_be_false();
		// And it cleared the right one of the twelve.
		expect(midiEngine.globalMIDICommands[static_cast<int32_t>(GlobalMIDICommand::RECORD)].containsSomething())
		    .to_be_false();
		expect(menu_scene().focused()).to_equal(&nodes::midi_commands);
		saved.restore();
	});

	it("CvSelectionItem: the picked row becomes MenuContext::source_index before the chain reads it", _ {
		reset_globals();
		spec::SongFixture song = spec::make_synth_song();
		const MenuContext ctx = spec::ctx_of(*song.song, deluge::gui::menuViewOf(nullptr), 0, false, false, false, false);
		(void)menu::assign_node_ids(nodes::kAllRoots);
		menu_scene().open(nodes::cv_selection, ctx);

		const uitest::RunResult result =
		    uitest::expect_script_goldens(self, "menu_cv_selection", ui::scenes::scene_registry(),
		                                  deluge::ui::shell::Services{}, opts_for(nodes::cv_selection));
		expect(result.snapshots).to_equal(uint32_t{4});

		// The press navigated (`cv_selection_chain`), so it did not ascend; the Back brought the
		// selector back with its cursor intact, which is `MenuState::list[depth]` surviving a descend.
		expect(menu_scene().focused()).to_equal(&nodes::cv_selection);
		expect(cursor_now()).to_equal(1);
	});

	it("PatchCablesItem: the mod matrix opens the selected cable's strength page, and that page edits", _ {
		reset_globals();
		spec::SongFixture song = spec::make_synth_song();
		const MenuContext ctx = spec::ctx_of(*song.song, deluge::gui::menuViewOf(song.clip), 0, false, false, false, false);
		(void)menu::assign_node_ids(nodes::kAllRoots);

		// A synth fixture already has cables of its own (the golden's NOTE and VELO rows), so the
		// interesting assertion is not "a cable moved" but "the SELECTED one moved and the others did
		// not". One more is added here so the list is a real list rather than a pair -- the same cable
		// `tables_2_spec`'s own mod-matrix goldens install.
		Song* const previous_song = currentSong;
		currentSong = song.song;
		deluge::synth::CableTarget plain = deluge::synth::CableTarget::depth_of(deluge::modulation::params::LOCAL_LPF_FREQ, PatchSource::ENVELOPE_1);
		expect(seed_cable(ctx, plain, 1234)).to_be_true();
		PatchCableSet* const set = spec::menu_engine(ctx, *song.song).params->getPatchCableSetAllowJibberish();
		expect(set).not_().to_be_null();
		deluge::synth::CableTarget destination = deluge::synth::CableTarget::param(deluge::modulation::params::LOCAL_LPF_FREQ);
		const uint8_t added = set->getPatchCableIndex(PatchSource::ENVELOPE_1, destination);
		expect(added).not_().to_equal(kNoSelection);
		expect(added).not_().to_equal(uint8_t{0}); // not the row the cursor opens on
		const int32_t added_before = set->patchCables[added].param.getCurrentValue();
		// `PatchCablesItem` builds one row per `patchCables` entry in array order, so the cursor's
		// own row -- the one a select press with no turn before it opens -- is entry 0.
		const int32_t selected_before = set->patchCables[0].param.getCurrentValue();

		menu_scene().open(nodes::patch_cables, ctx);
		uitest::RunOptions opts = opts_for(nodes::patch_cables);
		// The real three, UNSEEDED -- `mod_target` deliberately left null. See this file's
		// `@par Every script here installs the real three, unseeded`.
		spec::RealServices svc;
		svc.song = song.song;
		opts.after_construct = svc.installer();
		const uitest::RunResult result =
		    uitest::expect_script_goldens(self, "menu_patch_cables", ui::scenes::scene_registry(),
		                                  svc.slots(), opts);
		expect(result.snapshots).to_equal(uint32_t{5});

		// The script's two saturating `Horizontal` steps left the digit cursor on the hundreds place
		// whatever an earlier block's page had left in `MenuState::decimal_pos`, so "one gold detent
		// is 1.00" is pinned rather than inherited -- and so is the arithmetic behind the two frames
		// the font choice is judged on.
		expect(static_cast<int>(menu_scene().state().decimal_pos)).to_equal(2);
		// `decimal_value` edits on the select encoder, not the gold knobs (`ui/widgets/oled/values.cpp`'s
		// `select_delta`); a `GoldUpper` turn on the strength page would test nothing, since
		// `decimal_value` claims no gold input. The script's two turns are `encoder Select` turns
		// sized `+4` and `-64` -- a select detent is not doubled by `edges_per_detent` the way an
		// `Accelerating` gold knob's magnitude is, so the cable the press selected moves.
		expect(set->patchCables[0].param.getCurrentValue()).not_().to_equal(selected_before);
		// The other cable never had a turn aimed at it either way, which is still worth pinning: a
		// port that somehow wrote through the wrong `patch_cables_chain` entry would show up here.
		expect(set->patchCables[added].param.getCurrentValue()).to_equal(added_before);
		expect(menu_scene().focused()).to_equal(&nodes::patch_cables);
		currentSong = previous_song;
	});

	it("ActionItem: the press files a MenuAction and leaves the parent list exactly where it was", _ {
		reset_globals();
		spec::SongFixture song = spec::make_synth_song();
		const MenuContext ctx = spec::ctx_of(*song.song, deluge::gui::menuViewOf(nullptr), 0, false, /*session_or_arranger_root=*/true,
		                                     /*session_view_root=*/true, false);
		(void)menu::assign_node_ids(nodes::kAllRoots);
		menu_scene().open(nodes::stem_export, ctx);
		(void)menu_scene().take_action(); // drain anything an earlier block left in the one-slot outbox

		const uitest::RunResult result = uitest::expect_script_goldens(
		    self, "menu_action", ui::scenes::scene_registry(), deluge::ui::shell::Services{}, opts_for(nodes::stem_export));
		expect(result.snapshots).to_equal(uint32_t{2});

		// The press has no golden -- the frame after it repeats `idle` exactly, which is the whole point of
		// `shouldEnterSubmenu() == false` (the select arm of `SoundEditor::buttonAction` in
		// `origin/main:src/deluge/gui/ui/sound_editor.cpp`). This is where it is visible instead.
		const MenuActionRequest taken = menu_scene().take_action();
		expect(taken.action).to_equal(MenuAction::StartStemExport);
		expect(static_cast<int>(taken.arg)).to_equal(0);
		// And the ascend was exactly one level: the EXPORT AUDIO list is focused again, not something
		// above it. (This run's path is rooted on that list, so a second ascend would have been a
		// visible no-op rather than a wrong page -- which is why the *cursor* is checked too.)
		expect(menu_scene().focused()).to_equal(&nodes::stem_export);
		expect(cursor_now()).to_equal(1);
	});

	it("PerformEditorItem: VALUE files a request and stays, DISABLED files nothing and ascends", _ {
		reset_globals();
		spec::SongFixture song = spec::make_synth_song();
		const MenuContext ctx =
		    spec::ctx_of(*song.song, deluge::gui::menuViewOf(nullptr), 0, /*performance_view=*/true, /*session_or_arranger_root=*/true, false, false);
		(void)menu::assign_node_ids(nodes::kAllRoots);
		// `on_focus` seeds the cursor from the performance view's mode (`UiFacts::performance_editing_mode`), so
		// the view's two bools are put at DISABLED, and put back afterwards like any other global.
		const bool saved_default = performanceView.defaultEditingMode;
		const bool saved_param = performanceView.editingParam;
		performanceView.defaultEditingMode = false;
		performanceView.editingParam = false;
		// From the root, for `menu_scales`' reason: the DISABLED arm's whole claim is that it ascends,
		// and at depth 0 an ascend draws the same frame as staying put.
		menu_scene().open(nodes::root_performance, ctx);
		(void)menu_scene().take_action();

		const uitest::RunResult result =
		    uitest::expect_script_goldens(self, "menu_perform_editor", ui::scenes::scene_registry(),
		                                  deluge::ui::shell::Services{}, opts_for(nodes::root_performance));
		expect(result.snapshots).to_equal(uint32_t{5});

		// The VALUE press filed `OpenPerformanceView` carrying the chosen mode as its `arg`; the DISABLED press that
		// follows files nothing (`EditingMode::selectButtonPress` in
		// `origin/main:src/deluge/gui/menu_item/performance_session_view/editing_mode.h` returns nullptr), so this
		// one-slot outbox still holds the first one.
		const MenuActionRequest taken = menu_scene().take_action();
		expect(taken.action).to_equal(MenuAction::OpenPerformanceView);
		expect(static_cast<int>(taken.arg)).to_equal(static_cast<int>(PerformanceEditingMode::VALUE));
		// The DISABLED arm ascended, which is the half a golden alone could not distinguish from a
		// page that merely redrew.
		expect(menu_scene().focused()).to_equal(&nodes::root_performance);
		performanceView.defaultEditingMode = saved_default;
		performanceView.editingParam = saved_param;
	});

	// -----------------------------------------------------------------------------------------
	// The per-device MIDI page and the three scene scripts; see the block above `ScriptDisplay`
	// for why three of the four are entered through the shortcut-pad jump.

	it("the per-device MIDI page: the cable, the direction and the zone all survive to the write", _ {
		reset_globals();
		spec::SongFixture song = spec::make_synth_song();
		const MenuContext ctx = spec::ctx_of(*song.song, deluge::gui::menuViewOf(nullptr), 0, false, false, false, false);
		(void)menu::assign_node_ids(nodes::kAllRoots);

		const SavedCables saved = SavedCables::capture();
		// The host's device list (`MidiDevices::connected`) walks DIN, the two upstream cables and the hosted ones, and
		// keeps every cable whose `connectionFlags` is non-zero. With no hosted devices the walk is the three built-in
		// cables, so lighting two of them is a two-row list -- and two is the number that matters: a page that read a
		// *global* cable rather than `MenuContext::midi_device` would pass a one-row list.
		MIDICable& din = MIDIDeviceManager::dinMIDIPorts;
		MIDICable& usb = MIDIDeviceManager::upstreamUSBMIDICable1;
		din.connectionFlags = 1;
		usb.connectionFlags = 1;
		MIDIDeviceManager::upstreamUSBMIDICable2.connectionFlags = 0;
		for (MIDICable* c : {&din, &usb}) {
			for (int32_t p = 0; p < 2; p++) {
				c->ports[p].mpeLowerZoneLastMemberChannel = 0;
				c->ports[p].mpeUpperZoneLastMemberChannel = 15; // 15 means "off" (`MIDIPort::mpeUpperZoneLastMemberChannel` in `io/midi/midi_device.h`)
			}
		}
		menu_scene().open(nodes::midi_devices, ctx);

		uitest::RunOptions opts = opts_for(nodes::midi_devices);
		// The real three, UNSEEDED -- `mod_target` deliberately left null. See this file's
		// `@par Every script here installs the real three, unseeded`.
		spec::RealServices svc;
		svc.song = song.song;
		opts.after_construct = svc.installer();
		const uitest::RunResult result =
		    uitest::expect_script_goldens(self, "menu_midi_device", ui::scenes::scene_registry(),
		                                  svc.slots(), opts);
		expect(result.snapshots).to_equal(uint32_t{10});

		// The script picked the SECOND row, the OUT direction and the UPPER zone -- the non-default arm of all three --
		// and then turned the select encoder twice on the member-channel count. `int_value` edits on the select
		// encoder, not the gold knobs (`ui/widgets/oled/values.cpp`'s `select_delta`); a `GoldUpper` turn would test nothing,
		// since `int_value` claims no gold input. The script's two turns are `encoder Select +2`, landing the count at
		// `0 -> 4`. `commit` writes `15 - 4` into the UPPER zone's field (`ZoneNumMemberChannels::writeCurrentValue` in
		// `origin/main:src/deluge/gui/menu_item/mpe/zone_num_member_channels.cpp`).
		expect(static_cast<int>(usb.ports[1].mpeUpperZoneLastMemberChannel)).to_equal(11);
		// And to that one field only. Every other one of the eight is still at its fixture value, so
		// a hook that reached for the wrong port, the wrong zone or the wrong cable fails here rather
		// than round-tripping through itself.
		expect(static_cast<int>(usb.ports[0].mpeUpperZoneLastMemberChannel)).to_equal(15);
		expect(static_cast<int>(usb.ports[0].mpeLowerZoneLastMemberChannel)).to_equal(0);
		expect(static_cast<int>(usb.ports[1].mpeLowerZoneLastMemberChannel)).to_equal(0);
		expect(static_cast<int>(din.ports[0].mpeUpperZoneLastMemberChannel)).to_equal(15);
		expect(static_cast<int>(din.ports[1].mpeUpperZoneLastMemberChannel)).to_equal(15);
		// The `MenuState` selections the four frames above were drawn from are still the ones the write used:
		// `mpe_direction` OUT and `mpe_zone` UPPER (`MIDI_DIRECTION_OUTPUT_FROM_DELUGE` and
		// `MPE_ZONE_UPPER_NUMBERED_FROM_0` in `io/midi/midi_device.h`).
		expect(static_cast<int>(menu_scene().state().walk.mpe_direction)).to_equal(1);
		expect(static_cast<int>(menu_scene().state().walk.mpe_zone)).to_equal(1);
		// The Back at the end left the zone selector, not the device list: the chain descended twice
		// and only one level came back.
		expect(menu_scene().focused()).to_equal(&nodes::mpe_zone);
		saved.restore();
	});

	it("BrowserScene: a shortcut jump opens the sample browser, which lists, descends and ascends", _ {
		reset_globals();
		install_display();
		ScratchCard card;
		spec::QuietAudio quiet;
		spec::SongFixture song = spec::make_synth_song();
		(void)install_sample_source(song, nullptr); // an empty holder -> the descriptor's default dir
		const MenuContext ctx = spec::ctx_of(*song.song, deluge::gui::menuViewOf(song.clip), 0, false, false, false, false);
		(void)menu::assign_node_ids(nodes::kAllRoots);
		menu_scene().open(nodes::sound_root, ctx);

		const uitest::RunResult result = uitest::expect_script_goldens(
		    self, "browser", ui::scenes::scene_registry(), deluge::ui::shell::Services{}, opts_for(nodes::sound_root));
		expect(result.snapshots).to_equal(uint32_t{7});

		// The script descended into KICKS, typed one character there, and came back out -- so the
		// scene really did run all three listings, and the last BACK (`Browser::backButtonAction`'s
		// top-of-tree arm) closed it. `MenuWalkState::go_up_on_resume` (`nodes/sample.h`,
		// legacy's `shouldGoUpOneLevelOnBegin`) then went up from the item the jump re-rooted onto, at
		// depth 0, which closed the menu too: `after_close.state` names no menu scene.
		expect(browser::browser_scene().browser_id()).to_equal(browser::BrowserId::Sample);
		// `on_exit` -> `FileCatalogue::emptyFileItems()` really ran. `browser_scene.h`'s own note
		// says an uncollected result holds the catalogue's claim and refuses every later listing --
		// this layer's or a legacy `Browser`'s -- for the life of the process, so this is the one
		// assertion the NEXT spec in this executable depends on.
		expect(browser::browser_scene().listing_in_flight()).to_be_false();
	});

	it("MarkerScene: a shortcut jump opens the marker editor, whose blink runs on quiet ticks", _ {
		reset_globals();
		install_display();
		spec::QuietAudio quiet;
		spec::SongFixture song = spec::make_synth_song();
		MultisampleRange* range = install_sample_source(song, make_sample());
		const MenuContext ctx = spec::ctx_of(*song.song, deluge::gui::menuViewOf(song.clip), 0, false, false, false, false);
		(void)menu::assign_node_ids(nodes::kAllRoots);
		menu_scene().open(nodes::sound_root, ctx);

		const uitest::RunResult result = uitest::expect_script_goldens(
		    self, "marker", ui::scenes::scene_registry(), deluge::ui::shell::Services{}, opts_for(nodes::sound_root));
		expect(result.snapshots).to_equal(uint32_t{7});

		// The pad press moved the START marker to column 6's first sample. At the opening zoom
		// (`WaveformView::open` with no remembered view: scroll 0, zoom = max) that is
		// `6 * xZoom` -- computed from the fixture rather than restated, so the assertion is about
		// the marker landing where the pad was and not about the arithmetic.
		const int64_t zoom = static_cast<int64_t>(((kSampleLength - 1) >> 4) + 1);
		expect(static_cast<int32_t>(range->sampleHolder.startPos)).to_equal(static_cast<int32_t>(6 * zoom));
		// The three frames after the blink pair were all drawn in the same blink phase, which is what
		// makes "the only difference is the marker" true of them. Pinned here rather than left to the
		// script's arithmetic: a change to `kBlinkPhaseMs` or to when the counter starts would move
		// those frames into another phase and silently turn three careful goldens into three
		// screenshots of a blink.
		expect(static_cast<int>(marker::marker_scene().blink_phase())).to_equal(0);
		// BACK closed the editor, and going up from the item the jump landed on at depth 0 closed the menu:
		// `after_back.state` names no menu scene.
		// The held-marker gesture did not survive `MarkerScene::on_exit`.
		expect(marker::marker_scene().holding_marker()).to_be_false();
	});

	it("RecorderScene: both doors arm a real recorder, and both close tidily", _ {
		reset_globals();
		install_display();
		spec::QuietAudio quiet;
		spec::SongFixture song = spec::make_synth_song();
		MultisampleRange* range = install_sample_source(song, nullptr);
		const MenuContext ctx = spec::ctx_of(*song.song, deluge::gui::menuViewOf(song.clip), 0, false, false, false, false);
		(void)menu::assign_node_ids(nodes::kAllRoots);
		menu_scene().open(nodes::sound_root, ctx);
		// `RecorderScene::can` refuses while a foreground recording is live, and this is the storage
		// nine subsystems read (`recorder_scene.h`). Asserted rather than cleared: if an earlier
		// block left one armed, that is the bug to find, not something to
		// paper over.
		expect(static_cast<int>(AudioEngine::foregroundRecordingSource())).to_equal(
		    static_cast<int>(AudioInputChannel::NONE));
		expect(recorder::recorder_scene().recording()).to_be_false();

		// Each tick runs an aborted recorder's card routine first, as the app loop's
		// `AudioEngine::doRecorderCardRoutines` does on the device: the scene closes once the take is
		// torn down (`SampleRecorder::settled`). Nothing captured, so no file exists and the teardown
		// is memory only.
		uitest::RunOptions opts = opts_for(nodes::sound_root);
		opts.before_tick = [](uint32_t) {
			SampleRecorder* rec = deluge::gui::shell_host::input_take();
			if (rec != nullptr && rec->status == RecorderStatus::ABORTED) {
				(void)rec->cardRoutine();
			}
		};
		const uitest::RunResult result = uitest::expect_script_goldens(
		    self, "recorder", ui::scenes::scene_registry(), deluge::ui::shell::Services{}, opts);
		expect(result.snapshots).to_equal(uint32_t{8});

		// Armed for real TWICE -- once through SOUND -> ACTIONS -> RECORD TO SAMPLE, once through the
		// shortcut pad -- and gone again both times. The second entry is itself an assertion: it
		// could only succeed because the first close ran `finish_recording()` in full, since
		// `on_enter` refuses on a leftover `recorder_` and `can()` refuses on a leftover published
		// recording. With nothing captured,
		// `endSyncedRecording` takes its `numSamplesCaptured == 0` abort arm, the card routine tears the
		// take down until it is settled, `after_render`'s completion arm sees that, and
		// `finish_recording()` unpublishes and discards -- so this pair says the whole close path ran,
		// not merely that nothing is left over.
		expect(recorder::recorder_scene().recording()).to_be_false();
		expect(recorder::recorder_scene().stopping()).to_be_false();
		// **This is the assertion that exposes `unit/ui/shell/support/script_runner.h`'s carried gap.**
		// `RecorderScene::on_enter` refuses on its own leaked `recorder_`
		// and `setup_recording` publishes a live foreground recording nine other subsystems read, so
		// a run that ended with the scene still on the stack would leave both behind for every later
		// spec in the process. `run_script` tears its stack down, which is what makes this hold
		// on the paths where the scene does NOT close itself.
		expect(static_cast<int>(AudioEngine::foregroundRecordingSource())).to_equal(
		    static_cast<int>(AudioInputChannel::NONE));
		// An aborted recording attaches nothing: `after_render`'s guard skips `attach_sample()` for
		// `RecorderStatus::ABORTED`, so the range's holder is still empty. A port that attached
		// unconditionally would have hung `recorder_->sample` -- a `Sample` with no captured frames
		// and a file that `abort()` deletes -- on the oscillator.
		expect(range->sampleHolder.audioFile).to_be_null();
		// The scene closed itself both times, and both doors opened it from the SAMPLE RECORDER page, so both
		// closes land on the source page with the file selector focused (`after_stop`, `after_shortcut_stop`).
	});
});
// clang-format on

DELUGE_SPEC(menu_scripts_spec, menu_scripts)
