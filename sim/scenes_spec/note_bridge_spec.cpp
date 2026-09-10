/// @file
/// @brief Task 4's spec: `deluge::scenes::menu::NoteBridge`, installed by `wireShellNoteBridge()`,
///        against the REAL `instrumentClipView` and a real `Song`/clip (ruling 5: the bridge's spec
///        is a stated sample, not full coverage of all sixteen members).
///
/// **Selecting a note through the real view.** `support/song_fixture.h` builds a synth/MIDI/kit
/// clip but places no `Note` and sets none of `instrumentClipView`'s selection fields, so this file
/// does both itself: a note is added through the model (`NoteRow::attemptNoteAdd`,
/// `model/note/note_row.h:204`, with a known non-zero velocity so the read below has something
/// concrete to check) and selected the way `InstrumentClipView` itself does, by driving
/// `editPadAction` (`gui/views/instrument_clip_view.cpp:2345`).
///
/// `editPadAction`'s "regular create-or-select press" branch unconditionally calls
/// `reassessAuditionStatus` (`:2702`), which calls `getVelocityForAudition` (`:4746`), which calls
/// `InstrumentClipMinder::makeCurrentClipActiveOnInstrumentIfPossible`
/// (`model/clip/instrument_clip_minder.cpp:591`) -- and THAT dereferences `currentPlaybackMode`, a
/// global only `deluge_boot()` sets (`deluge.cpp:659`), null under every `song_fixture.h`
/// direct-construction fixture. `RealNoteSelectionGuard` below seeds it with the real `session`,
/// exactly the precedent `audio_clip_scene_spec.cpp` already sets (`:349`), and seeds `display`
/// with a local `OLED`, exactly `count_in_spec.cpp`'s precedent (`:185-187`) -- both restored on
/// scope exit so no example leaks either global into the next.
///
/// **Covered:** the bridge installs; a note-editor read (`note_selected`, `note_velocity`) against
/// a note actually selected this way; `adjust_note_velocity`'s write pinned against legacy's own
/// gate, unchanged -- see below; `row_probability` creates a note row on a non-kit clip at an
/// auditioned y with none yet; `row_sequence_direction` answers false on a KIT clip even when a
/// drum's own row exists at that same y (fix round 1, important 3 -- without the `:50` non-kit test
/// this would have read that displayed drum's row instead); `iterance_preset_label` for preset 1,
/// both `compact` spellings, plus its out-of-range guard.
///
/// **The offset-write gate, pinned rather than assumed.** `adjustVelocity` only actually changes a
/// note when `display->hasPopup() || inAutomationView || inSoundEditor`
/// (`instrument_clip_view.cpp:2852-2858`); outside all three (this fixture's own state: no popup,
/// `getCurrentUI()` is neither `&automationView` nor `&soundEditor`), the write is a no-op --  but
/// `adjustVelocity`'s own tail unconditionally calls `displayVelocity`
/// (`:2930-2934`), which raises a popup regardless of whether anything changed. So the FIRST
/// `adjust_note_velocity` turn with no popup up leaves `note_velocity()` unchanged and leaves
/// `display->hasPopup()` true afterward; the SECOND turn, now satisfying the gate, actually commits
/// the change. `adjustNoteProbabilityWithOffset`/`adjustNoteIteranceWithOffset`/
/// `adjustNoteFillWithOffset` and every note-ROW `adjust_*`/`set_*` share the identical shape
/// through `adjustNoteParameterValue`'s `hasPopup || inNoteEditor` (`:3066`, `:3117`) and
/// `setNoteRowParameterValue`'s `hasPopup || inNoteRowEditor` (`:4063`, `:4066`) -- not re-pinned
/// here one entry at a time; velocity's gate is the stated sample for all of them. **The ported
/// note mode's own arm through this gate -- so a guest turn commits on its first press, the way a
/// user expects -- is a later task's** (a widened predicate that recognises the guest as legacy
/// recognises `soundEditor`); this bridge transcribes legacy's functions exactly as they stand
/// today, gate included, and must not paper over it.
///
/// **Uncovered, reported rather than faked:** `note_probability`/`adjust_note_probability`,
/// `note_iterance`/`adjust_note_iterance`/`set_note_iterance`, `note_fill`/`adjust_note_fill`,
/// `row_iterance`/`adjust_row_iterance`/`set_row_iterance`, `row_fill`/`adjust_row_fill`,
/// `set_row_sequence_direction`, `adjust_row_probability` -- entries this file's stated sample does
/// not additionally pin, each a straightforward sibling of an entry that is (`note_velocity`'s
/// selection mechanics, `adjust_note_velocity`'s gate shape, `row_probability`'s row-creation body).

#include "gui/ui/shell_note_bridge.h"
#include "scenes/menu/note_bridge.h"

#include "definitions_cxx.hpp"
#include "gui/views/instrument_clip_view.h"
#include "hid/display/display.h"
#include "hid/display/oled.h"
#include "model/clip/instrument_clip.h"
#include "model/model_stack.h"
#include "model/note/note_row.h"
#include "model/song/song.h"
#include "playback/mode/session.h"

#include "support/song_fixture.h"

#include "cppspec.hpp"

#include <span>
#include <string_view>

#include "spec/support/spec_main.h"

namespace menu = deluge::scenes::menu;

namespace {

/// @brief Seed the two legacy globals `InstrumentClipView::editPadAction`'s "regular
///        create-or-select press" branch needs outside a booted UI, place and select one note
///        through it, and undo all of it on scope exit -- see the file doc for the crash this
///        works around and its precedents.
///
/// `instrumentClipView` is a process-wide singleton this suite is the first to press a pad
/// through; `numEditPadPresses`/`editPadPresses[]`/`numEditPadPressesPerNoteRowOnScreen[]` are
/// shared state a later example would otherwise inherit mid-press, changing which branch a later
/// `editPadAction` call even takes. Releasing the press in the destructor (`state=false`, the same
/// call a real pad-up would make) is what `endEditPadPress` decrements them back for.
struct RealNoteSelectionGuard {
	deluge::hid::display::OLED oled_display;
	PlaybackMode* saved_playback_mode = currentPlaybackMode;
	deluge::hid::Display* saved_display = display;
	bool pressed = false;

	RealNoteSelectionGuard() {
		currentPlaybackMode = &session;
		deluge::hid::display::OLED::removePopup(); // clean slate: no popup carried from an earlier example
		display = &oled_display;
	}

	/// @brief Add one note to a fresh note row at `yDisplay` 0 on a non-kit clip, and select it the
	///        way `InstrumentClipView::editPadAction` does on a regular create-or-select press.
	void select_note(int32_t velocity) {
		char modelStackMemory[MODEL_STACK_MAX_SIZE];
		ModelStackWithTimelineCounter* modelStack = currentSong->setupModelStackWithCurrentClip(modelStackMemory);

		ModelStackWithNoteRow* modelStackWithNoteRow = instrumentClipView.createNoteRowForYDisplay(modelStack, 0);
		NoteRow* noteRow = modelStackWithNoteRow->getNoteRowAllowNull();
		(void)noteRow->attemptNoteAdd(/*pos=*/0, /*length=*/currentSong->xZoom[NAVIGATION_CLIP], velocity,
		                              /*probability=*/kNumProbabilityValues, Iterance{}, FillMode::OFF,
		                              modelStackWithNoteRow, /*action=*/nullptr);

		// `InstrumentClipView::editPadAction`'s own callers pass `currentSong->xZoom[NAVIGATION_CLIP]`
		// (`handleNoteEditorEditPadAction`, `instrument_clip_view.cpp:3651`); square 0 is where the
		// note above was just placed.
		instrumentClipView.editPadAction(/*state=*/true, /*yDisplay=*/0, /*xDisplay=*/0,
		                                 currentSong->xZoom[NAVIGATION_CLIP]);
		pressed = true;
	}

	~RealNoteSelectionGuard() {
		if (pressed) {
			instrumentClipView.editPadAction(/*state=*/false, /*yDisplay=*/0, /*xDisplay=*/0,
			                                 currentSong->xZoom[NAVIGATION_CLIP]);
		}
		currentPlaybackMode = saved_playback_mode;
		display = saved_display;
	}

	RealNoteSelectionGuard(const RealNoteSelectionGuard&) = delete;
	RealNoteSelectionGuard& operator=(const RealNoteSelectionGuard&) = delete;
};

/// @brief `instrumentClipView.lastAuditionedYDisplay` is real UI state a spec borrows and must give
///        back -- fix round 1, minor g.
struct RestoreLastAuditionedY {
	int32_t saved = instrumentClipView.lastAuditionedYDisplay;
	~RestoreLastAuditionedY() { instrumentClipView.lastAuditionedYDisplay = saved; }
};

} // namespace

// clang-format off
describe note_bridge_group("scenes::menu note bridge, installed via wireShellNoteBridge()", $ {
	before_all([] { deluge::gui::ui::wireShellNoteBridge(); });

	it("installs a non-null bridge", _ {
		expect(menu::note_bridge() != nullptr).to_be_true();
	});

	it("reads note_selected and note_velocity against a note selected the way InstrumentClipView does", _ {
		RealNoteSelectionGuard guard;
		auto f = deluge::scenes::spec::make_synth_song();
		guard.select_note(80);

		const menu::NoteBridge* bridge = menu::note_bridge();
		expect(bridge->note_selected()).to_be_true();
		expect(bridge->note_velocity()).to_equal(80);
	});

	it("adjust_note_velocity leaves the model unchanged and raises legacy's popup on the first turn "
	   "with no popup up, and commits on the second -- instrument_clip_view.cpp:2852-2858's gate", _ {
		RealNoteSelectionGuard guard;
		auto f = deluge::scenes::spec::make_synth_song();
		guard.select_note(80);

		const menu::NoteBridge* bridge = menu::note_bridge();
		expect(display->hasPopup()).to_be_false(); // the guard's removePopup() left a clean slate

		bridge->adjust_note_velocity(1);
		// `display->hasPopup() || inAutomationView || inSoundEditor` is false here (no popup yet,
		// `getCurrentUI()` is neither view under this fixture), so the write inside that gate never
		// ran -- but `adjustVelocity`'s tail calls `displayVelocity`/`popupVelocity` regardless of
		// the gate, which raises a popup.
		expect(bridge->note_velocity()).to_equal(80);
		expect(display->hasPopup()).to_be_true();

		bridge->adjust_note_velocity(1);
		// The popup `displayVelocity` raised above now satisfies the gate, so this turn commits.
		expect(bridge->note_velocity()).to_equal(81);
	});

	it("row_probability creates a note row on a non-kit clip at an auditioned y with none yet", _ {
		RestoreLastAuditionedY restore_y;
		auto f = deluge::scenes::spec::make_synth_song();
		// A y with no row: `make_synth_song` places none at all, and nothing above this example has
		// touched y 1.
		instrumentClipView.lastAuditionedYDisplay = 1;

		char modelStackMemory[MODEL_STACK_MAX_SIZE];
		ModelStackWithTimelineCounter* modelStack = currentSong->setupModelStackWithCurrentClip(modelStackMemory);
		ModelStackWithNoteRow* before = f.clip->getNoteRowOnScreen(1, modelStack);
		expect(before->getNoteRowAllowNull() == nullptr).to_be_true();

		const menu::NoteBridge* bridge = menu::note_bridge();
		// The assertion that matters is that a row now EXISTS -- `SelectedNoteRow::getIndividualNoteRow`
		// creates it, transcribed at `shell_note_bridge.cpp`'s `individualNoteRow` -- not which
		// default value it carries.
		(void)bridge->row_probability();

		char modelStackMemory2[MODEL_STACK_MAX_SIZE];
		ModelStackWithTimelineCounter* modelStack2 = currentSong->setupModelStackWithCurrentClip(modelStackMemory2);
		ModelStackWithNoteRow* after = f.clip->getNoteRowOnScreen(1, modelStack2);
		expect(after->getNoteRowAllowNull() != nullptr).to_be_true();
	});

	it("row_sequence_direction answers false on a KIT clip even though a drum's own row exists at "
	   "that same y -- fix round 1, important 3: without sequence::Direction::getIndividualNoteRow's "
	   "`:50` non-kit test, this would have read that DISPLAYED DRUM's row instead", _ {
		RestoreLastAuditionedY restore_y;
		auto f = deluge::scenes::spec::make_kit_song();
		// `InstrumentClip::getNoteRowOnScreen`'s kit arm indexes `noteRows` at `yDisplay + yScroll`
		// (`instrument_clip.cpp:973`); `yScroll` is `uint8_t lastAuditionedYDisplay` (`gui/views/
		// instrument_clip_view.h:263`) added to a signed offset the constructor derives from the
		// song's root note (`instrument_clip.cpp:90-96`), which is not 0 and not something a
		// `uint8_t` can cancel out (measured: 35 on this fixture's default song). Pinned to 0
		// instead, same as `song_fixture.h`'s own fixtures leave every other field it does not
		// itself seed -- so `yDisplay` 0 lands on `noteRows[0]`, where `add_drum`'s one drum row goes
		// (`atStart=false` on a clip with no rows yet).
		f.clip->yScroll = 0;
		auto* drum = deluge::scenes::spec::add_drum<GateDrum>(f);
		(void)drum;
		instrumentClipView.lastAuditionedYDisplay = 0;

		char modelStackMemory[MODEL_STACK_MAX_SIZE];
		ModelStackWithTimelineCounter* modelStack = currentSong->setupModelStackWithCurrentClip(modelStackMemory);
		ModelStackWithNoteRow* existing = f.clip->getNoteRowOnScreen(0, modelStack);
		expect(existing->getNoteRowAllowNull() != nullptr).to_be_true();

		int32_t direction = -1;
		expect(menu::note_bridge()->row_sequence_direction(&direction)).to_be_false();
	});

	it("iterance_preset_label matches legacy's compact `%d:%d` for preset 1", _ {
		char buf[20] = {};
		size_t n = menu::note_bridge()->iterance_preset_label(1, /*compact=*/true, std::span<char>(buf, sizeof(buf)));
		expect(n > 0).to_be_true();
		std::string_view text(buf, n);
		// Preset 1 is `iterancePresets[0]` == `Iterance{2, 0b1}` (`lookuptables.cpp:508-509`):
		// divisor 2, step 0 active -- legacy's `"%d:%d"` with `(i + 1, divisor)` is "1:2".
		expect(text == "1:2").to_be_true();
	});

	it("iterance_preset_label matches legacy's non-compact `%d of %d` for the same preset", _ {
		char buf[20] = {};
		size_t n = menu::note_bridge()->iterance_preset_label(1, /*compact=*/false, std::span<char>(buf, sizeof(buf)));
		expect(n > 0).to_be_true();
		std::string_view text(buf, n);
		expect(text == "1 of 2").to_be_true();
	});

	it("iterance_preset_label answers nothing outside 1..kNumIterancePresets", _ {
		char buf[20] = {};
		expect(menu::note_bridge()->iterance_preset_label(0, true, std::span<char>(buf, sizeof(buf)))).to_equal(0U);
		expect(menu::note_bridge()->iterance_preset_label(kCustomIterancePreset, true, std::span<char>(buf, sizeof(buf))))
		    .to_equal(0U);
	});
});
// clang-format on

DELUGE_SPEC(note_bridge_spec, note_bridge_group)
