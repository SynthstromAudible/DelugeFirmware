/// @file
/// @brief Level-2 acceptance scripts, driven through the real `Shell` over
///        `uitest::FakeSurfaceSet` (`tests/spec/shell/support/script_runner.h`) against the REAL
///        declarative tables (`src/scenes/menu/nodes/`) -- not a synthetic tree.
///
/// Three generations of script live here.
///
/// **E2 task 11's seven** run the same generic recipe -- a Select-encoder detent, a select-encoder
/// press/release, a GoldUpper detent, a Back press/release, snapshotting after every step -- against
/// a real node `RunOptions::initial_scene` opens directly (`SceneId{FirstApp, node_id(target)}`).
/// What that recipe visibly DOES depends entirely on the target's own `Kind`, which is the point:
/// one interpreter, seven different real screens.
///
/// **E2 task 12's twelve** cover what E2 itself added, and each one is shaped by its subject rather
/// than by a shared recipe, because the subjects are no longer all one kind of leaf:
///
// clang-format off
/// | script | subject | shipped by |
/// |---|---|---|
/// | `menu_sync_level` | `Kind::SyncLevel`'s renderer | task 1 |
/// | `menu_chain` | `Node::select_chain` on a value-leaf target | task 3 |
/// | `rename` | `nodes::EditNameItem` -> `rename::RenameScene` | tasks 4-5 |
/// | `menu_reset` | the five reset `nodes::ContextMenuItem`s, and `go_up_on_resume` | tasks 6-7 |
/// | `context_menu` | `context_menu::ContextMenuScene`'s multi-option descriptor | tasks 6, 11b |
/// | `menu_midi_learn` | `nodes::MidiCommandItem`, and the LEARN blink in `.state` | task 8 |
/// | `menu_patch_cables` | `nodes::PatchCablesItem` and the chain into a strength page | task 9 |
/// | `menu_cv_selection` | `nodes::CvSelectionItem`, a runtime list, and its `source_index` write | task 10 |
/// | `menu_action` | `nodes::ActionItem`: file a `MenuAction`, ascend straight back out | task 11b |
/// | `menu_perform_editor` | `nodes::PerformEditorItem`: the action item that is also a page | task 11b |
/// | `menu_range` | `Kind::Range`'s two-edge editor | task 11b |
/// | `menu_scales` | `nodes::ActiveScalesItem`, the preset-scale checklist | task 11b |
// clang-format on
///
/// Three of the twelve deliberately carry a script *step* with no snapshot after it. That is not an
/// omission: the frame would have been byte-identical to the one before it, and a golden whose whole
/// content repeats its predecessor can never fail for the right reason (E2 task 9, fix round 1 --
/// which deleted three such frames). Each of those steps is asserted in C++ instead, against the
/// state the pixels could not have shown anyway; the script files say which and why.
///
/// **Why one node id resolves correctly across every script.** `MenuScene::on_enter` re-targets the
/// path from `SceneId::arg` (`node_by_id`) but never touches `ctx_` (`menu_scene.cpp:359-418`), so
/// each `it` block calls `menu_scene().open(target, ctx)` itself first -- purely to install `ctx_`
/// for its own fixture on the process-wide singleton -- before handing `expect_script_goldens` a
/// `RunOptions::initial_scene` that re-targets the path a second time via the id. `assign_node_ids`
/// is called fresh in every block, over exactly the roots that block's own target is reachable from
/// (ruling: idempotent over one root set, but a later call over a DIFFERENT set is what makes ids
/// valid for that set -- see `node_id.h`), so blocks never depend on execution order.
///
/// **The range-permission script is the one target with no real host.** Every `range_dependent` node
/// in the committed tables (`nodes::kRangeDependentArray`, `nodes/nodes.h`) is a value column of a
/// `Layout::Horizontal` page (`source0`/`source1`), and a horizontal page's `columns` widget claims
/// `entered` only for a `Kind::Submenu` column (`ui/oled/columns.cpp`) -- so `enter_child` (the only
/// caller of `permission_for`) is never reached through the real committed tree today; that is an
/// E2 gap (Custom-column navigation), not something this task can wire around. To still exercise the
/// REAL `source0_transpose` node's permission gate, this file declares one small vertical wrapper
/// submenu naming it as a real child alongside `nodes::lpf_mode` -- the wrapper is synthetic, the
/// leaf it gates is not.
///
/// **E3 task 11's four** cover what E3 shipped -- and three of the four are the *sequel to that
/// last paragraph*, because the same Custom-column gap governs them:
///
// clang-format off
/// | script | subject | shipped by |
/// |---|---|---|
/// | `menu_midi_device` | the per-device MIDI page and its MPE chain (`nodes/midi_device.h`) | task 8 |
/// | `browser` | `browser::BrowserScene` over a real card: list, descend, search, ascend, close | tasks 2-3, 6 |
/// | `marker` | `marker::MarkerScene`: the pad waveform, the four-phase blink, the two ways to move a marker | tasks 4, 6 |
/// | `recorder` | `recorder::RecorderScene`: a real `SampleRecorder` armed, and the whole close path | tasks 5-6 |
// clang-format on
///
/// **How the three scene scripts get in, and why it is not a workaround.** Every one of the nine
/// `nodes/sample.h` items is declared on a `Layout::Horizontal` page (`source0`, `audio_clip_sample`,
/// `recorder`), so the paragraph above applies to them verbatim: no select press in the committed
/// tree descends into any of them. Unlike `source0_transpose`, though, these three DO have a real
/// door -- the shortcut-pad jump, whose table names all of them (`menu/shortcuts.cpp:100`, legacy's
/// own `paramShortcutsForSounds` cells). So these scripts use a real shift+pad press rather than a
/// synthetic wrapper submenu, and what they exercise is the path a user actually has.
///
/// Two consequences worth knowing before reading their goldens:
///
///   * the jump re-roots `MenuState::path` **onto the item itself** (`MenuScene::handle`'s
///     `Replace`), so the item is at depth 0 and `go_up_on_resume`'s ascend out of it is a no-op.
///     Each script's last frame is therefore the item's own bare title screen
///     (`SampleSceneItem::render`) rather than the page it was declared on -- see the task report;
///   * a gold-knob turn is **swallowed** unless another gold turn happened within
///     `LoweringConfig::short_press_ms` (500 ms) -- `InputLowering::action_gold_turn`'s wiggle
///     debounce. Every script written before this task turns a gold knob before t=500 and so is
///     inside that window from the shell's own zero; `menu_midi_device` edits at t=800 and has to
///     spend a detent arming it. Found by recording the frame and looking at it.
///
/// **And the second of those has a consequence for the whole directory, which no test covers.**
/// `last_turn_ms_` starts at 0 and `now_ms` is a script's own clock here, but on the device it is
/// uptime -- so on real hardware the debounce is *never* pre-armed and **the first gold turn after
/// boot, and after every 500 ms pause, is always swallowed**. Every gold-turn golden in this
/// directory therefore depicts the armed arm, which is the second detent of a gesture, and none of
/// them depicts the arm a user actually meets first. The goldens are not vacuous -- each records a
/// real edit and fails if that edit breaks -- but "one detent moves the value" is not what the
/// device does from cold, and nothing at any level asserts what it does instead. Recorded here
/// rather than papered over; closing it means a script whose clock starts past `short_press_ms`,
/// which `RunOptions` has no way to express today.
///
/// @par The twelve gold-turn scripts install the real three, UNSEEDED -- and what that does not settle
///
/// **Twelve of the 24 `expect_script_goldens` calls here install `services::ModKnobs`,
/// `GlobalControls` and `MidiLearn` through `support/real_services.h`, with `svc.song` set and
/// `svc.mod_target` deliberately LEFT NULL.** The other twelve still pass
/// `deluge::ui::shell::Services{}` -- three `NullService`s -- because they drive no gold turn and
/// the install would move their goldens for nothing.
///
/// Migration §10.2 blocker 9 asked for this, F1 task 11 first refused it, and the task-11 re-review
/// reversed that by doing the recording itself and showing what it costs: **57 `.state` files, 114
/// insertions, 0 deletions, 0 `.pbm` files, 0 value assertions**. Every inserted line is one of
/// exactly two:
///
///     indicator lower 0 0 0 0
///     indicator upper 0 0 0 0
///
/// -- `ModKnobs::leds` declaring both mod-knob rings on every tick, all four quadrants dark because
/// there is no target to read a level from. `led Mod1 on` does **not** appear; that comes from
/// seeding, and seeding is still owed. The reversal's argument is the one that matters: `Services{}`
/// is three nulls, which is *further* from the application than an unseeded `ModKnobs`, so this was
/// never a choice between a right fixture and a wrong one -- it is very wrong versus one seeded step
/// from right, and 114 lines of a single repeated pair is not the ambiguous two-cause diff E4 task
/// 13's rule exists to prevent.
///
/// @warning **Unseeded is exactly the configuration in which the swallowed-turn defect cannot
///          bite.** Do not read these goldens as settling anything about gold turns under real
///          services. What swallows a turn is `InputLowering::action_gold_turn`'s wiggle debounce
///          (below), and what arms it is `ModKnobs::pretend_knobs_untouched()` -- which
///          `RealServices::installer()` reaches **only when a `mod_target` is seeded**. With a null
///          target nothing backdates the knob clock, so every gold turn in these frames acts exactly
///          as it did under `Services{}`. These 57 frames record the *services being present*. They
///          record nothing about the gesture.
///
/// **Still owed, and it is now a named edit rather than a research task:** seed `svc.mod_target`,
/// arm the debounce in each script the way `tests/ui-scripts/shell/menu_gold_turn.txt` already does
/// (one leading detent inside `short_press_ms`), and re-record. Measured cost of that step, from the
/// task-11 scratch run with targets seeded: `led Mod1 on` on 57 frames plus **11 `.pbm` files moving
/// pixels** -- `menu_chain` (3), `menu_int` (1), `menu_patch_cables` (1), `menu_range` (4),
/// `menu_submenu_horizontal` (2) -- and two C++ value assertions changing, which is precisely the
/// diff the debounce arming is there to prevent.
///
/// @par Why a seeded install swallows the turn: a service reaching into the lowering behind the router
///
/// The prediction this file used to carry was that the split was decided by the page -- a turn the
/// page's widget CLAIMS is dropped by `Router::route_post_render` before `Router::run_chain`, so
/// installing the services would change nothing; an unclaimed turn would fall through and make
/// `ModKnobs` push a `ParamPopupScene`. **Neither half held.** No `ParamPopupScene` appears anywhere
/// (not one added or removed `scene` line, in either the seeded or the unseeded recording), and the
/// CLAIMED turns changed under seeding. The mechanism is three files away and the router is not in
/// it, which is why looking at the router found nothing:
///
///   1. The gold path runs `InputLowering::action_gold_turn` **unconditionally** -- the Continuous
///      arm of `InputLowering::apply_encoder_edges` (`shell/input_lowering.cpp:191`), which
///      `on_encoder_edges` forwards to (`:158-163`), gated on no service at all. Its
///      wiggle debounce is `(now_ms - last_turn_ms_[role]) < cfg_.short_press_ms` (`:214`, with
///      `short_press_ms` = 500, `input_lowering.h:26`), and a turn that fails it is recorded and
///      **swallowed** -- the test at `:216`, the direction recorded at `:223`, the refusal at
///      `:224`.
///   2. `ModKnobs::pretend_knobs_untouched()` (`scenes/services/mod_knobs.cpp:193-197`) writes that
///      same array from the other side, through `InputLowering::pretend_knob_untouched`
///      (`input_lowering.cpp:451-461`): `last_turn_ms_[role] = now_ms - kPretendUntouchedMs`, and
///      `kPretendUntouchedMs` is 1000 (`input_lowering.h:395`).
///   3. `RealServices::installer()` reaches it twice at construction -- `set_target_with_timeline`
///      (`mod_knobs.cpp:120`) and `set_mod_region` (`:165`) -- but **only when a `mod_target` is
///      seeded**, since both are skipped for a null one. That is the whole reason the fixture below
///      leaves it null.
///
/// The arithmetic closes it exactly, at this file's `300 encoder GoldUpper +2`:
///
/// | fixture | `last_turn_ms_[GoldUpper]` | test at t=300 | result |
/// |---|---|---|---|
/// | `Services{}` | 0 | `300 - 0 = 300 < 500` | acted -> **4** |
/// | real three, `mod_target = nullptr` (**what runs here**) | 0 | same | acted -> **4** |
/// | real three, `mod_target` seeded | `0 - 1000` (unsigned wrap) | `1300 >= 500` | swallowed -> **0** |
///
/// And it explains the disagreement with `scene_scripts_spec.cpp`'s `menu_gold_turn`, which drives
/// the same node with the same services SEEDED and still reads 4: its script turns `+1` at t=100
/// first, which sets `last_turn_ms_` to 100, so the `+2` at t=150 sees `50 < 500` and acts. That
/// leading detent is the whole difference, and it is the edit the scripts here are owed.
///
/// **The seam worth naming for whoever installs the next service**: a service mutated shared
/// `InputLowering` state at construction, and the effect is observable in a debounce that no
/// routed event passes through. "The router never gave it the event" was true and irrelevant.
///
/// **One number in this file's history was wrong and is corrected here**: task 11's first pass
/// reported "1 with `svc.mod_target = nullptr`", which was a misread of interleaved CppSpec output.
/// Re-run with a single block patched, unseeded raises no failure at all.
///
/// `src/scenes/README.md`'s carry table, the migration spec's §10.2 blocker list and F1 task 11's
/// report all carry this row.

#include "scenes/browser/browser_scene.h"
#include "scenes/context_menu/context_menu_scene.h"
#include "scenes/marker/marker_scene.h"
#include "scenes/menu/action.h"
#include "scenes/menu/bindings.h"
#include "scenes/menu/context.h"
#include "scenes/menu/menu_scene.h"
#include "scenes/menu/node.h"
#include "scenes/menu/node_id.h"
#include "scenes/menu/nodes/nodes.h"
#include "scenes/menu/nodes/scales.h"
#include "scenes/profile.h"
#include "scenes/recorder/recorder_scene.h"
#include "scenes/registry.h"
#include "scenes/rename/rename_scene.h"

#include "notify/dirty_set.h"
#include "notify/notice_queue.h"
#include "shell/profiles/deluge.h"
#include "shell/shell.h"

#include "gui/l10n/l10n.h"
#include "gui/l10n/strings.h"
#include "hid/display/display.h"
#include "io/midi/midi_device.h"
#include "io/midi/midi_device_manager.h"
#include "io/midi/midi_engine.h"
#include "model/clip/clip.h"
#include "model/sample/sample.h"
#include "model/song/song.h"
#include "modulation/params/param_descriptor.h"
#include "modulation/patch/patch_cable_set.h"
#include "processing/sound/sound.h"
#include "processing/sound/sound_instrument.h"
#include "processing/source.h"
#include "storage/cluster/cluster.h"
#include "storage/flash_storage.h"
#include "storage/multi_range/multisample_range.h"

#include "support/song_fixture.h"

// Before <cppspec.hpp>: it #defines `it`, which collides with every local named `it` in the project
// headers this drags in (`audio_engine.h` -> `util/container/ordered_pos_vector.h`). Same placement
// rule `browser_scene_spec.cpp`, `marker_scene_spec.cpp` and `recorder_scene_spec.cpp` run under.
#include "support/card_access.h"

// Before `script_runner.h`, which is what actually drags `<cppspec.hpp>` in (`script_runner.h:10`)
// -- so "before cppspec" is not enough here, it has to be before this line. `real_services.h`
// reaches `scenes/overlay/param_popup_scene.h`, whose `push` takes a parameter named `context`
// (`:163`), and CppSpec `#define`s `context` to `self.context` (`cppspec.hpp:23`). The same
// placement rule as `card_access.h` above, for the same kind of collision on a different keyword.
#include "support/real_services.h"

#include "spec/shell/support/script_runner.h"

#include "cppspec.hpp"

#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <string>

#include "spec/support/spec_main.h"

using namespace deluge::scenes;
using deluge::l10n::String;
using deluge::scenes::menu::MenuAction;
using deluge::scenes::menu::MenuActionRequest;
using deluge::scenes::menu::MenuContext;
using deluge::scenes::menu::Node;
namespace bindings = deluge::scenes::menu::bindings;
namespace nodes = deluge::scenes::menu::nodes;
namespace context_menu = deluge::scenes::context_menu;
namespace browser = deluge::scenes::browser;
namespace marker = deluge::scenes::marker;
namespace recorder = deluge::scenes::recorder;

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
	deluge::l10n::chosenLanguage = &deluge::l10n::built_in::english;
	deluge::notify::Notice discard;
	while (deluge::notify::notices().pop(discard)) {}
	deluge::notify::dirty().drain();
}

/// @brief The synthetic vertical wrapper the range-permission script opens: two REAL leaves,
///        `lpf_mode` (an ordinary Enum, so `Select +1` off the default cursor lands on row 1) and
///        `source0_transpose` (the real range-dependent Decimal `permission_for` gates).
constexpr const Node* kRangePermChildren[] = {&nodes::lpf_mode, &nodes::source0_transpose};
constexpr Node range_perm_root = deluge::scenes::menu::submenu(sid(String::STRING_FOR_TRANSPOSE), kRangePermChildren);
constexpr const Node* kRangePermRoots[] = {&range_perm_root};

/// @brief `menu_scene().state().list[0].cursor` -- the same accessor `menu_scene_spec.cpp` uses.
int cursor0() {
	return menu_scene().state().list[0].cursor;
}

/// @brief The cursor of the row list at whatever depth the menu is currently at.
///
/// `cursor0()` is right only while the target sits at depth 0. Task 12's scripts open nodes several
/// levels down and some of them descend further, so the depth has to be read rather than assumed.
int cursor_now() {
	const deluge::scenes::menu::MenuState& st = menu_scene().state();
	return st.depth < deluge::scenes::menu::kMenuDepth ? st.list[st.depth].cursor : -1;
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
	    .profile = &deluge::scenes::deluge_profile(),
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
/// `nodes::default_tempo` edits process-wide flash-backed state (`nodes/settings.cpp`
/// `get_default_tempo_lower`), and the script deliberately moves both ends of it. Every other spec in
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
// E3 task 11's four scripts, and the fixtures they need.
//
// The browser and the marker editor are reached here through the **shortcut-pad jump**, and that is
// not a shortcut in the convenience sense: `file0_selector` and `sample0_start` are declared only on
// `Layout::Horizontal` pages (`source0`, `audio_clip_sample`), and a horizontal page's `columns`
// widget claims `entered` only for a `Kind::Submenu` column (`ui/oled/columns.cpp`) -- so no select
// press in the committed tree descends into either. The shortcut table does
// (`menu/shortcuts.cpp:100`, legacy's own `paramShortcutsForSounds` cells), which makes it the only
// door those two have -- the same Custom-column navigation gap `menu_range_permission`'s synthetic
// wrapper already records one level up, met here with a real path instead of a synthetic one.
//
// **The recorder is the exception, and fix round 1 corrected this comment for it.**
// `sample0_recorder` is also a child of the *vertical* `sound_actions` (`nodes/sound_actions.h:82`)
// and `drum_actions` (`nodes/roots.h:315`), and `ui::oled::list` sets `entered` for any selectable
// row (`ui/oled/list.cpp:121`) -- so SOUND -> ACTIONS -> RECORD TO SAMPLE is two ordinary select
// presses. `recorder.txt` drives that door first and the shortcut second, in one run, because the
// two do not end the same way: the ordinary route leaves the item at depth 2 where
// `MenuState::go_up_on_resume` can ascend, and the jump re-roots onto the item at depth 0 where
// `ascend()` is a no-op. The dead end the other two scripts end on is manufactured by the door they
// had to use, and this script is what shows that.

/// @brief The minimum `Display` the model reaches for.
///
/// `display` is null in a spec process (`hid/display/display.cpp:4`), and both
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
/// `deluge::sample::find_peaks_per_col` reads clusters, and a spec process has no mounted card
/// holding the file this `Sample` claims to be. It does not have to: the scan's whole-cluster arm
/// reuses the sample's own overview cache when a column spans two or more clusters
/// (`waveform_scan.cpp`), so the fixture is exactly `kDisplayWidth` clusters' worth of frames with
/// every overview entry pre-marked investigated. Halving the length turns the whole waveform black.
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
	s->maxValueFound = 127 << 24;
	s->minValueFound = -128 << 24;
	s->resizeOverviewCache(kNumClusters);
	for (int32_t i = 0; i < kNumClusters; i++) {
		OverviewCacheEntry& e = s->overviewCacheEntry(static_cast<uint32_t>(i));
		e.min = static_cast<int8_t>(-cluster_peak(i));
		e.max = cluster_peak(i);
		e.investigated = true;
	}
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
	auto* sound = static_cast<Sound*>(static_cast<ModControllableAudio*>(song.instrument->toModControllable()));
	Source& source = sound->sources[0];
	source.oscType = OscType::SAMPLE;
	source.repeatMode = SampleRepeatMode::ONCE;
	source.sampleControls.reversed = false;
	auto* range = static_cast<MultisampleRange*>(source.getOrCreateFirstRange());
	if (sample != nullptr) {
		range->sampleHolder.audioFile = sample;
		range->sampleHolder.startPos = 0;
		range->sampleHolder.endPos = kSampleLength;
		range->sampleHolder.loopStartPos = 0;
		range->sampleHolder.loopEndPos = 0;
		range->sampleHolder.loopLocked = false;
		range->sampleHolder.waveformViewZoom = 0; // no remembered view -- open zoomed out
		range->sampleHolder.waveformViewScroll = 0;
	}
	return range;
}

/// @brief A card under the host temp directory, built and removed around the browser script.
///
/// `browser_scene_spec.cpp`'s `ScratchCard`, minus the MIDI-definition half this script never
/// lists. The committed `tests/fixtures/card` cannot serve: its `SAMPLES` folder holds one `.keep`
/// file and no subdirectory, so the script could neither descend nor search. `git status
/// --porcelain tests/fixtures` stays clean because nothing here goes near it.
struct ScratchCard {
	std::filesystem::path dir;
	spec::SdRoot root;

	static std::filesystem::path make() {
		std::filesystem::path d = std::filesystem::temp_directory_path() / "deluge_menu_scripts_spec";
		std::filesystem::remove_all(d);
		std::filesystem::create_directories(d / "SAMPLES" / "KICKS");
		for (const char* name : {"CLAP.WAV", "SNARE.WAV", "TOM.WAV"}) {
			std::ofstream(d / "SAMPLES" / name) << "RIFF";
		}
		for (const char* name : {"KICK1.WAV", "KICK2.WAV", "SUB.WAV"}) {
			std::ofstream(d / "SAMPLES" / "KICKS" / name) << "RIFF";
		}
		return d;
	}

	ScratchCard() : dir{make()}, root{dir.string()} {}
	~ScratchCard() {
		std::error_code ec;
		std::filesystem::remove_all(dir, ec);
	}
	ScratchCard(const ScratchCard&) = delete;
	ScratchCard& operator=(const ScratchCard&) = delete;
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

} // namespace

// clang-format off
describe menu_scripts("scenes::menu per-kind level-2 acceptance scripts", $ {
	it("Int: nodes::saturation renders and edits through the real Shell", _ {
		reset_globals();
		spec::SongFixture song = spec::make_synth_song();
		const MenuContext ctx = menu::context_for(*song.song, song.clip, 0, false, false, false, false);
		(void)menu::assign_node_ids(nodes::kAllRoots);
		menu_scene().open(nodes::saturation, ctx);

		const int32_t before = bindings::read(nodes::saturation, ctx);

		uitest::RunOptions opts{
		    .tick_ms = 10,
		    .board = &deluge::ui::shell::kDelugeBoard,
		    .profile = &deluge::scenes::deluge_profile(),
		    .initial_scene = deluge::ui::shell::SceneId{deluge::ui::shell::SceneKind::FirstApp,
		                                               menu::node_id(nodes::saturation)},
		};
		// The real three, UNSEEDED -- `mod_target` deliberately left null. See this file's
		// `@par Every script here installs the real three, unseeded`.
		spec::RealServices svc;
		svc.song = song.song;
		opts.after_construct = svc.installer();
		const uitest::RunResult result =
		    uitest::expect_script_goldens(self, "menu_int", scenes::scene_registry(), svc.slots(), opts);
		// Three, not five: E3 task 10 dropped the Select-turn and Select-press snapshots. Their
		// `.pbm` halves were byte-identical to `idle`, for two different reasons the script file's
		// own comments name (`render_leaf_int` claims-and-ignores the turn; the press's `ascend()`
		// is a no-op at depth 0) -- but their `.state` halves were NOT identical to `idle`'s: both
		// had gained a `pad` line (the shortcut/source blink lighting up shortly after the screen
		// opens), fix round 1 found on review. That line is not lost: the kept `after_gold_turn`
		// snapshot carries the identical line, so nothing about it went untested. The two *steps*
		// stay, because the assertions below are about them.
		expect(result.snapshots).to_equal(uint32_t{3});

		// Q7: the `.state` pins WHICH node is open; it says nothing about its label or value, so
		// both are asserted here directly. The name check is against the EXPECTED `l10n::String` id,
		// not `nodes::saturation.name` -- comparing a node's own field against itself cannot fail
		// once the pointer-identity check just above already holds (review round 1, I2); this is the
		// independent value Q7 actually wants pinned.
		expect(menu_scene().focused() == &nodes::saturation).to_be_true();
		expect(static_cast<uint32_t>(menu_scene().focused()->name)).to_equal(static_cast<uint32_t>(sid(String::STRING_FOR_SATURATION)));
		// `int_value` now edits on the select encoder, not the gold knobs (`ui/oled/values.cpp`'s
		// `select_delta`): the script's `100 encoder Select +1` step -- once a claimed-and-ignored
		// no-op (this file used to say so) -- is what moves the value now, by exactly 1 select
		// detent * the node's own `IntRange{0,15,1}.step` (1). Fix round 1's follow-up review found
		// the script's OLD closing `300 encoder GoldUpper +2` step no longer tested anything (gold
		// reaches nobody: `int_value` claims no gold input, and the unseeded `ModKnobs` behind it has
		// no target to write through) -- so that step is now `encoder Select +2` too, a second select
		// edit on the same leaf, landing at `1 + 2 = 3`.
		expect(bindings::read(nodes::saturation, ctx)).to_equal(std::min(before + 3, 15));
	});

	it("Decimal: nodes::source0_transpose renders and edits through the real Shell", _ {
		reset_globals();
		spec::SongFixture song = spec::make_synth_song();
		const MenuContext ctx = menu::context_for(*song.song, song.clip, 0, false, false, false, false);
		(void)menu::assign_node_ids(nodes::kAllRoots);
		menu_scene().open(nodes::source0_transpose, ctx);

		const int32_t before = bindings::read(nodes::source0_transpose, ctx);

		uitest::RunOptions opts{
		    .tick_ms = 10,
		    .board = &deluge::ui::shell::kDelugeBoard,
		    .profile = &deluge::scenes::deluge_profile(),
		    .initial_scene = deluge::ui::shell::SceneId{deluge::ui::shell::SceneKind::FirstApp,
		                                               menu::node_id(nodes::source0_transpose)},
		};
		// The real three, UNSEEDED -- `mod_target` deliberately left null. See this file's
		// `@par Every script here installs the real three, unseeded`.
		spec::RealServices svc;
		svc.song = song.song;
		opts.after_construct = svc.installer();
		const uitest::RunResult result = uitest::expect_script_goldens(
		    self, "menu_decimal", scenes::scene_registry(), svc.slots(), opts);
		// Five, not six: the script's first GoldUpper turn lands on a page that reads no edit
		// encoder, so its snapshot was byte-identical to `after_select_press` and was dropped (fix
		// round 1) -- the *step* stays, because the "unchanged" assertion below is about it.
		expect(result.snapshots).to_equal(uint32_t{5});

		// E2 task 9: `source0_transpose` is one of the 53 `PatchedParam` nodes, so its select press
		// opens the source picker rather than ascending (`patched_param.cpp:32-40`). The script's
		// first GoldUpper turn therefore lands on that picker and edits nothing; the Back brings the
		// leaf back and the script's closing select turn is the one that moves the value a second time.
		expect(menu_scene().focused() == &nodes::source0_transpose).to_be_true();
		expect(static_cast<uint32_t>(menu_scene().focused()->name)).to_equal(static_cast<uint32_t>(sid(String::STRING_FOR_TRANSPOSE)));
		// `decimal_value` now edits on the select encoder (`ui/oled/values.cpp`'s `select_delta`),
		// by `10^edit_pos` per detent -- `edit_pos` stays 0 throughout, so the script's very first
		// step, `100 encoder Select +1`, moves the value by 1 * 1 on its own. The `GoldUpper` turn
		// that follows still reaches nobody -- it lands on the source picker the select press opens,
		// which reads no edit encoder, as before -- but fix round 1's follow-up review found the
		// script's OLD closing `GoldUpper` turn (back on the leaf) no longer tested anything either,
		// since `decimal_value` claims no gold input at all now. That step is `encoder Select +2` now,
		// landing at `1 + 2 = 3`.
		expect(bindings::read(nodes::source0_transpose, ctx)).to_equal(std::min(before + 3, 9600));
		// `after_select_turn`'s pixel diff against `idle` is now a real effect of the Select turn
		// (the digits move by 1), not only the cursor's own time-driven blink as it was before this
		// fix. What still verifies the cursor specifically: `edit_pos` moves only on the
		// "Horizontal" encoder, which this script never touches, so it must still be 0 after both
		// select turns, a select press/release and the one remaining `GoldUpper` turn -- Select edits
		// the value on a Decimal screen now, exactly as it always moved the cursor's *place*, but it
		// still never moves the cursor's *position*.
		expect(static_cast<int>(menu_scene().state().decimal_pos)).to_equal(0);
	});

	it("Enum: nodes::lpf_mode renders and edits through the real Shell", _ {
		reset_globals();
		spec::SongFixture song = spec::make_synth_song();
		const MenuContext ctx = menu::context_for(*song.song, song.clip, 0, false, false, false, false);
		(void)menu::assign_node_ids(nodes::kAllRoots);
		menu_scene().open(nodes::lpf_mode, ctx);

		const int32_t before = bindings::read(nodes::lpf_mode, ctx); // TRANSISTOR_24DB == 1

		uitest::RunOptions opts{
		    .tick_ms = 10,
		    .board = &deluge::ui::shell::kDelugeBoard,
		    .profile = &deluge::scenes::deluge_profile(),
		    .initial_scene = deluge::ui::shell::SceneId{deluge::ui::shell::SceneKind::FirstApp,
		                                               menu::node_id(nodes::lpf_mode)},
		};
		// The real three, UNSEEDED -- `mod_target` deliberately left null. See this file's
		// `@par Every script here installs the real three, unseeded`.
		spec::RealServices svc;
		svc.song = song.song;
		opts.after_construct = svc.installer();
		const uitest::RunResult result =
		    uitest::expect_script_goldens(self, "menu_enum", scenes::scene_registry(), svc.slots(), opts);
		// Three, not five: E3 task 10 dropped the Select-press and GoldUpper-turn snapshots -- both
		// were byte-identical to `after_select_turn`, for the two reasons the script file's own
		// comments name. The steps stay; the final assertion below (`before + 1`, not `+2`) is their
		// proof: either one moving the value too would show up in that total.
		expect(result.snapshots).to_equal(uint32_t{3});

		expect(menu_scene().focused() == &nodes::lpf_mode).to_be_true();
		expect(static_cast<uint32_t>(menu_scene().focused()->name)).to_equal(static_cast<uint32_t>(sid(String::STRING_FOR_MODE)));
		// `enum_value` owns the Select turn itself: one detent moves the option index by one, and it
		// commits on the same tick (`Response::changed`). The later SelectEnc press/release and
		// GoldUpper turn (unclaimed by this widget) do not move it again.
		expect(bindings::read(nodes::lpf_mode, ctx)).to_equal(before + 1);
	});

	it("Toggle: nodes::osc_sync renders and edits through the real Shell", _ {
		reset_globals();
		spec::SongFixture song = spec::make_synth_song();
		const MenuContext ctx = menu::context_for(*song.song, song.clip, 0, false, false, false, false);
		(void)menu::assign_node_ids(nodes::kAllRoots);
		menu_scene().open(nodes::osc_sync, ctx);

		expect(bindings::read(nodes::osc_sync, ctx)).to_equal(int32_t{0}); // off by default

		uitest::RunOptions opts{
		    .tick_ms = 10,
		    .board = &deluge::ui::shell::kDelugeBoard,
		    .profile = &deluge::scenes::deluge_profile(),
		    .initial_scene = deluge::ui::shell::SceneId{deluge::ui::shell::SceneKind::FirstApp,
		                                               menu::node_id(nodes::osc_sync)},
		};
		// The real three, UNSEEDED -- `mod_target` deliberately left null. See this file's
		// `@par Every script here installs the real three, unseeded`.
		spec::RealServices svc;
		svc.song = song.song;
		opts.after_construct = svc.installer();
		const uitest::RunResult result = uitest::expect_script_goldens(
		    self, "menu_toggle", scenes::scene_registry(), svc.slots(), opts);
		// Three, not five: E3 task 10 dropped the Select-press and GoldUpper-turn snapshots, for the
		// same two reasons as `menu_enum` above. The steps stay; the final assertion below (flipped
		// exactly once, not twice) is their proof.
		expect(result.snapshots).to_equal(uint32_t{3});

		expect(menu_scene().focused() == &nodes::osc_sync).to_be_true();
		expect(static_cast<uint32_t>(menu_scene().focused()->name)).to_equal(static_cast<uint32_t>(sid(String::STRING_FOR_OSCILLATOR_SYNC)));
		// `toggle`'s turn selects the row in the turn's direction: one detent toward ON flips it.
		// The later SelectEnc press reports `entered` and leaves `changed` false (`ui/oled/values.h`),
		// and GoldUpper is unclaimed by this widget, so the value stays flipped, not flipped twice.
		expect(bindings::read(nodes::osc_sync, ctx)).to_equal(int32_t{1});
	});

	it("Submenu (horizontal): nodes::lpf renders and edits its focused column through the real Shell", _ {
		// **Rewritten for note-family task 2's fix.** The select turn now edits the focused column
		// and never moves focus (`HorizontalMenu::selectEncoderAction`, `horizontal_menu.cpp:309-326`);
		// KIT focuses column 1 directly (`buttonAction`'s `select_map`, `:97`); the gold knob no
		// longer touches the menu at all, falling through to the real (unseeded) `ModKnobs`; and the
		// select press on FREQ (a `PatchedParam` with a patch-chain) follows the chain rather than
		// raising a notification and staying.
		reset_globals();
		spec::SongFixture song = spec::make_synth_song();
		const MenuContext ctx = menu::context_for(*song.song, song.clip, 0, false, false, false, false);
		(void)menu::assign_node_ids(nodes::kAllRoots);
		menu_scene().open(nodes::lpf, ctx);

		const int32_t before_mode = bindings::read(nodes::lpf_mode, ctx);
		const int32_t before_freq = bindings::read(nodes::lpf_freq, ctx);

		uitest::RunOptions opts{
		    .tick_ms = 10,
		    .board = &deluge::ui::shell::kDelugeBoard,
		    .profile = &deluge::scenes::deluge_profile(),
		    .initial_scene = deluge::ui::shell::SceneId{deluge::ui::shell::SceneKind::FirstApp, menu::node_id(nodes::lpf)},
		};
		// The real three, UNSEEDED -- `mod_target` deliberately left null. See this file's
		// `@par Every script here installs the real three, unseeded`.
		spec::RealServices svc;
		svc.song = song.song;
		opts.after_construct = svc.installer();
		const uitest::RunResult result = uitest::expect_script_goldens(
		    self, "menu_submenu_horizontal", scenes::scene_registry(), svc.slots(), opts);
		expect(result.snapshots).to_equal(uint32_t{5});

		// The select press on FREQ followed its patch-chain into the source-selection page, so the
		// path is one level deeper than the page itself; `after_back` returns to it with FREQ still
		// the focused column.
		expect(menu_scene().focused() == &nodes::lpf).to_be_true();
		expect(static_cast<uint32_t>(menu_scene().focused()->name)).to_equal(static_cast<uint32_t>(sid(String::STRING_FOR_LPF)));
		expect(static_cast<int>(menu_scene().state().columns[0].focused)).to_equal(1); // FREQ

		// The select turn edited MODE by one -- clamped, matching `Enumeration::selectEncoderAction`'s
		// non-wrapping clamp -- and never touched FREQ.
		const int32_t last_mode_index = static_cast<int32_t>(std::size(nodes::kLpfModeNames)) - 1;
		expect(bindings::read(nodes::lpf_mode, ctx)).to_equal(std::min(before_mode + 1, last_mode_index));
		// GoldUpper reached the real, unseeded `ModKnobs` instead of the menu -- no target to write
		// through, so FREQ never moved.
		expect(bindings::read(nodes::lpf_freq, ctx)).to_equal(before_freq);
	});

	it("Shortcut: a real shift+pad jump lands on nodes::lpf_freq, then the real Shell edits it", _ {
		reset_globals();
		spec::SongFixture song = spec::make_synth_song();
		const MenuContext ctx = menu::context_for(*song.song, song.clip, 0, false, false, false, false);
		(void)menu::assign_node_ids(nodes::kAllRoots);
		menu_scene().open(nodes::sound_root, ctx);

		const int32_t before = bindings::read(nodes::lpf_freq, ctx);

		uitest::RunOptions opts{
		    .tick_ms = 10,
		    .board = &deluge::ui::shell::kDelugeBoard,
		    .profile = &deluge::scenes::deluge_profile(),
		    .initial_scene = deluge::ui::shell::SceneId{deluge::ui::shell::SceneKind::FirstApp,
		                                               menu::node_id(nodes::sound_root)},
		};
		// The real three, UNSEEDED -- `mod_target` deliberately left null. See this file's
		// `@par Every script here installs the real three, unseeded`.
		spec::RealServices svc;
		svc.song = song.song;
		opts.after_construct = svc.installer();
		const uitest::RunResult result = uitest::expect_script_goldens(
		    self, "menu_shortcut", scenes::scene_registry(), svc.slots(), opts);
		// Five: `lpf_freq` is a `PatchedParam` node too, so the script gained a second GoldUpper turn
		// after the Back -- and lost the first one's empty snapshot, same as `menu_decimal` above. E3
		// task 10 dropped a second one, `after_select_turn` (byte-identical to `after_shortcut`, for
		// the reason `render_leaf_decimal` claims-and-ignores the select encoder) -- see the script
		// file's own notes for both.
		expect(result.snapshots).to_equal(uint32_t{5});

		// The real pad (8, 7) of `paramShortcutsForSounds` -- `&lpfFreqMenu` -- proven in
		// `menu_scene_spec.cpp`'s own shortcut tests.
		expect(menu_scene().focused() == &nodes::lpf_freq).to_be_true();
		expect(static_cast<uint32_t>(menu_scene().focused()->name)).to_equal(static_cast<uint32_t>(sid(String::STRING_FOR_FREQUENCY)));
		// `int_value` now edits on the select encoder (`ui/oled/values.cpp`'s `select_delta`), so the
		// script's `200 encoder Select +1` step -- once claimed-and-ignored here too -- is what moves
		// the value first, by 1 select detent * the node's own step (1). The `GoldUpper` turn that
		// follows still lands on the source picker the select press opens and still does nothing
		// there. Fix round 1's follow-up review found the script's OLD closing `GoldUpper` turn (back
		// on this leaf) no longer tested anything either, since `int_value` claims no gold input at
		// all any more -- so that step is `encoder Select +2` now, landing at `1 + 2 = 3`.
		expect(bindings::read(nodes::lpf_freq, ctx)).to_equal(std::min(before + 3, 50));
	});

	it("Range permission: entering the real source0_transpose opens the real range picker first", _ {
		reset_globals();
		spec::SongFixture song = spec::make_synth_song();
		auto* sound = static_cast<SoundInstrument*>(song.instrument);
		// `permission_for`'s `MustSelectRange` arm (`context.cpp`): more than one `MultiRange` on the
		// source and none selected yet -- the exact setup `menu_scene_spec.cpp`'s own range-permission
		// test uses, here against the REAL `nodes::source0_transpose` instead of a synthetic param.
		sound->sources[0].ranges.insertMultiRange(0);
		sound->sources[0].ranges.insertMultiRange(1);
		const MenuContext ctx = menu::context_for(*song.song, song.clip, 0, false, false, false, false);
		(void)menu::assign_node_ids(kRangePermRoots);
		menu_scene().open(range_perm_root, ctx);

		const int32_t before = bindings::read(nodes::source0_transpose, ctx);

		uitest::RunOptions opts{
		    .tick_ms = 10,
		    .board = &deluge::ui::shell::kDelugeBoard,
		    .profile = &deluge::scenes::deluge_profile(),
		    .initial_scene = deluge::ui::shell::SceneId{deluge::ui::shell::SceneKind::FirstApp,
		                                               menu::node_id(range_perm_root)},
		};
		// The real three, UNSEEDED -- `mod_target` deliberately left null. See this file's
		// `@par Every script here installs the real three, unseeded`.
		spec::RealServices svc;
		svc.song = song.song;
		opts.after_construct = svc.installer();
		const uitest::RunResult result = uitest::expect_script_goldens(
		    self, "menu_range_permission", scenes::scene_registry(), svc.slots(), opts);
		expect(result.snapshots).to_equal(uint32_t{4});

		// The script ends with a Back, so the wrapper is on screen again -- but `after_select_press`
		// is the REAL range picker (its two rows, named by `MultiRange::getText`), not the
		// refused-entry no-op E1 recorded here. (The GoldUpper snapshot that used to follow it was
		// dropped in fix round 1: the picker reads no edit encoder, so it was byte-identical.)
		expect(menu_scene().focused() == &range_perm_root).to_be_true();
		expect(static_cast<uint32_t>(menu_scene().focused()->name)).to_equal(static_cast<uint32_t>(sid(String::STRING_FOR_TRANSPOSE)));
		expect(cursor0()).to_equal(1); // the Select detent landed the cursor on row 1
		// The substitution happened and is still recorded: `enter_child` set both, and `ascend` has no
		// business dropping them -- the item is still the one the picker was standing in for.
		expect(menu_scene().state().range_picker_pending).to_be_true();
		expect(menu_scene().state().pending_after_range == &nodes::source0_transpose).to_be_true();
		// And the picker really ran `MultiRange::beginSession` on this source: `defaultRangeI` was -1
		// and is now `numRanges >> 1` (`multi_range.cpp:46-50`), written back by `menu::select_range`.
		expect(static_cast<int>(sound->sources[0].defaultRangeI)).to_equal(1);
		// The leaf itself was never opened, so its model value is untouched.
		expect(bindings::read(nodes::source0_transpose, ctx)).to_equal(before);
	});

	// -----------------------------------------------------------------------------------------
	// E2 task 12. One block per thing E2 added; see this file's own table.

	it("SyncLevel: nodes::delay_sync draws legacy's note-length string, and only the SELECT encoder moves it", _ {
		reset_globals();
		spec::SongFixture song = spec::make_synth_song();
		const MenuContext ctx = menu::context_for(*song.song, song.clip, 0, false, false, false, false);
		(void)menu::assign_node_ids(nodes::kAllRoots);
		menu_scene().open(nodes::delay_sync, ctx);
		// Value 0 is legacy's literal "OFF" arm (`gui/menu_item/sync_level.cpp:30-38`
		// `SyncLevel::drawPixelsForOled`), which is what `idle` records.
		bindings::write(nodes::delay_sync, ctx, 0);

		const deluge::scenes::menu::IntRange sync_range = bindings::range(nodes::delay_sync, ctx);

		uitest::RunOptions opts = opts_for(nodes::delay_sync);
		// The real three, UNSEEDED -- `mod_target` deliberately left null. See this file's
		// `@par Every script here installs the real three, unseeded`.
		spec::RealServices svc;
		svc.song = song.song;
		opts.after_construct = svc.installer();
		const uitest::RunResult result = uitest::expect_script_goldens(
		    self, "menu_sync_level", scenes::scene_registry(), svc.slots(), opts);
		expect(result.snapshots).to_equal(uint32_t{3});

		// `SyncLevel` derives from `Enumeration`, not `Integer` (`gui/menu_item/sync_level.h:27`), and
		// `Enumeration::selectEncoderAction` (`enumeration.cpp:18-36`) clamps rather than wraps. So the
		// +40 detents stop at the top of the node's own range instead of coming back round to OFF.
		//
		// **And this is what backs the dropped gold-turn snapshot** (fix round 1). The script's last
		// step is `GoldUpper -4`, after the clamp: `render_leaf_sync_level` never calls `f.edit()`, so
		// the value must still be sitting on `range.max`. A gold knob that was live would have dragged
		// it 8 steps down off the clamp and failed here. (Until fix round 1 the gold step sat between
		// the two select turns, where the following clamp made this assertion true whatever the gold
		// turn had done -- the one dropped golden in this file's set that nothing actually backed.)
		expect(bindings::read(nodes::delay_sync, ctx)).to_equal(sync_range.max);
		expect(sync_range.max > int32_t{4}).to_be_true(); // the -4 really could have shown, had it acted
		expect(menu_scene().focused() == &nodes::delay_sync).to_be_true();
	});

	it("select_chain: a select press on nodes::unison_count opens nodes::unison_spread, and edits it", _ {
		reset_globals();
		spec::SongFixture song = spec::make_synth_song();
		const MenuContext ctx = menu::context_for(*song.song, song.clip, 0, false, false, false, false);
		(void)menu::assign_node_ids(nodes::kAllRoots);
		menu_scene().open(nodes::unison_count, ctx);

		const int32_t count_before = bindings::read(nodes::unison_count, ctx);
		const int32_t spread_before = bindings::read(nodes::unison_spread, ctx);
		const deluge::scenes::menu::IntRange count_range = bindings::range(nodes::unison_count, ctx);
		const deluge::scenes::menu::IntRange spread_range = bindings::range(nodes::unison_spread, ctx);

		uitest::RunOptions opts = opts_for(nodes::unison_count);
		// The real three, UNSEEDED -- `mod_target` deliberately left null. See this file's
		// `@par Every script here installs the real three, unseeded`.
		spec::RealServices svc;
		svc.song = song.song;
		opts.after_construct = svc.installer();
		const uitest::RunResult result = uitest::expect_script_goldens(
		    self, "menu_chain", scenes::scene_registry(), svc.slots(), opts);
		expect(result.snapshots).to_equal(uint32_t{6});

		// `CountToStereoSpread::selectButtonPress` (`gui/menu_item/unison/count.h:76-79`) navigates
		// *into* the spread, so the press must not have ascended -- and the second select turn, after
		// the Back, must have landed on the count again.
		expect(menu_scene().focused() == &nodes::unison_count).to_be_true();
		// `int_value` now edits on the select encoder, not the gold knobs (`ui/oled/values.cpp`'s
		// `select_delta`); fix round 1's follow-up review found this script's three OLD `GoldUpper`
		// turns (on the count, on the spread the select press opens, on the count again after the
		// Back) no longer tested anything, since `int_value` claims no gold input at all now. All
		// three are `encoder Select` turns now, sized at `+/-4` (double the old gold script number,
		// since a select detent is not doubled by `edges_per_detent` the way an `Accelerating` gold
		// knob's magnitude is) -- so the arithmetic below is unchanged from before this fix, only the
		// encoder producing it is different: the select turns land the count at exactly the same
		// clamp the gold turns used to.
		const int32_t after_first = std::min(count_before + 4, count_range.max);
		expect(after_first).to_equal(count_range.max); // the clamp really did happen, so -4 is visible
		expect(bindings::read(nodes::unison_count, ctx)).to_equal(std::max(after_first - 4, count_range.min));
		expect(bindings::read(nodes::unison_spread, ctx)).to_equal(std::min(spread_before + 4, spread_range.max));
	});

	it("Range: nodes::default_tempo picks an edge with the horizontal encoder; its gold turns move neither end", _ {
		reset_globals();
		spec::SongFixture song = spec::make_synth_song();
		const MenuContext ctx = menu::context_for(*song.song, nullptr, 0, false, false, false, false);
		(void)menu::assign_node_ids(nodes::kAllRoots);
		// Process-wide flash-backed state (`nodes/settings.cpp` `get_default_tempo_lower`), so it is
		// saved and put back. Set BEFORE `open()`, which is what seeds `MenuState::edit_int` /
		// `edit_upper` from the model (`MenuScene::enter_leaf`'s `Kind::Range` arm). A split pair well
		// inside `kDefaultTempoRange{60, 240, 1}`, so the two ends are visibly distinct in `idle`,
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
		    self, "menu_range", scenes::scene_registry(), svc.slots(), opts);
		expect(result.snapshots).to_equal(uint32_t{5});

		// `range_value` now edits the selected edge on the select encoder, not the gold knobs
		// (`ui/oled/values.cpp`'s `select_delta`, and its doc's note on the two ported
		// `selectEncoderAction`s). This script only ever turns `GoldUpper`, so neither edge moves:
		// `range_value` claims none of those turns, and the unseeded `ModKnobs` behind them (`@par
		// Every script here installs the real three, unseeded`, above) has no target to write
		// through. The horizontal encoder still walks the edge exactly as before -- that half of
		// this script's own name still holds -- it is only the edit half that goes quiet.
		expect(FlashStorage::defaultRanges[FlashStorage::DEFAULT_RANGE_TEMPO].lower).to_equal(int32_t{100});
		expect(FlashStorage::defaultRanges[FlashStorage::DEFAULT_RANGE_TEMPO].upper).to_equal(int32_t{200});
		saved.restore();
	});

	it("ActiveScales: a select press toggles the row's box and stays on the page", _ {
		reset_globals();
		spec::SongFixture song = spec::make_synth_song();
		const MenuContext ctx = menu::context_for(*song.song, nullptr, 0, false, /*session_or_arranger_root=*/true,
		                                                  /*session_view_root=*/true, false);
		(void)menu::assign_node_ids(nodes::kAllRoots);
		// Entered from the root, not opened directly: at depth 0 an unwanted `ascend()` is a silent
		// no-op, so "the item consumed the select press" would be indistinguishable from "it did not".
		menu_scene().open(nodes::root_song, ctx);

		expect(nodes::kSongActiveScalesItem.is_disabled(ctx, 2)).to_be_false();

		const uitest::RunResult result = uitest::expect_script_goldens(
		    self, "menu_scales", scenes::scene_registry(), deluge::ui::shell::Services{}, opts_for(nodes::root_song));
		expect(result.snapshots).to_equal(uint32_t{7});

		// Two presses on row 2: disabled, then enabled again. The *pair* is what makes the goldens
		// mean something -- one press alone could not tell a flip from a one-way write.
		expect(nodes::kSongActiveScalesItem.is_disabled(ctx, 2)).to_be_false();
		// Nothing else moved: `commit` writes only the cursor's own bit.
		expect(nodes::kSongActiveScalesItem.is_disabled(ctx, 0)).to_be_false();
		expect(nodes::kSongActiveScalesItem.is_disabled(ctx, 3)).to_be_false();
		// `selectButtonPress` returns NO_NAVIGATION (`gui/menu_item/active_scales.cpp:108-112`), so
		// the two presses must not have ascended; the Back at the end is what leaves, and it leaves to
		// the root with its own cursor still on the row the descent came from.
		expect(menu_scene().focused() == &nodes::root_song).to_be_true();
		expect(cursor_now()).to_equal(4);
	});

	it("ContextMenuItem: a reset item pushes the context menu, and the menu goes up a level when it closes", _ {
		reset_globals();
		spec::SongFixture song = spec::make_synth_song();
		const MenuContext ctx = menu::context_for(*song.song, nullptr, 0, false, false, false, false);
		(void)menu::assign_node_ids(nodes::kAllRoots);
		menu_scene().open(nodes::reset_settings, ctx);

		const uitest::RunResult result = uitest::expect_script_goldens(
		    self, "menu_reset", scenes::scene_registry(), deluge::ui::shell::Services{}, opts_for(nodes::reset_settings));
		expect(result.snapshots).to_equal(uint32_t{3});

		// Row 0 of `kResetSettingsChildren` is `reset_flash`, so that is the descriptor the push
		// named. The scene keeps `id_` after it closes, which is what lets this be checked at all.
		expect(context_menu::context_menu_scene().descriptor_id() == context_menu::DescriptorId::ResetFlash)
		    .to_be_true();
		// The Select detent inside the window moved nothing: `ResetSettings::getOptions()` is a single
		// `STRING_FOR_OK` (task 6's correction -- not a NO/YES pair), and `step()` clamps.
		expect(static_cast<int>(context_menu::context_menu_scene().selection().option)).to_equal(0);
		// `shouldGoUpOneLevelOnBegin` (`gui/menu_item/reset_settings/reset.h:29-33`) as
		// `MenuState::go_up_on_resume`: the resume ascends OUT of the item the press descended into,
		// so the list is on screen again rather than the item that opened the window. (`reset_settings`
		// is this run's path root, so that ascend lands back on it -- one level up from `reset_flash`.)
		expect(menu_scene().focused() == &nodes::reset_settings).to_be_true();
	});

	it("ContextMenuScene: the audio-source descriptor opens on its own initial row and walks", _ {
		reset_globals();
		spec::AudioSongFixture song = spec::make_audio_clip_song();
		const MenuContext ctx = menu::context_for(*song.song, song.clip, 0, false, false, false, false);
		(void)menu::assign_node_ids(nodes::kAllRoots);
		menu_scene().open(nodes::root_audio_clip, ctx);

		const uitest::RunResult result =
		    uitest::expect_script_goldens(self, "context_menu", scenes::scene_registry(),
		                                  deluge::ui::shell::Services{}, opts_for(nodes::root_audio_clip));
		expect(result.snapshots).to_equal(uint32_t{5});

		expect(context_menu::context_menu_scene().descriptor_id() == context_menu::DescriptorId::AudioSource)
		    .to_be_true();
		// `initial_audio_source` (`gui/context_menu/audio_input_selector.cpp:87-127`
		// setupAndCheckAvailability) opened it on the output's own `inputChannel`, and the two Select
		// detents walked from there -- `step()`'s clamp is at 7, which this does not reach.
		expect(static_cast<int>(context_menu::context_menu_scene().selection().option)).to_equal(3);
		// Backed out without accepting, so the menu is back where the press left it.
		expect(menu_scene().focused() == &nodes::root_audio_clip).to_be_true();
		expect(cursor_now()).to_equal(1);
	});

	it("RenameScene: the menu pushes it, the cursor blinks, a held backspace repeats, and enter commits", _ {
		reset_globals();
		spec::SongFixture song = spec::make_synth_song();
		const MenuContext ctx = menu::context_for(*song.song, song.clip, 0, false, false, false, false);
		(void)menu::assign_node_ids(nodes::kAllRoots);
		menu_scene().open(nodes::edit_name, ctx);
		song.clip->name = "";

		const uitest::RunResult result = uitest::expect_script_goldens(
		    self, "rename", scenes::scene_registry(), deluge::ui::shell::Services{}, opts_for(nodes::edit_name));
		expect(result.snapshots).to_equal(uint32_t{12});

		// Typed "ABAD", then a held backspace: the press itself deletes one
		// (`gui/ui/qwerty_ui.cpp:219`), the first repeat fires 500 ms later (`:220`) and the second
		// 80 ms after that (`:452`), before the pad is released at 1540 ms.
		//
		// Three deletions, not two (fix round 1). One repeat proves the hold is armed; it says
		// nothing about `kBackspaceRepeatMs`, the 80 ms steady state -- and that interval's only
		// other cover is `rename_scene_spec.cpp`, whose hand-placed ticks all land on grid passes and
		// which was therefore blind to the bug this script found. A regression that fired once and
		// then stalled would have passed both. "A" is the discriminating answer at both ends: a
		// missing arming delay would have emptied the buffer, a missing cadence would have left "AB".
		expect(std::string{song.clip->name}).to_equal(std::string{"A"});
		// The enter pad committed and popped the editor, so the menu owns the screen again -- one level
		// up from the item that opened it, `EditNameItem` being a `pending_scene` opener like the five
		// reset items.
		expect(menu_scene().focused() == &nodes::edit_name).to_be_true();
	});

	it("MidiCommandItem: the learned readout, the LEARN blink, and a select turn that unlearns", _ {
		reset_globals();
		spec::SongFixture song = spec::make_synth_song();
		const MenuContext ctx = menu::context_for(*song.song, nullptr, 0, false, false, false, false);
		(void)menu::assign_node_ids(nodes::kAllRoots);
		const CommandSlots saved = CommandSlots::capture();
		CommandSlots::clear_all();
		// Focus the item to learn onto it -- `MenuScene::learn_cc` forwards to `focused()` -- and then
		// re-root on the list the script actually walks. The slot itself is
		// `midiEngine.globalMIDICommands`, a global, so the binding survives the re-rooting.
		menu_scene().open(nodes::midi_command_play, ctx);
		// Channel 3 + `IS_A_CC`, CC 42 -- the same binding `tables_3_spec`'s learned-readout golden
		// uses, so the three-line shape in `entered` is the one that file already pins at unit level.
		expect(menu_scene().learn_cc(fake_cable(), 3, 42, 127)).to_be_true();
		menu_scene().open(nodes::midi_commands, ctx);

		const uitest::RunResult result =
		    uitest::expect_script_goldens(self, "menu_midi_learn", scenes::scene_registry(),
		                                  deluge::ui::shell::Services{}, opts_for(nodes::midi_commands));
		expect(result.snapshots).to_equal(uint32_t{4});

		// `Command::selectEncoderAction` (`gui/menu_item/midi/command.cpp:88-91`) clears on ANY motion.
		// The script turns once each way; the second turn is the step with no snapshot, and this is
		// what stands in for it -- the slot is still empty, not re-learned by the opposite direction.
		expect(midiEngine.globalMIDICommands[static_cast<int32_t>(GlobalMIDICommand::PLAY)].containsSomething())
		    .to_be_false();
		// And it cleared the right one of the twelve.
		expect(midiEngine.globalMIDICommands[static_cast<int32_t>(GlobalMIDICommand::RECORD)].containsSomething())
		    .to_be_false();
		expect(menu_scene().focused() == &nodes::midi_commands).to_be_true();
		saved.restore();
	});

	it("CvSelectionItem: the picked row becomes MenuContext::source_index before the chain reads it", _ {
		reset_globals();
		spec::SongFixture song = spec::make_synth_song();
		const MenuContext ctx = menu::context_for(*song.song, nullptr, 0, false, false, false, false);
		(void)menu::assign_node_ids(nodes::kAllRoots);
		menu_scene().open(nodes::cv_selection, ctx);

		const uitest::RunResult result =
		    uitest::expect_script_goldens(self, "menu_cv_selection", scenes::scene_registry(),
		                                  deluge::ui::shell::Services{}, opts_for(nodes::cv_selection));
		expect(result.snapshots).to_equal(uint32_t{4});

		// The press navigated (`cv_selection_chain`), so it did not ascend; the Back brought the
		// selector back with its cursor intact, which is `MenuState::list[depth]` surviving a descend.
		expect(menu_scene().focused() == &nodes::cv_selection).to_be_true();
		expect(cursor_now()).to_equal(1);
	});

	it("PatchCablesItem: the mod matrix opens the selected cable's strength page, and that page edits", _ {
		reset_globals();
		spec::SongFixture song = spec::make_synth_song();
		const MenuContext ctx = menu::context_for(*song.song, song.clip, 0, false, false, false, false);
		(void)menu::assign_node_ids(nodes::kAllRoots);

		// A synth fixture already has cables of its own (the golden's NOTE and VELO rows), so the
		// interesting assertion is not "a cable moved" but "the SELECTED one moved and the others did
		// not". One more is added here so the list is a real list rather than a pair -- the same cable
		// `tables_2_spec`'s own mod-matrix goldens install.
		Song* const previous_song = currentSong;
		currentSong = song.song;
		ParamDescriptor plain;
		plain.setToHaveParamAndSource(deluge::modulation::params::LOCAL_LPF_FREQ, PatchSource::ENVELOPE_1);
		expect(bindings::patch_cable_write(ctx, plain, 1234)).to_be_true();
		PatchCableSet* const set = ctx.params->getPatchCableSetAllowJibberish();
		expect(set != nullptr).to_be_true();
		ParamDescriptor destination;
		destination.setToHaveParamOnly(deluge::modulation::params::LOCAL_LPF_FREQ);
		const uint8_t added = set->getPatchCableIndex(PatchSource::ENVELOPE_1, destination);
		expect(added != kNoSelection).to_be_true();
		expect(added != uint8_t{0}).to_be_true(); // not the row the cursor opens on
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
		    uitest::expect_script_goldens(self, "menu_patch_cables", scenes::scene_registry(),
		                                  svc.slots(), opts);
		expect(result.snapshots).to_equal(uint32_t{5});

		// The script's two saturating `Horizontal` steps left the digit cursor on the hundreds place
		// whatever an earlier block's page had left in `MenuState::decimal_pos`, so "one gold detent
		// is 1.00" is pinned rather than inherited -- and so is the arithmetic behind the two frames
		// the fix-round-1 font change is judged on.
		expect(static_cast<int>(menu_scene().state().decimal_pos)).to_equal(2);
		// `decimal_value` now edits on the select encoder, not the gold knobs (`ui/oled/values.cpp`'s
		// `select_delta`); fix round 1's follow-up review found this script's two OLD `GoldUpper`
		// turns on the strength page no longer tested anything, since `decimal_value` claims no gold
		// input at all now. Both are `encoder Select` turns now, sized at double the old gold script
		// number (`+4`, `-64` in place of `+2`, `-32`) -- a select detent is not doubled by
		// `edges_per_detent` the way an `Accelerating` gold knob's magnitude is, so this reproduces
		// the exact same internal delta and the cable the press selected moves again.
		expect(set->patchCables[0].param.getCurrentValue() != selected_before).to_be_true();
		// The other cable never had a turn aimed at it either way, which is still worth pinning: a
		// port that somehow wrote through the wrong `patch_cables_chain` entry would show up here.
		expect(set->patchCables[added].param.getCurrentValue()).to_equal(added_before);
		expect(menu_scene().focused() == &nodes::patch_cables).to_be_true();
		currentSong = previous_song;
	});

	it("ActionItem: the press files a MenuAction and leaves the parent list exactly where it was", _ {
		reset_globals();
		spec::SongFixture song = spec::make_synth_song();
		const MenuContext ctx = menu::context_for(*song.song, nullptr, 0, false, /*session_or_arranger_root=*/true,
		                                                  /*session_view_root=*/true, false);
		(void)menu::assign_node_ids(nodes::kAllRoots);
		menu_scene().open(nodes::stem_export, ctx);
		(void)menu_scene().take_action(); // drain anything an earlier block left in the one-slot outbox

		const uitest::RunResult result = uitest::expect_script_goldens(
		    self, "menu_action", scenes::scene_registry(), deluge::ui::shell::Services{}, opts_for(nodes::stem_export));
		expect(result.snapshots).to_equal(uint32_t{2});

		// The press has no golden -- the frame after it repeats `idle` exactly, which is the whole
		// point of `shouldEnterSubmenu() == false` (`gui/ui/sound_editor.cpp:420-423`). This is where
		// it is visible instead.
		const MenuActionRequest taken = menu_scene().take_action();
		expect(taken.action == MenuAction::StartStemExport).to_be_true();
		expect(static_cast<int>(taken.arg)).to_equal(0);
		// And the ascend was exactly one level: the EXPORT AUDIO list is focused again, not something
		// above it. (This run's path is rooted on that list, so a second ascend would have been a
		// visible no-op rather than a wrong page -- which is why the *cursor* is checked too.)
		expect(menu_scene().focused() == &nodes::stem_export).to_be_true();
		expect(cursor_now()).to_equal(1);
	});

	it("PerformEditorItem: VALUE files a request and stays, DISABLED files nothing and ascends", _ {
		reset_globals();
		spec::SongFixture song = spec::make_synth_song();
		const MenuContext ctx =
		    menu::context_for(*song.song, nullptr, 0, /*performance_view=*/true, /*session_or_arranger_root=*/true, false, false);
		(void)menu::assign_node_ids(nodes::kAllRoots);
		// `on_focus` seeds the cursor from this mirror (`nodes/actions.cpp` `performance_editing_mode`),
		// and the VALUE press writes it -- so it is saved and put back like any other global.
		const PerformanceEditingMode saved_mode = menu::performance_editing_mode();
		menu::set_performance_editing_mode(PerformanceEditingMode::DISABLED);
		// From the root, for `menu_scales`' reason: the DISABLED arm's whole claim is that it ascends,
		// and at depth 0 an ascend draws the same frame as staying put.
		menu_scene().open(nodes::root_performance, ctx);
		(void)menu_scene().take_action();

		const uitest::RunResult result =
		    uitest::expect_script_goldens(self, "menu_perform_editor", scenes::scene_registry(),
		                                  deluge::ui::shell::Services{}, opts_for(nodes::root_performance));
		expect(result.snapshots).to_equal(uint32_t{5});

		// The VALUE press filed `OpenPerformanceView` carrying the chosen mode as its `arg`; the
		// DISABLED press that follows files nothing (`editing_mode.h:51-53` returns nullptr), so this
		// one-slot outbox still holds the first one.
		const MenuActionRequest taken = menu_scene().take_action();
		expect(taken.action == MenuAction::OpenPerformanceView).to_be_true();
		expect(static_cast<int>(taken.arg)).to_equal(static_cast<int>(PerformanceEditingMode::VALUE));
		// The DISABLED arm ascended, which is the half a golden alone could not distinguish from a
		// page that merely redrew.
		expect(menu_scene().focused() == &nodes::root_performance).to_be_true();
		menu::set_performance_editing_mode(saved_mode);
	});

	// -----------------------------------------------------------------------------------------
	// E3 task 11. The four scenes/pages this plan shipped; see the block above `ScriptDisplay`
	// for why three of the four are entered through the shortcut-pad jump.

	it("the per-device MIDI page: the cable, the direction and the zone all survive to the write", _ {
		reset_globals();
		spec::SongFixture song = spec::make_synth_song();
		const MenuContext ctx = menu::context_for(*song.song, nullptr, 0, false, false, false, false);
		(void)menu::assign_node_ids(nodes::kAllRoots);

		const SavedCables saved = SavedCables::capture();
		// `connected_cables` walks `kLowestDeviceNum..hostedMIDIDevices.size()` and keeps every cable
		// whose `connectionFlags` is non-zero (`nodes/dynamic.cpp:155-169`). With no hosted devices
		// the walk is the three built-in cables, so lighting two of them is a two-row list -- and two
		// is the number that matters: a page that read a *global* cable rather than
		// `MenuContext::midi_cable` would pass a one-row list.
		MIDICable& din = MIDIDeviceManager::dinMIDIPorts;
		MIDICable& usb = MIDIDeviceManager::upstreamUSBMIDICable1;
		din.connectionFlags = 1;
		usb.connectionFlags = 1;
		MIDIDeviceManager::upstreamUSBMIDICable2.connectionFlags = 0;
		for (MIDICable* c : {&din, &usb}) {
			for (int32_t p = 0; p < 2; p++) {
				c->ports[p].mpeLowerZoneLastMemberChannel = 0;
				c->ports[p].mpeUpperZoneLastMemberChannel = 15; // 15 means "off" (`midi_device.h:62`)
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
		    uitest::expect_script_goldens(self, "menu_midi_device", scenes::scene_registry(),
		                                  svc.slots(), opts);
		expect(result.snapshots).to_equal(uint32_t{10});

		// The script picked the SECOND row, the OUT direction and the UPPER zone -- the non-default
		// arm of all three -- and then turned the select encoder twice on the member-channel count.
		// `int_value` now edits on the select encoder, not the gold knobs (`ui/oled/values.cpp`'s
		// `select_delta`); fix round 1's follow-up review found this script's two OLD `GoldUpper`
		// turns no longer tested anything, since `int_value` claims no gold input at all now. Both
		// are `encoder Select +2` turns now -- and unlike the gold knobs, the select encoder is not
		// `EncoderKind::Accelerating`, so it has no wiggle debounce to swallow the first one: both
		// turns act, landing the count at the same `0 -> 4` the gold turns used to (one of theirs
		// used to be swallowed; neither of these is). `commit` writes `15 - 4` into the UPPER zone's
		// field (`mpe/zone_num_member_channels.cpp:66-77`).
		expect(static_cast<int>(usb.ports[1].mpeUpperZoneLastMemberChannel)).to_equal(11);
		// And to that one field only. Every other one of the eight is still at its fixture value, so
		// a hook that reached for the wrong port, the wrong zone or the wrong cable fails here rather
		// than round-tripping through itself.
		expect(static_cast<int>(usb.ports[0].mpeUpperZoneLastMemberChannel)).to_equal(15);
		expect(static_cast<int>(usb.ports[0].mpeLowerZoneLastMemberChannel)).to_equal(0);
		expect(static_cast<int>(usb.ports[1].mpeLowerZoneLastMemberChannel)).to_equal(0);
		expect(static_cast<int>(din.ports[0].mpeUpperZoneLastMemberChannel)).to_equal(15);
		expect(static_cast<int>(din.ports[1].mpeUpperZoneLastMemberChannel)).to_equal(15);
		// The `MenuState` selections the four frames above were drawn from are still the ones the
		// write used: `mpe_direction` OUT and `mpe_zone` UPPER (`io/midi/midi_device.h:33-36`).
		expect(static_cast<int>(menu_scene().state().mpe_direction)).to_equal(1);
		expect(static_cast<int>(menu_scene().state().mpe_zone)).to_equal(1);
		// The Back at the end left the zone selector, not the device list: the chain descended twice
		// and only one level came back.
		expect(menu_scene().focused() == &nodes::mpe_zone).to_be_true();
		saved.restore();
	});

	it("BrowserScene: a shortcut jump opens the sample browser, which lists, descends and ascends", _ {
		reset_globals();
		install_display();
		ScratchCard card;
		spec::QuietAudio quiet;
		spec::SongFixture song = spec::make_synth_song();
		(void)install_sample_source(song, nullptr); // an empty holder -> the descriptor's default dir
		const MenuContext ctx = menu::context_for(*song.song, song.clip, 0, false, false, false, false);
		(void)menu::assign_node_ids(nodes::kAllRoots);
		menu_scene().open(nodes::sound_root, ctx);

		const uitest::RunResult result = uitest::expect_script_goldens(
		    self, "browser", scenes::scene_registry(), deluge::ui::shell::Services{}, opts_for(nodes::sound_root));
		expect(result.snapshots).to_equal(uint32_t{7});

		// The script descended into KICKS, typed one character there, and came back out -- so the
		// scene really did run all three listings, and the last BACK (`Browser::backButtonAction`'s
		// top-of-tree arm, `browser.cpp:1283`) closed it. The menu owns the screen again, on the item
		// the shortcut jump re-rooted the path onto; `MenuState::go_up_on_resume` (`nodes/sample.h`,
		// legacy's `shouldGoUpOneLevelOnBegin`) ascends out of it, which at depth 0 is a no-op.
		expect(menu_scene().focused() == &nodes::file0_selector).to_be_true();
		expect(browser::browser_scene().browser_id() == browser::BrowserId::Sample).to_be_true();
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
		const MenuContext ctx = menu::context_for(*song.song, song.clip, 0, false, false, false, false);
		(void)menu::assign_node_ids(nodes::kAllRoots);
		menu_scene().open(nodes::sound_root, ctx);

		const uitest::RunResult result = uitest::expect_script_goldens(
		    self, "marker", scenes::scene_registry(), deluge::ui::shell::Services{}, opts_for(nodes::sound_root));
		expect(result.snapshots).to_equal(uint32_t{7});

		// The pad press moved the START marker to column 6's first sample. At the opening zoom
		// (`WaveformNav::opened` with no remembered view: scroll 0, zoom = max) that is
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
		// BACK closed the editor, so the menu owns the screen again, on the item the jump landed on.
		expect(menu_scene().focused() == &nodes::sample0_start).to_be_true();
		// The held-marker gesture did not survive `on_exit` (`marker_scene.cpp:197-205`).
		expect(marker::marker_scene().holding_marker()).to_be_false();
	});

	it("RecorderScene: both doors arm a real recorder, and only one of them closes tidily", _ {
		reset_globals();
		install_display();
		spec::QuietAudio quiet;
		spec::SdRoot scratch{(std::filesystem::temp_directory_path() / "deluge_menu_scripts_rec").string()};
		spec::SongFixture song = spec::make_synth_song();
		MultisampleRange* range = install_sample_source(song, nullptr);
		const MenuContext ctx = menu::context_for(*song.song, song.clip, 0, false, false, false, false);
		(void)menu::assign_node_ids(nodes::kAllRoots);
		menu_scene().open(nodes::sound_root, ctx);
		// `RecorderScene::can` refuses while a foreground recording is live, and this is the storage
		// nine subsystems read (`recorder_scene.h`). Asserted rather than cleared: if an earlier
		// block left one armed, that is the bug this task was told to look for, not something to
		// paper over.
		expect(static_cast<int>(AudioEngine::foregroundRecordingSource())).to_equal(
		    static_cast<int>(AudioInputChannel::NONE));
		expect(recorder::recorder_scene().recording()).to_be_false();

		const uitest::RunResult result = uitest::expect_script_goldens(
		    self, "recorder", scenes::scene_registry(), deluge::ui::shell::Services{}, opts_for(nodes::sound_root));
		expect(result.snapshots).to_equal(uint32_t{7});

		// Armed for real TWICE -- once through SOUND -> ACTIONS -> RECORD TO SAMPLE, once through the
		// shortcut pad -- and gone again both times. The second entry is itself an assertion: it
		// could only succeed because the first close ran `finish_recording()` in full, since
		// `on_enter` refuses on a leftover `recorder_` and `can()` refuses on a leftover published
		// recording. With nothing captured,
		// `endSyncedRecording` takes its `numSamplesCaptured == 0` abort arm
		// (`model/sample/sample_recorder.cpp:1243-1246`), `after_render`'s completion arm sees
		// `status >= COMPLETE` and `finish_recording()` unpublishes and discards -- so this pair says
		// the whole close path ran, not merely that nothing is left over.
		expect(recorder::recorder_scene().recording()).to_be_false();
		expect(recorder::recorder_scene().stopping()).to_be_false();
		// **This is the assertion that made `script_runner.h`'s carried gap stop being latent.**
		// `RecorderScene::on_enter` refuses on its own leaked `recorder_` (`recorder_scene.cpp:149`)
		// and `setup_recording` publishes a live foreground recording nine other subsystems read, so
		// a run that ended with the scene still on the stack would leave both behind for every later
		// spec in the process. `run_script` tears its stack down now, which is what makes this hold
		// on the paths where the scene does NOT close itself.
		expect(static_cast<int>(AudioEngine::foregroundRecordingSource())).to_equal(
		    static_cast<int>(AudioInputChannel::NONE));
		// An aborted recording attaches nothing: `after_render`'s guard skips `attach_sample()` for
		// `RecorderStatus::ABORTED`, so the range's holder is still empty. A port that attached
		// unconditionally would have hung `recorder_->sample` -- a `Sample` with no captured frames
		// and a file that `abort()` deletes -- on the oscillator.
		expect(range->sampleHolder.audioFile == nullptr).to_be_true();
		// The scene closed itself both times. Where that leaves the menu is what the two closes
		// differ in, and the run ends on the second: the shortcut jump re-rooted the path onto the
		// item, so the ascend `go_up_on_resume` asks for happens at depth 0 and does nothing
		// (`menu_scene.cpp:1570-1572`). `after_stop`, seven hundred milliseconds earlier, is the
		// other outcome -- the ACTIONS list, cursor still on the row the descent came from.
		expect(menu_scene().focused() == &nodes::sample0_recorder).to_be_true();
		expect(static_cast<int>(menu_scene().state().depth)).to_equal(0);
	});
});
// clang-format on

int menu_scripts_spec(int argc, char** const argv) {
	return CppSpec::parse(argc, argv).add_specs(menu_scripts).exec().is_success() ? EXIT_SUCCESS : EXIT_FAILURE;
}
