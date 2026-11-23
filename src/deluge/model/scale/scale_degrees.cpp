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
	int count = 0;
	std::array<int8_t, 7> notes{};
	std::array<int8_t, 7> chosen{};
	std::array<int8_t, 7> best{};
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

std::optional<DegreeLetters> degreeLetters(NoteSet scale) {
	scale.add(0);
	if (scale.count() > 7) {
		return std::nullopt;
	}
	DegreeLetters letters;
	letters.fill(-1);
	Search search;
	search.count = scale.count();
	for (int i = 0; i < search.count; i++) {
		search.notes[i] = scale[i];
	}
	search.chosen[0] = 0; // The root is always the 1st
	if (search.count == 1 || search.count == 7) {
		for (int i = 0; i < search.count; i++) {
			letters[search.notes[i]] = i; // 7 notes: one per letter
		}
		return letters;
	}
	search.run(1, 0);
	if (search.bestCost == kInfeasible) {
		return std::nullopt;
	}
	letters[0] = 0;
	for (int i = 1; i < search.count; i++) {
		letters[search.notes[i]] = search.best[i];
	}
	return letters;
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

LetterNotes letterNotes(const DegreeLetters& letters) {
	LetterNotes notes;
	notes.primary.fill(-1);
	notes.secondary.fill(-1);
	for (int interval = 0; interval < 12; interval++) {
		int letter = letters[interval];
		if (letter < 0) {
			continue;
		}
		if (notes.primary[letter] < 0) {
			notes.primary[letter] = interval;
			continue;
		}
		// Two notes: the primary is the one nearer the major scale's, the lower one if equally near
		int other = notes.primary[letter];
		int alterationNew = std::abs(interval - kMajorNotes[letter]);
		int alterationOther = std::abs(other - kMajorNotes[letter]);
		if (alterationNew < alterationOther) {
			notes.primary[letter] = interval;
			notes.secondary[letter] = other;
		}
		else {
			notes.secondary[letter] = interval;
		}
	}
	return notes;
}

int majorNote(int letter) {
	return kMajorNotes[letter];
}

std::optional<ScaleChange> letterChange(const MusicalKey& key, NoteSet target) {
	target.add(0);
	std::optional<DegreeLetters> sourceLetters = degreeLetters(key.modeNotes);
	std::optional<DegreeLetters> targetLetters = degreeLetters(target);
	if (!sourceLetters || !targetLetters) {
		return std::nullopt;
	}
	LetterNotes targetNotes = letterNotes(*targetLetters);

	ScaleChange change;
	change.source = key.modeNotes;
	change.target = target;
	change.letterMapping = LetterMapping{
	    .preferFlats = key.prefersFlats(),
	    .sourceLetter = *sourceLetters,
	    .sourcePrimary = letterNotes(*sourceLetters).primary,
	    .targetPrimary = targetNotes.primary,
	    .targetSecond = targetNotes.secondary,
	};
	return change;
}

} // namespace deluge::scale_degrees
