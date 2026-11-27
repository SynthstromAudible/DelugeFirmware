/// @file
/// @brief The ten menu items that open a card-backed scene (`ui/scenes/menu/nodes/sample.h`): which
///        node carries which item, and what each one actually opens when the menu focuses it.
///
/// Two groups, and they are deliberately different kinds of claim:
///
///   * **The mapping**, asserted by *identity* -- every one of the ten nodes carries the item its
///     legacy class became, with the source index and marker that class was constructed with, and
///     no other node in `nodes::kAllRoots` carries one of these items. Ten near-identical items
///     across two macro-generated oscillator pages is exactly the shape where a positional check
///     looks strong and pins nothing, so the observed table is compared whole and a deliberately
///     swapped copy is asserted **not** to match.
///   * **The opening**, driven end-to-end through a real `Shell`: the menu is opened on the node,
///     ticked, and the scene that lands on the stack is asked what it opened on. Each of the three
///     scenes is discriminated per source by a fact that differs between the two oscillators -- a
///     different folder, a different sample length, a range that exists on one source and not the
///     other -- so an item wired to the wrong source fails rather than passing on symmetry.
///
/// Plus relevance: the file selector, loop start and loop end nodes' relevance
/// is compared with the reference rows main's `isRelevant()` overrides gave over **eight** oscillator
/// configurations rather than the single fixture `tables_1_spec.cpp` uses, the port asked through a
/// `MenuContext` built from the model state the rows were taken in.

#include "gui/ui/menu_view_of.h"
#include "project/sample_marker.h"
#include "ui/scenes/browser/browser_scene.h"
#include "ui/scenes/marker/marker_scene.h"
#include "ui/scenes/menu/bindings.h"
#include "ui/scenes/menu/context.h"
#include "ui/scenes/menu/menu_scene.h"
#include "ui/scenes/menu/node.h"
#include "ui/scenes/menu/node_id.h"
#include "ui/scenes/menu/nodes/nodes.h"
#include "ui/scenes/menu/nodes/sample.h"
#include "ui/scenes/menu/relevance.h"
#include "ui/scenes/menu/shortcuts.h"
#include "ui/scenes/menu/validate.h"
#include "ui/scenes/profile.h"
#include "ui/scenes/recorder/recorder_scene.h"
#include "ui/scenes/registry.h"

#include "ui/shell/profiles/deluge.h"
#include "ui/shell/shell.h"

#include "gui/l10n/l10n.h"
#include "hid/display/display.h"
#include "hid/display/oled.h"
#include "model/clip/audio_clip.h"
#include "model/edits/setting_edit.h"
#include "model/project_bridge/slot_bridge.h"
#include "model/sample/sample.h"
#include "model/song/song.h"
#include "processing/sound/sound.h"
#include "processing/sound/source_resources.h"
#include "storage/audio/audio_file_holder.h"
#include "storage/cluster/cluster.h"
#include "storage/multi_range/multisample_range.h"

#include "fakes.h"
#include "support/menu_engine.h"
#include "support/oracle_table.h"
#include "support/sample_items_fixtures.h"
#include "support/shortcut_layout.h"
#include "support/song_fixture.h"
#include "support/torn_down_take.h"

// Before <cppspec.hpp>: it #defines `it`, which collides with every local named `it` in the project
// headers this drags in (`audio_engine.h` -> `util/container/ordered_pos_vector.h`).
#include "support/card_access.h"

#include "support/mocks/fake_surfaces.h"

#include "cppspec.hpp"
#include "support/record_state.h"

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <set>
#include <string>
#include <vector>

#include "support/menu_context.h"
#include "support/spec_main.h"

using deluge::project::SampleMarker;
using namespace deluge::ui::scenes;
using namespace deluge::ui::scenes::spec::sampleitems;
using deluge::ui::scenes::menu::MenuContext;
using deluge::ui::scenes::menu::Node;
using deluge::ui::shell::BoardProfile;
using deluge::ui::shell::SceneId;
using deluge::ui::shell::SceneKind;
using deluge::ui::shell::Shell;
using deluge::ui::shell::ShellConfig;
namespace nodes = deluge::ui::scenes::menu::nodes;

namespace {

// ---------------------------------------------------------------------------------------------
// The card the browser examples list

/// @brief The card these examples list: `tests/fixtures/scenes_spec/sample_items`, installed for this file before boot
///        (`tests/integration/checks/scenes_spec_cards.txt`). Constructing one checks it is the installed card.
struct ScratchCard {
	std::filesystem::path dir = std::filesystem::path(DELUGE_REPO_ROOT) / "tests/fixtures/scenes_spec/sample_items";
	spec::SdRoot root{dir.string()};
};

// ---------------------------------------------------------------------------------------------
// The samples the marker examples open on

/// `WaveformView::max_zoom()` for a sample of @p num_clusters clusters.
int64_t zoom_max(int32_t num_clusters) {
	const uint64_t length = static_cast<uint64_t>(kClusterSize) * num_clusters / kBytesPerFrame;
	return static_cast<int64_t>(((length - 1) >> 4) + 1);
}

/// @brief The minimum `Display` the legacy items and the marker scene need. `display` is null in a
///        spec process (`hid/display/display.cpp`).
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

void select_language() {
	deluge::l10n::chosenLanguage = &deluge::l10n::built_in::english;
}

bool scene_on_stack(Shell& shell, SceneKind kind) {
	for (const deluge::ui::shell::Scene* s : shell.stack().scenes()) {
		if (s->kind() == kind) {
			return true;
		}
	}
	return false;
}

/// @brief Focus @p node in the menu and let the shell apply whatever the item asked for.
///
/// Four ticks, not one. `MenuScene::after_render` files the `Push` at the *end* of the tick that
/// rendered the item, so the tick that opens a scene is never the tick that draws it; and
/// `BrowserScene` collects its listing in its own `after_render`, a tick later again (the seam is
/// asynchronous by construction even though `storage::Owner::run` is inline on the host --
/// `listing.h`'s honesty note). Ticking past both is what lets one helper serve all three scenes.
void focus_in(Shell& shell, const MenuContext& ctx, const Node& node) {
	select_language();
	(void)menu::assign_node_ids(nodes::kAllRoots);
	menu_scene().open(node, ctx);
	shell.init(SceneId{SceneKind::FirstApp, menu::node_id(node)});
	for (uint32_t at = 0; at <= 30; at += 10) {
		shell.tick(at);
	}
}

[[noreturn]] void spec_bail(const char* what) {
	std::fprintf(stderr, "sample_items_spec: %s\n", what);
	std::abort();
}

/// @brief Refuse to start, or to finish, with either scene singleton still holding something.
///
/// **Assert-then-clear, not defensive-clear.** Scenes are process singletons, so an example that
/// leaked a listing or an armed recorder would make the *next* example's refusal indistinguishable
/// from a wiring bug -- and a `before_each` that quietly tidied up hides a real defect.
/// Each rig's destructor tears its own stack down; this guard's job
/// is to say so out loud if one did not. `before_each`/`after_each` take a `std::function<void()>`
/// with no `self` (CppSpec's description header), so `expect` is unavailable there and a named reason
/// on stderr plus `std::abort()` is what "loudly" means.
struct CleanScenes {
	CleanScenes() { check("a scene singleton was still busy when an example started"); }
	~CleanScenes() { check("an example finished with a scene singleton still busy"); }
	CleanScenes(const CleanScenes&) = delete;
	CleanScenes& operator=(const CleanScenes&) = delete;

private:
	static void check(const char* what) {
		// One per scene, and all three, because all three are singletons this file opens. The
		// marker editor has no allocation to leak, but it does have per-visit gesture state that
		// `on_exit` clears (`MarkerScene::on_exit`: legacy's `UI_MODE_HOLDING_SAMPLE_MARKER`), and a
		// leaked hold would make the next example's pad input behave differently for no visible
		// reason -- the same class of cross-contamination, one layer smaller.
		if (browser::browser_scene().listing_in_flight() || recorder::recorder_scene().recording()
		    || marker::marker_scene().holding_marker()) {
			spec_bail(what);
		}
	}
};

// ---------------------------------------------------------------------------------------------
// The rig: a synth song whose two oscillators are both sample sources, each with its own sample

struct SoundRig {
	spec::SongFixture song = spec::make_synth_song();
	Sound* sound = nullptr;
	SourceResources* source[2] = {nullptr, nullptr};
	MultisampleRange* range[2] = {nullptr, nullptr};
	Sample* sample[2] = {nullptr, nullptr};

	deluge::ui::shell::SceneRegistry reg = scene_registry();
	uitest::FakeSurfaceSet fake;
	Shell shell;

	SoundRig(const SoundRig&) = delete;
	SoundRig& operator=(const SoundRig&) = delete;

	/// @brief Tear the stack down, so every scene's `on_exit` always runs.
	///
	/// **Scenes are process singletons**: an example that left the recorder
	/// armed would make the next example's `on_enter` refuse and cascade into failures that say
	/// nothing about the mutation that caused them. Popping here is the assert-then-clear discipline
	/// as an RAII guard rather than as a `before_each` reset -- the state is destroyed on the way
	/// out of the example that made it, not tidied up on the way into the next one.
	~SoundRig() {
		while (shell.stack().depth() > 0) {
			shell.stack().pop(SceneKind::Null);
		}
		spec::settle_torn_down_take();      // a take ended with its screen keeps the next one out until it is freed
		menu_scene().reset_process_state(); // each example starts with the rows a boot has
	}

	/// @param second_source True to give oscillator 1 a sample source of its own; false to leave it
	///                      a square wave with no `MultiRange` at all, which is the shape that makes
	///                      "the item selected source 1" observable as the range it gives that source.
	explicit SoundRig(bool second_source)
	    : fake(deluge::ui::shell::kDelugeBoard, deluge_profile()),
	      shell(fake, deluge_profile(), reg, deluge::ui::shell::Services{}, ShellConfig{}) {
		sound = deluge::model::effects_owner_of(deluge::model::owner_of(*song.instrument))->sound;
		setup_source(0, 32);
		if (second_source) {
			setup_source(1, 64);
		}
		// Set up field by field: record it as a load does, so the record the menu reads is the sound's truth.
		deluge::bridge::recordLoadedSound(*sound, *song.song);
	}

	void setup_source(int32_t i, int32_t num_clusters) {
		source[i] = &sound->sourceResources[i];
		const std::string osc = "osc" + std::to_string(i + 1) + "/";
		*sound->state_ = deluge::test::with_kind(*sound->state_, static_cast<deluge::instruments::classic::SourceId>(i),
		                                         OscType::SAMPLE);
		deluge::test::set(*sound->state_, osc + "loopMode", "1"); // ONCE
		deluge::test::set(*sound->state_, osc + "reversed", "0");
		sound->deriveConfig();
		sample[i] = make_sample(num_clusters);
		range[i] = static_cast<MultisampleRange*>(sound->getOrCreateFirstRange(i));
		range[i]->sampleHolder.audioFile = sample[i];
		range[i]->sampleHolder.filePath = i == 0 ? "SAMPLES/OSC_A/A1.WAV" : "SAMPLES/OSC_B/B1.WAV";
		sample[i]->filePath = range[i]->sampleHolder.filePath; // the record names the range's file by its sample
		range[i]->sampleHolder.startPos = 0;
		range[i]->sampleHolder.endPos = sample[i]->lengthInSamples;
		range[i]->sampleHolder.loopStartPos = 0;
		range[i]->sampleHolder.loopEndPos = 0;
		range[i]->sampleHolder.loopLocked = false;
	}

	/// @brief The context `SoundEditor::setup` would have resolved for **source 0** -- which is the
	///        point: nothing re-resolves it on the way into source 1's page, so an item that opens a
	///        scene over source 1 has to say so itself.
	MenuContext ctx() {
		MenuContext c = spec::ctx_of(*song.song, deluge::gui::menuViewOf(song.clip), 0, false, false, false,
		                             /*kit_global_fx=*/false);
		c.range_index = 0;
		return c;
	}

	/// @brief Focus @p node in the menu and let the shell apply whatever it asked for
	///        (`focus_in`, which says how many ticks and why).
	void focus(const Node& node) { focus_in(shell, ctx(), node); }

	bool on_stack(SceneKind kind) { return scene_on_stack(shell, kind); }
};

/// @brief The audio-clip counterpart, for `audio_clip_waveform`.
struct ClipRig {
	spec::AudioSongFixture song = spec::make_audio_clip_song();
	Sample* sample = make_sample(48);

	deluge::ui::shell::SceneRegistry reg = scene_registry();
	uitest::FakeSurfaceSet fake;
	Shell shell;

	ClipRig(const ClipRig&) = delete;
	ClipRig& operator=(const ClipRig&) = delete;
	~ClipRig() {
		while (shell.stack().depth() > 0) {
			shell.stack().pop(SceneKind::Null);
		}
		menu_scene().reset_process_state(); // each example starts with the rows a boot has
	}

	ClipRig()
	    : fake(deluge::ui::shell::kDelugeBoard, deluge_profile()),
	      shell(fake, deluge_profile(), reg, deluge::ui::shell::Services{}, ShellConfig{}) {
		song.clip->sampleHolder.audioFile = sample;
		song.clip->setSampleFilePath("SAMPLES/OSC_A/A1.WAV");
		song.clip->sampleHolder.startPos = 0;
		song.clip->sampleHolder.endPos = sample->lengthInSamples;
	}

	MenuContext ctx() {
		return spec::ctx_of(*song.song, deluge::gui::menuViewOf(song.clip), 0, false, false, false,
		                    /*kit_global_fx=*/false);
	}

	void focus(const Node& node) { focus_in(shell, ctx(), node); }

	bool on_stack(SceneKind kind) { return scene_on_stack(shell, kind); }
};

// ---------------------------------------------------------------------------------------------
// The mapping, as a value that can be compared whole

/// @brief Which of the three item classes a node carries, plus what it was constructed with.
struct Wiring {
	std::string node; ///< the node's own name, so a failure names it
	int kind = -1;    ///< 0 = FileBrowserItem, 1 = MarkerItem, 2 = AudioRecorderItem, -1 = none
	int source = -99; ///< `SampleSceneItem::source_index()`
	int marker = -1;  ///< `MarkerItem::marker()`, or -1 for the two that have none

	bool operator==(const Wiring&) const = default;
};

Wiring wiring_of(const char* name, const Node& node) {
	Wiring w;
	w.node = name;
	const auto* base = dynamic_cast<const nodes::SampleSceneItem*>(node.custom);
	if (base == nullptr) {
		return w;
	}
	w.source = base->source_index();
	if (dynamic_cast<const nodes::FileBrowserItem*>(base) != nullptr) {
		w.kind = 0;
	}
	else if (const auto* m = dynamic_cast<const nodes::MarkerItem*>(base); m != nullptr) {
		w.kind = 1;
		w.marker = static_cast<int>(m->marker());
	}
	else if (dynamic_cast<const nodes::AudioRecorderItem*>(base) != nullptr) {
		w.kind = 2;
	}
	return w;
}

std::string format_wiring(const std::vector<Wiring>& ws) {
	std::string out;
	for (const Wiring& w : ws) {
		out += w.node + "={kind=" + std::to_string(w.kind) + ",src=" + std::to_string(w.source)
		       + ",marker=" + std::to_string(w.marker) + "} ";
	}
	return out;
}

std::vector<Wiring> observed_wiring() {
	return {
	    wiring_of("file0_selector", nodes::file0_selector),
	    wiring_of("file1_selector", nodes::file1_selector),
	    wiring_of("sample0_start", nodes::sample0_start),
	    wiring_of("sample1_start", nodes::sample1_start),
	    wiring_of("sample0_end", nodes::sample0_end),
	    wiring_of("sample1_end", nodes::sample1_end),
	    wiring_of("audio_clip_waveform", nodes::audio_clip_waveform),
	    wiring_of("audio_clip_waveform_start", nodes::audio_clip_waveform_start),
	    wiring_of("sample0_recorder", nodes::sample0_recorder),
	    wiring_of("sample1_recorder", nodes::sample1_recorder),
	};
}

/// @brief What each node must carry, derived from the legacy class each node's item ports.
///
/// `SampleMarker::START` is 0 and `SampleMarker::END` is 3 (`project/sample_marker.h`); the same
/// two numbers are `static_assert`ed against the enum in `nodes/sample.cpp`, so this table and the
/// items cannot drift apart silently.
std::vector<Wiring> expected_wiring() {
	return {
	    // `FileSelector` (`origin/main:src/deluge/gui/menu_item/file_selector.cpp`): `setCurrentSource(sourceId_)`,
	    // then `openUI(&sampleBrowser)`.
	    {"file0_selector", 0, 0, -1},
	    {"file1_selector", 0, 1, -1},
	    // `sample::Start` (`origin/main:src/deluge/gui/menu_item/sample/start.h`) sets `markerType = START`;
	    // `LoopPoint::beginSession` (`origin/main:src/deluge/gui/menu_item/sample/loop_point.cpp`) sets the source
	    // first.
	    {"sample0_start", 1, 0, 0},
	    {"sample1_start", 1, 1, 0},
	    // `sample::End` (`origin/main:src/deluge/gui/menu_item/sample/end.h`) sets `markerType = END`.
	    {"sample0_end", 1, 0, 3},
	    {"sample1_end", 1, 1, 3},
	    // `audioClipSampleMarkerEditorMenuEnd` (`origin/main:src/deluge/gui/ui/menus.cpp`) -- `SampleMarker::END`, and
	    // **no** `setCurrentSource` (`audio_clip::SampleMarkerEditor::beginSession` in
	    // `origin/main:src/deluge/gui/menu_item/audio_clip/sample_marker_editor.cpp`), which is the -1.
	    {"audio_clip_waveform", 1, -1, 3},
	    // `audioClipSampleMarkerEditorMenuStart`: the same, on `SampleMarker::START`.
	    {"audio_clip_waveform_start", 1, -1, 0},
	    // `osc::AudioRecorder::beginSession` (`origin/main:src/deluge/gui/menu_item/osc/audio_recorder.h`).
	    {"sample0_recorder", 2, 0, -1},
	    {"sample1_recorder", 2, 1, -1},
	};
}

// ---------------------------------------------------------------------------------------------
// The relevance comparison

spec::OracleTable& table() {
	return spec::OracleTable::named("sample_items");
}

} // namespace

// clang-format off
describe sample_items_mapping("scenes::menu the sample items: the mapping", $ {

	it("carries the item its legacy class became, on every one of the ten nodes", _ {
		// Compared as one whole table rather than node by node, so a failure prints which row moved.
		expect(format_wiring(observed_wiring())).to_equal(format_wiring(expected_wiring()));

		// ... and the comparison is not vacuous. Ten near-identical items over two macro-generated
		// oscillator pages is precisely the shape where a positional check passes with two of them
		// crossed over, so a deliberately crossed copy is asserted NOT to match. (This is the guard that keeps the
		// assertion itself honest if someone later relaxes it.)
		//
		// The crossing swaps the *payload* fields only and leaves each row's node name where it is,
		// because that is the shape a real miswiring has: `sample0_start` still called
		// `sample0_start`, carrying source 1's item. Swapping whole rows -- name included -- would
		// only have shown that the table notices a reordering, which nothing can cause.
		std::vector<Wiring> crossed_sources = observed_wiring();
		std::swap(crossed_sources[2].source, crossed_sources[3].source); // sample0_start <-> sample1_start
		expect(format_wiring(crossed_sources)).not_().to_equal(format_wiring(expected_wiring()));
		std::vector<Wiring> crossed_markers = observed_wiring();
		std::swap(crossed_markers[2].marker, crossed_markers[4].marker); // sample0_start <-> sample0_end
		expect(format_wiring(crossed_markers)).not_().to_equal(format_wiring(expected_wiring()));
	});

	it("puts these items on those ten nodes and on nothing else in the tree", _ {
		// By identity over the whole reachable tree, not by a count: an item accidentally attached to
		// an eleventh node somewhere else would leave every assertion above passing.
		select_language();
		const uint16_t assigned = menu::assign_node_ids(nodes::kAllRoots);

		const Node* const kExpected[] = {
		    &nodes::file0_selector,   &nodes::file1_selector,   &nodes::sample0_start,
		    &nodes::sample1_start,    &nodes::sample0_end,      &nodes::sample1_end,
		    &nodes::audio_clip_waveform, &nodes::audio_clip_waveform_start, &nodes::sample0_recorder,
		    &nodes::sample1_recorder,
		};
		std::set<const Node*> want{std::begin(kExpected), std::end(kExpected)};

		std::set<const Node*> found;
		for (uint16_t id = 1; id <= assigned; id++) {
			const Node* n = menu::node_by_id(id);
			if (n != nullptr && n->kind == menu::Kind::Custom
			    && dynamic_cast<const nodes::SampleSceneItem*>(n->custom) != nullptr) {
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

	it("gives each of the nine sample nodes a real item, none a placeholder", _ {
		// The count `validate.h` carries, checked from the other end: none of the nine is a
		// placeholder. The tree has no placeholder anywhere (`nodes/output_items.h`
		// holds the last two real items), so `kPendingCustomNodes` is 0 -- `output_items_spec.cpp` is
		// where that is asserted; this only re-checks these nine.
		for (const Node* n : {&nodes::file0_selector, &nodes::file1_selector, &nodes::sample0_start,
		                      &nodes::sample1_start, &nodes::sample0_end, &nodes::sample1_end,
		                      &nodes::audio_clip_waveform, &nodes::sample0_recorder, &nodes::sample1_recorder}) {
			// Two assertions, not one: `custom != nullptr && is_placeholder()` is also false for a
			// null `custom`, which is a worse bug than a placeholder (`validate.h`'s `NullCustom`).
			expect(n->custom).not_().to_be_null();
			if (n->custom != nullptr) {
				expect(n->custom->is_placeholder()).to_be_false();
			}
		}
		expect(menu::kPendingCustomNodes).to_equal(size_t(0));
	});
});

describe sample_items_opening("scenes::menu the sample items: what each one opens", $ {

	before_each([] {
		static QuietDisplay d;
		if (display == nullptr) {
			display = &d;
		}
	});

	it("file0_selector opens the sample browser in source 0's folder", _ {
		spec::QuietAudio quiet;
		CleanScenes clean;
		ScratchCard card;
		SoundRig r{/*second_source=*/true};
		r.focus(nodes::file0_selector);

		expect(r.on_stack(browser::kBrowserSceneKind)).to_be_true();
		expect(static_cast<int>(browser::browser_scene().browser_id()))
		    .to_equal(static_cast<int>(browser::BrowserId::Sample));
		// `sample_start_at` (`browser_scene.cpp`) split source 0's holder path, which is only source
		// 0's because `select_source` left the context where it was.
		expect(std::string(browser::browser_scene().current_dir())).to_equal(std::string("SAMPLES/OSC_A"));
	});

	it("file1_selector opens it in source 1's folder instead", _ {
		// The per-source discriminator for this scene: the two oscillators' holders name different
		// directories, so an item wired to the wrong source lands in the wrong one. Wiring both
		// nodes to `kFile0Selector` passes the previous example and fails this one.
		spec::QuietAudio quiet;
		CleanScenes clean;
		ScratchCard card;
		SoundRig r{/*second_source=*/true};
		r.focus(nodes::file1_selector);

		expect(r.on_stack(browser::kBrowserSceneKind)).to_be_true();
		expect(std::string(browser::browser_scene().current_dir())).to_equal(std::string("SAMPLES/OSC_B"));
	});

	it("sample0_start and sample0_end open the marker editor on their own marker", _ {
		spec::QuietAudio quiet;
		CleanScenes clean;
		{
			SoundRig r{true};
			r.focus(nodes::sample0_start);
			expect(r.on_stack(marker::kMarkerSceneKind)).to_be_true();
			expect(static_cast<int>(marker::marker_scene().marker_type()))
			    .to_equal(static_cast<int>(SampleMarker::START));
		}
		{
			SoundRig r{true};
			r.focus(nodes::sample0_end);
			expect(r.on_stack(marker::kMarkerSceneKind)).to_be_true();
			expect(static_cast<int>(marker::marker_scene().marker_type())).to_equal(static_cast<int>(SampleMarker::END));
		}
	});

	it("sample1_start opens it on source 1's sample, not source 0's", _ {
		// The per-source discriminator for this scene. The two samples are 32 and 64 clusters long,
		// so `WaveformView::open` -- which zooms all the way out on a holder with no remembered view
		// -- lands on a different `xZoom` for each. A `sample1_start` wired to source 0 opens the
		// editor, shows a waveform, and reports 32 clusters' zoom.
		spec::QuietAudio quiet;
		CleanScenes clean;
		{
			SoundRig r{true};
			r.focus(nodes::sample0_start);
			expect(static_cast<int64_t>(marker::marker_scene().x_zoom())).to_equal(zoom_max(32));
		}
		{
			SoundRig r{true};
			r.focus(nodes::sample1_start);
			expect(r.on_stack(marker::kMarkerSceneKind)).to_be_true();
			expect(static_cast<int64_t>(marker::marker_scene().x_zoom())).to_equal(zoom_max(64));
		}
	});

	it("audio_clip_waveform opens the marker editor on the clip's own holder, on END", _ {
		// `audioClipSampleMarkerEditorMenuEnd` (`origin/main:src/deluge/gui/ui/menus.cpp`) is the END-marker
		// spelling, and the audio-clip opener selects no source at all -- the context has no `Sound`
		// for `select_source` to act on, which is why the item carries -1.
		spec::QuietAudio quiet;
		CleanScenes clean;
		ClipRig r;
		r.focus(nodes::audio_clip_waveform);

		expect(r.on_stack(marker::kMarkerSceneKind)).to_be_true();
		expect(static_cast<int>(marker::marker_scene().marker_type())).to_equal(static_cast<int>(SampleMarker::END));
		expect(static_cast<int64_t>(marker::marker_scene().x_zoom())).to_equal(zoom_max(48));
	});

	it("audio_clip_waveform_start opens the marker editor on the clip's own holder, on START", _ {
		// `audioClipSampleMarkerEditorMenuStart`: the START spelling, reached by the V1 shortcut cells.
		spec::QuietAudio quiet;
		CleanScenes clean;
		ClipRig r;
		r.focus(nodes::audio_clip_waveform_start);

		expect(r.on_stack(marker::kMarkerSceneKind)).to_be_true();
		expect(static_cast<int>(marker::marker_scene().marker_type())).to_equal(static_cast<int>(SampleMarker::START));
		expect(static_cast<int64_t>(marker::marker_scene().x_zoom())).to_equal(zoom_max(48));
	});

	it("opens the marker editor over the keyboard root with the greyout shown at once", _ {
		// `MarkerItem::on_focus` hands the menu's context to the editor, `root_is_keyboard` included.
		spec::QuietAudio quiet;
		CleanScenes clean;
		ClipRig r;
		MenuContext c = r.ctx();
		c.root_is_keyboard = true;
		focus_in(r.shell, c, nodes::audio_clip_waveform);
		expect(r.on_stack(marker::kMarkerSceneKind)).to_be_true();
		expect(marker::marker_scene().greyout_changes_instantly()).to_be_true();
	});

	it("lands the V1 shortcut (0, 7) on the marker editor's START through the shell, and BACK leaves the menu too", _ {
		spec::QuietAudio quiet;
		CleanScenes clean;
		ClipRig r;
		const spec::ShortcutLayout v1{deluge::project::ShortcutsVersion::V1};
		// The menu is open on an audio clip item; SHIFT + pad (0, 7) is the START item's shortcut.
		focus_in(r.shell, r.ctx(), nodes::audio_clip_reverse);
		expect(r.on_stack(marker::kMarkerSceneKind)).to_be_false();
		uint32_t at = 100;
		r.shell.on_board_event(DelugeInputEvent{DELUGE_EVENT_BUTTON, 8, 0, 255}, at += 10);
		r.shell.tick(at += 10);
		r.shell.on_board_event(DelugeInputEvent{DELUGE_EVENT_PAD, 0, 7, 255}, at += 10);
		r.shell.on_board_event(DelugeInputEvent{DELUGE_EVENT_PAD, 0, 7, 0}, at += 10);
		for (int i = 0; i < 4; i++) {
			r.shell.tick(at += 10);
		}
		const bool landed = r.on_stack(marker::kMarkerSceneKind);
		const int type = static_cast<int>(marker::marker_scene().marker_type());
		r.shell.on_board_event(DelugeInputEvent{DELUGE_EVENT_BUTTON, 8, 0, 0}, at += 10);
		// BACK (matrix (7, 1)).
		r.shell.on_board_event(DelugeInputEvent{DELUGE_EVENT_BUTTON, 7, 1, 255}, at += 10);
		r.shell.on_board_event(DelugeInputEvent{DELUGE_EVENT_BUTTON, 7, 1, 0}, at += 10);
		for (int i = 0; i < 4; i++) {
			r.shell.tick(at += 10);
		}
		const bool still = r.on_stack(marker::kMarkerSceneKind);
		const size_t depth = r.shell.stack().depth();
		expect(landed).to_be_true();
		expect(type).to_equal(static_cast<int>(SampleMarker::START));
		expect(still).to_be_false();
		// The item sat at depth 0 (main opens it so), so BACK leaves the menu with the editor.
		expect(depth).to_equal(size_t(0));
	});

	it("refuses to open the marker editor when the clip has no sample, as legacy refuses", _ {
		// `MenuItem::checkPermissionToBeginSession` asks `audio_clip::SampleMarkerEditor::isRelevant`
		// (`origin/main:src/deluge/gui/menu_item/audio_clip/sample_marker_editor.cpp`), which refuses a clip without a
		// sample. The node has no relevance predicate, so the refusal is `MarkerScene::can`'s audio-clip arm, which has
		// to actually happen.
		spec::QuietAudio quiet;
		CleanScenes clean;
		ClipRig r;
		r.song.clip->sampleHolder.audioFile = nullptr;
		r.focus(nodes::audio_clip_waveform);
		expect(r.on_stack(marker::kMarkerSceneKind)).to_be_false();
	});

	it("sample0_recorder opens the audio recorder", _ {
		spec::QuietAudio quiet;
		CleanScenes clean;
		ScratchCard card; // so a future change that DID write a file lands here, not in the fixture
		SoundRig r{true};
		r.focus(nodes::sample0_recorder);

		expect(r.on_stack(recorder::kRecorderSceneKind)).to_be_true();
		expect(recorder::recorder_scene().recording()).to_be_true();
	});

	it("sample1_recorder gives source 1 its first range and opens the recorder on it, as main's AudioRecorder does", _ {
		// `osc::AudioRecorder::checkPermissionToBeginSession`
		// (`origin/main:src/deluge/gui/menu_item/osc/audio_recorder.h`) asks
		// `checkPermissionToBeginSessionForRangeSpecificParam` about its own `source_id_`, whose first statement is
		// `getOrCreateFirstRange()`: a square-wave oscillator 2 with no `MultiRange` gets one and records into it. The
		// menu's context stays on source 0 (`SoundRig::ctx`), so only an item that asks about source 1 itself makes the
		// range there; source 0 keeps the one range it had.
		spec::QuietAudio quiet;
		CleanScenes clean;
		ScratchCard card;
		SoundRig r{/*second_source=*/false};
		expect(r.sound->sourceResources[1].ranges.size()).to_equal(size_t{0});

		r.focus(nodes::sample1_recorder);
		expect(r.on_stack(recorder::kRecorderSceneKind)).to_be_true();
		expect(recorder::recorder_scene().recording()).to_be_true();
		expect(r.sound->sourceResources[1].ranges.size()).to_equal(size_t{1});
		expect(r.sound->sourceResources[0].ranges.size()).to_equal(size_t{1});
	});

	it("sample1_recorder asks about source 1's ranges, not the menu's multisampled source 0, as main's whichThing", _ {
		// `checkPermissionToBeginSessionForRangeSpecificParam(sound, source_id_, ...)`: oscillator 2 has one range once
		// it is made, so REC 2 opens with no picker, however many ranges oscillator 1 has and whether one is picked.
		spec::QuietAudio quiet;
		CleanScenes clean;
		ScratchCard card;
		SoundRig r{/*second_source=*/false};
		r.source[0]->ranges.insertMultiRange(1);
		deluge::edits::recordRanges(*r.sound, 0);
		MenuContext c = r.ctx();
		c.range_index = -1; // oscillator 1 multisampled, no range picked
		expect(menu::permission_for(nodes::sample0_recorder, c)).to_equal(menu::Permission::MustSelectRange);
		expect(menu::permission_for(nodes::sample1_recorder, c)).to_equal(menu::Permission::Yes);

		focus_in(r.shell, c, nodes::sample1_recorder);
		expect(r.on_stack(recorder::kRecorderSceneKind)).to_be_true();
		expect(r.sound->sourceResources[1].ranges.size()).to_equal(size_t{1});
		expect(r.sound->sourceResources[0].ranges.size()).to_equal(size_t{2});
	});

	it("sample1_recorder makes a range on source 1 alone, never on the menu's source 0", _ {
		// `main` allocates in the item's own permission check, on `source_id_`; oscillator 1 here has no range and
		// gets none.
		spec::QuietAudio quiet;
		CleanScenes clean;
		ScratchCard card;
		SoundRig r{/*second_source=*/false};
		r.range[0]->sampleHolder.audioFile = nullptr; // the rig placed the sample without a reason to drop
		r.source[0]->ranges.clear();
		r.range[0] = nullptr;
		deluge::edits::recordRanges(*r.sound, 0);
		MenuContext c = r.ctx();
		c.range_index = -1;

		focus_in(r.shell, c, nodes::sample1_recorder);
		expect(r.on_stack(recorder::kRecorderSceneKind)).to_be_true();
		expect(r.sound->sourceResources[1].ranges.size()).to_equal(size_t{1});
		expect(r.sound->sourceResources[0].ranges.size()).to_equal(size_t{0});
	});

	it("sample1_recorder whose first range cannot be made refuses to open, as main's refused permission", _ {
		// `checkPermissionToBeginSessionForRangeSpecificParam`'s `displayError(INSUFFICIENT_RAM)` and `NO`: `setup`
		// refuses, so the menu does not open on the item and the recorder does not open at all. The menu's refusal is
		// `on_enter` answering false, which the shell rolls back. A sound port that refuses is one
		// whose range request is refused.
		spec::QuietAudio quiet;
		CleanScenes clean;
		SoundRig r{/*second_source=*/false};
		MenuContext c = r.ctx();
		bind::test::RecordingSoundWrites refusing;
		refusing.accept = false;
		c.ports.sound_writes = &refusing;
		select_language();
		(void)menu::assign_node_ids(nodes::kAllRoots);
		menu_scene().open(nodes::sample1_recorder, c);
		expect(menu_scene().on_enter(SceneId{menu::kMenuSceneKind, menu::node_id(nodes::sample1_recorder)})).to_be_false();
		expect(r.on_stack(recorder::kRecorderSceneKind)).to_be_false();
		expect(r.sound->sourceResources[1].ranges.size()).to_equal(size_t{0});
	});

	it("does not leave source 0's range behind when it selects source 1", _ {
		// `menu::select_source` folds in main's `SoundEditor::setup`
		// (`origin/main:src/deluge/gui/ui/sound_editor.cpp`), which nulls a remembered range whenever the source index
		// moves. Asserted directly, because the two examples above can only see it through a scene: without it, a
		// context resolved for source 0 and re-pointed at source 1 would still name source 0's range, and every scene
		// here opens on the range the context names.
		spec::QuietAudio quiet;
		CleanScenes clean;
		SoundRig r{/*second_source=*/true};
		MenuContext c = r.ctx();
		expect(spec::menu_engine(c, *r.song.song).range).to_equal(r.range[0]);

		menu::select_source(c, 1);
		expect(static_cast<int>(c.source_index)).to_equal(1);
		const spec::MenuEngine engine = spec::menu_engine(c, *r.song.song);
		expect(engine.source).to_equal(r.source[1]);
		expect(engine.range).to_equal(r.range[1]);

		// Re-selecting the source already named keeps the selected range -- `setCurrentSource`'s own
		// `currentMultiRange == nullptr` guard (`origin/main:src/deluge/gui/ui/sound_editor.cpp`), which is what stops
		// a range the picker chose from being thrown away by the item that opens on it.
		menu::select_source(c, 1);
		expect(spec::menu_engine(c, *r.song.song).range).to_equal(r.range[1]);

		// A context with no `Sound` is left exactly as it is -- the audio-clip case.
		MenuContext none{};
		menu::select_source(none, 1);
		expect(none.range_index).to_equal(static_cast<int16_t>(-1));
		expect(static_cast<int>(none.source_index)).to_equal(0);
	});
});

describe sample_items_relevance("scenes::menu the sample items: relevance against main", $ {

	it("agrees with FileSelector::isRelevant and isSampleModeSample in every oscillator mode", _ {
		// `tables_1_spec.cpp` already compares every node's relevance with its reference row, but on ONE fixture
		// sound -- which for these nodes exercises one of the eight configurations below. The model is written, then the
		// port is asked through a freshly built `MenuContext`.
		select_language();
		spec::SongFixture song = spec::make_synth_song();
		auto* sound = deluge::model::effects_owner_of(deluge::model::owner_of(*song.instrument))->sound;
		Sample* sample = make_sample(32);

		std::vector<std::string> observed;
		std::vector<std::string> want;
		for (const OscCase& c : kOscCases) {
			for (int32_t which = 0; which < 2; which++) {
				seed_osc_case(*sound, *song.song, sample, c, which);
				const MenuContext ctx = spec::ctx_of(*song.song, deluge::gui::menuViewOf(song.clip),
				                                     static_cast<int8_t>(which), false, false, false, false);
				const std::string path = std::string{"sample/"} + c.name + "/" + std::to_string(which);
				const std::vector<std::string>* frozen = table().find(path, "rel");
				const auto flag = [](bool on) { return std::string{on ? "1" : "0"}; };
				want.push_back(path + ":"
				               + (frozen != nullptr && frozen->size() == 3
				                      ? flag((*frozen)[0] == "1") + flag((*frozen)[1] == "1") + flag((*frozen)[2] == "1")
				                      : std::string{"<no row>"}));
				observed.push_back(path + ":"
				                   + flag(menu::bindings::relevant(which == 0 ? nodes::file0_selector : nodes::file1_selector, ctx))
				                   + flag(menu::bindings::relevant(which == 0 ? nodes::sample0_start : nodes::sample1_start, ctx))
				                   + flag(menu::bindings::relevant(which == 0 ? nodes::sample0_end : nodes::sample1_end, ctx)));
			}
		}
		expect(observed).to_equal(want);

		// Not vacuous: the sixteen rows are not all the same answer. (If they were, a predicate that
		// returned a constant would pass the comparison above.)
		std::set<std::string> answers;
		for (const std::string& row : want) {
			answers.insert(row.substr(row.find(':') + 1));
		}
		expect(answers.size()).to_be_greater_than(size_t(1));
	});
});

describe sample_items_reference_rows("scenes::menu the sample items: the reference rows", $ {
	it("parses sample_items.tsv clean and asks every row of it", _ {
		spec::OracleTable& t = table();
		expect(t.problems().size()).to_equal(size_t{0});
		expect(t.size()).to_be_greater_than(size_t{15});
		expect(t.unasked()).to_equal(std::string{});
	});
});
// clang-format on

DELUGE_SPEC(sample_items_spec, sample_items_mapping, sample_items_opening, sample_items_relevance,
            sample_items_reference_rows)
