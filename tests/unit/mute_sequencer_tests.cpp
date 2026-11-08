#include "CppUTest/TestHarness.h"
#include "model/sequencer/mute_sequencer.h"
#include <string>

namespace {

// Advance the sequencer `count` times, recording 'M' for muted and 'P' for played steps
std::string run(MuteSequencer& sequencer, int count) {
	std::string pattern;
	for (int i = 0; i < count; i++) {
		pattern += sequencer.advance() ? 'M' : 'P';
	}
	return pattern;
}

MuteSequencer make(int steps, int mutes, int offset) {
	MuteSequencer sequencer;
	sequencer.setSteps(steps);
	sequencer.setMutes(mutes);
	sequencer.setOffset(offset);
	return sequencer;
}

} // namespace

TEST_GROUP(MuteSequencer){};

TEST(MuteSequencer, offNeverMutes) {
	MuteSequencer sequencer;
	CHECK_FALSE(sequencer.isEnabled());
	STRCMP_EQUAL("PPPPPP", run(sequencer, 6).c_str());
}

TEST(MuteSequencer, fiveStepsTwoMutes) {
	MuteSequencer sequencer = make(5, 2, 0);
	STRCMP_EQUAL("MMPPPMMPPPMMPPP", run(sequencer, 15).c_str());
}

TEST(MuteSequencer, offsetOneShiftsLeft) {
	MuteSequencer sequencer = make(5, 2, 1);
	STRCMP_EQUAL("MPPPMMPPPM", run(sequencer, 10).c_str());
}

TEST(MuteSequencer, offsetTwoStartsOnPlayedSteps) {
	MuteSequencer sequencer = make(5, 2, 2);
	STRCMP_EQUAL("PPPMMPPPMM", run(sequencer, 10).c_str());
}

TEST(MuteSequencer, zeroMutesPlaysEverything) {
	MuteSequencer sequencer = make(4, 0, 0);
	STRCMP_EQUAL("PPPPPPPP", run(sequencer, 8).c_str());
}

TEST(MuteSequencer, allMutedMutesEverything) {
	MuteSequencer sequencer = make(3, 3, 0);
	STRCMP_EQUAL("MMMMMM", run(sequencer, 6).c_str());
}

TEST(MuteSequencer, restartGoesBackToFirstStep) {
	MuteSequencer sequencer = make(5, 2, 0);
	run(sequencer, 3);
	sequencer.restart();
	STRCMP_EQUAL("MMPPP", run(sequencer, 5).c_str());
}

TEST(MuteSequencer, continueFromKeepsPosition) {
	MuteSequencer original = make(5, 2, 0);
	run(original, 3);
	MuteSequencer clone;
	clone.copySettingsFrom(original);
	clone.continueFrom(original);
	STRCMP_EQUAL("PPMMP", run(clone, 5).c_str());
}

TEST(MuteSequencer, shrinkingStepsClampsMutesAndOffset) {
	MuteSequencer sequencer = make(45, 45, 44);
	sequencer.setSteps(30);
	CHECK_EQUAL(30, sequencer.getMutes());
	CHECK_EQUAL(29, sequencer.getOffset());
}

TEST(MuteSequencer, switchingOffKeepsMutesAndOffset) {
	MuteSequencer sequencer = make(8, 3, 2);
	sequencer.setSteps(0);
	CHECK_FALSE(sequencer.isEnabled());
	CHECK_EQUAL(3, sequencer.getMutes());
	CHECK_EQUAL(2, sequencer.getOffset());
	sequencer.setSteps(8);
	CHECK_EQUAL(3, sequencer.getMutes());
	CHECK_EQUAL(2, sequencer.getOffset());
}

TEST(MuteSequencer, valuesAreClampedToRange) {
	MuteSequencer sequencer = make(99, 99, 99);
	CHECK_EQUAL(MuteSequencer::kMaxSteps, sequencer.getSteps());
	CHECK_EQUAL(MuteSequencer::kMaxSteps, sequencer.getMutes());
	CHECK_EQUAL(MuteSequencer::kMaxSteps - 1, sequencer.getOffset());
}
