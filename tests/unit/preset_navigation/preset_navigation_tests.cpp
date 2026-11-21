// CppUTest TEST macros generate static registration and owning factory code.
// NOLINTBEGIN(bugprone-throwing-static-initialization, cppcoreguidelines-owning-memory)
#include "CppUTest/TestHarness.h"
#include "gui/ui/browser/preset_navigation.h"
#include <array>
#include <cstddef>

using deluge::gui::performPresetLoad;
using deluge::gui::presetInstrumentAvailable;
using deluge::gui::PresetNavigationContext;
using deluge::gui::PresetNavigationDestination;

namespace {
template <typename T>
T* identity(std::max_align_t& token) {
	// Opaque pointer identities only; the production policies never dereference them.
	return reinterpret_cast<T*>(&token); // NOLINT(cppcoreguidelines-pro-type-reinterpret-cast)
}
} // namespace

TEST_GROUP(PresetLoadTests) {
	bool pending = true;
	bool interrupted = false;
	uint32_t mode = UI_MODE_NONE;
	Error result = Error::NONE;
};

// Assertion macros contribute branching that is outside the test body.
// NOLINTNEXTLINE(readability-function-cognitive-complexity)
TEST(PresetLoadTests, ConsumesPreviewAndSetsBusyBeforeTimerCancellation) {
	bool cancelled = false;
	performPresetLoad(
	    pending, mode, interrupted, result,
	    [&] {
		    CHECK_FALSE(pending);
		    CHECK_EQUAL(UI_MODE_LOADING_BUT_ABORT_IF_SELECT_ENCODER_TURNED, mode);
		    cancelled = true;
	    },
	    [&] {
		    CHECK_TRUE(cancelled);
		    CHECK_FALSE(pending);
		    CHECK_EQUAL(UI_MODE_LOADING_BUT_ABORT_IF_SELECT_ENCODER_TURNED, mode);
		    return Error::NONE;
	    });
	CHECK_FALSE(pending);
	CHECK_EQUAL(UI_MODE_NONE, mode);
}

TEST(PresetLoadTests, FailedDirectLoadRecordsErrorAndRestoresPreviousMode) {
	mode = UI_MODE_CLIP_PRESSED_IN_SONG_VIEW;
	performPresetLoad(pending, mode, interrupted, result, [] {}, [] { return Error::FILE_CORRUPTED; });
	CHECK(result == Error::FILE_CORRUPTED);
	CHECK_EQUAL(UI_MODE_CLIP_PRESSED_IN_SONG_VIEW, mode);
	CHECK_FALSE(pending);
}

TEST(PresetLoadTests, SuccessfulRetryClearsPreviousError) {
	result = Error::FILE_CORRUPTED;
	pending = false; // Direct retry also works without a scheduled preview.
	performPresetLoad(pending, mode, interrupted, result, [] {}, [] { return Error::NONE; });
	CHECK(result == Error::NONE);
	CHECK_EQUAL(UI_MODE_NONE, mode);
}

TEST(PresetLoadTests, RetryDiscardsStaleInterruptButPreservesNewInterrupt) {
	interrupted = true;
	performPresetLoad(
	    pending, mode, interrupted, result, [&] { CHECK_FALSE(interrupted); },
	    [&] {
		    CHECK_FALSE(interrupted);
		    interrupted = true;
		    return Error::ABORTED_BY_USER;
	    });
	CHECK_TRUE(interrupted);
	CHECK(result == Error::ABORTED_BY_USER);
	CHECK_EQUAL(UI_MODE_NONE, mode);
	performPresetLoad(
	    pending, mode, interrupted, result, [] {},
	    [&] {
		    CHECK_FALSE(interrupted);
		    return Error::NONE;
	    });
	CHECK(result == Error::NONE);
}

TEST_GROUP(PresetInputTests){};

TEST(PresetInputTests, ReleasesAndUnrelatedInputNeverWaitForCard) {
	using enum deluge::gui::PresetInputAction;
	CHECK(deluge::gui::presetInputAction(true, false, true, true) == PASS);
	CHECK(deluge::gui::presetInputAction(false, true, true, true) == PASS);
}

TEST(PresetInputTests, PendingPressWaitsForEntireParserLifetime) {
	using enum deluge::gui::PresetInputAction;
	CHECK(deluge::gui::presetInputAction(true, true, true, false) == DEFER);
	CHECK(deluge::gui::presetInputAction(true, true, false, true) == DEFER);
	CHECK(deluge::gui::presetInputAction(true, true, false, false) == RESOLVE);
}

TEST_GROUP(PresetNavigationStepsTests){};

// Assertion macros contribute branches outside the test logic.
// NOLINTNEXTLINE(readability-function-cognitive-complexity)
TEST(PresetNavigationStepsTests, AccumulatedDetentsSkipUnavailableCandidatesAcrossPages) {
	for (int32_t offset : {3, -3}) {
		deluge::gui::PresetNavigationSteps steps(offset);
		CHECK_EQUAL(offset > 0 ? 1 : -1, steps.direction());
		// First page: one selectable preset, then two occupied outputs.
		CHECK_FALSE(steps.visit(true));
		CHECK_FALSE(steps.visit(false));
		CHECK_FALSE(steps.visit(false));
		// Second page: another selectable preset. Wrapping must retain one step.
		CHECK_FALSE(steps.visit(true));
		CHECK_FALSE(steps.finished());
		CHECK_TRUE(steps.visit(true));
	}
}

TEST(PresetNavigationStepsTests, SingleDetentDoesNotConsumeOccupiedEntry) {
	deluge::gui::PresetNavigationSteps steps(1);
	CHECK_FALSE(steps.visit(false));
	CHECK_TRUE(steps.visit(true));
}

TEST_GROUP(PresetNavigationContextTests) {
	PresetNavigationContext context;
	// The context compares identities without dereferencing model/UI objects.
	// Distinct, aligned addresses supply those identities without fake class definitions.
	std::array<std::max_align_t, 5> identities{};
	Song* song = nullptr;
	UI* ui = nullptr;
	Clip* clip = nullptr;
	Clip* other_clip = nullptr;

	void setup() override {
		song = identity<Song>(identities.at(0));
		ui = identity<UI>(identities.at(1));
		clip = identity<Clip>(identities.at(2));
		other_clip = identity<Clip>(identities.at(3));
		context.song = song;
		context.ui = ui;
		context.currentClip = clip;
		context.selectedClip = clip;
		context.displayClip = clip;
		context.uiMode = UI_MODE_CLIP_PRESSED_IN_SONG_VIEW;
	}
};

TEST(PresetNavigationContextTests, RepressingSameClipStartsANewGesture) {
	context.markPadReleased(true);
	CHECK_FALSE(context.matchesUI(song, ui, UI_MODE_CLIP_PRESSED_IN_SONG_VIEW, clip));
	CHECK(context.decision(song, ui, UI_MODE_CLIP_PRESSED_IN_SONG_VIEW, clip, false)
	      == deluge::gui::PresetNavigationDecision::CANCEL);
}

TEST(PresetNavigationContextTests, SameHeldTargetMatches) {
	CHECK(context.matchesUI(song, ui, context.uiMode, clip));
	CHECK(context.matchesSelection(clip, false));
}

TEST(PresetNavigationContextTests, ChangedSongUIOrCurrentClipCancels) {
	CHECK_FALSE(context.matchesUI(nullptr, ui, context.uiMode, clip));
	CHECK_FALSE(context.matchesUI(song, nullptr, context.uiMode, clip));
	CHECK_FALSE(context.matchesUI(song, ui, context.uiMode, other_clip));
}

TEST(PresetNavigationContextTests, UnrecordedModeChangeCancels) {
	CHECK_FALSE(context.matchesUI(song, ui, UI_MODE_NONE, clip));
}

TEST(PresetNavigationContextTests, ChangedSelectionCancelsWhilePadHeld) {
	CHECK_FALSE(context.matchesSelection(other_clip, true));
	CHECK_FALSE(context.matchesSelection(nullptr, true));
}

TEST(PresetNavigationContextTests, SessionPadReleasePreservesPendingTargetAfterSelectionClears) {
	context.markPadReleased(true);
	CHECK_TRUE(context.padReleased);
	CHECK(context.matchesUI(song, ui, UI_MODE_NONE, clip));
	CHECK(context.matchesSelection(nullptr, false));
	POINTERS_EQUAL(clip, context.displayClip);
}

TEST(PresetNavigationContextTests, ReleaseCannotAuthorizeChangedTarget) {
	context.markPadReleased(false);
	CHECK_FALSE(context.padReleased);
	CHECK_FALSE(context.matchesUI(song, ui, UI_MODE_NONE, clip));
}

TEST(PresetNavigationContextTests, NewGestureAfterReleaseCancels) {
	context.markPadReleased(true);
	CHECK_FALSE(context.matchesUI(song, ui, UI_MODE_CLIP_PRESSED_IN_SONG_VIEW, clip));
}

TEST(PresetNavigationContextTests, ChangingViewOrTargetAfterReleaseStillCancels) {
	context.markPadReleased(true);
	CHECK_FALSE(context.matchesUI(song, nullptr, UI_MODE_NONE, clip));
	CHECK_FALSE(context.matchesUI(song, ui, UI_MODE_NONE, other_clip));
	CHECK_FALSE(context.matchesUI(nullptr, ui, UI_MODE_NONE, clip));
}

TEST(PresetNavigationContextTests, UnrelatedUIModeDoesNotBecomeAPadRelease) {
	context.uiMode = UI_MODE_NONE;
	context.markPadReleased(true);
	CHECK_FALSE(context.padReleased);
}

TEST(PresetNavigationContextTests, ArrangerNavigationUsesOriginatingCallerRegardlessOfVisibleUI) {
	context.fromArranger = true;
	context.replacingWholeOutput = true;
	context.displayClip = nullptr;
	CHECK(context.destination() == PresetNavigationDestination::ARRANGER);
	context.ui = nullptr; // Dispatch does not infer the caller from this UI pointer.
	CHECK(context.destination() == PresetNavigationDestination::ARRANGER);
}

TEST(PresetNavigationContextTests, ArrangerTargetChangeCancelsEvenAfterPadRelease) {
	context.uiMode = UI_MODE_HOLDING_ARRANGEMENT_ROW_AUDITION;
	context.fromArranger = true;
	context.selectedClip = nullptr;
	CHECK_FALSE(context.matchesSelection(nullptr, false));
	context.markPadReleased(true);
	CHECK(context.matchesUI(song, ui, UI_MODE_NONE, clip));
	CHECK(context.matchesSelection(nullptr, true));
	CHECK_FALSE(context.matchesSelection(nullptr, false));
}

TEST(PresetNavigationContextTests, RowNavigationResumesWithSavedClip) {
	CHECK(context.destination() == PresetNavigationDestination::CLIP);
	context.markPadReleased(true);
	CHECK(context.destination() == PresetNavigationDestination::CLIP);
	POINTERS_EQUAL(clip, context.displayClip);
}

TEST(PresetNavigationContextTests, GridLabelClipDoesNotTurnWholeOutputReplacementIntoClipReplacement) {
	context.replacingWholeOutput = true;
	CHECK(context.destination() == PresetNavigationDestination::OUTPUT);
	context.markPadReleased(true);
	CHECK(context.destination() == PresetNavigationDestination::OUTPUT);
}

TEST(PresetNavigationContextTests, GridReplacementScopeSurvivesDelegationToAnotherUI) {
	context.replacingWholeOutput = true;
	context.ui = identity<UI>(identities.at(4));
	CHECK(context.destination() == PresetNavigationDestination::OUTPUT);
	CHECK_FALSE(context.matchesSelection(other_clip, true));
}

TEST(PresetNavigationContextTests, NavigationWithoutADisplayClipReplacesOutput) {
	context.displayClip = nullptr;
	CHECK(context.destination() == PresetNavigationDestination::OUTPUT);
}

TEST_GROUP(PresetAvailabilityTests) {
	std::array<std::max_align_t, 2> identities{};
	Instrument* origin = nullptr;
	Instrument* other = nullptr;
	void setup() override {
		origin = identity<Instrument>(identities.at(0));
		other = identity<Instrument>(identities.at(1));
	}
};

TEST(PresetAvailabilityTests, OriginWithActiveSessionClipRemainsReachableFromCandidate) {
	CHECK(presetInstrumentAvailable(Availability::INSTRUMENT_AVAILABLE_IN_SESSION, true, true, origin, origin));
}

TEST(PresetAvailabilityTests, WholeOutputNavigationCanReturnToOccupiedOrigin) {
	CHECK(presetInstrumentAvailable(Availability::INSTRUMENT_UNUSED, true, true, origin, origin));
}

TEST(PresetAvailabilityTests, ExemptionDoesNotMakeAnotherActivePresetAvailable) {
	CHECK_FALSE(presetInstrumentAvailable(Availability::INSTRUMENT_AVAILABLE_IN_SESSION, true, true, other, origin));
	CHECK_FALSE(presetInstrumentAvailable(Availability::INSTRUMENT_UNUSED, true, false, other, origin));
}

TEST(PresetAvailabilityTests, OrdinaryNavigationStillFiltersOccupiedOrigin) {
	CHECK_FALSE(presetInstrumentAvailable(Availability::INSTRUMENT_AVAILABLE_IN_SESSION, true, true, origin));
	CHECK_FALSE(presetInstrumentAvailable(Availability::INSTRUMENT_UNUSED, true, false, origin));
}

TEST(PresetAvailabilityTests, InactiveSessionInstrumentIsAvailableOnlyForClipNavigation) {
	CHECK(presetInstrumentAvailable(Availability::INSTRUMENT_AVAILABLE_IN_SESSION, true, false, other));
	CHECK_FALSE(presetInstrumentAvailable(Availability::INSTRUMENT_UNUSED, true, false, other));
}

TEST(PresetAvailabilityTests, DiskAndHibernatingPresetsRemainAvailable) {
	CHECK(presetInstrumentAvailable(Availability::INSTRUMENT_UNUSED, false, false, nullptr));
	CHECK(presetInstrumentAvailable(Availability::INSTRUMENT_UNUSED, false, false, other));
}

TEST(PresetAvailabilityTests, UnrestrictedBrowserKeepsOccupiedPresets) {
	CHECK(presetInstrumentAvailable(Availability::ANY, true, true, other));
}

TEST(PresetLoadTests, GuardRestoresCallerModeOnSuccessAndFailure) {
	for (Error error : {Error::NONE, Error::FILE_CORRUPTED}) {
		mode = UI_MODE_HOLDING_BUTTON_POTENTIAL_LONG_PRESS;
		pending = true;
		performPresetLoad(pending, mode, interrupted, result, [] {}, [error] { return error; });
		CHECK_EQUAL(UI_MODE_HOLDING_BUTTON_POTENTIAL_LONG_PRESS, mode);
		CHECK(result == error);
	}
}

TEST(PresetNavigationContextTests, ArrangementRecordingCancelsHeldAndReleasedSessionSelections) {
	for (bool whole_output : {false, true}) {
		context.replacingWholeOutput = whole_output;
		context.padReleased = false;
		CHECK(context.decision(song, ui, context.uiMode, clip, false) == deluge::gui::PresetNavigationDecision::LOAD);
		CHECK(context.decision(song, ui, context.uiMode, clip, true) == deluge::gui::PresetNavigationDecision::CANCEL);
		context.markPadReleased(true);
		CHECK(context.decision(song, ui, UI_MODE_NONE, clip, true) == deluge::gui::PresetNavigationDecision::CANCEL);
	}
}

TEST(PresetNavigationContextTests, RecordingRestrictionFollowsSessionOriginNotVisibleUI) {
	context.ui = identity<UI>(identities.at(4));
	CHECK(context.decision(song, context.ui, context.uiMode, clip, true)
	      == deluge::gui::PresetNavigationDecision::CANCEL);
}

TEST(PresetNavigationContextTests, ClipAuditionWaitsThenResumesOnRelease) {
	context.selectedClip = nullptr;
	context.uiMode = UI_MODE_NONE;
	CHECK(context.decision(song, ui, UI_MODE_NONE, clip, false) == deluge::gui::PresetNavigationDecision::LOAD);
	CHECK(context.decision(song, ui, UI_MODE_AUDITIONING, clip, false) == deluge::gui::PresetNavigationDecision::WAIT);
	CHECK(context.decision(song, ui, UI_MODE_NONE, clip, false) == deluge::gui::PresetNavigationDecision::LOAD);
}

TEST(PresetNavigationContextTests, AuditionDoesNotKeepNavigationAliveAfterViewOrTargetChanges) {
	context.selectedClip = nullptr;
	context.uiMode = UI_MODE_NONE;
	CHECK(context.decision(song, nullptr, UI_MODE_AUDITIONING, clip, false)
	      == deluge::gui::PresetNavigationDecision::CANCEL);
	CHECK(context.decision(song, ui, UI_MODE_AUDITIONING, other_clip, false)
	      == deluge::gui::PresetNavigationDecision::CANCEL);
	CHECK(context.decision(nullptr, ui, UI_MODE_AUDITIONING, clip, false)
	      == deluge::gui::PresetNavigationDecision::CANCEL);
}

TEST(PresetNavigationContextTests, AuditionExceptionDoesNotPermitUnrelatedGesturesOrOutputReplacement) {
	context.selectedClip = nullptr;
	context.uiMode = UI_MODE_NONE;
	CHECK(context.decision(song, ui, UI_MODE_HOLDING_BUTTON_POTENTIAL_LONG_PRESS, clip, false)
	      == deluge::gui::PresetNavigationDecision::CANCEL);
	CHECK(context.decision(song, ui, UI_MODE_AUDITIONING | UI_MODE_NOTES_PRESSED, clip, false)
	      == deluge::gui::PresetNavigationDecision::CANCEL);
	context.replacingWholeOutput = true;
	CHECK(context.decision(song, ui, UI_MODE_AUDITIONING, clip, false)
	      == deluge::gui::PresetNavigationDecision::CANCEL);
}

TEST(PresetNavigationContextTests, SessionNavigationDoesNotBecomeClipAuditionOnRelease) {
	context.markPadReleased(true);
	CHECK(context.decision(song, ui, UI_MODE_AUDITIONING, clip, false)
	      == deluge::gui::PresetNavigationDecision::CANCEL);
}

// NOLINTEND(bugprone-throwing-static-initialization, cppcoreguidelines-owning-memory)
