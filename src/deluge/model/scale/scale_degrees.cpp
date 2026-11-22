#include "model/scale/scale_degrees.h"

#include <cstdlib>

namespace deluge::scale_degrees {

namespace {
constexpr int8_t kMajorNotes[7] = {0, 2, 4, 5, 7, 9, 11};
constexpr int kMaxAlteration = 2;
constexpr int kInfeasible = 1 << 20;
// Added for each note which shares its letter with the one below it, so that spellings with one letter per note win
// whenever they are about as close to the major scale (blues: 1 b3 4 b5 5 b7 costs 30 + 8, against 43 for the
// injective 1 #2 #3 #4 5 b7).
constexpr int kSharingPenalty = 8;

struct Search {
	int count;
	int8_t notes[7];
	int8_t chosen[7];
	int8_t best[7];
	int bestCost = kInfeasible;

	void run(int index, int cost) {
		if (cost >= bestCost) {
			return; // Strictly better only: of equal ones, the first (lowest letters) wins
		}
		if (index == count) {
			bestCost = cost;
			for (int i = 0; i < count; i++) {
				best[i] = chosen[i];
			}
			return;
		}
		// Letters never decrease, the root's is its alone, and at most 2 notes share one
		int firstLetter = chosen[index - 1] == 0 ? 1 : chosen[index - 1];
		for (int letter = firstLetter; letter <= 6; letter++) {
			bool shares = letter == chosen[index - 1];
			if (shares && index >= 2 && chosen[index - 2] == letter) {
				continue;
			}
			int alteration = notes[index] - kMajorNotes[letter];
			if (alteration > kMaxAlteration || alteration < -kMaxAlteration) {
				continue;
			}
			// Sharps cost slightly more than flats, so b3 beats #2 and b7 beats #6
			int step = (alteration < 0 ? -alteration * 10 : alteration * 10 + 1) + (shares ? kSharingPenalty : 0);
			chosen[index] = letter;
			run(index + 1, cost + step);
		}
	}
};
} // namespace

bool degreeLetters(NoteSet scale, std::array<int8_t, 12>& letters) {
	letters.fill(-1);
	scale.add(0);
	if (scale.count() > 7) {
		return false;
	}
	Search s;
	s.count = scale.count();
	for (int i = 0; i < s.count; i++) {
		s.notes[i] = scale[i];
	}
	s.chosen[0] = 0; // The root is always the 1st
	if (s.count == 1 || s.count == 7) {
		for (int i = 0; i < s.count; i++) {
			letters[s.notes[i]] = i; // 7 notes: one per letter
		}
		return true;
	}
	s.run(1, 0);
	if (s.bestCost == kInfeasible) {
		return false;
	}
	letters[0] = 0;
	for (int i = 1; i < s.count; i++) {
		letters[s.notes[i]] = s.best[i];
	}
	return true;
}

int spellLetter(int interval, bool flats) {
	int letter = 0;
	if (flats) {
		while (kMajorNotes[letter] < interval) {
			letter++;
		}
	}
	else {
		while (letter < 6 && kMajorNotes[letter + 1] <= interval) {
			letter++;
		}
	}
	return letter;
}

void letterNotes(const std::array<int8_t, 12>& letters, int8_t primary[7], int8_t secondary[7]) {
	for (int letter = 0; letter < 7; letter++) {
		primary[letter] = -1;
		secondary[letter] = -1;
	}
	for (int interval = 0; interval < 12; interval++) {
		int letter = letters[interval];
		if (letter < 0) {
			continue;
		}
		if (primary[letter] < 0) {
			primary[letter] = interval;
			continue;
		}
		// Two notes: the primary is the one nearer the major scale's, the lower one if equally near
		int other = primary[letter];
		int altNew = std::abs(interval - kMajorNotes[letter]);
		int altOther = std::abs(other - kMajorNotes[letter]);
		if (altNew < altOther) {
			primary[letter] = interval;
			secondary[letter] = other;
		}
		else {
			secondary[letter] = interval;
		}
	}
}

int majorNote(int letter) {
	return kMajorNotes[letter];
}

bool letterChange(const MusicalKey& key, NoteSet target, ScaleChange& change) {
	std::array<int8_t, 12> sourceLetters;
	std::array<int8_t, 12> targetLetters;
	target.add(0);
	if (!degreeLetters(key.modeNotes, sourceLetters) || !degreeLetters(target, targetLetters)) {
		return false;
	}
	change.source = key.modeNotes;
	change.target = target;
	change.byLetter = true;
	change.preferFlats = key.prefersFlats();
	for (int interval = 0; interval < 12; interval++) {
		change.sourceLetter[interval] = sourceLetters[interval];
	}
	int8_t unused[7];
	letterNotes(sourceLetters, change.sourcePrimary, unused);
	letterNotes(targetLetters, change.targetNote, change.targetSecond);
	return true;
}

} // namespace deluge::scale_degrees
