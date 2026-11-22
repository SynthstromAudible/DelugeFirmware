#include "model/scale/scale_degrees.h"

namespace deluge::scale_degrees {

namespace {
constexpr int8_t kMajorNotes[7] = {0, 2, 4, 5, 7, 9, 11};
constexpr int kMaxAlteration = 2;
constexpr int kInfeasible = 1 << 20;

struct Search {
	int count;
	int8_t notes[7];
	int8_t chosen[7];
	int8_t best[7];
	int bestCost = kInfeasible;

	void run(int index, int firstLetter, int cost) {
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
		// Leave enough letters above for the notes still to place
		for (int letter = firstLetter; letter <= 7 - (count - index); letter++) {
			int alteration = notes[index] - kMajorNotes[letter];
			if (alteration > kMaxAlteration || alteration < -kMaxAlteration) {
				continue;
			}
			// Sharps cost slightly more than flats, so b3 beats #2 and b7 beats #6
			int step = (alteration < 0 ? -alteration * 10 : alteration * 10 + 1);
			chosen[index] = letter;
			run(index + 1, letter + 1, cost + step);
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
	if (s.count == 1) {
		letters[0] = 0;
		return true;
	}
	s.run(1, 1, 0);
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
	for (int8_t& n : change.targetNote) {
		n = -1;
	}
	for (int interval = 0; interval < 12; interval++) {
		if (targetLetters[interval] >= 0) {
			change.targetNote[targetLetters[interval]] = interval;
		}
	}
	return true;
}

} // namespace deluge::scale_degrees
