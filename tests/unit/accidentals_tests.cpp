#include "CppUTest/TestHarness.h"
#include "model/scale/accidentals.h"
#include "model/scale/musical_key.h"
#include "model/scale/note_set.h"
#include "model/scale/preset_scales.h"
#include "model/scale/scale_change.h"
#include "model/scale/scale_mapper.h"

#include <algorithm>
#include <utility>
#include <vector>

using namespace deluge::accidentals;

namespace {

struct SimNote {
	int32_t pitch;
	int8_t marker;
	bool operator==(const SimNote& o) const { return pitch == o.pitch && marker == o.marker; }
	bool operator<(const SimNote& o) const { return pitch != o.pitch ? pitch < o.pitch : marker < o.marker; }
};

/// A clip's worth of notes under a song key, with one persistent ScaleMapper like Song has.
struct SimSong {
	MusicalKey key;
	ScaleMapper mapper;
	std::vector<SimNote> notes;

	SimSong(NoteSet scale, int16_t root, std::vector<int32_t> pitches) {
		key.modeNotes = scale;
		key.rootNote = root;
		for (int32_t p : pitches) {
			notes.push_back({p, 0});
		}
	}

	/// pendingTranspose mirrors InstrumentClip::regroupNotes: markers are chosen against the key as it will be after a
	/// uniform shift of that many semitones, which the caller applies afterwards.
	void regroup(const ScaleChange& changes, bool followAnchor, int32_t pendingTranspose = 0) {
		MusicalKey newKey = key;
		newKey.modeNotes = changes.target;
		MusicalKey finalKey = newKey;
		finalKey.rootNote += pendingTranspose;
		for (SimNote& n : notes) {
			NoteDestination d = destination(n.pitch, n.marker, key, changes, followAnchor);
			n.pitch = d.pitch;
			n.marker = markerFor(d.pitch + pendingTranspose, d.anchor + pendingTranspose, finalKey);
		}
		key = newKey;
	}

	/// Mirrors Song::transposeAllScaleModeClips(offset, false): rotate the mode, then shift every note and the root
	void transposeInKey(int32_t offset) {
		int32_t noteCount = key.modeNotes.count();
		int32_t octaves, rootIndex;
		if (offset < 0) {
			octaves = ((offset + 1) / noteCount) - 1;
			rootIndex = ((offset + 1) % noteCount) + noteCount - 1;
		}
		else {
			octaves = offset / noteCount;
			rootIndex = offset % noteCount;
		}
		int32_t semitones = 12 * octaves + key.modeNotes[rootIndex];

		// Mirrors Song::rotateMusicalMode
		ScaleChange changes;
		changes.source = key.modeNotes;
		int32_t steps = (offset % noteCount + noteCount) % noteCount;
		int32_t newRoot = key.modeNotes[steps];
		for (int32_t i = 0; i < noteCount; i++) {
			changes[i] = (key.modeNotes[(i + steps) % noteCount] - newRoot) - key.modeNotes[i];
			if (i >= noteCount - steps) {
				changes[i] += 12;
			}
		}
		changes.target = changes.applyTo(changes.source);
		regroup(changes, true, semitones);
		key.modeNotes = changes.target;

		for (SimNote& n : notes) {
			n.pitch += semitones;
		}
		key.rootNote += semitones;
	}

	/// Mirrors Song::setScaleNotes
	bool setScaleNotes(NoteSet newScale) {
		newScale.add(0);
		NoteSet inUse;
		for (const SimNote& n : notes) {
			int32_t a = lineageAnchor(n.pitch, n.marker, key);
			if (key.isInKey(a)) {
				inUse.add(key.intervalOf(a));
			}
		}
		inUse.add(0);
		if (inUse.scaleSize() > newScale.scaleSize()) {
			return false;
		}
		ScaleChange changes;
		if (!mapper.computeChangeFrom(inUse, key.modeNotes, newScale, changes)) {
			return false;
		}
		regroup(changes, true);
		key.modeNotes = newScale;
		return true;
	}

	/// Mirrors Song::changeMusicalMode (sharpen or flatten a degree, accidentals keep their pitch)
	void changeMusicalMode(int degree, int change) {
		ScaleChange changes;
		changes.source = key.modeNotes;
		changes[degree] += change;
		changes.target = changes.applyTo(changes.source);
		regroup(changes, false);
	}

	std::vector<SimNote> sorted() const {
		std::vector<SimNote> s = notes;
		std::sort(s.begin(), s.end());
		return s;
	}

	/// Every note's lineage anchor must be in key, or the next scale change would lose it
	bool anchorsInKey() const {
		for (const SimNote& n : notes) {
			if (!key.isInKey(lineageAnchor(n.pitch, n.marker, key))) {
				return false;
			}
		}
		return true;
	}
};

const NoteSet kMajor{0, 2, 4, 5, 7, 9, 11};
const NoteSet kMinor{0, 2, 3, 5, 7, 8, 10};

void checkSame(const std::vector<SimNote>& expected, const std::vector<SimNote>& actual) {
	CHECK_EQUAL(expected.size(), actual.size());
	for (size_t i = 0; i < expected.size(); i++) {
		CHECK_EQUAL(expected[i].pitch, actual[i].pitch);
		CHECK_EQUAL(expected[i].marker, actual[i].marker);
	}
}

} // namespace

TEST_GROUP(AccidentalsTest){};

TEST(AccidentalsTest, lineageAnchor) {
	MusicalKey c;
	c.modeNotes = kMajor;
	CHECK_EQUAL(60, lineageAnchor(60, 0, c));
	CHECK_EQUAL(60, lineageAnchor(61, 0, c)); // C major prefers sharps
	CHECK_EQUAL(62, lineageAnchor(63, -1, c));
	CHECK_EQUAL(64, lineageAnchor(63, 1, c));
	MusicalKey f = c;
	f.rootNote = 5;
	f.modeNotes = kMajor;
	CHECK_EQUAL(70, lineageAnchor(70, 0, f)); // Bb is in F major
	CHECK_EQUAL(72, lineageAnchor(71, 0, f)); // B isn't: flat key, anchor above
	CHECK_EQUAL(62, lineageAnchor(61, 0, f)); // flat key: anchor above
}

TEST(AccidentalsTest, markerFor) {
	MusicalKey c;
	c.modeNotes = kMajor;
	CHECK_EQUAL(0, markerFor(61, 60, c)); // derivable
	CHECK_EQUAL(0, markerFor(60, 60, c));
	MusicalKey minor;
	minor.modeNotes = kMinor;
	CHECK_EQUAL(-1, markerFor(63, 62, minor)); // Eb in C minor, hanging off D
	CHECK_EQUAL(-1, markerFor(69, 68, minor)); // A natural in C minor (flats: derived anchor Bb), hanging off Ab
	CHECK_EQUAL(0, markerFor(0, 1000, c));     // doesn't fit an int8
}

TEST(AccidentalsTest, majorMinorRoundTrip) {
	SimSong song(kMajor, 0, {60, 62, 63, 64, 70});
	auto original = song.sorted();

	CHECK(song.setScaleNotes(kMinor));
	CHECK(song.anchorsInKey());
	// D# hangs off D, which stays D in minor, so it is now the in-scale Eb, marked
	std::vector<SimNote> expected = {{60, 0}, {62, 0}, {63, -1}, {63, 0}, {69, -1}};
	// 64 (E) -> 63 (Eb); 70 (A#, anchor A) -> A -> Ab (68) + 1 = 69
	checkSame(expected, song.sorted());

	CHECK(song.setScaleNotes(kMajor));
	checkSame(original, song.sorted());
}

TEST(AccidentalsTest, cycleAllPresetScalesAndBack) {
	const std::vector<std::vector<int32_t>> sets = {
	    {60, 62, 63, 64, 70},
	    {60, 61, 62, 64, 66, 67},
	    {60, 62, 64, 65, 67, 69, 71, 72, 73, 66},
	};
	int numApplied = 0;
	for (int16_t root : {0, 5, 7, 66}) {
		for (const auto& pitches : sets) {
			SimSong song(kMajor, root, {});
			for (int32_t p : pitches) {
				song.notes.push_back({p + root, 0});
			}
			auto original = song.sorted();
			for (int s = 0; s < NUM_PRESET_SCALES; s++) {
				// Refused ones are skipped, like cycleThroughScales
				if (song.setScaleNotes(presetScaleNotes[s])) {
					numApplied++;
				}
				CHECK(song.anchorsInKey());
			}
			CHECK(song.setScaleNotes(kMajor));
			checkSame(original, song.sorted());
		}
	}
	// Must not pass vacuously because every scale was refused
	CHECK(numApplied > 12);
}

TEST(AccidentalsTest, inKeyTransposeChoosesMarkersAgainstFinalKey) {
	// F lydian. The intermediate key (lydian rotated at the old root) prefers the other accidental direction from the
	// final key, which used to flip markers and collapse B onto C.
	const NoteSet lydian{0, 2, 4, 6, 7, 9, 11};
	SimSong song(lydian, 5, {72});
	song.notes.push_back({71, 1});
	auto original = song.sorted();

	song.transposeInKey(1);
	CHECK(song.anchorsInKey());
	song.transposeInKey(-1);
	CHECK(song.anchorsInKey());
	checkSame(original, song.sorted());
	CHECK_EQUAL(5, song.key.rootNote);
	CHECK(song.key.modeNotes == lydian);

	// And the other direction, and a longer walk
	song.transposeInKey(-1);
	song.transposeInKey(1);
	checkSame(original, song.sorted());
	for (int32_t step : {1, 3, -2, 5, -7, 2}) {
		song.transposeInKey(step);
		CHECK(song.anchorsInKey());
	}
	for (int32_t step : {-2, 7, -5, 2, -3, -1}) {
		song.transposeInKey(step);
	}
	checkSame(original, song.sorted());
}

TEST(AccidentalsTest, cycleOneScaleAtATimeAndBack) {
	// Each preset scale directly from major and back, so every pair is checked
	for (int s = 0; s < NUM_PRESET_SCALES; s++) {
		SimSong song(kMajor, 0, {60, 62, 63, 64, 70});
		auto original = song.sorted();
		if (!song.setScaleNotes(presetScaleNotes[s])) {
			continue;
		}
		CHECK(song.anchorsInKey());
		CHECK(song.setScaleNotes(kMajor));
		checkSame(original, song.sorted());
	}
}

TEST(AccidentalsTest, pentatonicRoundTrips) {
	const NoteSet pentMajor{0, 2, 4, 7, 9};
	const NoteSet pentMinor{0, 3, 5, 7, 10};
	for (const NoteSet& target : {pentMajor, pentMinor}) {
		// Accidentals in the gaps of the pentatonic scale, hanging off notes it contains
		SimSong song(kMajor, 0, {60, 61, 62, 63, 64, 67, 68, 69, 70});
		auto original = song.sorted();
		CHECK(song.setScaleNotes(target));
		CHECK(song.anchorsInKey());
		CHECK(song.setScaleNotes(kMajor));
		checkSame(original, song.sorted());
	}
}

TEST(AccidentalsTest, pentatonicStartingScale) {
	const NoteSet pentMinor{0, 3, 5, 7, 10};
	SimSong song(pentMinor, 0, {60, 61, 62, 63, 65, 66, 67, 70, 71});
	auto original = song.sorted();
	CHECK(song.setScaleNotes(kMajor));
	CHECK(song.anchorsInKey());
	CHECK(song.setScaleNotes(pentMinor));
	checkSame(original, song.sorted());
}

TEST(AccidentalsTest, keepPitchSharpenThenFlatten) {
	SimSong song(kMajor, 0, {60, 62, 64, 65, 66, 67});
	auto original = song.sorted();
	int fDegree = 3;
	song.changeMusicalMode(fDegree, 1);
	// F row went to F# (66), the F# accidental keeps pitch but remembers it hangs off F
	std::vector<SimNote> expected = {{60, 0}, {62, 0}, {64, 0}, {66, -1}, {66, 0}, {67, 0}};
	checkSame(expected, song.sorted());
	// Note the marked F# deliberately keeps F as its (now stale) anchor, so it can split back out
	song.changeMusicalMode(fDegree, -1);
	checkSame(original, song.sorted());
}

TEST(AccidentalsTest, keepPitchOtherAccidentalsUntouched) {
	SimSong song(kMajor, 0, {60, 61, 63, 65, 68, 70});
	auto original = song.sorted();
	song.changeMusicalMode(4, 1); // sharpen G: moves only G rows (none here)
	// The G# accidental keeps its pitch, which is now in key, and remembers it hangs off G
	checkSame({{60, 0}, {61, 0}, {63, 0}, {65, 0}, {68, -1}, {70, 0}}, song.sorted());
	song.changeMusicalMode(4, -1);
	checkSame(original, song.sorted());
}

TEST(AccidentalsTest, flatKeyAndFlipRoundTrip) {
	// F major (flats), accidentals above their anchors
	SimSong song(kMajor, 5, {65, 66, 67, 69, 70, 71, 73});
	auto original = song.sorted();
	CHECK(song.setScaleNotes(kMinor)); // F minor, still flats
	CHECK(song.anchorsInKey());
	CHECK(song.setScaleNotes(kMajor));
	checkSame(original, song.sorted());

	// C major (sharps) <-> C minor (flats)
	SimSong c(kMajor, 0, {60, 61, 62, 63, 65, 66, 68, 70});
	auto cOriginal = c.sorted();
	CHECK(c.setScaleNotes(kMinor));
	CHECK(c.anchorsInKey());
	CHECK(c.setScaleNotes(kMajor));
	checkSame(cOriginal, c.sorted());
}

TEST(AccidentalsTest, followAnchorMovesAccidentalWithItsAnchor) {
	// Plan example: C major, A# (anchor A), to C minor: A moves to Ab, so the accidental is A natural
	SimSong song(kMajor, 0, {69, 70});
	CHECK(song.setScaleNotes(kMinor));
	checkSame({{68, 0}, {69, -1}}, song.sorted());
}
