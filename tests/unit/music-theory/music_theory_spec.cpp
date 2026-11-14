#include "music-theory/musical_key.h"
#include "music-theory/note_set.h"
#include "music-theory/preset_scales.h"
#include "music-theory/scale_change.h"
#include "music-theory/scale_mapper.h"
#include "music-theory/utils.h"

#include "note_set_printing.h"

#include <algorithm>
#include <iostream>
#include <random>
#include <vector>

#include "cppspec.hpp"
#include "support/spec_main.h"

using namespace deluge::music;

namespace {

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

} // namespace

// clang-format off
describe note_set_group("note set", ${
	it("init", _ {
		NoteSet notes;
		expect(notes.size).to_equal(12);
		for (int i = 0; i < notes.size; i++) {
			expect(notes.has(i)).to_be_false();
		}
	});

	it("list constructor adds each listed note", _ {
		NoteSet notes = NoteSet({0, 1, 4, 11});
		expect(notes.count()).to_equal(4);
		expect(notes[0]).to_equal(0);
		expect(notes[1]).to_equal(1);
		expect(notes[2]).to_equal(4);
		expect(notes[3]).to_equal(11);
	});

	it("add", _ {
		NoteSet notes;
		notes.add(7);
		for (int i = 0; i < notes.size; i++) {
			expect(notes.has(i)).to_equal(i == 7);
		}
	});

	it("fill", _ {
		NoteSet notes;
		notes.fill();
		for (int i = 0; i < notes.size; i++) {
			expect(notes.has(i)).to_be_true();
		}
	});

	it("count", _ {
		NoteSet notes;
		expect(notes.count()).to_equal(0);
		notes.add(3);
		expect(notes.count()).to_equal(1);
		notes.fill();
		expect(notes.count()).to_equal(12);
	});

	it("union", _ {
		NoteSet a;
		NoteSet b;
		NoteSet c;
		expect(a | b).to_equal(c);
		expect(b | a).to_equal(c);
		a.fill();
		c.fill();
		expect(a | b).to_equal(c);
		expect(b | a).to_equal(c);
		a.clear();
		c.clear();
		a.add(0);
		b.add(7);
		c.add(0);
		c.add(7);
		expect(a | b).to_equal(c);
		expect(b | a).to_equal(c);
		expect(a.count()).to_equal(1);
		expect(b.count()).to_equal(1);
	});

	it("scale size", _ {
		NoteSet notes;
		expect(notes.scaleSize()).to_equal(1);
		notes.add(0);
		expect(notes.scaleSize()).to_equal(1);
		notes.add(3);
		expect(notes.scaleSize()).to_equal(2);
	});

	it("clear", _ {
		NoteSet notes;
		notes.add(1);
		notes.add(2);
		expect(notes.count()).to_equal(2);
		notes.clear();
		expect(notes.count()).to_equal(0);
	});

	it("highest", _ {
		NoteSet notes;
		notes.add(0);
		expect(notes.highest()).to_equal(0);
		notes.add(1);
		expect(notes.highest()).to_equal(1);
		notes.add(7);
		expect(notes.highest()).to_equal(7);
		notes.add(11);
		expect(notes.highest()).to_equal(11);
	});

	it("add untrusted", _ {
		NoteSet a;
		a.addUntrusted(0);
		a.addUntrusted(0);
		a.addUntrusted(12);
		expect(a[0]).to_equal(0);
		expect(a[1]).to_equal(1);
		expect(a[2]).to_equal(11);
		expect(a.count()).to_equal(3);
	});

	it("degree of basic", _ {
		NoteSet a;
		a.add(0);
		a.add(2);
		a.add(4);
		expect(a.degreeOf(0)).to_equal(0);
		expect(a.degreeOf(2)).to_equal(1);
		expect(a.degreeOf(4)).to_equal(2);
	});

	it("degree of not a scale", _ {
		NoteSet a;
		a.add(1);
		a.add(2);
		a.add(4);
		expect(a.degreeOf(0)).to_equal(-1);
		expect(a.degreeOf(1)).to_equal(0);
		expect(a.degreeOf(2)).to_equal(1);
		expect(a.degreeOf(4)).to_equal(2);
	});

	it("is subset of", _ {
		NoteSet a;
		NoteSet b;
		expect(a.isSubsetOf(b)).to_be_true();
		expect(b.isSubsetOf(a)).to_be_true();
		a.add(3);
		b.add(3);
		expect(a.isSubsetOf(b)).to_be_true();
		expect(b.isSubsetOf(a)).to_be_true();
		a.add(0);
		expect(a.isSubsetOf(b)).to_be_false();
		expect(b.isSubsetOf(a)).to_be_true();
		b.add(0);
		b.add(11);
		expect(a.isSubsetOf(b)).to_be_true();
		expect(b.isSubsetOf(a)).to_be_false();
		a.add(7);
		expect(a.isSubsetOf(b)).to_be_false();
		expect(b.isSubsetOf(a)).to_be_false();
	});

	it("equality", _ {
		NoteSet a;
		NoteSet b;
		expect(a).to_equal(b);
		a.add(0);
		expect(a).not_().to_equal(b);
	});

	it("empty note sets compare equal", _ {
		expect(NoteSet()).to_equal(NoteSet());
	});

	it("is empty", _ {
		NoteSet a;
		expect(a.isEmpty()).to_be_true();
		a.add(0);
		expect(a.isEmpty()).to_be_false();
	});

	it("subscript of an empty set is minus one", _ {
		NoteSet a;
		for (int i = 0; i < NoteSet::size; i++) {
			expect(a[i]).to_equal(-1);
		}
	});

	it("subscript keeps the notes in order as notes are added", _ {
		NoteSet a;
		a.add(0);
		a.add(2);
		a.add(4);
		a.add(5);
		a.add(7);
		a.add(9);
		a.add(11);
		expect(a[0]).to_equal(0);
		expect(a[1]).to_equal(2);
		expect(a[2]).to_equal(4);
		expect(a[3]).to_equal(5);
		expect(a[4]).to_equal(7);
		expect(a[5]).to_equal(9);
		expect(a[6]).to_equal(11);
		a.add(1);
		expect(a[0]).to_equal(0);
		expect(a[1]).to_equal(1);
		expect(a[2]).to_equal(2);
		expect(a[3]).to_equal(4);
		expect(a[4]).to_equal(5);
		expect(a[5]).to_equal(7);
		expect(a[6]).to_equal(9);
		expect(a[7]).to_equal(11);
	});

	it("subscript gives the notes in order whatever order they were added in", _ {
		NoteSet a;
		a.add(0);
		a.add(2);
		a.add(4);
		a.add(5);
		a.add(7);
		a.add(9);
		a.add(11);
		a.add(1);
		expect(a[0]).to_equal(0);
		expect(a[1]).to_equal(1);
		expect(a[2]).to_equal(2);
		expect(a[3]).to_equal(4);
		expect(a[4]).to_equal(5);
		expect(a[5]).to_equal(7);
		expect(a[6]).to_equal(9);
		expect(a[7]).to_equal(11);
	});

	it("subscript of a set without the root starts at its lowest note", _ {
		NoteSet a;
		a.add(4);
		a.add(7);
		expect(a[0]).to_equal(4);
		expect(a[1]).to_equal(7);
	});

	it("remove", _ {
		NoteSet a;
		for (int i = 0; i < 12; i++) {
			a.remove(i);
			expect(a.count()).to_equal(0);
		}
		a.fill();
		for (int i = 0; i < 12; i++) {
			expect(a.has(i)).to_be_true();
			a.remove(i);
			expect(a.has(i)).to_be_false();
		}
		a.fill();
		for (int i = 11; i >= 0; i--) {
			expect(a.has(i)).to_be_true();
			a.remove(i);
			expect(a.has(i)).to_be_false();
		}
	});

	it("get scale", _ {
		expect(getScale(presetScaleNotes[MAJOR_SCALE])).to_equal(MAJOR_SCALE);
		expect(getScale(presetScaleNotes[MINOR_SCALE])).to_equal(MINOR_SCALE);
		expect(getScale(NoteSet())).to_equal(USER_SCALE);
	});

	it("majorness", _ {
		// Thirds?
		expect(NoteSet({0}).majorness()).to_equal(0);
		expect(NoteSet({0, 3}).majorness()).to_equal(-1);
		expect(NoteSet({0, 4}).majorness()).to_equal(1);
		expect(NoteSet({0, 3, 4}).majorness()).to_equal(0);
		// Indeterminate after third, what about 2nd?
		expect(NoteSet({0, 1}).majorness()).to_equal(-1);
		expect(NoteSet({0, 1, 3, 4}).majorness()).to_equal(-1);
		// Indeterminate after third, what about 6th?
		expect(NoteSet({0, 8}).majorness()).to_equal(-1);
		expect(NoteSet({0, 8, 3, 4}).majorness()).to_equal(-1);
		// Indeterminate after third, what about 7th?
		expect(NoteSet({0, 9}).majorness()).to_equal(1);
		expect(NoteSet({0, 9, 3, 4}).majorness()).to_equal(1);
	});

	it("add major dependent mode notes", _ {
		NoteSet a;
		// Case 1: the lower interval is present -> preferHigher does not matter
		a.addMajorDependentModeNotes(1, false, NoteSet{1});
		expect(a).to_equal(NoteSet({1}));
		a.clear();
		a.addMajorDependentModeNotes(1, true, NoteSet{1});
		expect(a).to_equal(NoteSet({1}));

		// Case 2: the higher interval is present -> preferHigher does not matter
		a.clear();
		a.addMajorDependentModeNotes(1, false, NoteSet{2});
		expect(a).to_equal(NoteSet({2}));
		a.clear();
		a.addMajorDependentModeNotes(1, true, NoteSet{2});
		expect(a).to_equal(NoteSet({2}));

		// Case 3: both intervals are present -> preferHigher does not matter
		a.clear();
		a.addMajorDependentModeNotes(1, false, NoteSet{1, 2});
		expect(a).to_equal(NoteSet({1, 2}));
		// Case 3: both intervals are present -> preferHigher does not matter
		a.clear();
		a.addMajorDependentModeNotes(1, true, NoteSet{1, 2});
		expect(a).to_equal(NoteSet({1, 2}));

		// Case 4: neither interval is present -> prefer higher determines
		a.clear();
		a.addMajorDependentModeNotes(1, false, NoteSet{});
		expect(a).to_equal(NoteSet({1}));
		a.clear();
		a.addMajorDependentModeNotes(1, true, NoteSet{});
		expect(a).to_equal(NoteSet({2}));
	});

	it("to implied scale", _ {
		// There's thousands of combinations to test - for sake of making sense what's
		// going on, just going through each semitone on its own.

		// Major scale is the default
		expect(NoteSet({}).toImpliedScale()).to_equal(presetScaleNotes[MAJOR_SCALE]);
		// Minor second gets us the phrygian
		expect(NoteSet({1}).toImpliedScale()).to_equal(presetScaleNotes[PHRYGIAN_SCALE]);
		// Major second gets us the major
		expect(NoteSet({2}).toImpliedScale()).to_equal(presetScaleNotes[MAJOR_SCALE]);
		// Minor third gets us the minor
		expect(NoteSet({3}).toImpliedScale()).to_equal(presetScaleNotes[MINOR_SCALE]);
		// Major third gets us the major
		expect(NoteSet({4}).toImpliedScale()).to_equal(presetScaleNotes[MAJOR_SCALE]);
		// Perfect fourth gets us the major
		expect(NoteSet({5}).toImpliedScale()).to_equal(presetScaleNotes[MAJOR_SCALE]);
		// Tritone gets us the lydian scale
		expect(NoteSet({6}).toImpliedScale()).to_equal(presetScaleNotes[LYDIAN_SCALE]);
		// Perfect fifth gets us the major
		expect(NoteSet({7}).toImpliedScale()).to_equal(presetScaleNotes[MAJOR_SCALE]);
		// Minor sixth gets us the minor
		expect(NoteSet({8}).toImpliedScale()).to_equal(presetScaleNotes[MINOR_SCALE]);
		// Major sixth gets us the major
		expect(NoteSet({9}).toImpliedScale()).to_equal(presetScaleNotes[MAJOR_SCALE]);
		// Minor seventh gets us the mixolydian
		expect(NoteSet({10}).toImpliedScale()).to_equal(presetScaleNotes[MIXOLYDIAN_SCALE]);
		// Major seventh gets us the major
		expect(NoteSet({11}).toImpliedScale()).to_equal(presetScaleNotes[MAJOR_SCALE]);
	});

	it("highest not in", _ {
		// A is always the receiver and B the argument in these tests.
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
		expect(a.highestNotIn(b)).to_equal(-1);
		a.clear();
		b.fill();
		expect(a.highestNotIn(b)).to_equal(-1);
		a.fill();
		b.clear();
		expect(a.highestNotIn(b)).to_equal(11);
		a.fill();
		b.fill();
		expect(a.highestNotIn(b)).to_equal(-1);
		// Major scale in A, one less note in B
		const NoteSet major{0, 2, 4, 5, 7, 9, 11};
		a = major;
		for (int i = 0; i < major.count(); i++) {
			b = major;
			b.remove(major[i]);
			expect(a.highestNotIn(b)).to_equal(major[i]);
		}
		// Major scale in A, three missing notes in B
		a = major;
		b = major;
		b.remove(4);
		b.remove(7);
		b.remove(11);
		expect(a.highestNotIn(b)).to_equal(11);
	});

	it("modulate by offset", _ {
		NoteSet notes = NoteSet({0, 1, 4, 11});
		NoteSet modulated = notes.modulateByOffset(3);
		expect(modulated[0]).to_equal(2); // wrapped around 11 + 3 % 12 = 2
		expect(modulated[1]).to_equal(3); // 0 + 3 % 12 = 3
		expect(modulated[2]).to_equal(4); // 1 + 3 % 12 = 4
		expect(modulated[3]).to_equal(7); // 4 + 3 % 12 = 7
	});
});

describe musical_key_group("musical key", ${
	it("a default key is C with only the root", _ {
		MusicalKey k;
		expect(k.rootNote).to_equal(0);
		expect(k.modeNotes.count()).to_equal(1);
		expect(k.modeNotes.has(0)).to_be_true();
	});

	it("interval of", _ {
		MusicalKey key;
		for (int8_t octave = -10; octave <= 10; octave++) {
			for (uint8_t note = 0; note < 12; note++) {
				for (uint8_t root = 0; root < 12; root++) {
					key.rootNote = root;
					if (root <= note) {
						expect(key.intervalOf(note + octave * 12)).to_equal(note - root);
					}
					else {
						// Consider: root B==11, note D=2, offset=3
						expect(key.intervalOf(note + octave * 12)).to_equal(12 - root + note);
					}
				}
			}
		}
	});

	it("degree of", _ {
		MusicalKey key;
		key.rootNote = 9; // A
		key.modeNotes = presetScaleNotes[MINOR_SCALE];

		for (int octave = -2; octave <= 2; octave++) {
			// In key
			expect(key.degreeOf(9 + octave * 12)).to_equal(0);  // A
			expect(key.degreeOf(11 + octave * 12)).to_equal(1); // B
			expect(key.degreeOf(0 + octave * 12)).to_equal(2);  // C
			expect(key.degreeOf(2 + octave * 12)).to_equal(3);  // D
			expect(key.degreeOf(4 + octave * 12)).to_equal(4);  // E
			expect(key.degreeOf(5 + octave * 12)).to_equal(5);  // F
			expect(key.degreeOf(7 + octave * 12)).to_equal(6);  // G
			// Out of key
			expect(key.degreeOf(10 + octave * 12)).to_equal(-1); // A#
			expect(key.degreeOf(1 + octave * 12)).to_equal(-1);  // C#
			expect(key.degreeOf(3 + octave * 12)).to_equal(-1);  // D#
			expect(key.degreeOf(6 + octave * 12)).to_equal(-1);  // F#
			expect(key.degreeOf(8 + octave * 12)).to_equal(-1);  // G#
		}
	});
});

describe util_group("util", ${
	it("is same note", _ {
		// Exhaustive over a reasonable input range. One expect per check, over the inputs that fail it: CppSpec keeps a
		// result per expect, and this loop checks 160,801 pairs.
		std::vector<std::pair<int, int>> unlikeLegacy1;
		std::vector<std::pair<int, int>> unlikeLegacy2;
		for (int a = -200; a <= 200; a++) {
			for (int b = -200; b <= 200; b++) {
				// Two hand-written same-note checks that isSameNote() stands in for.
				int legacy1 = (int32_t)std::abs(a - b) % 12 == 0;
				int legacy2 = (uint16_t)(a - b + 120) % (uint8_t)12 == 0;
				bool same = isSameNote(a, b);
				// The first variation matches always.
				if (legacy1 != same) {
					unlikeLegacy1.emplace_back(a, b);
				}
				// The second variation returns bogus values if (a-b+120) goes negative, due to the cast to u16.
				if (a - b + 120 >= 0 && legacy2 != same) {
					unlikeLegacy2.emplace_back(a, b);
				}
			}
		}
		expect(unlikeLegacy1).to_equal(std::vector<std::pair<int, int>>{});
		expect(unlikeLegacy2).to_equal(std::vector<std::pair<int, int>>{});
		// Explicit demo of the legacy2 code misbehaving: both numbers are 2 modulo 12.
		int a = 2;
		int b = 146;
		int legacy2 = (uint16_t)(a - b + 120) % (uint8_t)12 == 0;
		expect(legacy2).to_equal(0);
		expect(isSameNote(a, b)).to_be_true();
	});
});

describe scale_mapper_group("scale mapper", ${
	it("smaller target scale exactly used", _ {
		ScaleMapper scaleMapper;
		const NoteSet diatonicMajor =   NoteSet{0, 2, 4, 5, 7, 9, 11};
		const NoteSet pentatonicMajor = NoteSet{0, 2, 4,    7, 9};
		NoteSet notes = pentatonicMajor;

		ScaleChange changes;
		bool ok = scaleMapper.computeChangeFrom(notes, diatonicMajor, pentatonicMajor, changes);
		expect(ok).to_be_true();
		NoteSet targetNotes = changes.applyTo(notes);
		// only legal transformation!
		expect(targetNotes).to_equal(pentatonicMajor);

		// Now reverse

		ok = scaleMapper.computeChangeFrom(targetNotes, pentatonicMajor, diatonicMajor, changes);
		expect(ok).to_be_true();
		NoteSet reverseNotes = changes.applyTo(targetNotes);
		expect(reverseNotes).to_equal(notes);
	});

	it("smaller target scale partially used", _ {
		ScaleMapper scaleMapper;
		const NoteSet diatonicMajor =   NoteSet{0, 2, 4, 5, 7, 9, 11};
		const NoteSet pentatonicMajor = NoteSet{0, 2, 4,    7, 9};
		const NoteSet notes =           NoteSet{0,    4,    7};

		ScaleChange changes;
		bool ok = scaleMapper.computeChangeFrom(notes, diatonicMajor, pentatonicMajor, changes);
		expect(ok).to_be_true();
		NoteSet targetNotes = changes.applyTo(notes);
		// one possible transformation
		expect(targetNotes).to_equal(notes);

		// Now reverse

		ok = scaleMapper.computeChangeFrom(targetNotes, pentatonicMajor, diatonicMajor, changes);
		expect(ok).to_be_true();
		NoteSet reverseNotes = changes.applyTo(targetNotes);
		expect(reverseNotes).to_equal(notes);
	});

	it("notes outside smaller target scale", _ {
		ScaleMapper scaleMapper;
		const NoteSet diatonicMajor =   NoteSet{0, 2, 4, 5, 7, 9, 11};
		const NoteSet pentatonicMajor = NoteSet{0, 2, 4,    7, 9};
		const NoteSet notes =           NoteSet{0,    4,    7, 9, 11};

		ScaleChange changes;
		bool ok = scaleMapper.computeChangeFrom(notes, diatonicMajor, pentatonicMajor, changes);
		expect(ok).to_be_true();
		NoteSet targetNotes = changes.applyTo(notes);
		// only legal transformation!
		expect(targetNotes).to_equal(pentatonicMajor);

		// Now reverse

		ok = scaleMapper.computeChangeFrom(targetNotes, pentatonicMajor, diatonicMajor, changes);
		expect(ok).to_be_true();
		NoteSet reverseNotes = changes.applyTo(targetNotes);
		expect(reverseNotes).to_equal(notes);
	});

	it("notes don't match target scale plus no root in use", _ {
		// Check that we're not counting root as used if it's not, and still get the right answer
		ScaleMapper scaleMapper;
		const NoteSet diatonicMajor =   NoteSet{0, 2, 4, 5, 7, 9, 11};
		const NoteSet pentatonicMajor = NoteSet{0, 2, 4,    7, 9};
		const NoteSet notes =           NoteSet{   2, 4,    7,    11};
		const NoteSet want =            NoteSet{   2, 4,    7, 9};

		ScaleChange changes;
		bool ok = scaleMapper.computeChangeFrom(notes, diatonicMajor, pentatonicMajor, changes);
		expect(ok).to_be_true();
		NoteSet targetNotes = changes.applyTo(notes);
		// the only legal transformation - assuming we don't transpose non-root notes to root.
		expect(targetNotes).to_equal(want);

		// Now reverse

		ok = scaleMapper.computeChangeFrom(targetNotes, pentatonicMajor, diatonicMajor, changes);
		expect(ok).to_be_true();
		NoteSet reverseNotes = changes.applyTo(targetNotes);
		expect(reverseNotes).to_equal(notes);
	});

	it("pentatonic minor to diatonic major", _ {
		ScaleMapper scaleMapper;
		const NoteSet pentatonicMinor = NoteSet{0, 2, 3,    7, 9};
		const NoteSet diatonicMajor =   NoteSet{0, 2, 4, 5, 7, 9, 11};
		const NoteSet notes =           NoteSet{0,    3,    7, 9};
		const NoteSet want =            NoteSet{0,    4,    7, 9};

		ScaleChange changes;
		bool ok = scaleMapper.computeChangeFrom(notes, pentatonicMinor, diatonicMajor, changes);
		expect(ok).to_be_true();
		NoteSet targetNotes = changes.applyTo(notes);
		// not the only legal transformation, but certainly the most intuitive one...
		expect(targetNotes).to_equal(want);

		// Now reverse

		ok = scaleMapper.computeChangeFrom(targetNotes, diatonicMajor, pentatonicMinor, changes);
		expect(ok).to_be_true();
		NoteSet reverseNotes = changes.applyTo(targetNotes);
		expect(reverseNotes).to_equal(notes);
	});

	it("random scale changes map back to the source notes", _ {
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

		// Having the same scale mapper for all cases is important: this tests the transition
		// scale flushing logic.
		ScaleMapper scaleMapper;
		for (auto& sourceScale : scales) {
			ScaleChange changes;
			NoteSet sourceNotes = randomNotesIn(sourceScale, engine);
			int size = sourceNotes.scaleSize();
			int start = scaleRanges[size];
			expect(start).not_().to_be_less_than(0);
			int end = scales.size();
			NoteSet testScale = sourceScale;
			NoteSet testNotes = sourceNotes;
			for (int n : randomWorklist(start, end, engine)) {
				NoteSet targetScale = scales[n];
				bool ok = scaleMapper.computeChangeFrom(testNotes, testScale, targetScale, changes);
				expect(ok).to_be_true();
				testNotes = changes.applyTo(testNotes);
				testScale = targetScale;
				expect(testNotes.isSubsetOf(testScale)).to_be_true();
				ok = scaleMapper.computeChangeFrom(testNotes, testScale, sourceScale, changes);
				expect(ok).to_be_true();
				NoteSet reverse = changes.applyTo(testNotes);
				expect(reverse).to_equal(sourceNotes);
			}
		}
	});
});

DELUGE_SPEC(music_theory_spec, note_set_group, musical_key_group, util_group, scale_mapper_group)
