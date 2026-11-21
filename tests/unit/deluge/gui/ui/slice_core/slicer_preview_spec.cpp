/*
 * Copyright © 2026 Synthstrom Audible Limited
 *
 * This file is part of The Synthstrom Audible Deluge Firmware.
 *
 * The Synthstrom Audible Deluge Firmware is free software: you can redistribute it and/or modify it under the
 * terms of the GNU General Public License as published by the Free Software Foundation,
 * either version 3 of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY;
 * without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along with this program.
 * If not, see <https://www.gnu.org/licenses/>.
 */

#include "gui/ui/slice_core.h"
#include "gui/waveform/waveform_basic_navigator.h"

#include "mock_display.h"
#include "mock_slicer_env.h"

#include <utility>

#include "cppspec.hpp"
#include "support/spec_main.h"

using deluge::test::LoadAnswer;

/// Drives the real `SliceCore` through the entry points the slicer scenes call -- a pad's press or release
/// (`audition()`), the tick surface (`tick_audition()`) and the cancel -- with two seams: how each tick of the async
/// sample load answers, and a count of audition notes. Nothing here reimplements slicer logic -- if it did, the test
/// would pass against a core that never works. In particular `tick_audition()` is the shipped tick surface, so a core
/// that never ticks its held load fails "a cold press defers then sounds on the next tick" rather than being rescued by
/// a loop the test brought with it.
class SlicerPreviewProbe {
public:
	SlicerPreviewProbe() {
		deluge::test::resetSlicerEnv();
		deluge::test::lastDisplayedError = Error::NONE;
	}

	/// How every tick of a held load answers from now on.
	void answer(LoadAnswer answer) { deluge::test::loadAnswer = answer; }

	/// The audition a held slice pad asks for, as the slice ops' audition request makes it.
	void press() {
		core_.audition(*waveformBasicNavigator.sample, {.startPoint = 0, .endPoint = -1, .transpose = 0, .on = 1});
	}
	/// A press of a slice from @p start to @p end, transposed by @p transpose.
	void press_slice(int64_t start, int64_t end, int32_t transpose) {
		core_.audition(*waveformBasicNavigator.sample,
		               {.startPoint = start, .endPoint = end, .transpose = transpose, .on = 1});
	}
	/// The release of that pad.
	void release() {
		core_.audition(*waveformBasicNavigator.sample, {.startPoint = 0, .endPoint = 0, .transpose = 0, .on = 0});
	}

	/// What leaving the slicer, switching its mode or committing a slice calls.
	void cancel() { core_.cancel_audition(); }

	/// The slicer's per-frame tick of a held audition.
	void graphicsRoutine() { core_.tick_audition(); }

	/// Whether a load is held for the tick surface to finish.
	[[nodiscard]] bool previewIsPending() const { return core_.audition_pending(); }
	[[nodiscard]] int32_t auditionNotesSent() const { return deluge::test::auditionNoteCount; }
	[[nodiscard]] int32_t lastAuditionOn() const { return deluge::test::lastAuditionOn; }
	[[nodiscard]] int32_t previewGiveUps() const { return core_.audition_give_ups(); }

private:
	deluge::gui::ui::SliceCore core_{};
};

// clang-format off
describe slicer_preview_group("slicer preview", ${
	it("plays the pressed slice's zone and transpose, through the drum's ranges", _ {
		SlicerPreviewProbe probe;
		probe.answer(LoadAnswer::Loaded);
		probe.press_slice(1000, 5000, 3);
		probe.graphicsRoutine(); // the held load lands, and the slice sounds
		expect(deluge::test::lastAppliedRanges.size()).to_equal(size_t{1});
		if (deluge::test::lastAppliedRanges.size() == 1) {
			expect(static_cast<int64_t>(deluge::test::lastAppliedRanges[0].zone.start)).to_equal(int64_t{1000});
			expect(static_cast<int64_t>(deluge::test::lastAppliedRanges[0].zone.end)).to_equal(int64_t{5000});
			expect(int{deluge::test::lastAppliedRanges[0].tuning.transpose}).to_equal(3);
		}
	});

	/// A release loads nothing and therefore never waits: its note-off is sent at once, whatever a load is doing. This is
	/// asserted rather than left as a reading of the control flow, because a release that waited for a load would leave a
	/// note sounding after the finger lifted.
	it("a release never defers and always sounds its note off", _ {
		SlicerPreviewProbe probe;
		probe.answer(LoadAnswer::Loading); // every load is still loading
		probe.release();
		expect(probe.previewIsPending()).to_be_false();
		expect(probe.auditionNotesSent()).to_equal(1);
		expect(probe.lastAuditionOn()).to_equal(0);
	});

	/// A cold press holds its load and sounds nothing yet; the tick surface finishes it, and the note lands with the load.
	/// The load, the params and the note stay together; only WHEN they run moves.
	it("a cold press defers then sounds on the next tick", _ {
		SlicerPreviewProbe probe;
		probe.answer(LoadAnswer::Loading);
		probe.press();
		expect(probe.previewIsPending()).to_be_true();
		expect(probe.auditionNotesSent()).to_equal(0);

		// Ticked while the load is STILL loading: the holder already names the file, so a slicer that asked only "is this a
		// different sample?" would sound a slice of a sample that never arrived.
		probe.graphicsRoutine();
		expect(probe.auditionNotesSent()).to_equal(0);
		expect(probe.previewIsPending()).to_be_true();

		probe.answer(LoadAnswer::Loaded);
		probe.graphicsRoutine();
		expect(probe.auditionNotesSent()).to_equal(1);
		expect(probe.lastAuditionOn()).to_equal(1);
		expect(probe.previewIsPending()).to_be_false();
	});

	/// A load that stops moving gives up once, rather than being asked for on every tick for ever.
	///
	/// When to give up is the async sample load's own stall bound (kMaxStalledUs); here the load simply
	/// answers Failed/Stalled. At the give-up nothing is left held, no audition note is sent, the card
	/// error is shown and the give-up is counted. The next press asks again, so a card that does answer
	/// later still auditions.
	it("a stalled load gives up once", _ {
		SlicerPreviewProbe probe;
		probe.answer(LoadAnswer::Loading);
		probe.press();
		expect(probe.previewIsPending()).to_be_true();

		probe.answer(LoadAnswer::Stalled);
		probe.graphicsRoutine();
		expect(probe.previewIsPending()).to_be_false();
		expect(probe.previewGiveUps()).to_equal(1);
		expect(probe.auditionNotesSent()).to_equal(0);
		expect(std::to_underlying(deluge::test::lastDisplayedError)).to_equal(std::to_underlying(Error::SD_CARD));

		probe.graphicsRoutine(); // nothing left to tick
		expect(probe.auditionNotesSent()).to_equal(0);
		expect(probe.previewGiveUps()).to_equal(1);

		probe.answer(LoadAnswer::Loaded);
		probe.press();
		probe.graphicsRoutine();
		expect(probe.auditionNotesSent()).to_equal(1);
		expect(probe.lastAuditionOn()).to_equal(1);
	});

	/// A finger lifted before its press's load lands sends its note-off, and the note-on never sounds.
	///
	/// The release cancels the held load. Without that, the load would land on a later tick and sound the note-on after
	/// the note-off -- on a device, a note sounding with no finger on the pad, and no note-off left to stop it.
	it("a release before the load lands never sounds its note on", _ {
		SlicerPreviewProbe probe;
		probe.answer(LoadAnswer::Loading);
		probe.press();
		probe.release();
		expect(probe.auditionNotesSent()).to_equal(1);
		expect(probe.lastAuditionOn()).to_equal(0);

		// The notes first: a held load that lands sounds a note-on here, which is the damage. Nothing held is how.
		probe.answer(LoadAnswer::Loaded);
		probe.graphicsRoutine();
		expect(probe.auditionNotesSent()).to_equal(1);
		expect(probe.lastAuditionOn()).to_equal(0);
		expect(probe.previewIsPending()).to_be_false();
	});

	/// A cancel while a press's load is held sounds nothing, and leaves nothing that would wedge the next press.
	///
	/// A held load that survived the cancel would sound a slice into a slicer the user has left: the core outlives the
	/// scene, and the tick runs again the moment the slicer is current.
	it("a cancel while loading sounds nothing and strands nothing", _ {
		SlicerPreviewProbe probe;
		probe.answer(LoadAnswer::Loading);
		probe.press();
		probe.cancel();
		expect(probe.previewIsPending()).to_be_false();

		probe.answer(LoadAnswer::Loaded);
		probe.graphicsRoutine();
		expect(probe.auditionNotesSent()).to_equal(0);

		probe.press();
		probe.graphicsRoutine();
		expect(probe.auditionNotesSent()).to_equal(1);
		expect(probe.lastAuditionOn()).to_equal(1);
	});
});

DELUGE_SPEC(slicer_preview_spec, slicer_preview_group)
