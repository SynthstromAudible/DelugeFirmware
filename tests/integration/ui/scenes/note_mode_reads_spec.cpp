/// @file
/// @brief Legacy's own "is a note editor open?" reads, and the note-mode handlers' own menu
///        calls, against the note mode.
///
/// **Everything real.** A real `Song`/clip (`support/song_fixture.h`), the real `instrumentClipView`,
/// the real `MenuScene` on the GLOBAL guest's stack, and the guest itself opened over
/// `instrumentClipView` with `openUI` -- production's shape, which is what makes
/// `ActionLogger::getNewAction`'s current-UI gate a real question. No recorder: the forwarded
/// handlers run for real. Without that, every navigation-record example below would dereference
/// `soundEditor`'s null record or run legacy's `exitCompletely`.
///
/// **What is pinned:** `noteEditorOpen()`/`noteRowEditorOpen()` per root, closed and open; each gated
/// pair's port observed through the menu's own state (its depth, its focused node, the value its edit
/// storage re-read) with `activeNoteMode()` asked at every step of the toggle; and, per edited file a
/// spec can reach, the legacy arm with the menu closed. The offset gate's note-mode arm is
/// `note_bridge_spec.cpp`'s, beside its legacy-arm example.
///
/// **Not reachable here, argued per site instead:** `timeline_view.cpp`'s zoom read (its
/// arm starts an `initiateXZoom` animation), `notify_drain.cpp`'s playback re-dispatch (it needs a
/// playback-state edge), and `note_row.cpp`'s render read (it is inside `NoteRow::renderRow`).

#include "gui/ui/shell_note_mode.h"
#include "project/iterance.h"
#include "project/note_probability.h"
#include "util/note_editor_open.h"

#include "base/notify/notice_queue.h"
#include "definitions_cxx.hpp"
#include "gui/ui/shell_host_ui.h" // shellHostUI
#include "gui/ui/shell_note_bridge.h"
#include "gui/ui/shell_pad_forward.h" // wireShellNoteBridge
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
#include "model/edits/setting_edit.h"
#include "model/instrument/kit.h"
#include "model/model_stack.h"
#include "model/note/note_row.h"
#include "model/project_bridge/param_stack.h"
#include "model/settings/runtime_feature_settings.h"
#include "model/song/song.h"
#include "playback/mode/session.h"
#include "processing/sound/sound_drum.h"
#include "project/sequence_direction.h"

#include "ui/scenes/menu/context.h"
#include "ui/scenes/menu/menu_scene.h"
#include "ui/scenes/menu/node_id.h"
#include "ui/scenes/menu/nodes/nodes.h"
#include "ui/scenes/menu/note_bridge.h"
#include "ui/scenes/registry.h"
#include "ui/shell/scene.h"

#include "support/feature_setting.h"
#include "support/global_guest_menu.h"
#include "support/song_fixture.h"

#include "cppspec.hpp"

#include <algorithm>
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

#include "support/spec_main.h"

using deluge::project::Iterance;

/// @name Legacy's held-button state, reached by bare `extern`
///
/// `audio_clip_scene_spec.cpp`'s block, for its reason: `Buttons::isButtonPressed` reads
/// `buttonStates` (`hid/buttons.cpp`), which the header does not export, and `Buttons::buttonAction`
/// cannot set it in a spec process.
/// @{
namespace Buttons {
extern bool buttonStates[NUM_BUTTON_COLS + 1][NUM_BUTTON_ROWS];
extern bool shiftCurrentlyPressed;
extern bool shiftCurrentlyStuck;
} // namespace Buttons
/// @}

using namespace deluge::ui::scenes;
using deluge::gui::ui::NoteModeKind;
using deluge::ui::scenes::menu::MenuContext;
using deluge::ui::scenes::menu::Node;
using deluge::ui::scenes::spec::FeatureSetting;
using deluge::ui::scenes::spec::GlobalGuestGuard;
using deluge::ui::scenes::spec::open_note_mode_over_clip_view;
using deluge::ui::scenes::spec::SongFixture;
using deluge::ui::scenes::spec::UiStackGuard;
using deluge::ui::shell::SceneId;
using deluge::ui::shell::SceneKind;
namespace nodes = deluge::ui::scenes::menu::nodes;
namespace guest = deluge::gui::ui;

namespace {

using Root = MenuContext::Root;

/// @brief Whether the view's knobs move @p controllable: its focus's owner stack, resolved only while the focus is
///        on the current song.
/// @param controllable The owner to look for.
bool knobs_move(const deluge::model::LaneOwner& controllable) {
	alignas(ModelStackWithAutoParam) char memory[MODEL_STACK_MAX_SIZE];
	const ModelStackWithThreeMainThings* owner = view.knobOwnerStack(memory);
	return owner != nullptr && owner->owner == controllable;
}

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

/// @brief Hold SHIFT for an example's lifetime: `Buttons::isShiftButtonPressed` reads
///        `shiftCurrentlyPressed` (`hid/buttons.cpp`), not the button matrix.
struct HeldShift {
	HeldShift() {
		::Buttons::shiftCurrentlyPressed = true;
		::Buttons::shiftCurrentlyStuck = false;
	}
	~HeldShift() { ::Buttons::shiftCurrentlyPressed = false; }
	HeldShift(const HeldShift&) = delete;
	HeldShift& operator=(const HeldShift&) = delete;
};

/// @name Two spec-local pages for `MenuScene::go_up_one_level`'s refusal arm
///
/// A root whose `relevant` hook refuses makes `permission_for` answer `No` for it
/// (`ui/scenes/menu/context.cpp`'s base rule), which is the refusal `SoundEditor::goUpOneLevel`'s loop
/// meets in the same function. No production root refuses at depth 0, so these stand in.
/// @{
bool never_relevant(const MenuContext&) {
	return false;
}
constexpr std::span<const Node* const> kNoChildren{};
constexpr Node kChildPage = menu::submenu(0, kNoChildren);
constexpr const Node* kRootChildren[] = {&kChildPage};
constexpr Node kOpenRoot = menu::submenu(0, kRootChildren);
constexpr Node kRefusedRoot = menu::submenu(0, kRootChildren, menu::Layout::Vertical, never_relevant);
/// @}

/// @brief `Output::activeClip` is protected, and `Output::setActiveClip` is the very call a spec must not
///        make (see the kit audition example). Reads it the way `note_nodes_spec.cpp`'s `SubmenuPeek`
///        reaches `Submenu::items`: a member pointer taken in a derived class, never instantiated.
struct OutputPeek : Output {
	static Clip*& active_clip(Output& output) { return output.*(&OutputPeek::activeClip); }
};

/// @brief Seed an output's active clip for an example, and put the previous one back on scope exit --
///        the suite's guard idiom (`FeatureSetting`, `HeldButton`, `HeldShift`) for `OutputPeek`'s write.
struct ActiveClipSeed {
	Output& output;
	Clip* saved;
	ActiveClipSeed(Output& o, Clip* clip) : output(o), saved(OutputPeek::active_clip(o)) {
		OutputPeek::active_clip(output) = clip;
	}
	~ActiveClipSeed() { OutputPeek::active_clip(output) = saved; }
	ActiveClipSeed(const ActiveClipSeed&) = delete;
	ActiveClipSeed& operator=(const ActiveClipSeed&) = delete;
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
				instrumentClipView.editPadAction(false, sq.y, sq.x, currentSong->persistentView.xZoom[NAVIGATION_CLIP]);
			}
		}
		instrumentClipView.exitNoteEditor(); // clears the note editor's remembered selection
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
		alignas(ModelStackWithAutoParam) char memory[MODEL_STACK_MAX_SIZE];
		ModelStackWithTimelineCounter* stack = currentSong->setupModelStackWithCurrentClip(memory);
		ModelStackWithNoteRow* with_row = getCurrentInstrumentClip()->getNoteRowOnScreen(y, stack);
		if (with_row->getNoteRowAllowNull() == nullptr) {
			with_row = instrumentClipView.createNoteRowForYDisplay(stack, y);
		}
		return with_row->getNoteRowAllowNull();
	}

	/// @brief One note at tick @p pos on row @p y, @p length ticks long.
	void add_note(int32_t y, int32_t pos, int32_t length, int32_t probability) {
		alignas(ModelStackWithAutoParam) char memory[MODEL_STACK_MAX_SIZE];
		ModelStackWithTimelineCounter* stack = currentSong->setupModelStackWithCurrentClip(memory);
		NoteRow* row = row_at(y);
		int32_t row_index = 0;
		(void)getCurrentInstrumentClip()->getNoteRowOnScreen(y, currentSong, &row_index);
		ModelStackWithNoteRow* with_row =
		    stack->addNoteRow(getCurrentInstrumentClip()->getNoteRowId(row, row_index), row);
		(void)row->attemptNoteAdd(pos, length, /*velocity=*/80, probability, Iterance{},
		                          std::to_underlying(deluge::project::FillMode::OFF), with_row,
		                          /*action=*/nullptr);
	}

	/// @brief A note filling square (@p x, @p y) at the clip's zoom.
	void add_square_note(int32_t x, int32_t y, int32_t probability) {
		const int32_t zoom = currentSong->persistentView.xZoom[NAVIGATION_CLIP];
		add_note(y, x * zoom, zoom, probability);
	}

	/// @brief Press (@p x, @p y) the way `InstrumentClipView` presses a pad, and remember to release it.
	void press(int32_t x, int32_t y) {
		instrumentClipView.editPadAction(true, y, x, currentSong->persistentView.xZoom[NAVIGATION_CLIP]);
		pressed.push_back({x, y});
	}

	/// @brief Row @p y auditioning, the state the note-row editor's own entry guarantees -- set
	///        directly, because a real audition press would sound the synth.
	static void audition_row(int32_t y) {
		instrumentClipView.lastAuditionedYDisplay = y;
		enterUIMode(UI_MODE_AUDITIONING);
	}

	static void drain_notices() {
		deluge::base::notify::Notice n;
		while (deluge::base::notify::notices().pop(n)) {}
	}
};

/// @brief The clip's own play direction, through the clip-setting edit the model reads it back from.
void set_clip_direction(SongFixture& fx, SequenceDirection direction) {
	deluge::edits::clipSettingFor(
	    *fx.clip, "sequenceDirection",
	    deluge::serialization::Value::makeScalar(std::string{deluge::project::to_string(direction)}));
}

/// @brief Row @p y's own play direction, set the way `NoteRow::setSequenceDirectionMode` is reached.
void set_row_direction(LegacyViewGuard& legacy, int32_t y, SequenceDirection direction) {
	NoteRow* row = legacy.row_at(y);
	alignas(ModelStackWithAutoParam) char memory[MODEL_STACK_MAX_SIZE];
	ModelStackWithTimelineCounter* stack = currentSong->setupModelStackWithCurrentClip(memory);
	int32_t row_index = 0;
	(void)getCurrentInstrumentClip()->getNoteRowOnScreen(y, currentSong, &row_index);
	row->setSequenceDirectionMode(stack->addNoteRow(getCurrentInstrumentClip()->getNoteRowId(row, row_index), row),
	                              direction);
}

/// @brief The last notice queued since the previous drain, or empty.
std::string drain_last_notice() {
	deluge::base::notify::Notice n;
	std::string text;
	while (deluge::base::notify::notices().pop(n)) {
		text = n.text;
	}
	return text;
}

/// @brief Is the menu on the global guest's stack at all?
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
describe note_mode_reads_predicates("util/note_editor_open.h -- legacy's question, asked of the menu scene's mode too", $ {
	it("both answer false with the menu off the guest's stack, and on it at a root that is not a note root", _ {
		UiStackGuard ui_guard;
		GlobalGuestGuard guest_guard;
		LegacyViewGuard legacy;
		SongFixture fx = deluge::ui::scenes::spec::make_synth_song();
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
		SongFixture fx = deluge::ui::scenes::spec::make_synth_song();

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
		SongFixture fx = deluge::ui::scenes::spec::make_synth_song();
		expect(open_note_mode_over_clip_view(fx, Root::NoteEditor)).to_be_true();

		expect(shellHostUI.shell().stack().remove(SceneKind::FirstApp)).to_be_true();
		expect(guest::noteEditorOpen()).to_be_false();
		expect(guest::noteRowEditorOpen()).to_be_false();
	});
});

describe note_mode_reads_note_editor("the note editor's handlers' own menu calls", $ {
	before_all([] { deluge::gui::ui::wireShellNoteBridge(); });

	it("a vertical turn off a note item keeps the selection and the focused item -- the note editor remembers its "
	   "selection across a scroll (handleNoteEditorVerticalEncoderAction is verticalEncoderAction)", _ {
		UiStackGuard ui_guard;
		GlobalGuestGuard guest_guard;
		LegacyViewGuard legacy;
		SongFixture fx = deluge::ui::scenes::spec::make_synth_song();
		legacy.add_square_note(0, 0, deluge::project::kProbabilityValueCount);
		legacy.press(0, 0);
		expect(open_note_mode_over_clip_view(fx, Root::NoteEditor)).to_be_true();
		menu_scene().enter_submenu(nodes::note_editor_probability);
		expect(menu_scene().state().depth).to_equal(uint8_t{1});

		(void)guest::forwardNoteVerticalEncoder(0);

		expect(menu_scene().state().depth).to_equal(uint8_t{1});
		expect(menu_scene().focused()).to_equal(&nodes::note_editor_probability);
		expect(menu_on_guest_stack()).to_be_true();
		expect(std::ranges::any_of(instrumentClipView.get_note_edit_targets(), &EditPadPress::isActive)).to_be_true();
	});

	it("a vertical turn at the root neither goes up nor closes", _ {
		UiStackGuard ui_guard;
		GlobalGuestGuard guest_guard;
		LegacyViewGuard legacy;
		SongFixture fx = deluge::ui::scenes::spec::make_synth_song();
		legacy.add_square_note(0, 0, deluge::project::kProbabilityValueCount);
		legacy.press(0, 0);
		expect(open_note_mode_over_clip_view(fx, Root::NoteEditor)).to_be_true();

		(void)guest::forwardNoteVerticalEncoder(0);

		expect(menu_scene().state().depth).to_equal(uint8_t{0});
		expect(menu_scene().focused()).to_equal(&nodes::note_editor_root);
		expect(menu_on_guest_stack()).to_be_true();
	});

	it("goUpOneLevel's port steps up from depth 1, and at depth 0 closes the menu as exitCompletely does", _ {
		UiStackGuard ui_guard;
		GlobalGuestGuard guest_guard;
		LegacyViewGuard legacy;
		SongFixture fx = deluge::ui::scenes::spec::make_synth_song();
		legacy.add_square_note(0, 0, deluge::project::kProbabilityValueCount);
		legacy.press(0, 0);
		expect(open_note_mode_over_clip_view(fx, Root::NoteEditor)).to_be_true();
		menu_scene().enter_submenu(nodes::note_velocity);

		guest::noteModeGoUpOneLevel();
		expect(menu_scene().state().depth).to_equal(uint8_t{0});
		expect(menu_on_guest_stack()).to_be_true();

		guest::noteModeGoUpOneLevel();
		expect(menu_on_guest_stack()).to_be_false();
		expect(guest::activeNoteMode()).to_equal(NoteModeKind::None);
	});

	it("pressing a pad holding a different note re-reads the focused item for that note -- "
	   "handleNoteEditorEditPadAction's readValueAgain", _ {
		UiStackGuard ui_guard;
		GlobalGuestGuard guest_guard;
		LegacyViewGuard legacy;
		SongFixture fx = deluge::ui::scenes::spec::make_synth_song();
		legacy.add_square_note(0, 0, deluge::project::kProbabilityValueCount);
		legacy.add_square_note(1, 0, 10);
		legacy.press(0, 0);
		expect(open_note_mode_over_clip_view(fx, Root::NoteEditor)).to_be_true();
		menu_scene().enter_submenu(nodes::note_editor_probability);
		expect(menu_scene().state().edit_int).to_equal(int32_t{deluge::project::kProbabilityValueCount});

		// Release the first pad -- the note editor remembers it -- then press the other: a fresh press
		// replaces the remembered selection, and the item re-reads for the note now held.
		guest::noteModeLegacyCalls().noteEditorEditPad(0, 0, 0);
		legacy.pressed.clear();
		guest::noteModeLegacyCalls().noteEditorEditPad(1, 0, 1);
		legacy.pressed.push_back({1, 0});

		expect(std::ranges::any_of(instrumentClipView.get_note_edit_targets(),
		                            [](const EditPadPress& p) { return p.xDisplay == 1; }))
		    .to_be_true();
		expect(menu_scene().state().edit_int).to_equal(int32_t{10});
	});

	it("with two pads held, re-reads the focused item for the leftmost note, as note::Probability does", _ {
		UiStackGuard ui_guard;
		GlobalGuestGuard guest_guard;
		LegacyViewGuard legacy;
		SongFixture fx = deluge::ui::scenes::spec::make_synth_song();
		legacy.add_square_note(0, 0, deluge::project::kProbabilityValueCount);
		legacy.add_square_note(1, 0, 10);
		legacy.press(0, 0);
		expect(open_note_mode_over_clip_view(fx, Root::NoteEditor)).to_be_true();
		menu_scene().enter_submenu(nodes::note_editor_probability);

		// The second press is the later one and the rightmost; the item still reads the leftmost.
		guest::noteModeLegacyCalls().noteEditorEditPad(1, 0, 1);
		legacy.pressed.push_back({1, 0});
		expect(menu_scene().state().edit_int).to_equal(int32_t{deluge::project::kProbabilityValueCount});
	});

	it("a velocity turn on the full-screen velocity item shows the item's new value and no popup -- adjustVelocity's "
	   "noteVelocityMenu arm", _ {
		UiStackGuard ui_guard;
		GlobalGuestGuard guest_guard;
		LegacyViewGuard legacy;
		SongFixture fx = deluge::ui::scenes::spec::make_synth_song();
		legacy.add_square_note(0, 0, deluge::project::kProbabilityValueCount); // velocity 80
		legacy.press(0, 0);
		expect(open_note_mode_over_clip_view(fx, Root::NoteEditor)).to_be_true();
		menu_scene().enter_submenu(nodes::note_velocity);
		expect(menu_scene().state().edit_int).to_equal(int32_t{80});
		deluge::hid::display::OLED::removePopup();

		instrumentClipView.adjustVelocity(5);

		expect(display->hasPopup()).to_be_false();
		expect(menu_scene().state().edit_int).to_equal(int32_t{85});
	});

	it("with horizontal menus on, the note editor's root shows velocity as a column: no popup", _ {
		UiStackGuard ui_guard;
		GlobalGuestGuard guest_guard;
		LegacyViewGuard legacy;
		FeatureSetting horizontal(RuntimeFeatureSettingType::HorizontalMenus, RuntimeFeatureStateToggle::On);
		SongFixture fx = deluge::ui::scenes::spec::make_synth_song();
		legacy.add_square_note(0, 0, deluge::project::kProbabilityValueCount);
		legacy.press(0, 0);
		expect(open_note_mode_over_clip_view(fx, Root::NoteEditor)).to_be_true();
		deluge::hid::display::OLED::removePopup();
		(void)deluge::base::notify::dirty().drain();

		instrumentClipView.adjustVelocity(5);

		expect(display->hasPopup()).to_be_false();
		(void)deluge::base::notify::dirty().drain(deluge::base::notify::DirtyConsumer::Legacy);
		expect(deluge::base::notify::dirty().drain(deluge::base::notify::DirtyConsumer::Shell).has(deluge::base::notify::Dirty::Oled))
		    .to_be_true();
	});

	it("with horizontal menus off, the root is a list without velocity: the popup shows, as main's", _ {
		UiStackGuard ui_guard;
		GlobalGuestGuard guest_guard;
		LegacyViewGuard legacy;
		FeatureSetting horizontal(RuntimeFeatureSettingType::HorizontalMenus, RuntimeFeatureStateToggle::Off);
		SongFixture fx = deluge::ui::scenes::spec::make_synth_song();
		legacy.add_square_note(0, 0, deluge::project::kProbabilityValueCount);
		legacy.press(0, 0);
		expect(open_note_mode_over_clip_view(fx, Root::NoteEditor)).to_be_true();
		deluge::hid::display::OLED::removePopup();

		instrumentClipView.adjustVelocity(5);

		expect(display->hasPopup()).to_be_true();
	});
});

describe note_mode_reads_note_row_editor("the note-row editor's handlers' own menu calls", $ {
	before_all([] { deluge::gui::ui::wireShellNoteBridge(); });

	it("holding a note pad at the row root enters the note root and releasing it goes back, with activeNoteMode() "
	   "NoteRowEditor at every step -- toggleNoteEditorParamMenu's root arms", _ {
		UiStackGuard ui_guard;
		GlobalGuestGuard guest_guard;
		LegacyViewGuard legacy;
		FeatureSetting horizontal(RuntimeFeatureSettingType::HorizontalMenus, RuntimeFeatureStateToggle::On);
		SongFixture fx = deluge::ui::scenes::spec::make_synth_song();
		legacy.add_square_note(0, 0, deluge::project::kProbabilityValueCount);
		LegacyViewGuard::audition_row(0);
		expect(open_note_mode_over_clip_view(fx, Root::NoteRowEditor)).to_be_true();
		expect(guest::activeNoteMode()).to_equal(NoteModeKind::NoteRowEditor);

		expect(guest::noteModeLegacyCalls().noteRowEditorPad(0, 0, 1)).to_be_true();
		legacy.pressed.push_back({0, 0});
		expect(menu_scene().state().depth).to_equal(uint8_t{1});
		expect(menu_scene().focused()).to_equal(&nodes::note_editor_root);
		expect(guest::activeNoteMode()).to_equal(NoteModeKind::NoteRowEditor);
		expect(menu_scene().menu_context().root).to_equal(Root::NoteRowEditor);

		expect(guest::noteModeLegacyCalls().noteRowEditorPad(0, 0, 0)).to_be_true();
		expect(menu_scene().state().depth).to_equal(uint8_t{0});
		expect(menu_scene().focused()).to_equal(&nodes::note_row_editor_root);
		expect(guest::activeNoteMode()).to_equal(NoteModeKind::NoteRowEditor);
	});

	it("on the row's probability item, holding a note enters the note's probability item and releasing it comes back "
	   "-- the param arms", _ {
		UiStackGuard ui_guard;
		GlobalGuestGuard guest_guard;
		LegacyViewGuard legacy;
		SongFixture fx = deluge::ui::scenes::spec::make_synth_song();
		legacy.add_square_note(0, 0, deluge::project::kProbabilityValueCount);
		LegacyViewGuard::audition_row(0);
		expect(open_note_mode_over_clip_view(fx, Root::NoteRowEditor)).to_be_true();
		menu_scene().enter_submenu(nodes::note_row_probability);

		expect(guest::noteModeLegacyCalls().noteRowEditorPad(0, 0, 1)).to_be_true();
		legacy.pressed.push_back({0, 0});
		expect(menu_scene().state().depth).to_equal(uint8_t{2});
		expect(menu_scene().focused()).to_equal(&nodes::note_editor_probability);
		expect(guest::activeNoteMode()).to_equal(NoteModeKind::NoteRowEditor);

		expect(guest::noteModeLegacyCalls().noteRowEditorPad(0, 0, 0)).to_be_true();
		expect(menu_scene().state().depth).to_equal(uint8_t{1});
		expect(menu_scene().focused()).to_equal(&nodes::note_row_probability);
		expect(guest::activeNoteMode()).to_equal(NoteModeKind::NoteRowEditor);
	});

	it("with horizontal menus off, holding a note pad at the row root moves nothing -- runtimeFeatureSettings read as "
	   "legacy reads it", _ {
		UiStackGuard ui_guard;
		GlobalGuestGuard guest_guard;
		LegacyViewGuard legacy;
		FeatureSetting horizontal(RuntimeFeatureSettingType::HorizontalMenus, RuntimeFeatureStateToggle::Off);
		SongFixture fx = deluge::ui::scenes::spec::make_synth_song();
		legacy.add_square_note(0, 0, deluge::project::kProbabilityValueCount);
		LegacyViewGuard::audition_row(0);
		expect(open_note_mode_over_clip_view(fx, Root::NoteRowEditor)).to_be_true();

		expect(guest::noteModeLegacyCalls().noteRowEditorPad(0, 0, 1)).to_be_true();
		legacy.pressed.push_back({0, 0});
		expect(menu_scene().state().depth).to_equal(uint8_t{0});
		expect(menu_scene().focused()).to_equal(&nodes::note_row_editor_root);
	});

	it("the release stays on the note item and raises \"Select Row\" when no row is auditioning -- the target's "
	   "shouldEnterSubmenu() refuses", _ {
		UiStackGuard ui_guard;
		GlobalGuestGuard guest_guard;
		LegacyViewGuard legacy;
		SongFixture fx = deluge::ui::scenes::spec::make_synth_song();
		legacy.add_square_note(0, 0, deluge::project::kProbabilityValueCount);
		LegacyViewGuard::audition_row(0);
		expect(open_note_mode_over_clip_view(fx, Root::NoteRowEditor)).to_be_true();
		menu_scene().enter_submenu(nodes::note_row_probability);
		expect(guest::noteModeLegacyCalls().noteRowEditorPad(0, 0, 1)).to_be_true();
		legacy.pressed.push_back({0, 0});
		expect(menu_scene().focused()).to_equal(&nodes::note_editor_probability);

		exitUIMode(UI_MODE_AUDITIONING);
		(void)drain_last_notice();
		expect(guest::noteModeLegacyCalls().noteRowEditorPad(0, 0, 0)).to_be_true();
		expect(menu_scene().focused()).to_equal(&nodes::note_editor_probability);
		expect(drain_last_notice()).to_equal(std::string{"Select Row"});
	});

	it("with no note selected, a press on the row's probability item stays put, raises \"Select Note\" and leaves "
	   "activeNoteMode() NoteRowEditor -- the toggle's press refusal", _ {
		UiStackGuard ui_guard;
		GlobalGuestGuard guest_guard;
		LegacyViewGuard legacy;
		SongFixture fx = deluge::ui::scenes::spec::make_synth_song();
		LegacyViewGuard::audition_row(0);
		expect(open_note_mode_over_clip_view(fx, Root::NoteRowEditor)).to_be_true();
		menu_scene().enter_submenu(nodes::note_row_probability);
		expect(std::ranges::none_of(instrumentClipView.get_note_edit_targets(), &EditPadPress::isActive)).to_be_true();
		(void)drain_last_notice();

		// The port directly, not through the pad handler: every real press that holds a note also selects it
		// (`InstrumentClipView::editPadAction`), so no press reaches the toggle in
		// `InstrumentClipView::handleNoteRowEditorMainPadAction` with nothing selected. This is the call that toggle
		// makes.
		guest::noteModeToggleNoteEditorParamMenu(1);

		expect(menu_scene().state().depth).to_equal(uint8_t{1});
		expect(menu_scene().focused()).to_equal(&nodes::note_row_probability);
		expect(drain_last_notice()).to_equal(std::string{"Select Note"});
		expect(guest::activeNoteMode()).to_equal(NoteModeKind::NoteRowEditor);
	});

	it("releasing one of two held notes re-reads the focused item for the note still held -- "
	   "handleNoteRowEditorMainPadAction's readValueAgain", _ {
		UiStackGuard ui_guard;
		GlobalGuestGuard guest_guard;
		LegacyViewGuard legacy;
		SongFixture fx = deluge::ui::scenes::spec::make_synth_song();
		legacy.add_square_note(0, 0, deluge::project::kProbabilityValueCount);
		legacy.add_square_note(1, 0, 10);
		LegacyViewGuard::audition_row(0);
		expect(open_note_mode_over_clip_view(fx, Root::NoteRowEditor)).to_be_true();
		menu_scene().enter_submenu(nodes::note_row_probability);

		expect(guest::noteModeLegacyCalls().noteRowEditorPad(0, 0, 1)).to_be_true(); // the toggle: into the note item
		legacy.pressed.push_back({0, 0});
		expect(guest::noteModeLegacyCalls().noteRowEditorPad(1, 0, 1)).to_be_true(); // still holding: a re-read
		legacy.pressed.push_back({1, 0});
		expect(menu_scene().focused()).to_equal(&nodes::note_editor_probability);
		expect(menu_scene().state().edit_int).to_equal(int32_t{deluge::project::kProbabilityValueCount});

		expect(guest::noteModeLegacyCalls().noteRowEditorPad(0, 0, 0)).to_be_true(); // one released, one held
		expect(menu_scene().focused()).to_equal(&nodes::note_editor_probability);
		expect(menu_scene().state().edit_int).to_equal(int32_t{10});
	});

	it("an audition pad on another row re-reads the focused item for that row -- "
	   "handleNoteRowEditorAuditionPadAction's readValueAgain", _ {
		UiStackGuard ui_guard;
		GlobalGuestGuard guest_guard;
		LegacyViewGuard legacy;
		HeldButton shift_for_silence(deluge::hid::button::SHIFT); // `auditionPadAction`'s silent arm: no synth voice
		SongFixture fx = deluge::ui::scenes::spec::make_synth_song();
		legacy.row_at(0)->probabilityValue = 5;
		LegacyViewGuard::audition_row(0);
		expect(open_note_mode_over_clip_view(fx, Root::NoteRowEditor)).to_be_true();
		menu_scene().enter_submenu(nodes::note_row_probability);
		expect(menu_scene().state().edit_int).to_equal(int32_t{5});

		expect(guest::noteModeLegacyCalls().noteRowEditorPad(kDisplayWidth + 1, 1, 1)).to_be_true();

		expect(instrumentClipView.lastAuditionedYDisplay).to_equal(uint8_t{1});
		expect(menu_scene().state().edit_int).to_equal(int32_t{legacy.row_at(1)->probabilityValue});
		expect(menu_scene().state().edit_int).not_().to_equal(5);
	});

	it("an audition on another row re-reads Play Direction for that row -- potentiallyRefreshNoteRowMenu", _ {
		UiStackGuard ui_guard;
		GlobalGuestGuard guest_guard;
		LegacyViewGuard legacy;
		SongFixture fx = deluge::ui::scenes::spec::make_synth_song();
		NoteRow* row = legacy.row_at(0);
		LegacyViewGuard::audition_row(0);
		expect(open_note_mode_over_clip_view(fx, Root::NoteRowEditor)).to_be_true();
		menu_scene().enter_submenu(nodes::sequence_direction);
		const int32_t before = menu_scene().state().enum_index;

		alignas(ModelStackWithAutoParam) char memory[MODEL_STACK_MAX_SIZE];
		ModelStackWithTimelineCounter* stack = currentSong->setupModelStackWithCurrentClip(memory);
		int32_t row_index = 0;
		(void)fx.clip->getNoteRowOnScreen(0, currentSong, &row_index);
		row->setSequenceDirectionMode(stack->addNoteRow(fx.clip->getNoteRowId(row, row_index), row),
		                              SequenceDirection::PINGPONG);
		expect(static_cast<int32_t>(row->sequenceDirectionMode())).not_().to_equal(before);

		instrumentClipView.potentiallyRefreshNoteRowMenu();
		expect(menu_scene().state().enum_index).to_equal(static_cast<int32_t>(row->sequenceDirectionMode()));
	});

	it("a vertical turn off the row root re-reads the focused item -- "
	   "handleNoteRowEditorVerticalEncoderAction's readValueAgain", _ {
		UiStackGuard ui_guard;
		GlobalGuestGuard guest_guard;
		LegacyViewGuard legacy;
		SongFixture fx = deluge::ui::scenes::spec::make_synth_song();
		NoteRow* row = legacy.row_at(0);
		row->probabilityValue = 5;
		LegacyViewGuard::audition_row(0);
		expect(open_note_mode_over_clip_view(fx, Root::NoteRowEditor)).to_be_true();
		menu_scene().enter_submenu(nodes::note_row_probability);
		expect(menu_scene().state().edit_int).to_equal(int32_t{5});

		row->probabilityValue = 7; // behind the menu's back
		(void)guest::forwardNoteVerticalEncoder(0);
		expect(menu_scene().state().edit_int).to_equal(int32_t{7});
	});

	it("a mute pad on another drum's row leaves the selected drum alone -- mutePadPress' `inNoteEditor`", _ {
		UiStackGuard ui_guard;
		GlobalGuestGuard guest_guard;
		LegacyViewGuard legacy;
		SongFixture fx = deluge::ui::scenes::spec::make_kit_song();
		fx.clip->yScroll = 0; // `note_bridge_spec.cpp`'s kit example says why
		auto* first = deluge::ui::scenes::spec::add_drum<GateDrum>(fx);
		auto* second = deluge::ui::scenes::spec::add_drum<GateDrum>(fx);
		Drum* const row_drum = fx.clip->getNoteRowOnScreen(1, currentSong)->drum;
		Drum* const other = row_drum == first ? static_cast<Drum*>(second) : static_cast<Drum*>(first);
		Kit* const kit = static_cast<Kit*>(fx.clip->output);
		kit->selectedDrum = other;
		expect(open_note_mode_over_clip_view(fx, Root::NoteRowEditor)).to_be_true();

		expect(guest::noteModeLegacyCalls().noteRowEditorPad(kDisplayWidth, 1, 1)).to_be_true();
		expect(kit->selectedDrum).to_equal(other);
	});

	it("a main pad on a kit y with no NoteRow creates no row and leaves the selected drum alone -- editPadAction's "
	   "`inNoteRowEditor`", _ {
		// the `inNoteRowEditor` arm of `InstrumentClipView::editPadAction` (`gui/views/instrument_clip_view.cpp`)
		// returns before it creates a row (or, failing that, clears the kit's selected drum).
		UiStackGuard ui_guard;
		GlobalGuestGuard guest_guard;
		LegacyViewGuard legacy;
		SongFixture fx = deluge::ui::scenes::spec::make_kit_song();
		fx.clip->yScroll = 0; // `note_bridge_spec.cpp`'s kit example says why
		auto* drum = deluge::ui::scenes::spec::add_drum<GateDrum>(fx);
		Kit* const kit = static_cast<Kit*>(fx.clip->output);
		kit->selectedDrum = drum;
		const auto rows = std::ssize(fx.clip->noteRows);
		expect(fx.clip->getNoteRowOnScreen(1, currentSong)).to_be_null();
		LegacyViewGuard::audition_row(0);
		expect(open_note_mode_over_clip_view(fx, Root::NoteRowEditor)).to_be_true();

		expect(guest::noteModeLegacyCalls().noteRowEditorPad(0, 1, 1)).to_be_true();
		legacy.pressed.push_back({0, 1});

		expect(std::ssize(fx.clip->noteRows)).to_equal(rows);
		expect(kit->selectedDrum).to_equal(drum);
	});

	it("a vertical turn past the kit's last row with that row auditioning does not scroll -- scrollVertical's "
	   "`preventOverScrolling`", _ {
		// `InstrumentClipView::scrollVertical`'s `preventOverScrolling` limit: no row at the
		// auditioned y once scrolled, so no scroll.
		UiStackGuard ui_guard;
		GlobalGuestGuard guest_guard;
		LegacyViewGuard legacy;
		SongFixture fx = deluge::ui::scenes::spec::make_kit_song();
		fx.clip->yScroll = 0;
		(void)deluge::ui::scenes::spec::add_drum<GateDrum>(fx);
		(void)deluge::ui::scenes::spec::add_drum<GateDrum>(fx);
		expect(fx.clip->getNoteRowOnScreen(2, currentSong)).to_be_null();
		LegacyViewGuard::audition_row(1);
		expect(open_note_mode_over_clip_view(fx, Root::NoteRowEditor)).to_be_true();

		(void)guest::forwardNoteVerticalEncoder(1);
		expect(fx.clip->yScroll).to_equal(int32_t{0});
	});

	it("SHIFT with a vertical turn does not shift the clip's colour -- verticalEncoderAction's `inNoteRowEditor`", _ {
		// `InstrumentClipView::verticalEncoderAction` (`gui/views/instrument_clip_view.cpp`): no colour shifting in the
		// note-row editor; the turn falls to the scroll arm instead of `commandShiftColour`'s whole-clip shift.
		UiStackGuard ui_guard;
		GlobalGuestGuard guest_guard;
		LegacyViewGuard legacy;
		SongFixture fx = deluge::ui::scenes::spec::make_synth_song();
		LegacyViewGuard::audition_row(0);
		expect(open_note_mode_over_clip_view(fx, Root::NoteRowEditor)).to_be_true();
		const auto colour = fx.clip->colourOffset();
		HeldShift shift;

		(void)guest::forwardNoteVerticalEncoder(1);
		expect(fx.clip->colourOffset()).to_equal(colour);
	});

	it("an audition on another drum's row gives the gold knobs that drum -- setSelectedDrum's "
	   "`inNoteOrNoteRowEditor`", _ {
		// `startAuditioningRow` selects the row's drum (`gui/views/instrument_clip_view.cpp`), and
		// `InstrumentClipView::setSelectedDrum` then re-targets the active mod-controllable.
		UiStackGuard ui_guard;
		GlobalGuestGuard guest_guard;
		LegacyViewGuard legacy;
		SongFixture fx = deluge::ui::scenes::spec::make_kit_song();
		fx.clip->yScroll = 0;
		fx.clip->affectEntire = false;
		(void)deluge::ui::scenes::spec::add_drum<SoundDrum>(fx);
		(void)deluge::ui::scenes::spec::add_drum<SoundDrum>(fx);
		Drum* const row0 = fx.clip->getNoteRowOnScreen(0, currentSong)->drum;
		Drum* const row1 = fx.clip->getNoteRowOnScreen(1, currentSong)->drum;
		Kit* const kit = static_cast<Kit*>(fx.clip->output);
		kit->selectedDrum = row0;
		// The clip already active on its kit, as a booted session has it: otherwise the audition's
		// `makeCurrentClipActiveOnInstrumentIfPossible` runs
		// `Kit::setActiveClip`, which services the audio routine for a SOUND drum (`model/instrument/kit.cpp`)
		// and faults in a spec process, as `note_bridge_spec.cpp`'s `ActionLogGuard` records.
		ActiveClipSeed active_clip(*kit, fx.clip);
		LegacyViewGuard::audition_row(0);
		expect(open_note_mode_over_clip_view(fx, Root::NoteRowEditor)).to_be_true();
		view.setActiveModControllableTimelineCounter(fx.clip, false);
		expect(knobs_move(deluge::model::owner_of(*row0))).to_be_true();

		expect(guest::noteModeLegacyCalls().noteRowEditorPad(kDisplayWidth + 1, 1, 1)).to_be_true();

		expect(kit->selectedDrum).to_equal(row1);
		expect(knobs_move(deluge::model::owner_of(*row1))).to_be_true();
	});

	it("nudging a held note out of its square goes up one level -- nudgeNotes' goUpOneLevel", _ {
		UiStackGuard ui_guard;
		GlobalGuestGuard guest_guard;
		LegacyViewGuard legacy;
		FeatureSetting horizontal(RuntimeFeatureSettingType::HorizontalMenus, RuntimeFeatureStateToggle::On);
		SongFixture fx = deluge::ui::scenes::spec::make_synth_song();
		legacy.add_note(0, /*pos=*/0, /*length=*/1, deluge::project::kProbabilityValueCount);
		LegacyViewGuard::audition_row(0);
		expect(open_note_mode_over_clip_view(fx, Root::NoteRowEditor)).to_be_true();
		expect(guest::noteModeLegacyCalls().noteRowEditorPad(0, 0, 1)).to_be_true(); // the toggle: into the note root
		legacy.pressed.push_back({0, 0});
		expect(menu_scene().focused()).to_equal(&nodes::note_editor_root);

		HeldButton x_enc(deluge::hid::button::X_ENC);
		// Nudged left from tick 0, the note wraps to the row's end (`nudgeNotes`' `newPos +=
		// noteRowEffectiveLength`), which leaves square 0 empty.
		(void)guest::forwardNoteHorizontalEncoder(-1);

		expect(menu_scene().state().depth).to_equal(uint8_t{0});
		expect(menu_scene().focused()).to_equal(&nodes::note_row_editor_root);
		expect(menu_on_guest_stack()).to_be_true();
	});
});

describe note_mode_reads_menu_entries("MenuScene::go_up_one_level / enter_submenu -- the two entries the note modes use", $ {
	it("go_up_one_level answers false at depth 0, steps up otherwise, and answers false when the step lands on a "
	   "refused root -- goUpOneLevel's two exitCompletely passes", _ {
		// `SoundEditor::goUpOneLevel` (`origin/main:src/deluge/gui/ui/sound_editor.cpp`): `exitCompletely` on the
		// loop's first pass at depth 0, and again on a later pass when the loop's `checkPermissionToBeginSession` test
		// refuses the level it stepped down to.
		SongFixture fx = deluge::ui::scenes::spec::make_synth_song();
		const MenuContext ctx = deluge::ui::scenes::spec::menu_context_at(fx, Root::Song);

		menu_scene().open(kOpenRoot, ctx);
		expect(menu_scene().go_up_one_level()).to_be_false(); // first pass, at depth 0
		menu_scene().enter_submenu(kChildPage);
		expect(menu_scene().state().depth).to_equal(uint8_t{1});
		expect(menu_scene().go_up_one_level()).to_be_true();
		expect(menu_scene().state().depth).to_equal(uint8_t{0});
		expect(menu_scene().focused()).to_equal(&kOpenRoot);

		menu_scene().open(kRefusedRoot, ctx);
		menu_scene().enter_submenu(kChildPage); // no permission asked, as `enterSubmenu` asks none
		expect(menu_scene().state().depth).to_equal(uint8_t{1});
		expect(menu_scene().go_up_one_level()).to_be_false(); // a later pass, refused at depth 0
		expect(menu_scene().state().depth).to_equal(uint8_t{0});
		// The caller closes the editor on false; the scene asks for no close of its own afterwards.
		const deluge::ui::shell::Result after = menu_scene().after_render();
		for (const deluge::ui::shell::Action& action : after.actions) {
			expect(action.kind).not_().to_equal(deluge::ui::shell::Action::Kind::Remove);
		}
		expect(menu_scene().menu_context().root).to_equal(Root::Song); // neither entry touches the context
	});
});

describe note_mode_reads_legacy_leg("each edited file's legacy arm with the menu closed, beside its note-mode twin", $ {
	before_all([] { deluge::gui::ui::wireShellNoteBridge(); });

	it("instrument_clip_view.cpp: adjust_note_probability's first turn with no popup changes nothing and raises the "
	   "PROBABILITY popup", _ {
		UiStackGuard ui_guard;
		GlobalGuestGuard guest_guard;
		LegacyViewGuard legacy;
		SongFixture fx = deluge::ui::scenes::spec::make_synth_song();
		setRootUILowLevel(&instrumentClipView);
		legacy.add_square_note(0, 0, deluge::project::kProbabilityValueCount);
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
		SongFixture fx = deluge::ui::scenes::spec::make_kit_song();
		fx.clip->yScroll = 0;
		auto* first = deluge::ui::scenes::spec::add_drum<GateDrum>(fx);
		auto* second = deluge::ui::scenes::spec::add_drum<GateDrum>(fx);
		setRootUILowLevel(&instrumentClipView);
		Drum* const row_drum = fx.clip->getNoteRowOnScreen(1, currentSong)->drum;
		Drum* const other = row_drum == first ? static_cast<Drum*>(second) : static_cast<Drum*>(first);
		Kit* const kit = static_cast<Kit*>(fx.clip->output);
		kit->selectedDrum = other;
		expect(menu_on_guest_stack()).to_be_false();

		instrumentClipView.mutePadPress(1);
		expect(kit->selectedDrum).to_equal(row_drum);
	});

	it("action_logger.cpp: menu closed, getNewAction allows under the root and refuses under any other current UI", _ {
		// Legacy's gate, `ActionLogger::getNewAction` (`model/action/action_logger.cpp`), with no menu on the guest's
		// stack.
		UiStackGuard ui_guard;
		GlobalGuestGuard guest_guard;
		LegacyViewGuard legacy;
		SongFixture fx = deluge::ui::scenes::spec::make_synth_song();
		setRootUILowLevel(&instrumentClipView);
		expect(menu_on_guest_stack()).to_be_false();
		expect(actionLogger.getNewAction(ActionType::NOTE_EDIT, ActionAddition::ALLOWED)).not_().to_be_null();

		// The guest current over the root, the menu then taken off its stack: a current UI that is neither
		// the root, the sound editor nor `loadPatternUI`.
		expect(open_note_mode_over_clip_view(fx, Root::NoteEditor)).to_be_true();
		expect(shellHostUI.shell().stack().remove(SceneKind::FirstApp)).to_be_true();
		expect(menu_on_guest_stack()).to_be_false();
		expect(getCurrentUI()).to_equal(static_cast<UI*>(&shellHostUI));
		expect(actionLogger.getNewAction(ActionType::NOTE_EDIT, ActionAddition::ALLOWED)).to_be_null();
	});

	it("action_logger.cpp: the note-mode twin -- with the guest current, getNewAction refuses at a non-note root "
	   "and allows at a note root", _ {
		UiStackGuard ui_guard;
		GlobalGuestGuard guest_guard;
		LegacyViewGuard legacy;
		SongFixture fx = deluge::ui::scenes::spec::make_synth_song();

		expect(open_note_mode_over_clip_view(fx, Root::Sound)).to_be_true();
		expect(actionLogger.getNewAction(ActionType::NOTE_EDIT, ActionAddition::ALLOWED)).to_be_null();

		nullifyUIs();
		GlobalGuestGuard::empty_guest_stack();
		expect(open_note_mode_over_clip_view(fx, Root::NoteEditor)).to_be_true();
		expect(actionLogger.getNewAction(ActionType::NOTE_EDIT, ActionAddition::ALLOWED)).not_().to_be_null();
	});

	it("view.cpp: SYNC_SCALING as Fill, auditioning, menu closed -- the row-fill arm: no fill mode, FILL popup", _ {
		UiStackGuard ui_guard;
		GlobalGuestGuard guest_guard;
		LegacyViewGuard legacy;
		FeatureSetting fill(RuntimeFeatureSettingType::SyncScalingAction, RuntimeFeatureStateSyncScalingAction::Fill);
		SongFixture fx = deluge::ui::scenes::spec::make_synth_song();
		setRootUILowLevel(&instrumentClipView);
		LegacyViewGuard::audition_row(0);
		currentSong->changeFillMode(false);

		(void)view.buttonAction(deluge::hid::button::SYNC_SCALING, true);
		expect(currentSong->isFillModeActive()).to_be_false();
		expect(display->hasPopupOfType(PopupType::FILL)).to_be_true();
	});

	it("view.cpp: the same press with the note-row editor open -- the note-editor arm: fill mode on, no popup", _ {
		UiStackGuard ui_guard;
		GlobalGuestGuard guest_guard;
		LegacyViewGuard legacy;
		FeatureSetting fill(RuntimeFeatureSettingType::SyncScalingAction, RuntimeFeatureStateSyncScalingAction::Fill);
		SongFixture fx = deluge::ui::scenes::spec::make_synth_song();
		LegacyViewGuard::audition_row(0);
		currentSong->changeFillMode(false);
		expect(open_note_mode_over_clip_view(fx, Root::NoteRowEditor)).to_be_true();

		(void)view.buttonAction(deluge::hid::button::SYNC_SCALING, true);
		expect(currentSong->isFillModeActive()).to_be_true();
		expect(display->hasPopup()).to_be_false();
		currentSong->changeFillMode(false);
	});
});
describe note_mode_reads_sound_direction("Play Direction under the sound menu -- getIndividualNoteRow's selectedNoteRow arm", $ {
	before_all([] { deluge::gui::ui::wireShellNoteBridge(); });

	it("with no row auditioning it reads the clip's direction", _ {
		UiStackGuard ui_guard;
		GlobalGuestGuard guest_guard;
		LegacyViewGuard legacy;
		SongFixture fx = deluge::ui::scenes::spec::make_synth_song();
		set_clip_direction(fx, SequenceDirection::REVERSE);
		set_row_direction(legacy, 0, SequenceDirection::PINGPONG);
		instrumentClipView.lastAuditionedYDisplay = 0;
		expect(open_note_mode_over_clip_view(fx, Root::Sound)).to_be_true();
		menu_scene().enter_submenu(nodes::sequence_direction);
		expect(menu_scene().state().enum_index).to_equal(static_cast<int32_t>(SequenceDirection::REVERSE));
	});

	it("with a row auditioning when it is entered it reads that row's direction", _ {
		UiStackGuard ui_guard;
		GlobalGuestGuard guest_guard;
		LegacyViewGuard legacy;
		SongFixture fx = deluge::ui::scenes::spec::make_synth_song();
		set_clip_direction(fx, SequenceDirection::REVERSE);
		set_row_direction(legacy, 0, SequenceDirection::PINGPONG);
		LegacyViewGuard::audition_row(0);
		expect(open_note_mode_over_clip_view(fx, Root::Sound)).to_be_true();
		menu_scene().enter_submenu(nodes::sequence_direction);
		expect(menu_scene().state().enum_index).to_equal(static_cast<int32_t>(SequenceDirection::PINGPONG));
	});

	it("the latch outlives the audition: released after entry, the row is still the one read", _ {
		UiStackGuard ui_guard;
		GlobalGuestGuard guest_guard;
		LegacyViewGuard legacy;
		SongFixture fx = deluge::ui::scenes::spec::make_synth_song();
		set_clip_direction(fx, SequenceDirection::REVERSE);
		set_row_direction(legacy, 0, SequenceDirection::PINGPONG);
		LegacyViewGuard::audition_row(0);
		expect(open_note_mode_over_clip_view(fx, Root::Sound)).to_be_true();
		menu_scene().enter_submenu(nodes::sequence_direction);
		currentUIMode = UI_MODE_NONE;
		menu_scene().read_value_again();
		expect(menu_scene().state().enum_index).to_equal(static_cast<int32_t>(SequenceDirection::PINGPONG));
	});

	it("an audition on another row re-reads it for that row -- potentiallyRefreshNoteRowMenu's soundEditor arm", _ {
		UiStackGuard ui_guard;
		GlobalGuestGuard guest_guard;
		LegacyViewGuard legacy;
		SongFixture fx = deluge::ui::scenes::spec::make_synth_song();
		set_clip_direction(fx, SequenceDirection::REVERSE);
		set_row_direction(legacy, 0, SequenceDirection::PINGPONG);
		set_row_direction(legacy, 1, SequenceDirection::FORWARD);
		LegacyViewGuard::audition_row(0);
		expect(open_note_mode_over_clip_view(fx, Root::Sound)).to_be_true();
		menu_scene().enter_submenu(nodes::sequence_direction);
		expect(menu_scene().state().enum_index).to_equal(static_cast<int32_t>(SequenceDirection::PINGPONG));

		(void)deluge::base::notify::dirty().drain();
		instrumentClipView.lastAuditionedYDisplay = 1;
		instrumentClipView.potentiallyRefreshNoteRowMenu();
		expect(menu_scene().state().enum_index).to_equal(static_cast<int32_t>(SequenceDirection::FORWARD));
		// `Value::readValueAgain`'s `renderUIsForOled()` (`origin/main:src/deluge/gui/menu_item/value.h`): a re-read
		// value is shown on the shell's next tick, which a pending fact asks for, whatever the legacy drain takes
		// first.
		expect(deluge::base::notify::dirty().pending(deluge::base::notify::DirtyConsumer::Shell)).to_be_true();
		(void)deluge::base::notify::dirty().drain(deluge::base::notify::DirtyConsumer::Legacy);
		expect(deluge::base::notify::dirty().drain(deluge::base::notify::DirtyConsumer::Shell).has(deluge::base::notify::Dirty::Oled))
		    .to_be_true();
		// And nothing holds a 0 ms timer for it.
		expect(menu_scene().next_timer()).not_().to_equal(std::optional<uint32_t>{0});
	});

	it("real audition presses forwarded from the menu re-read it for the newly auditioned row", _ {
		UiStackGuard ui_guard;
		GlobalGuestGuard guest_guard;
		LegacyViewGuard legacy;
		HeldButton shift_for_silence(deluge::hid::button::SHIFT); // `auditionPadAction`'s silent arm: no synth voice
		SongFixture fx = deluge::ui::scenes::spec::make_synth_song();
		set_clip_direction(fx, SequenceDirection::REVERSE);
		set_row_direction(legacy, 4, SequenceDirection::PINGPONG);
		set_row_direction(legacy, 5, SequenceDirection::FORWARD);
		instrumentClipView.lastAuditionedYDisplay = 0;
		expect(open_note_mode_over_clip_view(fx, Root::Sound)).to_be_true();
		(void)deluge::gui::ui::forwardGridPad(kDisplayWidth + 1, 4, 64);
		menu_scene().enter_submenu(nodes::sequence_direction);
		expect(menu_scene().state().enum_index).to_equal(static_cast<int32_t>(SequenceDirection::PINGPONG));

		(void)deluge::gui::ui::forwardGridPad(kDisplayWidth + 1, 4, 0);
		(void)deluge::gui::ui::forwardGridPad(kDisplayWidth + 1, 5, 64);
		expect(menu_scene().state().enum_index).to_equal(static_cast<int32_t>(SequenceDirection::FORWARD));
		(void)deluge::gui::ui::forwardGridPad(kDisplayWidth + 1, 5, 0);
	});

	it("entered with no audition, a later audition does not move it off the clip", _ {
		UiStackGuard ui_guard;
		GlobalGuestGuard guest_guard;
		LegacyViewGuard legacy;
		SongFixture fx = deluge::ui::scenes::spec::make_synth_song();
		set_clip_direction(fx, SequenceDirection::REVERSE);
		set_row_direction(legacy, 1, SequenceDirection::FORWARD);
		instrumentClipView.lastAuditionedYDisplay = 0;
		expect(open_note_mode_over_clip_view(fx, Root::Sound)).to_be_true();
		menu_scene().enter_submenu(nodes::sequence_direction);
		LegacyViewGuard::audition_row(1);
		instrumentClipView.potentiallyRefreshNoteRowMenu();
		menu_scene().read_value_again();
		expect(menu_scene().state().enum_index).to_equal(static_cast<int32_t>(SequenceDirection::REVERSE));
	});
});
// clang-format on

DELUGE_SPEC(note_mode_reads_spec, note_mode_reads_predicates, note_mode_reads_note_editor,
            note_mode_reads_note_row_editor, note_mode_reads_menu_entries, note_mode_reads_legacy_leg,
            note_mode_reads_sound_direction)
