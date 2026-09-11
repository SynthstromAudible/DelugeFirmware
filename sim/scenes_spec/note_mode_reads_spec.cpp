/// @file
/// @brief Note-family Task 7a: legacy's own "is a note editor open?" reads, and the note-mode
///        handlers' own menu calls, against the ported note mode (design spec §4, "Beyond the eight
///        sites -- correction 7").
///
/// **Everything real.** A real `Song`/clip (`support/song_fixture.h`), the real `instrumentClipView`,
/// the real `MenuScene` on the GLOBAL guest's stack, and the guest itself opened over
/// `instrumentClipView` with `openUI` -- production's shape, which is what makes
/// `ActionLogger::getNewAction`'s current-UI gate a real question. No recorder: the forwarded
/// handlers run for real, which is what the task is for. Before it, every navigation-record example
/// below dereferenced `soundEditor`'s null record or ran legacy's `exitCompletely`.
///
/// **What is pinned:** `noteEditorOpen()`/`noteRowEditorOpen()` per root, closed and open; each gated
/// pair's port observed through the menu's own state (its depth, its focused node, the value its edit
/// storage re-read) with `activeNoteMode()` asked at every step of the toggle; and, per edited file a
/// spec can reach, the legacy arm with the ported menu closed. The offset gate's note-mode arm is
/// `note_bridge_spec.cpp`'s, beside Task 4's legacy-arm example.
///
/// **Not reachable here, argued in the task report instead:** `timeline_view.cpp`'s zoom read (its
/// arm starts an `initiateXZoom` animation), `notify_drain.cpp`'s playback re-dispatch (it needs a
/// playback-state edge), and `note_row.cpp`'s render read (it is inside `NoteRow::renderRow`).

#include "gui/ui/shell_note_mode.h"
#include "util/note_editor_open.h"

#include "definitions_cxx.hpp"
#include "gui/ui/shell_host_ui.h"     // shellHostUI
#include "gui/ui/shell_note_bridge.h" // wireShellNoteBridge
#include "gui/ui/ui.h"                // setRootUILowLevel, openUI, enterUIMode, currentUIMode
#include "gui/views/instrument_clip_view.h"
#include "gui/views/view.h"
#include "hid/button.h"
#include "hid/buttons.h"
#include "hid/display/display.h"
#include "hid/display/oled.h"
#include "model/action/action_logger.h"
#include "model/clip/instrument_clip.h"
#include "model/drum/gate_drum.h"
#include "model/instrument/kit.h"
#include "model/model_stack.h"
#include "model/note/note_row.h"
#include "model/settings/runtime_feature_settings.h"
#include "model/song/song.h"
#include "notify/notice_queue.h"
#include "playback/mode/session.h"

#include "scenes/menu/context.h"
#include "scenes/menu/menu_scene.h"
#include "scenes/menu/node_id.h"
#include "scenes/menu/nodes/nodes.h"
#include "scenes/menu/note_bridge.h"
#include "scenes/registry.h"
#include "shell/scene.h"

#include "support/global_guest_menu.h"
#include "support/song_fixture.h"

#include "cppspec.hpp"

#include <cstdint>
#include <string>
#include <vector>

#include "spec/support/spec_main.h"

/// @name Legacy's held-button state, reached by bare `extern`
///
/// `audio_clip_scene_spec.cpp`'s block, for its reason: `Buttons::isButtonPressed` reads
/// `buttonStates` (`hid/buttons.cpp:73`), which the header does not export, and `Buttons::buttonAction`
/// cannot set it in a spec process.
/// @{
namespace Buttons {
extern bool buttonStates[NUM_BUTTON_COLS + 1][NUM_BUTTON_ROWS];
} // namespace Buttons
/// @}

using namespace deluge::scenes;
using deluge::gui::ui::NoteModeKind;
using deluge::scenes::menu::MenuContext;
using deluge::scenes::menu::Node;
using deluge::scenes::spec::GlobalGuestGuard;
using deluge::scenes::spec::SongFixture;
using deluge::scenes::spec::UiStackGuard;
using deluge::ui::shell::SceneId;
using deluge::ui::shell::SceneKind;
namespace nodes = deluge::scenes::menu::nodes;
namespace guest = deluge::gui::ui;

namespace {

using Root = MenuContext::Root;

/// @brief Hold one legacy button for an example's lifetime, by writing only what `isButtonPressed`
///        reads.
struct HeldButton {
	deluge::hid::Button b;
	explicit HeldButton(deluge::hid::Button button) : b(button) { set(true); }
	~HeldButton() { set(false); }
	HeldButton(const HeldButton&) = delete;
	HeldButton& operator=(const HeldButton&) = delete;
	void set(bool on) const {
		const auto xy = deluge::hid::button::toXY(b);
		::Buttons::buttonStates[xy.x][xy.y] = on;
	}
};

/// @brief Set one runtime feature setting for an example, and put it back.
struct FeatureSetting {
	RuntimeFeatureSettingType type;
	uint32_t saved;
	FeatureSetting(RuntimeFeatureSettingType t, uint32_t value) : type(t), saved(runtimeFeatureSettings.get(t)) {
		runtimeFeatureSettings.set(type, value);
	}
	~FeatureSetting() { runtimeFeatureSettings.set(type, saved); }
	FeatureSetting(const FeatureSetting&) = delete;
	FeatureSetting& operator=(const FeatureSetting&) = delete;
};

/// @brief Every piece of legacy view state these examples borrow, seeded and given back.
///
/// `note_bridge_spec.cpp`'s `RealNoteSelectionGuard` seeds, for its reasons -- `currentPlaybackMode`
/// for `editPadAction`'s audition reassessment, a local OLED as `display` -- plus what the real
/// handlers here also touch: the held edit pads, the note and row selection, `currentUIMode`, and the
/// undo log (a process singleton a later example's edit could otherwise join).
struct LegacyViewGuard {
	deluge::hid::display::OLED oled_display;
	PlaybackMode* saved_playback_mode = currentPlaybackMode;
	deluge::hid::Display* saved_display = display;
	uint8_t saved_last_auditioned_y = instrumentClipView.lastAuditionedYDisplay;
	struct Square {
		int32_t x;
		int32_t y;
	};
	std::vector<Square> pressed;

	LegacyViewGuard() {
		currentPlaybackMode = &session;
		deluge::hid::display::OLED::removePopup();
		display = &oled_display;
		currentUIMode = UI_MODE_NONE;
		drain_notices();
	}

	~LegacyViewGuard() {
		for (const Square& sq : pressed) {
			if (instrumentClipView.numEditPadPresses > 0) {
				instrumentClipView.editPadAction(false, sq.y, sq.x, currentSong->xZoom[NAVIGATION_CLIP]);
			}
		}
		instrumentClipView.lastSelectedNoteXDisplay = kNoSelection;
		instrumentClipView.lastSelectedNoteYDisplay = kNoSelection;
		instrumentClipView.lastAuditionedYDisplay = saved_last_auditioned_y;
		currentUIMode = UI_MODE_NONE;
		// `closeAction`, not `deleteAllLogs()` -- see `note_bridge_spec.cpp`'s `ActionLogGuard`.
		actionLogger.closeAction(ActionType::NOTE_EDIT);
		actionLogger.closeAction(ActionType::NOTE_NUDGE);
		deluge::hid::display::OLED::removePopup();
		display = saved_display;
		currentPlaybackMode = saved_playback_mode;
		drain_notices();
	}

	LegacyViewGuard(const LegacyViewGuard&) = delete;
	LegacyViewGuard& operator=(const LegacyViewGuard&) = delete;

	/// @brief The note row at @p y, created if the clip has none there.
	NoteRow* row_at(int32_t y) {
		char memory[MODEL_STACK_MAX_SIZE];
		ModelStackWithTimelineCounter* stack = currentSong->setupModelStackWithCurrentClip(memory);
		ModelStackWithNoteRow* with_row = getCurrentInstrumentClip()->getNoteRowOnScreen(y, stack);
		if (with_row->getNoteRowAllowNull() == nullptr) {
			with_row = instrumentClipView.createNoteRowForYDisplay(stack, y);
		}
		return with_row->getNoteRowAllowNull();
	}

	/// @brief One note at tick @p pos on row @p y, @p length ticks long.
	void add_note(int32_t y, int32_t pos, int32_t length, int32_t probability) {
		char memory[MODEL_STACK_MAX_SIZE];
		ModelStackWithTimelineCounter* stack = currentSong->setupModelStackWithCurrentClip(memory);
		NoteRow* row = row_at(y);
		int32_t row_index = 0;
		(void)getCurrentInstrumentClip()->getNoteRowOnScreen(y, currentSong, &row_index);
		ModelStackWithNoteRow* with_row =
		    stack->addNoteRow(getCurrentInstrumentClip()->getNoteRowId(row, row_index), row);
		(void)row->attemptNoteAdd(pos, length, /*velocity=*/80, probability, Iterance{}, FillMode::OFF, with_row,
		                          /*action=*/nullptr);
	}

	/// @brief A note filling square (@p x, @p y) at the clip's zoom.
	void add_square_note(int32_t x, int32_t y, int32_t probability) {
		const int32_t zoom = currentSong->xZoom[NAVIGATION_CLIP];
		add_note(y, x * zoom, zoom, probability);
	}

	/// @brief Press (@p x, @p y) the way `InstrumentClipView` presses a pad, and remember to release it.
	void press(int32_t x, int32_t y) {
		instrumentClipView.editPadAction(true, y, x, currentSong->xZoom[NAVIGATION_CLIP]);
		pressed.push_back({x, y});
	}

	/// @brief Row @p y auditioning, the state the note-row editor's own entry guarantees -- set
	///        directly, because a real audition press would sound the synth.
	static void audition_row(int32_t y) {
		instrumentClipView.lastAuditionedYDisplay = y;
		enterUIMode(UI_MODE_AUDITIONING);
	}

	static void drain_notices() {
		deluge::notify::Notice n;
		while (deluge::notify::notices().pop(n)) {}
	}
};

/// @brief The last notice queued since the previous drain, or empty.
std::string drain_last_notice() {
	deluge::notify::Notice n;
	std::string text;
	while (deluge::notify::notices().pop(n)) {
		text = n.text;
	}
	return text;
}

/// @brief The guest opened over `instrumentClipView` with the ported menu at @p root -- production's
///        shape (`note_bridge_spec.cpp`'s helper of the same name).
bool open_note_mode_over_clip_view(SongFixture& fx, Root root) {
	setRootUILowLevel(&instrumentClipView);
	const Node& node = deluge::scenes::spec::menu_node_for_root(root);
	const Node* const roots[] = {&node};
	(void)menu::assign_node_ids(roots);
	menu_scene().open(node, deluge::scenes::spec::menu_context_at(fx, root));
	shellHostUI.setInitialScene(SceneId{SceneKind::FirstApp, menu::node_id(node)});
	return openUI(&shellHostUI);
}

/// @brief Is the ported menu on the global guest's stack at all?
bool menu_on_guest_stack() {
	for (const deluge::ui::shell::Scene* scene : shellHostUI.shell().stack().scenes()) {
		if (scene == &menu_scene()) {
			return true;
		}
	}
	return false;
}

} // namespace

// clang-format off
describe note_mode_reads_predicates("util/note_editor_open.h -- legacy's question, asked of the ported mode too", $ {
	it("both answer false with the ported menu off the guest's stack, and on it at a root that is not a note root", _ {
		UiStackGuard ui_guard;
		GlobalGuestGuard guest_guard;
		LegacyViewGuard legacy;
		SongFixture fx = deluge::scenes::spec::make_synth_song();
		setRootUILowLevel(&instrumentClipView);
		expect(guest::noteEditorOpen()).to_be_false();
		expect(guest::noteRowEditorOpen()).to_be_false();

		expect(open_note_mode_over_clip_view(fx, Root::Sound)).to_be_true();
		expect(guest::noteEditorOpen()).to_be_false();
		expect(guest::noteRowEditorOpen()).to_be_false();
	});

	it("noteEditorOpen answers the note-editor root and noteRowEditorOpen the note-row editor root", _ {
		UiStackGuard ui_guard;
		GlobalGuestGuard guest_guard;
		LegacyViewGuard legacy;
		SongFixture fx = deluge::scenes::spec::make_synth_song();

		expect(open_note_mode_over_clip_view(fx, Root::NoteEditor)).to_be_true();
		expect(guest::noteEditorOpen()).to_be_true();
		expect(guest::noteRowEditorOpen()).to_be_false();

		nullifyUIs();
		GlobalGuestGuard::empty_guest_stack();
		expect(open_note_mode_over_clip_view(fx, Root::NoteRowEditor)).to_be_true();
		expect(guest::noteEditorOpen()).to_be_false();
		expect(guest::noteRowEditorOpen()).to_be_true();
	});

	it("both answer false again once the menu has left the guest's stack", _ {
		UiStackGuard ui_guard;
		GlobalGuestGuard guest_guard;
		LegacyViewGuard legacy;
		SongFixture fx = deluge::scenes::spec::make_synth_song();
		expect(open_note_mode_over_clip_view(fx, Root::NoteEditor)).to_be_true();

		expect(shellHostUI.shell().stack().remove(SceneKind::FirstApp)).to_be_true();
		expect(guest::noteEditorOpen()).to_be_false();
		expect(guest::noteRowEditorOpen()).to_be_false();
	});
});

describe note_mode_reads_note_editor("the note editor's handlers' own menu calls, ported", $ {
	before_all([] { deluge::gui::ui::wireShellNoteBridge(); });

	it("a vertical turn off a note item deselects the note and goes up one level to the root, the menu still open "
	   "-- deselectNoteAndGoUpOneLevel", _ {
		UiStackGuard ui_guard;
		GlobalGuestGuard guest_guard;
		LegacyViewGuard legacy;
		SongFixture fx = deluge::scenes::spec::make_synth_song();
		legacy.add_square_note(0, 0, kNumProbabilityValues);
		legacy.press(0, 0);
		expect(open_note_mode_over_clip_view(fx, Root::NoteEditor)).to_be_true();
		menu_scene().enter_submenu(nodes::note_editor_probability);
		expect(menu_scene().state().depth).to_equal(uint8_t{1});

		(void)guest::forwardNoteVerticalEncoder(0, false);

		expect(menu_scene().state().depth).to_equal(uint8_t{0});
		expect(menu_scene().focused() == &nodes::note_editor_root).to_be_true();
		expect(menu_on_guest_stack()).to_be_true();
		expect(instrumentClipView.lastSelectedNoteXDisplay).to_equal(int32_t{kNoSelection}); // `exitNoteEditor()` ran
	});

	it("a vertical turn at the root deselects the note and neither goes up nor closes -- the `!= "
	   "&noteEditorRootMenu` test", _ {
		UiStackGuard ui_guard;
		GlobalGuestGuard guest_guard;
		LegacyViewGuard legacy;
		SongFixture fx = deluge::scenes::spec::make_synth_song();
		legacy.add_square_note(0, 0, kNumProbabilityValues);
		legacy.press(0, 0);
		expect(open_note_mode_over_clip_view(fx, Root::NoteEditor)).to_be_true();

		(void)guest::forwardNoteVerticalEncoder(0, false);

		expect(menu_scene().state().depth).to_equal(uint8_t{0});
		expect(menu_scene().focused() == &nodes::note_editor_root).to_be_true();
		expect(menu_on_guest_stack()).to_be_true();
	});

	it("goUpOneLevel's port steps up from depth 1, and at depth 0 closes the menu as exitCompletely does", _ {
		UiStackGuard ui_guard;
		GlobalGuestGuard guest_guard;
		LegacyViewGuard legacy;
		SongFixture fx = deluge::scenes::spec::make_synth_song();
		legacy.add_square_note(0, 0, kNumProbabilityValues);
		legacy.press(0, 0);
		expect(open_note_mode_over_clip_view(fx, Root::NoteEditor)).to_be_true();
		menu_scene().enter_submenu(nodes::note_velocity);

		guest::noteModeGoUpOneLevel();
		expect(menu_scene().state().depth).to_equal(uint8_t{0});
		expect(menu_on_guest_stack()).to_be_true();

		guest::noteModeGoUpOneLevel();
		expect(menu_on_guest_stack()).to_be_false();
		expect(guest::activeNoteMode() == NoteModeKind::None).to_be_true();
	});

	it("pressing a pad holding a different note re-reads the focused item for that note -- "
	   "handleNoteEditorEditPadAction's readValueAgain", _ {
		UiStackGuard ui_guard;
		GlobalGuestGuard guest_guard;
		LegacyViewGuard legacy;
		SongFixture fx = deluge::scenes::spec::make_synth_song();
		legacy.add_square_note(0, 0, kNumProbabilityValues);
		legacy.add_square_note(1, 0, 10);
		legacy.press(0, 0);
		expect(open_note_mode_over_clip_view(fx, Root::NoteEditor)).to_be_true();
		menu_scene().enter_submenu(nodes::note_editor_probability);
		expect(menu_scene().state().edit_int).to_equal(int32_t{kNumProbabilityValues});

		guest::noteModeLegacyCalls().noteEditorEditPad(1, 0, 1);
		legacy.pressed.push_back({1, 0});

		expect(instrumentClipView.lastSelectedNoteXDisplay).to_equal(int32_t{1});
		expect(menu_scene().state().edit_int).to_equal(int32_t{10});
	});
});

describe note_mode_reads_note_row_editor("the note-row editor's handlers' own menu calls, ported", $ {
	before_all([] { deluge::gui::ui::wireShellNoteBridge(); });

	it("holding a note pad at the row root enters the note root and releasing it goes back, with activeNoteMode() "
	   "NoteRowEditor at every step -- toggleNoteEditorParamMenu's root arms", _ {
		UiStackGuard ui_guard;
		GlobalGuestGuard guest_guard;
		LegacyViewGuard legacy;
		FeatureSetting horizontal(RuntimeFeatureSettingType::HorizontalMenus, RuntimeFeatureStateToggle::On);
		SongFixture fx = deluge::scenes::spec::make_synth_song();
		legacy.add_square_note(0, 0, kNumProbabilityValues);
		LegacyViewGuard::audition_row(0);
		expect(open_note_mode_over_clip_view(fx, Root::NoteRowEditor)).to_be_true();
		expect(guest::activeNoteMode() == NoteModeKind::NoteRowEditor).to_be_true();

		expect(guest::noteModeLegacyCalls().noteRowEditorPad(0, 0, 1)).to_be_true();
		legacy.pressed.push_back({0, 0});
		expect(menu_scene().state().depth).to_equal(uint8_t{1});
		expect(menu_scene().focused() == &nodes::note_editor_root).to_be_true();
		expect(guest::activeNoteMode() == NoteModeKind::NoteRowEditor).to_be_true();
		expect(menu_scene().menu_context().root == Root::NoteRowEditor).to_be_true();

		expect(guest::noteModeLegacyCalls().noteRowEditorPad(0, 0, 0)).to_be_true();
		expect(menu_scene().state().depth).to_equal(uint8_t{0});
		expect(menu_scene().focused() == &nodes::note_row_editor_root).to_be_true();
		expect(guest::activeNoteMode() == NoteModeKind::NoteRowEditor).to_be_true();
	});

	it("on the row's probability item, holding a note enters the note's probability item and releasing it comes back "
	   "-- the param arms", _ {
		UiStackGuard ui_guard;
		GlobalGuestGuard guest_guard;
		LegacyViewGuard legacy;
		SongFixture fx = deluge::scenes::spec::make_synth_song();
		legacy.add_square_note(0, 0, kNumProbabilityValues);
		LegacyViewGuard::audition_row(0);
		expect(open_note_mode_over_clip_view(fx, Root::NoteRowEditor)).to_be_true();
		menu_scene().enter_submenu(nodes::note_row_probability);

		expect(guest::noteModeLegacyCalls().noteRowEditorPad(0, 0, 1)).to_be_true();
		legacy.pressed.push_back({0, 0});
		expect(menu_scene().state().depth).to_equal(uint8_t{2});
		expect(menu_scene().focused() == &nodes::note_editor_probability).to_be_true();
		expect(guest::activeNoteMode() == NoteModeKind::NoteRowEditor).to_be_true();

		expect(guest::noteModeLegacyCalls().noteRowEditorPad(0, 0, 0)).to_be_true();
		expect(menu_scene().state().depth).to_equal(uint8_t{1});
		expect(menu_scene().focused() == &nodes::note_row_probability).to_be_true();
		expect(guest::activeNoteMode() == NoteModeKind::NoteRowEditor).to_be_true();
	});

	it("with horizontal menus off, holding a note pad at the row root moves nothing -- runtimeFeatureSettings read as "
	   "legacy reads it", _ {
		UiStackGuard ui_guard;
		GlobalGuestGuard guest_guard;
		LegacyViewGuard legacy;
		FeatureSetting horizontal(RuntimeFeatureSettingType::HorizontalMenus, RuntimeFeatureStateToggle::Off);
		SongFixture fx = deluge::scenes::spec::make_synth_song();
		legacy.add_square_note(0, 0, kNumProbabilityValues);
		LegacyViewGuard::audition_row(0);
		expect(open_note_mode_over_clip_view(fx, Root::NoteRowEditor)).to_be_true();

		expect(guest::noteModeLegacyCalls().noteRowEditorPad(0, 0, 1)).to_be_true();
		legacy.pressed.push_back({0, 0});
		expect(menu_scene().state().depth).to_equal(uint8_t{0});
		expect(menu_scene().focused() == &nodes::note_row_editor_root).to_be_true();
	});

	it("the release stays on the note item and raises \"Select Row\" when no row is auditioning -- the target's "
	   "shouldEnterSubmenu() refuses", _ {
		UiStackGuard ui_guard;
		GlobalGuestGuard guest_guard;
		LegacyViewGuard legacy;
		SongFixture fx = deluge::scenes::spec::make_synth_song();
		legacy.add_square_note(0, 0, kNumProbabilityValues);
		LegacyViewGuard::audition_row(0);
		expect(open_note_mode_over_clip_view(fx, Root::NoteRowEditor)).to_be_true();
		menu_scene().enter_submenu(nodes::note_row_probability);
		expect(guest::noteModeLegacyCalls().noteRowEditorPad(0, 0, 1)).to_be_true();
		legacy.pressed.push_back({0, 0});
		expect(menu_scene().focused() == &nodes::note_editor_probability).to_be_true();

		exitUIMode(UI_MODE_AUDITIONING);
		(void)drain_last_notice();
		expect(guest::noteModeLegacyCalls().noteRowEditorPad(0, 0, 0)).to_be_true();
		expect(menu_scene().focused() == &nodes::note_editor_probability).to_be_true();
		expect(drain_last_notice()).to_equal(std::string{"Select Row"});
	});

	it("releasing one of two held notes re-reads the focused item for the note still held -- "
	   "handleNoteRowEditorMainPadAction's readValueAgain", _ {
		UiStackGuard ui_guard;
		GlobalGuestGuard guest_guard;
		LegacyViewGuard legacy;
		SongFixture fx = deluge::scenes::spec::make_synth_song();
		legacy.add_square_note(0, 0, kNumProbabilityValues);
		legacy.add_square_note(1, 0, 10);
		LegacyViewGuard::audition_row(0);
		expect(open_note_mode_over_clip_view(fx, Root::NoteRowEditor)).to_be_true();
		menu_scene().enter_submenu(nodes::note_row_probability);

		expect(guest::noteModeLegacyCalls().noteRowEditorPad(0, 0, 1)).to_be_true(); // the toggle: into the note item
		legacy.pressed.push_back({0, 0});
		expect(guest::noteModeLegacyCalls().noteRowEditorPad(1, 0, 1)).to_be_true(); // still holding: a re-read
		legacy.pressed.push_back({1, 0});
		expect(menu_scene().focused() == &nodes::note_editor_probability).to_be_true();
		expect(menu_scene().state().edit_int).to_equal(int32_t{kNumProbabilityValues});

		expect(guest::noteModeLegacyCalls().noteRowEditorPad(0, 0, 0)).to_be_true(); // one released, one held
		expect(menu_scene().focused() == &nodes::note_editor_probability).to_be_true();
		expect(menu_scene().state().edit_int).to_equal(int32_t{10});
	});

	it("an audition pad on another row re-reads the focused item for that row -- "
	   "handleNoteRowEditorAuditionPadAction's readValueAgain", _ {
		UiStackGuard ui_guard;
		GlobalGuestGuard guest_guard;
		LegacyViewGuard legacy;
		HeldButton shift_for_silence(deluge::hid::button::SHIFT); // `auditionPadAction`'s silent arm: no synth voice
		SongFixture fx = deluge::scenes::spec::make_synth_song();
		legacy.row_at(0)->probabilityValue = 5;
		LegacyViewGuard::audition_row(0);
		expect(open_note_mode_over_clip_view(fx, Root::NoteRowEditor)).to_be_true();
		menu_scene().enter_submenu(nodes::note_row_probability);
		expect(menu_scene().state().edit_int).to_equal(int32_t{5});

		expect(guest::noteModeLegacyCalls().noteRowEditorPad(kDisplayWidth + 1, 1, 1)).to_be_true();

		expect(instrumentClipView.lastAuditionedYDisplay).to_equal(uint8_t{1});
		expect(menu_scene().state().edit_int).to_equal(int32_t{legacy.row_at(1)->probabilityValue});
		expect(menu_scene().state().edit_int == 5).to_be_false();
	});

	it("an audition on another row re-reads Play Direction for that row -- potentiallyRefreshNoteRowMenu", _ {
		UiStackGuard ui_guard;
		GlobalGuestGuard guest_guard;
		LegacyViewGuard legacy;
		SongFixture fx = deluge::scenes::spec::make_synth_song();
		NoteRow* row = legacy.row_at(0);
		LegacyViewGuard::audition_row(0);
		expect(open_note_mode_over_clip_view(fx, Root::NoteRowEditor)).to_be_true();
		menu_scene().enter_submenu(nodes::sequence_direction);
		const int32_t before = menu_scene().state().enum_index;

		char memory[MODEL_STACK_MAX_SIZE];
		ModelStackWithTimelineCounter* stack = currentSong->setupModelStackWithCurrentClip(memory);
		int32_t row_index = 0;
		(void)fx.clip->getNoteRowOnScreen(0, currentSong, &row_index);
		row->setSequenceDirectionMode(stack->addNoteRow(fx.clip->getNoteRowId(row, row_index), row),
		                              SequenceDirection::PINGPONG);
		expect(static_cast<int32_t>(row->sequenceDirectionMode) == before).to_be_false();

		instrumentClipView.potentiallyRefreshNoteRowMenu();
		expect(menu_scene().state().enum_index).to_equal(static_cast<int32_t>(row->sequenceDirectionMode));
	});

	it("a vertical turn off the row root re-reads the focused item -- "
	   "handleNoteRowEditorVerticalEncoderAction's readValueAgain", _ {
		UiStackGuard ui_guard;
		GlobalGuestGuard guest_guard;
		LegacyViewGuard legacy;
		SongFixture fx = deluge::scenes::spec::make_synth_song();
		NoteRow* row = legacy.row_at(0);
		row->probabilityValue = 5;
		LegacyViewGuard::audition_row(0);
		expect(open_note_mode_over_clip_view(fx, Root::NoteRowEditor)).to_be_true();
		menu_scene().enter_submenu(nodes::note_row_probability);
		expect(menu_scene().state().edit_int).to_equal(int32_t{5});

		row->probabilityValue = 7; // behind the menu's back
		(void)guest::forwardNoteVerticalEncoder(0, false);
		expect(menu_scene().state().edit_int).to_equal(int32_t{7});
	});

	it("a mute pad on another drum's row leaves the selected drum alone -- mutePadPress' `inNoteEditor`", _ {
		UiStackGuard ui_guard;
		GlobalGuestGuard guest_guard;
		LegacyViewGuard legacy;
		SongFixture fx = deluge::scenes::spec::make_kit_song();
		fx.clip->yScroll = 0; // `note_bridge_spec.cpp`'s kit example says why
		auto* first = deluge::scenes::spec::add_drum<GateDrum>(fx);
		auto* second = deluge::scenes::spec::add_drum<GateDrum>(fx);
		Drum* const row_drum = fx.clip->getNoteRowOnScreen(1, currentSong)->drum;
		Drum* const other = row_drum == first ? static_cast<Drum*>(second) : static_cast<Drum*>(first);
		Kit* const kit = static_cast<Kit*>(fx.clip->output);
		kit->selectedDrum = other;
		expect(open_note_mode_over_clip_view(fx, Root::NoteRowEditor)).to_be_true();

		expect(guest::noteModeLegacyCalls().noteRowEditorPad(kDisplayWidth, 1, 1)).to_be_true();
		expect(kit->selectedDrum == other).to_be_true();
	});

	it("nudging a held note out of its square goes up one level -- nudgeNotes' goUpOneLevel", _ {
		UiStackGuard ui_guard;
		GlobalGuestGuard guest_guard;
		LegacyViewGuard legacy;
		FeatureSetting horizontal(RuntimeFeatureSettingType::HorizontalMenus, RuntimeFeatureStateToggle::On);
		SongFixture fx = deluge::scenes::spec::make_synth_song();
		legacy.add_note(0, /*pos=*/0, /*length=*/1, kNumProbabilityValues);
		LegacyViewGuard::audition_row(0);
		expect(open_note_mode_over_clip_view(fx, Root::NoteRowEditor)).to_be_true();
		expect(guest::noteModeLegacyCalls().noteRowEditorPad(0, 0, 1)).to_be_true(); // the toggle: into the note root
		legacy.pressed.push_back({0, 0});
		expect(menu_scene().focused() == &nodes::note_editor_root).to_be_true();

		HeldButton x_enc(deluge::hid::button::X_ENC);
		// Nudged left from tick 0, the note wraps to the row's end (`nudgeNotes`' `newPos +=
		// noteRowEffectiveLength`), which leaves square 0 empty.
		(void)guest::forwardNoteHorizontalEncoder(-1);

		expect(menu_scene().state().depth).to_equal(uint8_t{0});
		expect(menu_scene().focused() == &nodes::note_row_editor_root).to_be_true();
		expect(menu_on_guest_stack()).to_be_true();
	});
});

describe note_mode_reads_legacy_leg("the legacy arm of each edited file, with the ported menu closed", $ {
	before_all([] { deluge::gui::ui::wireShellNoteBridge(); });

	it("instrument_clip_view.cpp: adjust_note_probability's first turn with no popup changes nothing and raises the "
	   "PROBABILITY popup", _ {
		UiStackGuard ui_guard;
		GlobalGuestGuard guest_guard;
		LegacyViewGuard legacy;
		SongFixture fx = deluge::scenes::spec::make_synth_song();
		setRootUILowLevel(&instrumentClipView);
		legacy.add_square_note(0, 0, kNumProbabilityValues);
		legacy.press(0, 0);
		expect(menu_on_guest_stack()).to_be_false();

		const menu::NoteBridge* bridge = menu::note_bridge();
		const int32_t before = bridge->note_probability();
		bridge->adjust_note_probability(-1);
		expect(bridge->note_probability()).to_equal(before);
		expect(display->hasPopupOfType(PopupType::PROBABILITY)).to_be_true();
	});

	it("instrument_clip_view.cpp: a mute pad on another drum's row selects that row's drum", _ {
		UiStackGuard ui_guard;
		GlobalGuestGuard guest_guard;
		LegacyViewGuard legacy;
		SongFixture fx = deluge::scenes::spec::make_kit_song();
		fx.clip->yScroll = 0;
		auto* first = deluge::scenes::spec::add_drum<GateDrum>(fx);
		auto* second = deluge::scenes::spec::add_drum<GateDrum>(fx);
		setRootUILowLevel(&instrumentClipView);
		Drum* const row_drum = fx.clip->getNoteRowOnScreen(1, currentSong)->drum;
		Drum* const other = row_drum == first ? static_cast<Drum*>(second) : static_cast<Drum*>(first);
		Kit* const kit = static_cast<Kit*>(fx.clip->output);
		kit->selectedDrum = other;
		expect(menu_on_guest_stack()).to_be_false();

		instrumentClipView.mutePadPress(1);
		expect(kit->selectedDrum == row_drum).to_be_true();
	});

	it("action_logger.cpp: getNewAction refuses under a current UI that is not the root unless a note mode is open", _ {
		UiStackGuard ui_guard;
		GlobalGuestGuard guest_guard;
		LegacyViewGuard legacy;
		SongFixture fx = deluge::scenes::spec::make_synth_song();

		expect(open_note_mode_over_clip_view(fx, Root::Sound)).to_be_true();
		expect(actionLogger.getNewAction(ActionType::NOTE_EDIT, ActionAddition::ALLOWED) == nullptr).to_be_true();

		nullifyUIs();
		GlobalGuestGuard::empty_guest_stack();
		expect(open_note_mode_over_clip_view(fx, Root::NoteEditor)).to_be_true();
		expect(actionLogger.getNewAction(ActionType::NOTE_EDIT, ActionAddition::ALLOWED) == nullptr).to_be_false();
	});

	it("view.cpp: SYNC_SCALING as Fill, auditioning, menu closed -- the row-fill arm: no fill mode, FILL popup", _ {
		UiStackGuard ui_guard;
		GlobalGuestGuard guest_guard;
		LegacyViewGuard legacy;
		FeatureSetting fill(RuntimeFeatureSettingType::SyncScalingAction, RuntimeFeatureStateSyncScalingAction::Fill);
		SongFixture fx = deluge::scenes::spec::make_synth_song();
		setRootUILowLevel(&instrumentClipView);
		LegacyViewGuard::audition_row(0);
		currentSong->changeFillMode(false);

		(void)view.buttonAction(deluge::hid::button::SYNC_SCALING, true, false);
		expect(currentSong->isFillModeActive()).to_be_false();
		expect(display->hasPopupOfType(PopupType::FILL)).to_be_true();
	});

	it("view.cpp: the same press with the ported note-row editor open -- the note-editor arm: fill mode on, no popup", _ {
		UiStackGuard ui_guard;
		GlobalGuestGuard guest_guard;
		LegacyViewGuard legacy;
		FeatureSetting fill(RuntimeFeatureSettingType::SyncScalingAction, RuntimeFeatureStateSyncScalingAction::Fill);
		SongFixture fx = deluge::scenes::spec::make_synth_song();
		LegacyViewGuard::audition_row(0);
		currentSong->changeFillMode(false);
		expect(open_note_mode_over_clip_view(fx, Root::NoteRowEditor)).to_be_true();

		(void)view.buttonAction(deluge::hid::button::SYNC_SCALING, true, false);
		expect(currentSong->isFillModeActive()).to_be_true();
		expect(display->hasPopup()).to_be_false();
		currentSong->changeFillMode(false);
	});
});
// clang-format on

int note_mode_reads_spec(int argc, char** const argv) {
	return CppSpec::parse(argc, argv)
	               .add_specs(note_mode_reads_predicates, note_mode_reads_note_editor, note_mode_reads_note_row_editor,
	                          note_mode_reads_legacy_leg)
	               .exec()
	               .is_success()
	           ? EXIT_SUCCESS
	           : EXIT_FAILURE;
}
