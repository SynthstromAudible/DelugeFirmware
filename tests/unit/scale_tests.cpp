#include "CppUTest/TestHarness.h"
#include "model/scale/musical_key.h"
#include "model/scale/note_set.h"
#include "model/scale/preset_scales.h"
#include "model/scale/scale_change.h"
#include "model/scale/scale_mapper.h"
#include "model/scale/utils.h"

#include <algorithm>
#include <random>
#include <vector>

TEST_GROUP(NoteSetTest){};

TEST(NoteSetTest, init) {
	NoteSet notes;
	CHECK(notes.size == 12);
	for (int i = 0; i < notes.size; i++) {
		CHECK(!notes.has(i));
	}
}

TEST(NoteSetTest, listConstructor) {
	NoteSet notes = NoteSet({0, 1, 4, 11});
	CHECK_EQUAL(4, notes.count());
	CHECK_EQUAL(0, notes[0]);
	CHECK_EQUAL(1, notes[1]);
	CHECK_EQUAL(4, notes[2]);
	CHECK_EQUAL(11, notes[3]);
}

TEST(NoteSetTest, add) {
	NoteSet notes;
	notes.add(7);
	for (int i = 0; i < notes.size; i++) {
		CHECK(notes.has(i) == (i == 7));
	}
}

TEST(NoteSetTest, fill) {
	NoteSet notes;
	notes.fill();
	for (int i = 0; i < notes.size; i++) {
		CHECK(notes.has(i));
	}
}

TEST(NoteSetTest, count) {
	NoteSet notes;
	CHECK(notes.count() == 0);
	notes.add(3);
	CHECK(notes.count() == 1);
	notes.fill();
	CHECK(notes.count() == 12);
}

TEST(NoteSetTest, union) {
	NoteSet a;
	NoteSet b;
	NoteSet c;
	CHECK_EQUAL(c, a | b);
	CHECK_EQUAL(c, b | a);
	a.fill();
	c.fill();
	CHECK_EQUAL(c, a | b);
	CHECK_EQUAL(c, b | a);
	a.clear();
	c.clear();
	a.add(0);
	b.add(7);
	c.add(0);
	c.add(7);
	CHECK_EQUAL(c, a | b);
	CHECK_EQUAL(c, b | a);
	CHECK_EQUAL(1, a.count());
	CHECK_EQUAL(1, b.count());
}

TEST(NoteSetTest, scaleSize) {
	NoteSet notes;
	CHECK_EQUAL(1, notes.scaleSize());
	notes.add(0);
	CHECK_EQUAL(1, notes.scaleSize());
	notes.add(3);
	CHECK_EQUAL(2, notes.scaleSize());
}

TEST(NoteSetTest, clear) {
	NoteSet notes;
	notes.add(1);
	notes.add(2);
	CHECK_EQUAL(2, notes.count());
	notes.clear();
	CHECK_EQUAL(0, notes.count());
}

TEST(NoteSetTest, highest) {
	NoteSet notes;
	notes.add(0);
	CHECK_EQUAL(0, notes.highest());
	notes.add(1);
	CHECK_EQUAL(1, notes.highest());
	notes.add(7);
	CHECK_EQUAL(7, notes.highest());
	notes.add(11);
	CHECK_EQUAL(11, notes.highest());
}

TEST(NoteSetTest, addUntrusted) {
	NoteSet a;
	a.addUntrusted(0);
	a.addUntrusted(0);
	a.addUntrusted(12);
	CHECK_EQUAL(0, a[0]);
	CHECK_EQUAL(1, a[1]);
	CHECK_EQUAL(11, a[2]);
	CHECK_EQUAL(3, a.count());
}

TEST(NoteSetTest, degreeOfBasic) {
	NoteSet a;
	a.add(0);
	a.add(2);
	a.add(4);
	CHECK_EQUAL(0, a.degreeOf(0));
	CHECK_EQUAL(1, a.degreeOf(2));
	CHECK_EQUAL(2, a.degreeOf(4));
}

TEST(NoteSetTest, degreeOfNotAScale) {
	NoteSet a;
	a.add(1);
	a.add(2);
	a.add(4);
	CHECK_EQUAL(-1, a.degreeOf(0));
	CHECK_EQUAL(0, a.degreeOf(1));
	CHECK_EQUAL(1, a.degreeOf(2));
	CHECK_EQUAL(2, a.degreeOf(4));
}

TEST(NoteSetTest, isSubsetOf) {
	NoteSet a;
	NoteSet b;
	CHECK(a.isSubsetOf(b));
	CHECK(b.isSubsetOf(a));
	a.add(3);
	b.add(3);
	CHECK(a.isSubsetOf(b));
	CHECK(b.isSubsetOf(a));
	a.add(0);
	CHECK(!a.isSubsetOf(b));
	CHECK(b.isSubsetOf(a));
	b.add(0);
	b.add(11);
	CHECK(a.isSubsetOf(b));
	CHECK(!b.isSubsetOf(a));
	a.add(7);
	CHECK(!a.isSubsetOf(b));
	CHECK(!b.isSubsetOf(a));
}

TEST(NoteSetTest, equality) {
	NoteSet a;
	NoteSet b;
	CHECK(a == b);
	a.add(0);
	CHECK(a != b);
}

TEST(NoteSetTest, checkEqualAllowed) {
	CHECK_EQUAL(NoteSet(), NoteSet());
}

TEST(NoteSetTest, isEmpty) {
	NoteSet a;
	CHECK_EQUAL(true, a.isEmpty());
	a.add(0);
	CHECK_EQUAL(false, a.isEmpty());
}

TEST(NoteSetTest, subscript1) {
	NoteSet a;
	for (int i = 0; i < NoteSet::size; i++) {
		CHECK_EQUAL(-1, a[i]);
	}
}

TEST(NoteSetTest, subscript2) {
	NoteSet a;
	a.add(0);
	a.add(2);
	a.add(4);
	a.add(5);
	a.add(7);
	a.add(9);
	a.add(11);
	CHECK_EQUAL(0, a[0]);
	CHECK_EQUAL(2, a[1]);
	CHECK_EQUAL(4, a[2]);
	CHECK_EQUAL(5, a[3]);
	CHECK_EQUAL(7, a[4]);
	CHECK_EQUAL(9, a[5]);
	CHECK_EQUAL(11, a[6]);
	a.add(1);
	CHECK_EQUAL(0, a[0]);
	CHECK_EQUAL(1, a[1]);
	CHECK_EQUAL(2, a[2]);
	CHECK_EQUAL(4, a[3]);
	CHECK_EQUAL(5, a[4]);
	CHECK_EQUAL(7, a[5]);
	CHECK_EQUAL(9, a[6]);
	CHECK_EQUAL(11, a[7]);
}

TEST(NoteSetTest, subscript3) {
	NoteSet a;
	a.add(0);
	a.add(2);
	a.add(4);
	a.add(5);
	a.add(7);
	a.add(9);
	a.add(11);
	a.add(1);
	CHECK_EQUAL(0, a[0]);
	CHECK_EQUAL(1, a[1]);
	CHECK_EQUAL(2, a[2]);
	CHECK_EQUAL(4, a[3]);
	CHECK_EQUAL(5, a[4]);
	CHECK_EQUAL(7, a[5]);
	CHECK_EQUAL(9, a[6]);
	CHECK_EQUAL(11, a[7]);
}

TEST(NoteSetTest, subscript4) {
	NoteSet a;
	a.add(4);
	a.add(7);
	CHECK_EQUAL(4, a[0]);
	CHECK_EQUAL(7, a[1]);
}

TEST(NoteSetTest, remove) {
	NoteSet a;
	for (int i = 0; i < 12; i++) {
		a.remove(i);
		CHECK_EQUAL(0, a.count());
	}
	a.fill();
	for (int i = 0; i < 12; i++) {
		CHECK_EQUAL(true, a.has(i));
		a.remove(i);
		CHECK_EQUAL(false, a.has(i));
	}
	a.fill();
	for (int i = 11; i >= 0; i--) {
		CHECK_EQUAL(true, a.has(i));
		a.remove(i);
		CHECK_EQUAL(false, a.has(i));
	}
}

TEST(NoteSetTest, presetScaleId) {
	CHECK_EQUAL(MAJOR_SCALE, presetScaleNotes[MAJOR_SCALE].presetScaleId());
	CHECK_EQUAL(MINOR_SCALE, presetScaleNotes[MINOR_SCALE].presetScaleId());
	CHECK_EQUAL(CUSTOM_SCALE_WITH_MORE_THAN_7_NOTES, NoteSet().presetScaleId());
}

TEST(NoteSetTest, majorness) {
	// Thirds?
	CHECK_EQUAL(0, NoteSet({0}).majorness());
	CHECK_EQUAL(-1, NoteSet({0, 3}).majorness());
	CHECK_EQUAL(1, NoteSet({0, 4}).majorness());
	CHECK_EQUAL(0, NoteSet({0, 3, 4}).majorness());
	// Indeterminate after third, what about 2nd?
	CHECK_EQUAL(-1, NoteSet({0, 1}).majorness());
	CHECK_EQUAL(-1, NoteSet({0, 1, 3, 4}).majorness());
	// Indeterminate after third, what about 6th?
	CHECK_EQUAL(-1, NoteSet({0, 8}).majorness());
	CHECK_EQUAL(-1, NoteSet({0, 8, 3, 4}).majorness());
	// Indeterminate after third, what about 7th?
	CHECK_EQUAL(1, NoteSet({0, 9}).majorness());
	CHECK_EQUAL(1, NoteSet({0, 9, 3, 4}).majorness());
}

TEST(NoteSetTest, addMajorDependentModeNotes) {
	NoteSet a;
	// Case 1: the lower interval is present -> preferHigher does not matter
	a.addMajorDependentModeNotes(1, false, NoteSet{1});
	CHECK_EQUAL(NoteSet({1}), a);
	a.clear();
	a.addMajorDependentModeNotes(1, true, NoteSet{1});
	CHECK_EQUAL(NoteSet({1}), a);

	// Case 2: the higher interval is present -> preferHigher does not matter
	a.clear();
	a.addMajorDependentModeNotes(1, false, NoteSet{2});
	CHECK_EQUAL(NoteSet({2}), a);
	a.clear();
	a.addMajorDependentModeNotes(1, true, NoteSet{2});
	CHECK_EQUAL(NoteSet({2}), a);

	// Case 3: both intervals are present -> preferHigher does not matter
	a.clear();
	a.addMajorDependentModeNotes(1, false, NoteSet{1, 2});
	CHECK_EQUAL(NoteSet({1, 2}), a);
	// Case 3: both intervals are present -> preferHigher does not matter
	a.clear();
	a.addMajorDependentModeNotes(1, true, NoteSet{1, 2});
	CHECK_EQUAL(NoteSet({1, 2}), a);

	// Case 4: neither interval is not present -> prefer higher determines
	a.clear();
	a.addMajorDependentModeNotes(1, false, NoteSet{});
	CHECK_EQUAL(NoteSet({1}), a);
	a.clear();
	a.addMajorDependentModeNotes(1, true, NoteSet{});
	CHECK_EQUAL(NoteSet({2}), a);
}

TEST(NoteSetTest, toImpliedScale) {
	// There's thousands of combinations to test - for sake of making sense what's
	// going on, just going through each semitone on it's own.

	// Major scale is the default
	CHECK_EQUAL(presetScaleNotes[MAJOR_SCALE], NoteSet({}).toImpliedScale());
	// Minor second gets us the phrygian
	CHECK_EQUAL(presetScaleNotes[PHRYGIAN_SCALE], NoteSet({1}).toImpliedScale());
	// Major second gets us the major
	CHECK_EQUAL(presetScaleNotes[MAJOR_SCALE], NoteSet({2}).toImpliedScale());
	// Minor third gets us the minor
	CHECK_EQUAL(presetScaleNotes[MINOR_SCALE], NoteSet({3}).toImpliedScale());
	// Major third gets us the major
	CHECK_EQUAL(presetScaleNotes[MAJOR_SCALE], NoteSet({4}).toImpliedScale());
	// Perfect fourth gets us the major
	CHECK_EQUAL(presetScaleNotes[MAJOR_SCALE], NoteSet({5}).toImpliedScale());
	// Tritone gets us the lydian scale
	CHECK_EQUAL(presetScaleNotes[LYDIAN_SCALE], NoteSet({6}).toImpliedScale());
	// Perfeft fifth gets us the major
	CHECK_EQUAL(presetScaleNotes[MAJOR_SCALE], NoteSet({7}).toImpliedScale());
	// Minor sixth gets us the minor
	CHECK_EQUAL(presetScaleNotes[MINOR_SCALE], NoteSet({8}).toImpliedScale());
	// Major sixth gets us the major
	CHECK_EQUAL(presetScaleNotes[MAJOR_SCALE], NoteSet({9}).toImpliedScale());
	// Minor seventh gets us the mixolydian
	CHECK_EQUAL(presetScaleNotes[MIXOLYDIAN_SCALE], NoteSet({10}).toImpliedScale());
	// Major seventh gets us the major
	CHECK_EQUAL(presetScaleNotes[MAJOR_SCALE], NoteSet({11}).toImpliedScale());
}

TEST(NoteSetTest, highestNotIn) {
	// A is aways the receiver and B the argument in these tests.
	NoteSet a;
	NoteSet b;
	// First the edge cases: empty or full notesets
	//
	//    A     B      result
	//    empty empty  -1
	//    empty full   -1
	//    full  empty  11
	//    full  full   -1
	//
	a.clear();
	b.clear();
	CHECK_EQUAL(-1, a.highestNotIn(b));
	a.clear();
	b.fill();
	CHECK_EQUAL(-1, a.highestNotIn(b));
	a.fill();
	b.clear();
	CHECK_EQUAL(11, a.highestNotIn(b));
	a.fill();
	b.fill();
	CHECK_EQUAL(-1, a.highestNotIn(b));
	// Major scale in A, one less note in B
	const NoteSet major{0, 2, 4, 5, 7, 9, 11};
	a = major;
	for (int i = 0; i < sizeof(major); i++) {
		b = major;
		b.remove(major[i]);
		CHECK_EQUAL(major[i], a.highestNotIn(b));
	}
	// Major scale in A, three missing notes in B
	a = major;
	b = major;
	b.remove(4);
	b.remove(7);
	b.remove(11);
	CHECK_EQUAL(11, a.highestNotIn(b));
}

TEST_GROUP(MusicalKeyTest){};

TEST(MusicalKeyTest, ctor) {
	MusicalKey k;
	CHECK_EQUAL(0, k.rootNote);
	CHECK_EQUAL(1, k.modeNotes.count());
	CHECK_EQUAL(true, k.modeNotes.has(0));
}

TEST(MusicalKeyTest, intervalOf) {
	MusicalKey key;
	for (int8_t octave = -10; octave <= 10; octave++) {
		for (uint8_t note = 0; note < 12; note++) {
			for (uint8_t root = 0; root < 12; root++) {
				key.rootNote = root;
				if (root <= note) {
					CHECK_EQUAL(note - root, key.intervalOf(note + octave * 12));
				}
				else {
					// Consider: root B==11, note D=2, offset=3
					CHECK_EQUAL(12 - root + note, key.intervalOf(note + octave * 12));
				}
			}
		}
	}
}

TEST(MusicalKeyTest, degreeOf) {
	MusicalKey key;
	key.rootNote = 9; // A
	key.modeNotes = presetScaleNotes[MINOR_SCALE];

	for (int octave = -2; octave <= 2; octave++) {
		// In key
		CHECK_EQUAL(0, key.degreeOf(9 + octave * 12));  // A
		CHECK_EQUAL(1, key.degreeOf(11 + octave * 12)); // B
		CHECK_EQUAL(2, key.degreeOf(0 + octave * 12));  // C
		CHECK_EQUAL(3, key.degreeOf(2 + octave * 12));  // D
		CHECK_EQUAL(4, key.degreeOf(4 + octave * 12));  // E
		CHECK_EQUAL(5, key.degreeOf(5 + octave * 12));  // F
		CHECK_EQUAL(6, key.degreeOf(7 + octave * 12));  // G
		// Out of key
		CHECK_EQUAL(-1, key.degreeOf(10 + octave * 12)); // A#
		CHECK_EQUAL(-1, key.degreeOf(1 + octave * 12));  // C#
		CHECK_EQUAL(-1, key.degreeOf(3 + octave * 12));  // D#
		CHECK_EQUAL(-1, key.degreeOf(6 + octave * 12));  // F#
		CHECK_EQUAL(-1, key.degreeOf(8 + octave * 12));  // G#
	}
}

TEST_GROUP(UtilTest){};

TEST(UtilTest, isSameNote) {
	// Exhaustive test for a reasonable input range
	for (int a = -200; a <= 200; a++) {
		for (int b = -200; b <= 200; b++) {
			// Different variations in the codebase that were replaced by isSameNote()
			int legacy1 = (int32_t)std::abs(a - b) % 12 == 0;
			int legacy2 = (uint16_t)(a - b + 120) % (uint8_t)12 == 0;
			bool same = isSameNote(a, b);
			// The first variation matches always.
			CHECK_EQUAL(legacy1, same);
			// The second variation returns bogus values if (a-b+120) goes negative,
			// due to the cast to u16.
			if (a - b + 120 >= 0) {
				CHECK_EQUAL(legacy2, same);
			}
		}
	}
	// Explicit demo of the legacy2 code misbehaving: both numbers are 2 modulo 12.
	int a = 2;
	int b = 146;
	int legacy2 = (uint16_t)(a - b + 120) % (uint8_t)12 == 0;
	CHECK(!legacy2);
	CHECK(isSameNote(a, b));
}

TEST_GROUP(ScaleMapperTest){};

TEST(ScaleMapperTest, smallerTargetScaleExactlyUsed) {
	ScaleMapper scaleMapper;
	// clang-format off
	const NoteSet diatonicMajor =   NoteSet{0, 2, 4, 5, 7, 9, 11};
	const NoteSet pentatonicMajor = NoteSet{0, 2, 4,    7, 9};
	// clang-format on
	NoteSet notes = pentatonicMajor;

	ScaleChange changes;
	bool ok = scaleMapper.computeChangeFrom(notes, diatonicMajor, pentatonicMajor, changes);
	CHECK(ok);
	NoteSet targetNotes = changes.applyTo(notes);
	// only legal transformation!
	CHECK_EQUAL(pentatonicMajor, targetNotes);

	// Now reverse

	ok = scaleMapper.computeChangeFrom(targetNotes, pentatonicMajor, diatonicMajor, changes);
	CHECK(ok);
	NoteSet reverseNotes = changes.applyTo(targetNotes);
	CHECK_EQUAL(notes, reverseNotes);
}

TEST(ScaleMapperTest, smallerTargetScalePartiallyUsed) {
	ScaleMapper scaleMapper;
	// clang-format off
	const NoteSet diatonicMajor =   NoteSet{0, 2, 4, 5, 7, 9, 11};
	const NoteSet pentatonicMajor = NoteSet{0, 2, 4,    7, 9};
	const NoteSet notes =           NoteSet{0,    4,    7};
	// clang-format on

	ScaleChange changes;
	bool ok = scaleMapper.computeChangeFrom(notes, diatonicMajor, pentatonicMajor, changes);
	CHECK(ok);
	NoteSet targetNotes = changes.applyTo(notes);
	// one possible transformation
	CHECK_EQUAL(notes, targetNotes);

	// Now reverse

	ok = scaleMapper.computeChangeFrom(targetNotes, pentatonicMajor, diatonicMajor, changes);
	CHECK(ok);
	NoteSet reverseNotes = changes.applyTo(targetNotes);
	CHECK_EQUAL(notes, reverseNotes);
}

TEST(ScaleMapperTest, notesOutsideSmallerTargetScale) {
	ScaleMapper scaleMapper;
	// clang-format off
	const NoteSet diatonicMajor =   NoteSet{0, 2, 4, 5, 7, 9, 11};
	const NoteSet pentatonicMajor = NoteSet{0, 2, 4,    7, 9};
	const NoteSet notes =           NoteSet{0,    4,    7, 9, 11};
	// clang-format on

	ScaleChange changes;
	bool ok = scaleMapper.computeChangeFrom(notes, diatonicMajor, pentatonicMajor, changes);
	CHECK(ok);
	NoteSet targetNotes = changes.applyTo(notes);
	// only legal transformation!
	CHECK_EQUAL(pentatonicMajor, targetNotes);

	// Now reverse

	ok = scaleMapper.computeChangeFrom(targetNotes, pentatonicMajor, diatonicMajor, changes);
	CHECK(ok);
	NoteSet reverseNotes = changes.applyTo(targetNotes);
	CHECK_EQUAL(notes, reverseNotes);
}

TEST(ScaleMapperTest, willRefuseIfDoesNotFitTarget) {
	// This specific case has five notes, but root is always implied: if we transpose something else to root,
	// we get a many to one mapping, and cannot recover.
	ScaleMapper scaleMapper;
	// clang-format off
	const NoteSet diatonicMajor =   NoteSet{0, 2, 4, 5, 7, 9, 11};
	const NoteSet pentatonicMajor = NoteSet{0, 2, 4,    7, 9};
	const NoteSet notes =           NoteSet{   2, 4,    7, 9, 11};
	// clang-format on

	ScaleChange changes;
	bool ok = scaleMapper.computeChangeFrom(notes, diatonicMajor, pentatonicMajor, changes);
	CHECK(!ok);
}

TEST(ScaleMapperTest, willRefuseIfSourceNotesNotInScale) {
	ScaleMapper scaleMapper;
	// clang-format off
	const NoteSet diatonicMajor =   NoteSet{0,    2,    4, 5, 7, 9, 11};
	const NoteSet pentatonicMajor = NoteSet{0,    2,    4,    7, 9};
	const NoteSet notes =           NoteSet{         3};
	// clang-format on

	ScaleChange changes;
	bool ok = scaleMapper.computeChangeFrom(notes, diatonicMajor, pentatonicMajor, changes);
	CHECK(!ok);
}

TEST(ScaleMapperTest, notesDontMatchTargetScale_plusNoRootInUse) {
	// Check that we're not counting root as used if it's not, and still get the right answer
	ScaleMapper scaleMapper;
	// clang-format off
	const NoteSet diatonicMajor =   NoteSet{0, 2, 4, 5, 7, 9, 11};
	const NoteSet pentatonicMajor = NoteSet{0, 2, 4,    7, 9};
	const NoteSet notes =           NoteSet{   2, 4,    7,    11};
	const NoteSet want =            NoteSet{   2, 4,    7, 9};
	// clang-format on

	ScaleChange changes;
	bool ok = scaleMapper.computeChangeFrom(notes, diatonicMajor, pentatonicMajor, changes);
	CHECK(ok);
	NoteSet targetNotes = changes.applyTo(notes);
	// the only legal transformation - assuming we don't transpose non-root notes to root.
	CHECK_EQUAL(want, targetNotes);

	// Now reverse

	ok = scaleMapper.computeChangeFrom(targetNotes, pentatonicMajor, diatonicMajor, changes);
	CHECK(ok);
	NoteSet reverseNotes = changes.applyTo(targetNotes);
	CHECK_EQUAL(notes, reverseNotes);
}

TEST(ScaleMapperTest, pentatonicMinorToDiatonicMajor) {
	ScaleMapper scaleMapper;
	// clang-format off
	const NoteSet pentatonicMinor = NoteSet{0, 2, 3,    7, 9};
	const NoteSet diatonicMajor =   NoteSet{0, 2, 4, 5, 7, 9, 11};
	const NoteSet notes =           NoteSet{0,    3,    7, 9};
	const NoteSet want =            NoteSet{0,    4,    7, 9};
	// clang-format on

	ScaleChange changes;
	bool ok = scaleMapper.computeChangeFrom(notes, pentatonicMinor, diatonicMajor, changes);
	CHECK(ok);
	NoteSet targetNotes = changes.applyTo(notes);
	// not the only legal transformation, but certainly the most intuitive one...
	CHECK_EQUAL(want, targetNotes);

	// Now reverse

	ok = scaleMapper.computeChangeFrom(targetNotes, diatonicMajor, pentatonicMinor, changes);
	CHECK(ok);
	NoteSet reverseNotes = changes.applyTo(targetNotes);
	CHECK_EQUAL(notes, reverseNotes);
}

std::uniform_int_distribution<int> randomNonRoot(1, 11);
std::uniform_int_distribution<int> randomCoin(0, 1);

NoteSet randomScale(uint8_t size, std::mt19937& engine) {
	NoteSet scale;
	scale.add(0);
	if (size > 6) {
		scale.fill();
	}
	while (scale.count() != size) {
		uint8_t note = randomNonRoot(engine);
		if (scale.count() < size && !scale.has(note)) {
			scale.add(note);
		}
		else if (scale.count() > size && scale.has(note)) {
			scale.remove(note);
		}
	}
	return scale;
}

NoteSet randomNotesIn(NoteSet scale, std::mt19937& engine) {
	NoteSet notes;
	// At least one note!
	while (notes.isEmpty()) {
		for (uint8_t n = 0; n < scale.count(); n++) {
			if (randomCoin(engine)) {
				notes.add(scale[n]);
			}
		}
	}
	return notes;
}

std::vector<int> randomWorklist(int start, int end, std::mt19937& engine) {
	std::vector<int> worklist(end - start);
	for (int i = start; i < end; i++) {
		worklist[i - start] = i;
	}
	std::shuffle(worklist.begin(), worklist.end(), engine);
	return worklist;
}

TEST(ScaleMapperTest, randomTest) {
	std::random_device r;
	int s1 = r();
	int s2 = r();
	// Output this so it's accessible in case of failure.
	std::cerr << "RANDOM TEST SEEDS = { " << s1 << ", " << s2 << " }" << std::endl;
	std::seed_seq seeds{s1, s2};
	std::mt19937 engine(seeds);

	// random scales in increasing size order (multiple of each size)
	std::vector<NoteSet> scales;
	// scaleRange[size] --> index of smallest scale of size or greater
	std::vector<int> scaleRanges;

	scaleRanges.push_back(0); // empty
	scaleRanges.push_back(0); // just the root

	const int nScalesPerSize = 2;

	for (int s = scaleRanges.size(); s < kMaxScaleSize; s++) {
		int base = scales.size();
		scaleRanges.push_back(base);
		while (scales.size() - base < nScalesPerSize) {
			NoteSet newScale = randomScale(s, engine);
			if (std::find(scales.begin() + base, scales.end(), newScale) == std::end(scales)) {
				scales.push_back(newScale);
			}
		}
	}

	// For each generated scale:
	//   Create a random set of notes in that scale
	//   For each scale of appropriate size for the notes, in random order
	//      Transform the notes to the scale, save the result for the scale.
	//      Transform back to original scale, verify that notes match.
	//      Keep going using transformed notes.

	// Having the same scale mapper for all cases is important: this tests the transitition
	// scale flushing logic.
	ScaleMapper scaleMapper;
	for (auto& sourceScale : scales) {
		ScaleChange changes;
		NoteSet sourceNotes = randomNotesIn(sourceScale, engine);
		// std::cerr << "TESTING SCALE " << sourceScale << " WITH " << sourceNotes << std::endl;
		int size = sourceNotes.scaleSize();
		int start = scaleRanges[size];
		CHECK(start >= 0);
		int end = scales.size();
		NoteSet testScale = sourceScale;
		NoteSet testNotes = sourceNotes;
		for (int n : randomWorklist(start, end, engine)) {
			NoteSet targetScale = scales[n];
			bool ok = scaleMapper.computeChangeFrom(testNotes, testScale, targetScale, changes);
			CHECK(ok);
			testNotes = changes.applyTo(testNotes);
			testScale = targetScale;
			// std::cerr << "-> " << testScale << " AS " << testNotes << " @" << n << std::endl << std::flush;
			CHECK(testNotes.isSubsetOf(testScale));
			ok = scaleMapper.computeChangeFrom(testNotes, testScale, sourceScale, changes);
			CHECK(ok);
			NoteSet reverse = changes.applyTo(testNotes);
			CHECK_EQUAL(sourceNotes, reverse);
		}
	}
}
