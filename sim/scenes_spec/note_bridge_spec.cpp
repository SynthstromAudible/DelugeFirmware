/// @file
/// @brief Task 4's spec: `deluge::scenes::menu::NoteBridge`, installed by `wireShellNoteBridge()`,
///        against the REAL `instrumentClipView` and a real `Song`/clip (ruling 5: the bridge's spec
///        is a stated sample, not full coverage of all sixteen members).
///
/// **Selecting a note cannot be done without booting the app -- measured, not assumed.**
/// `support/song_fixture.h` builds a synth/MIDI/kit clip but places no `Note` and sets none of
/// `instrumentClipView`'s selection fields, so this file tried driving the view the way it is
/// supposed to be driven: `NoteRow::attemptNoteAdd` (`model/note/note_row.h:204`) to place a note,
/// then `InstrumentClipView::editPadAction` (`gui/views/instrument_clip_view.cpp:2345`) to select
/// it, exactly as `InstrumentClipView` itself does and as decision 6 asks for.
///
/// `editPadAction`'s "regular create-or-select press" branch unconditionally calls
/// `reassessAuditionStatus` (`:2691`, cited in the brief as `:2690`), which calls
/// `getVelocityForAudition` (`:4837`), which calls
/// `InstrumentClipMinder::makeCurrentClipActiveOnInstrumentIfPossible`
/// (`model/clip/instrument_clip_minder.cpp:591`) -- and THAT dereferences `currentPlaybackMode`,
/// a global only `deluge_boot()` sets (`deluge.cpp:659`), null everywhere `song_fixture.h`'s
/// direct-construction fixtures run. Confirmed with a debugger, not inferred:
/// `SIGSEGV` in `PlaybackMode::isOutputAvailable` through exactly that call chain, on the very
/// first `editPadAction(true, ...)` call this file made. Poking `lastSelectedNoteXDisplay`/
/// `YDisplay`/`gridSquareInfo` by hand instead is the alternative decision 6 asks this file to
/// prefer against -- so per its escape hatch:
///
/// **Covered:** the bridge installs; `row_probability` creates a note row on a non-kit clip at an
/// auditioned y with none yet (`getIndividualNoteRow`'s body needs no note selection, only
/// `lastAuditionedYDisplay`); `iterance_preset_label` for preset 1, both `compact` spellings, plus
/// its out-of-range guard.
///
/// **Uncovered, reported rather than faked:** `note_selected`, `note_velocity`,
/// `adjust_note_velocity`, `note_probability`/`adjust_note_probability`,
/// `note_iterance`/`adjust_note_iterance`/`set_note_iterance`, `note_fill`/`adjust_note_fill` --
/// every entry that reads `lastSelectedNoteXDisplay`/`YDisplay`/`gridSquareInfo` or
/// `getLeftMostNotePressed()`, all of which only `editPadAction` populates. Also uncovered: the
/// note-row `adjust_*`/`set_*` writes and `row_sequence_direction`/`set_row_sequence_direction`,
/// which the brief's stated sample does not ask for and which this file did not attempt.

#include "gui/ui/shell_note_bridge.h"
#include "scenes/menu/note_bridge.h"

#include "definitions_cxx.hpp"
#include "gui/views/instrument_clip_view.h"
#include "model/clip/instrument_clip.h"
#include "model/model_stack.h"
#include "model/note/note_row.h"
#include "model/song/song.h"

#include "support/song_fixture.h"

#include "cppspec.hpp"

#include <span>
#include <string_view>

#include "spec/support/spec_main.h"

namespace menu = deluge::scenes::menu;

// clang-format off
describe note_bridge_group("scenes::menu note bridge, installed via wireShellNoteBridge()", $ {
	before_all([] { deluge::gui::ui::wireShellNoteBridge(); });

	it("installs a non-null bridge", _ {
		expect(menu::note_bridge() != nullptr).to_be_true();
	});

	it("row_probability creates a note row on a non-kit clip at an auditioned y with none yet", _ {
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

	it("iterance_preset_label matches legacy's compact `%d:%d` for preset 1", _ {
		char buf[20] = {};
		size_t n = menu::note_bridge()->iterance_preset_label(1, /*compact=*/true, std::span<char>(buf, sizeof(buf)));
		expect(n > 0).to_be_true();
		std::string_view text(buf, n);
		// Preset 1 is `iterancePresets[0]` == `Iterance{2, 0b1}` (`lookuptables.cpp:508`): divisor 2,
		// step 0 active -- legacy's `"%d:%d"` with `(i + 1, divisor)` is "1:2".
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
