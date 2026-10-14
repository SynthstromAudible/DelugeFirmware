/// @file
/// @brief Note-family Task 7: `gui/ui/shell_note_mode.h` -- the note editor's and note-row editor's
///        routing through the guest, as `SoundEditor` routes them (design spec §4, sites 2, 3, 4, 6
///        and 7; site 8, the pads, is `pad_forward_spec.cpp`'s).
///
/// **Against a real `MenuScene` on the global guest's own stack**, because `activeNoteMode()` reads
/// exactly that: `shellHostUI`'s `SceneStack` for the menu's identity, then `menu_context().root`.
///
/// **Through a recording seam, mostly, and why.** The legacy handlers these functions forward into
/// dereference `soundEditor.getCurrentMenuItem()` -- `menuItemNavigationRecord[navigationDepth]`,
/// null under the guest -- on most of their paths: `readValueAgain` (`instrument_clip_view.cpp:3677`,
/// `:3905`, `:3946`, `:3982-3984`), `goUpOneLevel` (`:3720-3721`) and `toggleNoteEditorParamMenu`
/// (`:3894`). Task 7a put an `activeNoteMode()` arm ahead of each, and `note_mode_reads_spec.cpp`
/// drives those; this file predates it, so no example here takes a handler down one of those paths
/// with the ported menu open. `setNoteModeLegacyCalls` replaces every `instrumentClipView` call with a recorder that
/// answers a settable `ActionResult`, and the routing is pinned through it.
///
/// **Driven for real, and why each is safe:** the two button handlers on the paths that touch no
/// navigation record -- AFFECT_ENTIRE in both modes (`handleNoteEditorButtonAction`'s fall-through,
/// `:3762`; `handleNoteRowEditorButtonAction`'s kit toggle, `:4043-4049`) and every swallowed button
/// (`:3762`, `:4057`). X_ENC and SYNC_SCALING are pinned through the seam only: their `return
/// buttonAction(...)` arm (`:3752-3754`) reaches `InstrumentClipView::buttonAction`, whose SYNC_SCALING fill arm asks
/// `noteEditorOpen() || noteRowEditorOpen()` (`gui/views/view.cpp:340`) -- one of correction 7's
/// reads, which Task 7a widened to see the ported note mode, so it takes the in-editor arm now.
///
/// **Not covered here:** that a hook feeds the shell when a function answers `std::nullopt` and not
/// when it answers a value -- `tests/spec/gui/shell_host_ui_spec.cpp`'s, against fakes.

#include "gui/ui/shell_note_mode.h"

#include "definitions_cxx.hpp"
#include "gui/ui/shell_host_ui.h" // shellHostUI
#include "gui/ui/ui.h"            // setRootUILowLevel, nullifyUIs, openUI, clearPendingUIRendering
#include "gui/ui_timer_manager.h"
#include "gui/views/session_view.h"
#include "hid/button.h"
#include "model/clip/instrument_clip.h"
#include "notify/dirty_set.h"
#include "processing/sound/sound_drum.h"

#include "scenes/menu/context.h"
#include "scenes/menu/menu_scene.h"
#include "scenes/menu/node_id.h"
#include "scenes/menu/nodes/nodes.h"
#include "scenes/overlay/param_popup_scene.h"
#include "scenes/registry.h"
#include "shell/scene.h"

#include "support/global_guest_menu.h"
#include "support/song_fixture.h"

#include "cppspec.hpp"

#include <array>
#include <cstdint>
#include <optional>

#include "spec/support/spec_main.h"

// `gui/ui/ui.cpp:267-268`: the pending-grid-render accumulators `uiNeedsRendering` writes, which
// `ui.h` does not export. Declared here the way `file_catalogue_spec.cpp` reaches its own globals.
extern uint32_t whichMainRowsNeedRendering;
extern uint32_t whichSideRowsNeedRendering;

using namespace deluge::scenes;
using deluge::gui::ui::NoteModeKind;
using deluge::scenes::menu::MenuContext;
using deluge::scenes::menu::Node;
using deluge::ui::shell::SceneId;
using deluge::ui::shell::SceneKind;
namespace nodes = deluge::scenes::menu::nodes;
namespace button = deluge::hid::button;
namespace guest = deluge::gui::ui;
using deluge::scenes::spec::GlobalGuestGuard;
using deluge::scenes::spec::menu_context_at;
using deluge::scenes::spec::open_menu_on_global_guest_at;
using deluge::scenes::spec::UiStackGuard;

namespace {

using Root = MenuContext::Root;

/// @brief Every root that is neither note root.
constexpr std::array kOtherRoots{Root::Sound,     Root::Drum,        Root::MidiOrCv, Root::MidiDrum,    Root::GateDrum,
                                 Root::AudioClip, Root::Performance, Root::Song,     Root::KitGlobalFx, Root::Settings};

/// @brief Every button `hid/button.h` names.
constexpr std::array kAllButtons{
    button::AFFECT_ENTIRE, button::SESSION_VIEW, button::CLIP_VIEW, button::SYNTH,         button::KIT,
    button::MIDI,          button::CV,           button::KEYBOARD,  button::SCALE_MODE,    button::CROSS_SCREEN_EDIT,
    button::BACK,          button::LOAD,         button::SAVE,      button::LEARN,         button::TAP_TEMPO,
    button::SYNC_SCALING,  button::TRIPLETS,     button::PLAY,      button::RECORD,        button::SHIFT,
    button::MOD7,          button::X_ENC,        button::Y_ENC,     button::MOD_ENCODER_0, button::MOD_ENCODER_1,
    button::SELECT_ENC,    button::TEMPO_ENC,
};

// ---- the recording seam -------------------------------------------------------------------------

/// @brief Which legacy call a recorder stood in for.
enum class Call : uint8_t {
	EditorButton,
	RowButton,
	EditorHorizontal,
	RowHorizontal,
	EditorVertical,
	RowVertical,
	EditorEditPad,
	RowPad,
	ExitEditor,
	ExitRow,
	Count,
};

/// @brief Everything the recorders saw, and what they answer.
struct Recorder {
	std::array<int, static_cast<size_t>(Call::Count)> counts{};
	deluge::hid::Button b = 0;
	bool on = false;
	bool in_card_routine = false;
	int32_t offset = 0;
	/// What every recording handler returns.
	ActionResult answer = ActionResult::DEALT_WITH;

	int count(Call c) const { return counts[static_cast<size_t>(c)]; }
	int total() const {
		int n = 0;
		for (int c : counts) {
			n += c;
		}
		return n;
	}
};
Recorder g_rec{};

ActionResult record_button(Call c, deluge::hid::Button b, bool on, bool in_card_routine) {
	g_rec.counts[static_cast<size_t>(c)]++;
	g_rec.b = b;
	g_rec.on = on;
	g_rec.in_card_routine = in_card_routine;
	return g_rec.answer;
}

ActionResult record_turn(Call c, int32_t offset, bool in_card_routine) {
	g_rec.counts[static_cast<size_t>(c)]++;
	g_rec.offset = offset;
	g_rec.in_card_routine = in_card_routine;
	return g_rec.answer;
}

const guest::NoteModeLegacyCalls kRecording{
    .noteEditorButton = [](deluge::hid::Button b, bool on,
                           bool icr) { return record_button(Call::EditorButton, b, on, icr); },
    .noteRowEditorButton = [](deluge::hid::Button b, bool on,
                              bool icr) { return record_button(Call::RowButton, b, on, icr); },
    .noteEditorHorizontal = [](int32_t offset) { return record_turn(Call::EditorHorizontal, offset, false); },
    .noteRowEditorHorizontal = [](int32_t offset) { return record_turn(Call::RowHorizontal, offset, false); },
    .noteEditorVertical = [](int32_t offset, bool icr) { return record_turn(Call::EditorVertical, offset, icr); },
    .noteRowEditorVertical = [](int32_t offset, bool icr) { return record_turn(Call::RowVertical, offset, icr); },
    .noteEditorEditPad = [](int32_t, int32_t, int32_t) { g_rec.counts[static_cast<size_t>(Call::EditorEditPad)]++; },
    .noteRowEditorPad =
        [](int32_t, int32_t, int32_t) {
	        g_rec.counts[static_cast<size_t>(Call::RowPad)]++;
	        return true;
        },
    .exitNoteEditor = [] { g_rec.counts[static_cast<size_t>(Call::ExitEditor)]++; },
    .exitNoteRowEditor = [] { g_rec.counts[static_cast<size_t>(Call::ExitRow)]++; },
};

/// @brief Install the recorders for one example, and put the real calls back however it ends.
struct RecordingCalls {
	RecordingCalls(const RecordingCalls&) = delete;
	RecordingCalls& operator=(const RecordingCalls&) = delete;
	RecordingCalls() {
		g_rec = Recorder{};
		guest::setNoteModeLegacyCalls(&kRecording);
	}
	~RecordingCalls() { guest::setNoteModeLegacyCalls(nullptr); }
};

/// @brief The button handler the mode's own arm names (`sound_editor.cpp:618`, `:622`).
Call button_call_for(Root root) {
	return root == Root::NoteEditor ? Call::EditorButton : Call::RowButton;
}

} // namespace

// clang-format off
describe note_mode_active("gui::ui::activeNoteMode -- inNoteEditor() / inNoteRowEditor(), for input", $ {
	it("answers the ported menu's root while the menu is on the guest's stack", _ {
		GlobalGuestGuard guest_guard;
		deluge::scenes::spec::SongFixture fx = deluge::scenes::spec::make_synth_song();

		expect(open_menu_on_global_guest_at(fx, Root::NoteEditor)).to_be_true();
		expect(guest::activeNoteMode() == NoteModeKind::NoteEditor).to_be_true();

		GlobalGuestGuard::empty_guest_stack();
		expect(open_menu_on_global_guest_at(fx, Root::NoteRowEditor)).to_be_true();
		expect(guest::activeNoteMode() == NoteModeKind::NoteRowEditor).to_be_true();
	});

	it("answers None for every other root on the guest's stack", _ {
		GlobalGuestGuard guest_guard;
		deluge::scenes::spec::SongFixture fx = deluge::scenes::spec::make_synth_song();
		for (Root root : kOtherRoots) {
			GlobalGuestGuard::empty_guest_stack();
			expect(open_menu_on_global_guest_at(fx, root)).to_be_true();
			expect(menu_scene().menu_context().root == root).to_be_true();
			expect(guest::activeNoteMode() == NoteModeKind::None).to_be_true();
		}
	});

	it("answers None once the menu has left the guest's stack, though menu_context().root still reads "
	   "NoteEditor", _ {
		// `MenuScene::ctx_` outlives the scene's time on the stack (it is replaced only by `open()`
		// and a song swap), so reading the root alone would leave the mode on after the menu is gone.
		GlobalGuestGuard guest_guard;
		deluge::scenes::spec::SongFixture fx = deluge::scenes::spec::make_synth_song();
		expect(open_menu_on_global_guest_at(fx, Root::NoteEditor)).to_be_true();

		expect(shellHostUI.shell().stack().remove(SceneKind::FirstApp)).to_be_true();
		expect(menu_scene().menu_context().root == Root::NoteEditor).to_be_true();
		expect(guest::activeNoteMode() == NoteModeKind::None).to_be_true();
	});

	it("leaves the mode on under a scene that takes input above the menu, and under one that does not", _ {
		// Legacy's `inNoteEditor()` reads `menuItemNavigationRecord[0]` (`sound_editor.cpp:1916-1918`),
		// not `getCurrentUI()`, so a UI opened over the sound editor does not switch the mode off --
		// which is why this is "on the stack" and not `menuIsFrontmost`'s `input_top()`.
		GlobalGuestGuard guest_guard;
		deluge::scenes::spec::SongFixture fx = deluge::scenes::spec::make_synth_song();
		expect(open_menu_on_global_guest_at(fx, Root::NoteEditor)).to_be_true();

		// `SceneKind::Loading` is the loading scene the shell registers for itself
		// (`shell/shell.cpp:187`), and it takes input -- `input_transparent()` is false
		// (`shell/loading_scene.h:33`) -- so with it on top, the menu is on the stack and NOT the input
		// top.
		expect(shellHostUI.shell().stack().push(SceneId{SceneKind::Loading, 0})).to_be_true();
		expect(&shellHostUI.shell().stack().input_top()
		       == static_cast<deluge::ui::shell::Scene*>(&menu_scene()))
		    .to_be_false();
		expect(guest::activeNoteMode() == NoteModeKind::NoteEditor).to_be_true();

		// The mod-encoder value popup, named by its own registration constant
		// (`scenes/overlay/param_popup_scene.h:109-110`) and asserted to BE that scene once pushed, so
		// a registry reorder cannot silently retarget this half at another kind. It is
		// input-transparent (`param_popup_scene.h:195`).
		expect(shellHostUI.shell().stack().push(SceneId{overlay::kParamPopupSceneKind, 0})).to_be_true();
		expect(&shellHostUI.shell().stack().top() == static_cast<deluge::ui::shell::Scene*>(&overlay::param_popup_scene()))
		    .to_be_true();
		expect(guest::activeNoteMode() == NoteModeKind::NoteEditor).to_be_true();
	});

	it("answers None after a song swap resets the menu's context", _ {
		// `MenuScene::on_model_changed`'s `SongSwapped` arm replaces `ctx_` with a default context,
		// whose root is `Settings` (`scenes/menu/context.h:162`).
		GlobalGuestGuard guest_guard;
		deluge::scenes::spec::SongFixture fx = deluge::scenes::spec::make_synth_song();
		expect(open_menu_on_global_guest_at(fx, Root::NoteRowEditor)).to_be_true();

		deluge::notify::DirtySet dirty;
		dirty.raise(deluge::notify::Dirty::SongSwapped);
		menu_scene().on_model_changed(dirty);
		expect(shellHostUI.shell().stack().depth() > 0u).to_be_true();
		expect(guest::activeNoteMode() == NoteModeKind::None).to_be_true();
	});
});

describe note_mode_buttons("gui::ui::forwardNoteButton -- SoundEditor::buttonAction's note arms", $ {
	it("answers nothing outside the note modes, for every button, and calls nothing", _ {
		RecordingCalls recording;
		GlobalGuestGuard guest_guard;
		deluge::scenes::spec::SongFixture fx = deluge::scenes::spec::make_synth_song();
		expect(open_menu_on_global_guest_at(fx, Root::Sound)).to_be_true();

		for (deluge::hid::Button b : kAllButtons) {
			expect(guest::forwardNoteButton(b, true, false).has_value()).to_be_false();
		}
		expect(g_rec.total()).to_equal(0);
	});

	it("answers nothing in either mode for the six buttons legacy's chain takes first", _ {
		// `sound_editor.cpp:385`, `:456`, `:488`, `:505`, `:564`, `:605`: each an outer `else if` on
		// the button alone, so it consumes the press before `:617` is reached, inner test or not.
		RecordingCalls recording;
		GlobalGuestGuard guest_guard;
		deluge::scenes::spec::SongFixture fx = deluge::scenes::spec::make_synth_song();
		for (Root root : {Root::NoteEditor, Root::NoteRowEditor}) {
			GlobalGuestGuard::empty_guest_stack();
			expect(open_menu_on_global_guest_at(fx, root)).to_be_true();
			for (deluge::hid::Button b : {button::SELECT_ENC, button::BACK, button::SAVE, button::LEARN,
			                              button::KEYBOARD, button::SESSION_VIEW}) {
				expect(guest::forwardNoteButton(b, true, false).has_value()).to_be_false();
				expect(guest::forwardNoteButton(b, false, false).has_value()).to_be_false();
			}
		}
		expect(g_rec.total()).to_equal(0);
	});

	it("answers nothing in either mode for SYNTH, KIT, MIDI and CV -- the column focus is the menu's", _ {
		// Both handlers send these to `soundEditor.getCurrentMenuItem()->buttonAction(...)`
		// (`instrument_clip_view.cpp:3746-3748`, `:4033-4035`), i.e. to the horizontal menu's column
		// focus -- design spec §3's, which `MenuScene` owns -- through a record that is null here.
		RecordingCalls recording;
		GlobalGuestGuard guest_guard;
		deluge::scenes::spec::SongFixture fx = deluge::scenes::spec::make_synth_song();
		for (Root root : {Root::NoteEditor, Root::NoteRowEditor}) {
			GlobalGuestGuard::empty_guest_stack();
			expect(open_menu_on_global_guest_at(fx, root)).to_be_true();
			for (deluge::hid::Button b : {button::SYNTH, button::KIT, button::MIDI, button::CV}) {
				expect(guest::forwardNoteButton(b, true, false).has_value()).to_be_false();
			}
		}
		expect(g_rec.total()).to_equal(0);
	});

	it("answers nothing in either mode for PLAY, SHIFT and both gold-knob pushes", _ {
		// `:3727-3729`, `:3975-3977`: `NOT_DEALT_WITH`, which is what the guest's own fed path
		// already returns -- so feeding them loses nothing legacy's answer carried.
		RecordingCalls recording;
		GlobalGuestGuard guest_guard;
		deluge::scenes::spec::SongFixture fx = deluge::scenes::spec::make_synth_song();
		for (Root root : {Root::NoteEditor, Root::NoteRowEditor}) {
			GlobalGuestGuard::empty_guest_stack();
			expect(open_menu_on_global_guest_at(fx, root)).to_be_true();
			for (deluge::hid::Button b : {button::PLAY, button::SHIFT, button::MOD_ENCODER_0, button::MOD_ENCODER_1}) {
				expect(guest::forwardNoteButton(b, true, false).has_value()).to_be_false();
			}
		}
		expect(g_rec.total()).to_equal(0);
	});

	it("forwards X_ENC and SYNC_SCALING to the mode's own handler with the press's arguments, and "
	   "returns its result", _ {
		// Both handlers' `:3752-3754` / `:4039-4041` arm is `return buttonAction(b, on, inCardRoutine)`
		// -- `InstrumentClipView::buttonAction`, whose answer is the handler's answer. Seam only: see
		// the file's note on `view.cpp:340`.
		RecordingCalls recording;
		GlobalGuestGuard guest_guard;
		deluge::scenes::spec::SongFixture fx = deluge::scenes::spec::make_synth_song();
		for (Root root : {Root::NoteEditor, Root::NoteRowEditor}) {
			GlobalGuestGuard::empty_guest_stack();
			expect(open_menu_on_global_guest_at(fx, root)).to_be_true();
			for (deluge::hid::Button b : {button::X_ENC, button::SYNC_SCALING}) {
				g_rec = Recorder{};
				g_rec.answer = ActionResult::REMIND_ME_OUTSIDE_CARD_ROUTINE;
				const std::optional<ActionResult> pressed = guest::forwardNoteButton(b, true, true);
				expect(pressed.value_or(ActionResult::DEALT_WITH) == ActionResult::REMIND_ME_OUTSIDE_CARD_ROUTINE)
				    .to_be_true();
				expect(g_rec.count(button_call_for(root))).to_equal(1);
				expect(g_rec.total()).to_equal(1);
				expect(g_rec.b == b).to_be_true();
				expect(g_rec.on).to_be_true();
				expect(g_rec.in_card_routine).to_be_true();

				g_rec.answer = ActionResult::ACTIONED_AND_CAUSED_CHANGE;
				const std::optional<ActionResult> released = guest::forwardNoteButton(b, false, false);
				expect(released.value_or(ActionResult::DEALT_WITH) == ActionResult::ACTIONED_AND_CAUSED_CHANGE)
				    .to_be_true();
				expect(g_rec.on).to_be_false();
				expect(g_rec.in_card_routine).to_be_false();
			}
		}
	});

	it("forwards every other button to the mode's own handler -- AFFECT_ENTIRE, CROSS_SCREEN and SCALE "
	   "included", _ {
		// Site 3: `sound_editor.cpp:540`'s AFFECT_ENTIRE arm is guarded on `!inNoteEditor() &&
		// !inNoteRowEditor()`, so in either mode the button falls to `:617`/`:621` like every button no
		// earlier arm names.
		RecordingCalls recording;
		GlobalGuestGuard guest_guard;
		deluge::scenes::spec::SongFixture fx = deluge::scenes::spec::make_synth_song();
		for (Root root : {Root::NoteEditor, Root::NoteRowEditor}) {
			GlobalGuestGuard::empty_guest_stack();
			expect(open_menu_on_global_guest_at(fx, root)).to_be_true();
			for (deluge::hid::Button b : {button::AFFECT_ENTIRE, button::CROSS_SCREEN_EDIT, button::SCALE_MODE,
			                              button::CLIP_VIEW, button::LOAD, button::TAP_TEMPO, button::TRIPLETS,
			                              button::RECORD, button::Y_ENC, button::TEMPO_ENC}) {
				g_rec = Recorder{};
				expect(guest::forwardNoteButton(b, true, false).has_value()).to_be_true();
				expect(g_rec.count(button_call_for(root))).to_equal(1);
				expect(g_rec.total()).to_equal(1);
				expect(g_rec.b == b).to_be_true();
			}
		}
	});

	it("the note editor's real handler swallows AFFECT_ENTIRE on a kit: DEALT_WITH, affectEntire untouched", _ {
		// `handleNoteEditorButtonAction` names no AFFECT_ENTIRE arm and falls to
		// `instrument_clip_view.cpp:3762`.
		GlobalGuestGuard guest_guard;
		deluge::scenes::spec::SongFixture fx = deluge::scenes::spec::make_kit_song();
		(void)deluge::scenes::spec::add_drum<SoundDrum>(fx);
		fx.clip->affectEntire = false;
		expect(open_menu_on_global_guest_at(fx, Root::NoteEditor)).to_be_true();

		const std::optional<ActionResult> result = guest::forwardNoteButton(button::AFFECT_ENTIRE, true, false);
		expect(result.has_value() && *result == ActionResult::DEALT_WITH).to_be_true();
		expect(fx.clip->affectEntire).to_be_false();
	});

	it("the note-row editor's real handler toggles a kit clip's affectEntire on the press, not the release", _ {
		// `instrument_clip_view.cpp:4043-4049`: `if (on && b == AFFECT_ENTIRE)`, then `if (KIT) { affectEntire = !affectEntire;
		// view.setActiveModControllableTimelineCounter(clip); }`, and `DEALT_WITH` at `:4057`.
		GlobalGuestGuard guest_guard;
		deluge::scenes::spec::SongFixture fx = deluge::scenes::spec::make_kit_song();
		(void)deluge::scenes::spec::add_drum<SoundDrum>(fx);
		fx.clip->affectEntire = false;
		expect(open_menu_on_global_guest_at(fx, Root::NoteRowEditor)).to_be_true();

		std::optional<ActionResult> result = guest::forwardNoteButton(button::AFFECT_ENTIRE, true, false);
		expect(result.has_value() && *result == ActionResult::DEALT_WITH).to_be_true();
		expect(fx.clip->affectEntire).to_be_true();

		result = guest::forwardNoteButton(button::AFFECT_ENTIRE, false, false);
		expect(result.has_value() && *result == ActionResult::DEALT_WITH).to_be_true();
		expect(fx.clip->affectEntire).to_be_true();

		result = guest::forwardNoteButton(button::AFFECT_ENTIRE, true, false);
		expect(result.has_value() && *result == ActionResult::DEALT_WITH).to_be_true();
		expect(fx.clip->affectEntire).to_be_false();
	});

	it("the note-row editor's real handler leaves a synth clip's affectEntire alone", _ {
		// Control for the example above, varying the output type: `:3967`'s `KIT` test.
		GlobalGuestGuard guest_guard;
		deluge::scenes::spec::SongFixture fx = deluge::scenes::spec::make_synth_song();
		fx.clip->affectEntire = false;
		expect(open_menu_on_global_guest_at(fx, Root::NoteRowEditor)).to_be_true();

		const std::optional<ActionResult> result = guest::forwardNoteButton(button::AFFECT_ENTIRE, true, false);
		expect(result.has_value() && *result == ActionResult::DEALT_WITH).to_be_true();
		expect(fx.clip->affectEntire).to_be_false();
	});

	it("the real handlers swallow any other button in both modes: DEALT_WITH", _ {
		// `:3731`, `:3979`. None of these reaches a navigation-record read: the only arms above either
		// return are SYNTH/KIT/MIDI/CV, X_ENC/SYNC_SCALING, PLAY/SHIFT/MOD and the kit toggle.
		GlobalGuestGuard guest_guard;
		deluge::scenes::spec::SongFixture fx = deluge::scenes::spec::make_synth_song();
		for (Root root : {Root::NoteEditor, Root::NoteRowEditor}) {
			GlobalGuestGuard::empty_guest_stack();
			expect(open_menu_on_global_guest_at(fx, root)).to_be_true();
			for (deluge::hid::Button b : {button::CROSS_SCREEN_EDIT, button::SCALE_MODE, button::CLIP_VIEW,
			                              button::LOAD, button::TAP_TEMPO, button::TRIPLETS, button::RECORD,
			                              button::Y_ENC, button::TEMPO_ENC}) {
				const std::optional<ActionResult> result = guest::forwardNoteButton(b, true, false);
				expect(result.has_value() && *result == ActionResult::DEALT_WITH).to_be_true();
			}
		}
	});

	it("through the guest's own hook: AFFECT_ENTIRE in the note-row editor on a kit is the handler's", _ {
		// End to end in the one binary where every half is real: `ShellHostUI::buttonAction` ->
		// `forwardNoteButton` -> `handleNoteRowEditorButtonAction`.
		GlobalGuestGuard guest_guard;
		deluge::scenes::spec::SongFixture fx = deluge::scenes::spec::make_kit_song();
		(void)deluge::scenes::spec::add_drum<SoundDrum>(fx);
		fx.clip->affectEntire = false;
		expect(open_menu_on_global_guest_at(fx, Root::NoteRowEditor)).to_be_true();

		expect(shellHostUI.buttonAction(button::AFFECT_ENTIRE, true, false) == ActionResult::DEALT_WITH).to_be_true();
		expect(fx.clip->affectEntire).to_be_true();
	});
});

describe note_mode_encoders("gui::ui::forwardNote*Encoder -- SoundEditor's two encoder hooks' note arms", $ {
	it("answers nothing for either encoder outside the note modes, and calls nothing", _ {
		// `sound_editor.cpp:1047-1052` and `:1593-1598` are the first test in each hook; neither holds.
		RecordingCalls recording;
		GlobalGuestGuard guest_guard;
		deluge::scenes::spec::SongFixture fx = deluge::scenes::spec::make_synth_song();
		expect(open_menu_on_global_guest_at(fx, Root::Sound)).to_be_true();

		expect(guest::forwardNoteHorizontalEncoder(1).has_value()).to_be_false();
		expect(guest::forwardNoteVerticalEncoder(-1, false).has_value()).to_be_false();
		expect(g_rec.total()).to_equal(0);
	});

	it("forwards a horizontal turn to the mode's own handler and returns its result, "
	   "REMIND_ME_OUTSIDE_CARD_ROUTINE included", _ {
		RecordingCalls recording;
		GlobalGuestGuard guest_guard;
		deluge::scenes::spec::SongFixture fx = deluge::scenes::spec::make_synth_song();
		for (Root root : {Root::NoteEditor, Root::NoteRowEditor}) {
			GlobalGuestGuard::empty_guest_stack();
			expect(open_menu_on_global_guest_at(fx, root)).to_be_true();
			g_rec = Recorder{};
			g_rec.answer = ActionResult::REMIND_ME_OUTSIDE_CARD_ROUTINE;

			const std::optional<ActionResult> result = guest::forwardNoteHorizontalEncoder(-3);
			expect(result.value_or(ActionResult::DEALT_WITH) == ActionResult::REMIND_ME_OUTSIDE_CARD_ROUTINE)
			    .to_be_true();
			expect(g_rec.count(root == Root::NoteEditor ? Call::EditorHorizontal : Call::RowHorizontal)).to_equal(1);
			expect(g_rec.total()).to_equal(1);
			expect(g_rec.offset).to_equal(int32_t(-3));
		}
	});

	it("forwards a vertical turn, with inCardRoutine, to the mode's own handler and returns its result", _ {
		RecordingCalls recording;
		GlobalGuestGuard guest_guard;
		deluge::scenes::spec::SongFixture fx = deluge::scenes::spec::make_synth_song();
		for (Root root : {Root::NoteEditor, Root::NoteRowEditor}) {
			GlobalGuestGuard::empty_guest_stack();
			expect(open_menu_on_global_guest_at(fx, root)).to_be_true();
			const Call call = root == Root::NoteEditor ? Call::EditorVertical : Call::RowVertical;

			g_rec = Recorder{};
			g_rec.answer = ActionResult::REMIND_ME_OUTSIDE_CARD_ROUTINE;
			std::optional<ActionResult> result = guest::forwardNoteVerticalEncoder(2, true);
			expect(result.value_or(ActionResult::DEALT_WITH) == ActionResult::REMIND_ME_OUTSIDE_CARD_ROUTINE)
			    .to_be_true();
			expect(g_rec.count(call)).to_equal(1);
			expect(g_rec.total()).to_equal(1);
			expect(g_rec.offset).to_equal(int32_t(2));
			expect(g_rec.in_card_routine).to_be_true();

			g_rec.answer = ActionResult::ACTIONED_AND_CAUSED_CHANGE;
			result = guest::forwardNoteVerticalEncoder(-1, false);
			expect(result.value_or(ActionResult::DEALT_WITH) == ActionResult::ACTIONED_AND_CAUSED_CHANGE).to_be_true();
			expect(g_rec.count(call)).to_equal(2);
			expect(g_rec.in_card_routine).to_be_false();
		}
	});
});

describe note_mode_exit("gui::ui::exitNoteModeBeforeClose -- SoundEditor::exitCompletely's note arms", $ {
	it("calls exitNoteEditor for the note editor and exitNoteRowEditor for the note-row editor, once each", _ {
		RecordingCalls recording;
		GlobalGuestGuard guest_guard;
		deluge::scenes::spec::SongFixture fx = deluge::scenes::spec::make_synth_song();

		expect(open_menu_on_global_guest_at(fx, Root::NoteEditor)).to_be_true();
		guest::exitNoteModeBeforeClose();
		expect(g_rec.count(Call::ExitEditor)).to_equal(1);
		expect(g_rec.total()).to_equal(1);

		GlobalGuestGuard::empty_guest_stack();
		expect(open_menu_on_global_guest_at(fx, Root::NoteRowEditor)).to_be_true();
		guest::exitNoteModeBeforeClose();
		expect(g_rec.count(Call::ExitRow)).to_equal(1);
		expect(g_rec.total()).to_equal(2);
	});

	it("calls neither on any other root", _ {
		RecordingCalls recording;
		GlobalGuestGuard guest_guard;
		deluge::scenes::spec::SongFixture fx = deluge::scenes::spec::make_synth_song();
		for (Root root : kOtherRoots) {
			GlobalGuestGuard::empty_guest_stack();
			expect(open_menu_on_global_guest_at(fx, root)).to_be_true();
			guest::exitNoteModeBeforeClose();
		}
		expect(g_rec.total()).to_equal(0);
	});

	it("still calls the root's exit once the menu has left the guest's stack -- which is where closeGuest "
	   "calls it", _ {
		// `ShellHostUI::exitUI` pops before it calls `closeGuest`, and `timerCallback` calls it only on
		// an empty stack: keyed on `activeNoteMode()`, this would never fire from either.
		RecordingCalls recording;
		GlobalGuestGuard guest_guard;
		deluge::scenes::spec::SongFixture fx = deluge::scenes::spec::make_synth_song();
		expect(open_menu_on_global_guest_at(fx, Root::NoteEditor)).to_be_true();
		expect(shellHostUI.shell().stack().remove(SceneKind::FirstApp)).to_be_true();
		expect(guest::activeNoteMode() == NoteModeKind::None).to_be_true();

		guest::exitNoteModeBeforeClose();
		expect(g_rec.count(Call::ExitEditor)).to_equal(1);
		expect(g_rec.total()).to_equal(1);
	});

	it("repaints the guest's main rows after the note editor's exit, as legacy's arm repaints its own, and "
	   "not after the note-row editor's", _ {
		// `sound_editor.cpp:751-752`: `uiNeedsRendering(this, 0xFFFFFFFF, 0)`, where `this` is the menu
		// UI that is about to close -- the guest, here. The note-row arm (`:754`) has no repaint. The
		// recorder stands in for `exitNoteEditor`, so its own `uiNeedsRendering(this, ...)`
		// (`instrument_clip_view.cpp:3639-3640`) is not what sets the rows below.
		RecordingCalls recording;
		UiStackGuard ui_guard;
		GlobalGuestGuard guest_guard;
		deluge::scenes::spec::SongFixture fx = deluge::scenes::spec::make_synth_song();
		setRootUILowLevel(&sessionView);

		// The guest on `uiNavigationHierarchy`, the way production leaves it -- `menu_actions_spec.cpp`'s
		// `open_global_guest_on_ui_stack`: `openUI` runs `opened()` itself.
		const Node* const roots[] = {&nodes::note_editor_root};
		(void)menu::assign_node_ids(roots);
		menu_scene().open(nodes::note_editor_root, menu_context_at(fx, Root::NoteEditor));
		shellHostUI.setInitialScene(SceneId{SceneKind::FirstApp, menu::node_id(nodes::note_editor_root)});
		expect(openUI(&shellHostUI)).to_be_true();

		clearPendingUIRendering();
		guest::exitNoteModeBeforeClose();
		expect(whichMainRowsNeedRendering).to_equal(0xFFFFFFFFu);
		expect(whichSideRowsNeedRendering).to_equal(0u);

		menu_scene().open(nodes::note_row_editor_root, menu_context_at(fx, Root::NoteRowEditor));
		clearPendingUIRendering();
		guest::exitNoteModeBeforeClose();
		expect(g_rec.count(Call::ExitRow)).to_equal(1);
		expect(whichMainRowsNeedRendering).to_equal(0u);
		expect(whichSideRowsNeedRendering).to_equal(0u);
		clearPendingUIRendering();
	});
});
// clang-format on

int note_mode_spec(int argc, char** const argv) {
	return CppSpec::parse(argc, argv)
	               .add_specs(note_mode_active, note_mode_buttons, note_mode_encoders, note_mode_exit)
	               .exec()
	               .is_success()
	           ? EXIT_SUCCESS
	           : EXIT_FAILURE;
}
