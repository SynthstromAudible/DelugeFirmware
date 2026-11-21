#include "CppUTest/TestHarness.h"
#include "modulation/params/param_control.h"

#include "model/action/parameter_action.h"

using namespace deluge::modulation::params;
TEST_GROUP(ParameterControlTests){};

TEST(ParameterControlTests, ExpressionReadsAndWritesRemainLiveWithOrWithoutTrackLink) {
	for (bool linked : {false, true}) {
		for (bool scalar : {false, true}) {
			CHECK(controlLayer(Kind::EXPRESSION, true, scalar, linked) == ControlLayer::LIVE);
		}
		CHECK(controlLayer(Kind::EXPRESSION, true, true, linked, true) == ControlLayer::REGION);
	}
}

TEST(ParameterControlTests, AutomationRemovalSendsMidiOnlyForActiveClipAndOutput) {
	AutomationTransition removal{true, false};
	CHECK(removal.sendMidi(true, true));
	CHECK_FALSE(removal.sendMidi(true, false));
	CHECK_FALSE(removal.sendMidi(false, true));
	// Preserve intentional manual edits of inactive clips.
	CHECK((AutomationTransition{false, false}.sendMidi(true, false)));
	CHECK((AutomationTransition{true, true}.sendMidi(true, false)));
}

TEST(ParameterControlTests, TempoRemovalRefreshesClockForSongOnly) {
	CHECK((AutomationTransition{true, false}.refreshTempo(true, Kind::UNPATCHED_GLOBAL, UNPATCHED_TEMPO)));
	CHECK_FALSE((AutomationTransition{true, true}.refreshTempo(true, Kind::UNPATCHED_GLOBAL, UNPATCHED_TEMPO)));
	CHECK_FALSE((AutomationTransition{true, false}.refreshTempo(false, Kind::UNPATCHED_GLOBAL, UNPATCHED_TEMPO)));
	CHECK_FALSE((AutomationTransition{true, false}.refreshTempo(true, Kind::UNPATCHED_GLOBAL, UNPATCHED_PAN)));
	CHECK_FALSE((AutomationTransition{true, false}.refreshTempo(true, Kind::MIDI, UNPATCHED_TEMPO)));
}

TEST(ParameterControlTests, ResetParamValuesCreatesItsActionFromItsConfirmationMenu) {
	CHECK(actionAllowedFromUI(false, false, false, false, ActionOrigin::RESET_PARAM_VALUES));
	CHECK_FALSE(actionAllowedFromUI(false, false, false, false, ActionOrigin::DEFAULT));
	CHECK_FALSE(actionAllowedFromUI(false, true, false, false, ActionOrigin::DEFAULT));
	CHECK(actionAllowedFromUI(true, false, false, false, ActionOrigin::DEFAULT));
	CHECK(actionAllowedFromUI(false, true, true, false, ActionOrigin::DEFAULT));
	CHECK(actionAllowedFromUI(false, false, false, true, ActionOrigin::DEFAULT));
}
