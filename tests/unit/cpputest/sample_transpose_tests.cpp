#include "CppUTest/TestHarness.h"
#include "model/sample/sample_transpose.h"

using deluge::sample::SampleTranspose;
using deluge::sample::transposeForNote;

namespace {
void checkTranspose(const SampleTranspose& t, int32_t semitones, int32_t cents) {
	CHECK_EQUAL(semitones, t.semitones);
	CHECK_EQUAL(cents, t.cents);
}
} // namespace

TEST_GROUP(SampleTransposeTests){};

// A multisample range covering only key 58, its file detected 3 cents flat of it: snapped to exactly 58, so the file
// plays in tune on the one key it covers. Folder import passes the key as the range's top note.
TEST(SampleTransposeTests, oneNoteRangeSnapsSmallDetuneToItsKey) {
	checkTranspose(transposeForNote(57.97f, false, true, 58), 2, 0);
}

TEST(SampleTransposeTests, oneNoteRangeKeepsDetuneBeyondFourCents) {
	checkTranspose(transposeForNote(57.90f, false, true, 58), 2, 10);
}

TEST(SampleTransposeTests, oneNoteRangeDoesNotSnapAnotherKey) {
	checkTranspose(transposeForNote(56.97f, false, true, 58), 3, 3);
}

TEST(SampleTransposeTests, rangeOfSeveralKeysKeepsItsCents) {
	checkTranspose(transposeForNote(57.97f, false, false, 0), 2, 3);
}

// The only range of its source: the transpose folds into -5..6 semitones.
TEST(SampleTransposeTests, onlyRangeMinimizesOctaves) {
	checkTranspose(transposeForNote(69.0f, true, false, 0), 3, 0); // A4: -9 -> 3
	checkTranspose(transposeForNote(45.0f, true, false, 0), 3, 0); // A2: 15 -> 3
	checkTranspose(transposeForNote(66.0f, true, false, 0), -6 + 12, 0);
}

TEST(SampleTransposeTests, exactNoteHasNoCents) {
	checkTranspose(transposeForNote(60.0f, false, false, 0), 0, 0);
}
