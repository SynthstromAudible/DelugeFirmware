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
/// path from `SceneId::arg` (`node_by_id`) but never touches `ctx_` (`menu_scene.cpp:272-292`), so
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
/// @par Every script here runs with `Services{}`, and some of these goldens are owed a re-record
///
/// All 24 `expect_script_goldens` calls in this file pass `deluge::ui::shell::Services{}` -- three
/// `NullService`s. That is E4's own ruling and not an oversight (three plans of goldens were
/// recorded that way; re-recording them as a side effect of building the services would be a diff
/// nobody could review), but it means these frames describe a shell with no mod-knob service in it,
/// and spec F installs one. Twelve of these scripts drive 19 gold-knob turns between them.
///
/// **What decides whether a given step moves is the PAGE, not the router's chain**, and the split
/// was measured for E4's final review rather than argued:
///
///   * Where the focused page's widget CLAIMS the gold turn -- `menu_int`'s Int leaf, whose
///     `after_gold_turn` golden really does show the value move -- `Router::route_post_render`
///     drops the event before `Router::run_chain` is entered (`src/shell/README.md`), so
///     `services::ModKnobs` never sees it and installing the services changes nothing.
///     `scene_scripts_spec.cpp`'s `menu_gold_turn` records exactly that, on this same node, with
///     the real three installed.
///   * Where the page reads no edit encoder -- `menu_decimal`'s first `GoldUpper` turn is the
///     worked example, and E3 task 10 dropped its snapshot for being byte-identical to the frame
///     before -- the turn is unclaimed, falls the whole way through the chain, and is dropped.
///     With a real `ModKnobs` it will write the mod param and push a `ParamPopupScene` over the
///     menu: a second `scene` line in `.state` and a value strip in the `.pbm`.
///
/// Classify each of the 19 as you re-record it. `src/scenes/README.md`'s carry table and the
/// migration spec's §10.2 blocker list both carry this row.

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
		const uitest::RunResult result =
		    uitest::expect_script_goldens(self, "menu_int", scenes::scene_registry(), deluge::ui::shell::Services{}, opts);
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
		// GoldUpper +2 detents -> `edges_per_detent`(2) * 2 = 4 raw edges (an Accelerating knob's
		// magnitude IS the raw edge count -- `script_runner.h`'s uniform detent->edge conversion,
		// `menu_scene_spec.cpp`'s `Rig::gold` note); `int_value`'s coarse step is `range.step` (1),
		// so the value should have moved by exactly 4, clamped to the node's own `IntRange{0,15,1}`.
		// The exact `+4` (not `+8` or anything else) is also the dropped snapshots' own proof, same
		// shape as the `menu_shortcut` script below: had the Select turn or press moved it too, this
		// total would not match.
		expect(bindings::read(nodes::saturation, ctx)).to_equal(std::min(before + 4, 15));
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
		const uitest::RunResult result = uitest::expect_script_goldens(
		    self, "menu_decimal", scenes::scene_registry(), deluge::ui::shell::Services{}, opts);
		// Five, not six: the script's first GoldUpper turn lands on a page that reads no edit
		// encoder, so its snapshot was byte-identical to `after_select_press` and was dropped (fix
		// round 1) -- the *step* stays, because the "unchanged" assertion below is about it.
		expect(result.snapshots).to_equal(uint32_t{5});

		// E2 task 9: `source0_transpose` is one of the 53 `PatchedParam` nodes, so its select press
		// opens the source picker rather than ascending (`patched_param.cpp:32-40`). The script's
		// first GoldUpper turn therefore lands on that picker and edits nothing; the Back brings the
		// leaf back and the script's second GoldUpper turn is the one that moves the value.
		expect(menu_scene().focused() == &nodes::source0_transpose).to_be_true();
		expect(static_cast<uint32_t>(menu_scene().focused()->name)).to_equal(static_cast<uint32_t>(sid(String::STRING_FOR_TRANSPOSE)));
		// `decimal_value`'s coarse amount is `10^edit_pos`, and `edit_pos` starts at 0, so the same
		// 4-raw-edge GoldUpper turn moves the value by 4 * 1, clamped to `kTransposeRange`.
		expect(bindings::read(nodes::source0_transpose, ctx)).to_equal(std::min(before + 4, 9600));
		// Review round 1, Minor 6: `after_select_turn`'s pixel diff against `idle` is the cursor's
		// own time-driven blink (`decimal_value`'s `Blink`), not an effect of the Select turn -- the
		// script's shared "Select +1" step is claimed-but-unused here (`render_leaf_decimal`'s
		// `f.claim_turn()`), so that snapshot alone verifies nothing about input. What DOES verify
		// something: `edit_pos` moves only on the "Horizontal" encoder, which this script never
		// touches, so it must still be 0 after Select turns, a select press/release and a GoldUpper
		// turn -- proving Select really is inert for a Decimal screen's cursor, not merely that its
		// pixels happen to look inert at the two moments a screenshot was taken.
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
		const uitest::RunResult result =
		    uitest::expect_script_goldens(self, "menu_enum", scenes::scene_registry(), deluge::ui::shell::Services{}, opts);
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
		const uitest::RunResult result = uitest::expect_script_goldens(
		    self, "menu_toggle", scenes::scene_registry(), deluge::ui::shell::Services{}, opts);
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
		reset_globals();
		spec::SongFixture song = spec::make_synth_song();
		const MenuContext ctx = menu::context_for(*song.song, song.clip, 0, false, false, false, false);
		(void)menu::assign_node_ids(nodes::kAllRoots);
		menu_scene().open(nodes::lpf, ctx);

		const int32_t before = bindings::read(nodes::lpf_freq, ctx);

		uitest::RunOptions opts{
		    .tick_ms = 10,
		    .board = &deluge::ui::shell::kDelugeBoard,
		    .profile = &deluge::scenes::deluge_profile(),
		    .initial_scene = deluge::ui::shell::SceneId{deluge::ui::shell::SceneKind::FirstApp, menu::node_id(nodes::lpf)},
		};
		const uitest::RunResult result = uitest::expect_script_goldens(
		    self, "menu_submenu_horizontal", scenes::scene_registry(), deluge::ui::shell::Services{}, opts);
		expect(result.snapshots).to_equal(uint32_t{5});

		expect(menu_scene().focused() == &nodes::lpf).to_be_true();
		expect(static_cast<uint32_t>(menu_scene().focused()->name)).to_equal(static_cast<uint32_t>(sid(String::STRING_FOR_LPF)));
		// The Select detent moves the column focus from MODE (0) to FREQ (1); a select press on a
		// value column raises the notice popup rather than navigating (`horizontal_menu.cpp:657-661`
		// port), so GoldUpper's turn still lands on FREQ -- `render_horizontal`'s own coarse step is
		// `range.step` (1) too, so the same 4-raw-edge turn moves it by 4.
		expect(static_cast<int>(menu_scene().state().columns[0].focused)).to_equal(1);
		expect(bindings::read(nodes::lpf_freq, ctx)).to_equal(std::min(before + 4, 50));
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
		const uitest::RunResult result = uitest::expect_script_goldens(
		    self, "menu_shortcut", scenes::scene_registry(), deluge::ui::shell::Services{}, opts);
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
		expect(bindings::read(nodes::lpf_freq, ctx)).to_equal(std::min(before + 4, 50));
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
		const uitest::RunResult result = uitest::expect_script_goldens(
		    self, "menu_range_permission", scenes::scene_registry(), deluge::ui::shell::Services{}, opts);
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

		const uitest::RunResult result = uitest::expect_script_goldens(
		    self, "menu_sync_level", scenes::scene_registry(), deluge::ui::shell::Services{}, opts_for(nodes::delay_sync));
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

		const uitest::RunResult result = uitest::expect_script_goldens(
		    self, "menu_chain", scenes::scene_registry(), deluge::ui::shell::Services{}, opts_for(nodes::unison_count));
		expect(result.snapshots).to_equal(uint32_t{6});

		// `CountToStereoSpread::selectButtonPress` (`gui/menu_item/unison/count.h:76-79`) navigates
		// *into* the spread, so the press must not have ascended -- and the second gold turn, after
		// the Back, must have landed on the count again.
		expect(menu_scene().focused() == &nodes::unison_count).to_be_true();
		// Two gold turns of +2 detents each reach the count, one reaches the spread. An `Accelerating`
		// knob's magnitude is its raw edge count (`script_runner.h`), `edges_per_detent` is 2, and
		// `int_value`'s coarse step is `range.step` -- so +4 per turn, clamped by the node's own range.
		// The first turn is +2 detents and the second, after the Back, is -2 -- downwards because the
		// first one already clamped at the top and a second climb would have drawn the identical frame.
		const int32_t after_first = std::min(count_before + 4, count_range.max);
		expect(after_first).to_equal(count_range.max); // the clamp really did happen, so -4 is visible
		expect(bindings::read(nodes::unison_count, ctx)).to_equal(std::max(after_first - 4, count_range.min));
		expect(bindings::read(nodes::unison_spread, ctx)).to_equal(std::min(spread_before + 4, spread_range.max));
	});

	it("Range: nodes::default_tempo picks an edge with the horizontal encoder and moves it with the gold one", _ {
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

		const uitest::RunResult result = uitest::expect_script_goldens(
		    self, "menu_range", scenes::scene_registry(), deluge::ui::shell::Services{}, opts_for(nodes::default_tempo));
		expect(result.snapshots).to_equal(uint32_t{5});

		// The page opened already on the LEFT edge (`kDefaultTempoSpec.focus_left_when_split`, legacy's
		// `IntegerRange::beginSession`), so the first gold turn moved the lower end; Horizontal `+1`
		// then selected the RIGHT one (`ui/oled/values.cpp:336-341`) and the second moved the upper.
		// +4 raw edges * `range.step` (1) each. A port that ignored the edge would have moved one end
		// twice and left the other at its fixture value.
		expect(FlashStorage::defaultRanges[FlashStorage::DEFAULT_RANGE_TEMPO].lower).to_equal(int32_t{104});
		expect(FlashStorage::defaultRanges[FlashStorage::DEFAULT_RANGE_TEMPO].upper).to_equal(int32_t{204});
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
		const uitest::RunResult result =
		    uitest::expect_script_goldens(self, "menu_patch_cables", scenes::scene_registry(),
		                                  deluge::ui::shell::Services{}, opts_for(nodes::patch_cables));
		expect(result.snapshots).to_equal(uint32_t{5});

		// The script's two saturating `Horizontal` steps left the digit cursor on the hundreds place
		// whatever an earlier block's page had left in `MenuState::decimal_pos`, so "one gold detent
		// is 1.00" is pinned rather than inherited -- and so is the arithmetic behind the two frames
		// the fix-round-1 font change is judged on.
		expect(static_cast<int>(menu_scene().state().decimal_pos)).to_equal(2);
		// The gold turn on the strength page wrote through to the cable the press selected...
		expect(set->patchCables[0].param.getCurrentValue() != selected_before).to_be_true();
		// ...and to that one only, which is what makes `patch_cables_chain` picking the right cable a
		// tested claim rather than an assumed one.
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
		// point of `shouldEnterSubmenu() == false` (`gui/ui/sound_editor.cpp:382-385`). This is where
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
		// DISABLED press that follows files nothing (`editing_mode.h:50-52` returns nullptr), so this
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

		const uitest::RunResult result =
		    uitest::expect_script_goldens(self, "menu_midi_device", scenes::scene_registry(),
		                                  deluge::ui::shell::Services{}, opts_for(nodes::midi_devices));
		expect(result.snapshots).to_equal(uint32_t{10});

		// The script picked the SECOND row, the OUT direction and the UPPER zone -- the non-default
		// arm of all three -- and then turned the gold knob twice. Exactly ONE of those two turns
		// acts: the first is eaten by `InputLowering::action_gold_turn`'s wiggle debounce, which the
		// script's own note explains. `int_value`'s coarse step is `range.step` (1) and an
		// `Accelerating` knob's magnitude is its raw edge count (4 for +2 detents), so the edited
		// value is 4 and not 8 -- a debounce that stopped swallowing would show up here as `7`
		// rather than as a frame nobody could read. `commit` then writes `15 - 4` into the UPPER
		// zone's field (`mpe/zone_num_member_channels.cpp:66-77`).
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
		// (`menu_scene.cpp:1091-1093`). `after_stop`, seven hundred milliseconds earlier, is the
		// other outcome -- the ACTIONS list, cursor still on the row the descent came from.
		expect(menu_scene().focused() == &nodes::sample0_recorder).to_be_true();
		expect(static_cast<int>(menu_scene().state().depth)).to_equal(0);
	});
});
// clang-format on

int menu_scripts_spec(int argc, char** const argv) {
	return CppSpec::parse(argc, argv).add_specs(menu_scripts).exec().is_success() ? EXIT_SUCCESS : EXIT_FAILURE;
}
