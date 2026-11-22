#include "CppUTest/TestHarness.h"
#include "model/scale/accidentals.h"
#include "model/scale/musical_key.h"
#include "model/scale/note_set.h"
#include "model/scale/preset_scales.h"
#include "model/scale/scale_change.h"
#include "model/scale/scale_degrees.h"
#include "model/scale/scale_mapper.h"

#include <algorithm>
#include <utility>
#include <vector>

using namespace deluge::accidentals;

namespace {

struct SimNote {
	int32_t pitch;
	AnchorMarker marker;
	/// A note whose anchor is anchorOffset semitones away (0: no marker)
	SimNote(int32_t pitch_, int32_t anchorOffset = 0) : pitch(pitch_), marker(AnchorMarker::offset(anchorOffset)) {}
	SimNote(int32_t pitch_, AnchorMarker marker_) : pitch(pitch_), marker(marker_) {}
	bool operator==(const SimNote& o) const { return pitch == o.pitch && marker == o.marker; }
	bool operator<(const SimNote& o) const {
		return pitch != o.pitch ? pitch < o.pitch : marker.raw() < o.marker.raw();
	}
};

/// A clip's worth of notes under a song key, with one persistent ScaleMapper like Song has.
struct SimSong {
	MusicalKey key;
	ScaleMapper mapper;
	std::vector<SimNote> notes;
	int numPitchPreserving = 0; // How many scale changes left every note on its pitch

	SimSong(NoteSet scale, int16_t root, std::vector<int32_t> pitches) {
		key.modeNotes = scale;
		key.rootNote = root;
		for (int32_t p : pitches) {
			notes.push_back({p, 0});
		}
	}

	/// pendingTranspose mirrors NoteRegrouper::regroup: markers are chosen against the key as it will be after a
	/// uniform shift of that many semitones, which the caller applies afterwards.
	void regroup(const ScaleChange& changes, AccidentalHandling handling, int32_t pendingTranspose = 0) {
		MusicalKey newKey = key;
		newKey.modeNotes = changes.target;
		MusicalKey finalKey = newKey;
		finalKey.rootNote += pendingTranspose;
		const SpelledKey oldSpelling{key};
		const SpelledKey finalSpelling{finalKey};
		for (SimNote& n : notes) {
			NoteDestination d = destination(n.pitch, n.marker, oldSpelling, changes, handling);
			n.pitch = d.pitch;
			n.marker = d.markerUnder(finalSpelling, pendingTranspose);
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
		regroup(changes, AccidentalHandling::FOLLOW_ANCHOR, semitones);
		key.modeNotes = changes.target;

		for (SimNote& n : notes) {
			n.pitch += semitones;
		}
		key.rootNote += semitones;
	}

	/// Mirrors Song::setScaleNotes
	bool setScaleNotes(NoteSet newScale) {
		newScale.add(0);
		ScaleChange changes;
		if (auto byLetter = deluge::scale_degrees::letterChange(key, newScale)) {
			regroup(*byLetter, AccidentalHandling::FOLLOW_ANCHOR);
			key.modeNotes = newScale;
			return true;
		}
		// More than 7 notes in either scale: the mapper
		NoteSet inUse;
		for (const SimNote& n : notes) {
			int32_t a = lineageAnchor(n.pitch, n.marker, SpelledKey{key});
			if (key.isInKey(a)) {
				inUse.add(key.intervalOf(a));
			}
		}
		inUse.add(0);
		// More pitch classes present than the scale has degrees: the notes can't be where a degree mapping left them,
		// which is what a pitch-preserving change leaves. If they are all in the new scale, nothing needs to move.
		NoteSet allPitches;
		for (const SimNote& n : notes) {
			allPitches.add(key.intervalOf(n.pitch));
		}
		bool allFit = allPitches.isSubsetOf(newScale) && allPitches.count() > key.modeNotes.count();
		if (allFit || inUse.scaleSize() > newScale.scaleSize()) {
			// Pitch-preserving
			numPitchPreserving++;
			changes.source = key.modeNotes;
			changes.target = newScale;
		}
		else if (!mapper.computeChangeFrom(inUse, key.modeNotes, newScale, changes)) {
			return false;
		}
		regroup(changes, AccidentalHandling::FOLLOW_ANCHOR);
		key.modeNotes = newScale;
		return true;
	}

	/// Mirrors Song::changeMusicalMode (sharpen or flatten a degree, accidentals keep their pitch)
	void changeMusicalMode(int degree, int change) {
		ScaleChange changes;
		changes.source = key.modeNotes;
		changes[degree] += change;
		changes.target = changes.applyTo(changes.source);
		regroup(changes, AccidentalHandling::KEEP_PITCH);
	}

	std::vector<SimNote> sorted() const {
		std::vector<SimNote> s = notes;
		std::sort(s.begin(), s.end());
		return s;
	}

	/// Every note's lineage anchor must be in key, or the next scale change would lose it
	bool anchorsInKey() const {
		for (const SimNote& n : notes) {
			if (!key.isInKey(lineageAnchor(n.pitch, n.marker, SpelledKey{key}))) {
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
		CHECK_EQUAL(expected[i].marker.raw(), actual[i].marker.raw());
	}
}

/// A marker whose anchor isn't in the key does nothing (liveAnchor ignores it), so a pitch-preserving change which
/// leaves one on a note that was in key gives the same notes as without it. Compares notes with such markers dropped.
std::vector<SimNote> effective(const SimSong& song) {
	std::vector<SimNote> s = song.sorted();
	for (SimNote& n : s) {
		if (!n.marker.isDerived() && !song.key.isInKey(lineageAnchor(n.pitch, n.marker, SpelledKey{song.key}))) {
			n.marker = AnchorMarker::derived();
		}
	}
	std::sort(s.begin(), s.end());
	return s;
}

} // namespace

TEST_GROUP(AccidentalsTest){};

TEST(AccidentalsTest, lineageAnchor) {
	MusicalKey cMajor;
	cMajor.modeNotes = kMajor;
	const SpelledKey c{cMajor};
	CHECK_EQUAL(60, lineageAnchor(60, AnchorMarker::derived(), c));
	CHECK_EQUAL(60, lineageAnchor(61, AnchorMarker::derived(), c)); // C major prefers sharps
	CHECK_EQUAL(62, lineageAnchor(63, AnchorMarker::offset(-1), c));
	CHECK_EQUAL(64, lineageAnchor(63, AnchorMarker::offset(1), c));
	MusicalKey fMajor = cMajor;
	fMajor.rootNote = 5;
	const SpelledKey f{fMajor};
	CHECK_EQUAL(70, lineageAnchor(70, AnchorMarker::derived(), f)); // Bb is in F major
	CHECK_EQUAL(72, lineageAnchor(71, AnchorMarker::derived(), f)); // B isn't: flat key, anchor above
	CHECK_EQUAL(62, lineageAnchor(61, AnchorMarker::derived(), f)); // flat key: anchor above
}

TEST(AccidentalsTest, markerFor) {
	MusicalKey cMajor;
	cMajor.modeNotes = kMajor;
	const SpelledKey c{cMajor};
	CHECK(markerFor(61, 60, c).isDerived()); // derivable
	CHECK(markerFor(60, 60, c).isDerived());
	MusicalKey cMinor;
	cMinor.modeNotes = kMinor;
	const SpelledKey minor{cMinor};
	CHECK(AnchorMarker::offset(-1) == markerFor(63, 62, minor)); // Eb in C minor, hanging off D
	// A natural in C minor (flats: derived anchor Bb), hanging off Ab
	CHECK(AnchorMarker::offset(-1) == markerFor(69, 68, minor));
	CHECK(markerFor(0, 1000, c).isDerived()); // too far to store
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
	int numRoundTripped = 0;
	for (int16_t root : {0, 5, 7, 66}) {
		for (const auto& pitches : sets) {
			SimSong song(kMajor, root, {});
			for (int32_t p : pitches) {
				song.notes.push_back({p + root, 0});
			}
			auto original = song.sorted();
			for (int s = 0; s < NUM_PRESET_SCALES; s++) {
				// None are refused: scales too small for the notes keep every pitch instead
				CHECK(song.setScaleNotes(presetScaleNotes[s]));
				numApplied++;
				CHECK(song.key.modeNotes == presetScaleNotes[s]);
				CHECK_EQUAL(original.size(), song.notes.size());
			}
			CHECK(song.setScaleNotes(kMajor));
			// All presets have letters, so every step maps by degree and the whole cycle must round-trip exactly
			CHECK_EQUAL(0, song.numPitchPreserving);
			checkSame(original, song.sorted());
			numRoundTripped++;
			CHECK_EQUAL(original.size(), song.notes.size());
		}
	}
	CHECK_EQUAL(4 * 3 * NUM_PRESET_SCALES, numApplied);
	CHECK_EQUAL(numApplied / NUM_PRESET_SCALES + 0, numRoundTripped); // Must not pass vacuously
}

TEST(AccidentalsTest, inKeyTransposeChoosesMarkersAgainstFinalKey) {
	// F lydian. The intermediate key (lydian rotated at the old root) prefers the other accidental direction from the
	// final key, which used to flip markers and collapse B onto C.
	const NoteSet lydian{0, 2, 4, 6, 7, 9, 11};
	SimSong song(lydian, 5, {72});
	song.notes.push_back(SimNote{71, 1});
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
		CHECK(song.setScaleNotes(presetScaleNotes[s]));
		CHECK(song.setScaleNotes(kMajor));
		checkSame(original, effective(song));
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
	CHECK(c.setScaleNotes(kMajor));
	checkSame(cOriginal, c.sorted());
}

TEST(AccidentalsTest, followAnchorMovesAccidentalWithItsAnchor) {
	// Plan example: C major, A# (anchor A), to C minor: A moves to Ab, so the accidental is A natural
	SimSong song(kMajor, 0, {69, 70});
	CHECK(song.setScaleNotes(kMinor));
	checkSame({{68, 0}, {69, -1}}, song.sorted());
}

namespace {
std::vector<int32_t> pitchesOf(const SimSong& song) {
	std::vector<int32_t> p;
	for (const SimNote& n : song.sorted()) {
		p.push_back(n.pitch);
	}
	return p;
}

const NoteSet kPentMinor{0, 3, 5, 7, 10};
const std::vector<int32_t> kAllDegrees = {60, 62, 64, 65, 67, 69, 71};
} // namespace

TEST(AccidentalsTest, tooSmallScaleMapsByDegreeAndRestores) {
	// All 7 degrees of C major to C pentatonic minor, which has no 2nd or 6th: E and B become Eb and Bb, F and G stay,
	// D and A keep their pitch as accidentals
	SimSong song(kMajor, 0, kAllDegrees);
	auto original = song.sorted();

	CHECK(song.setScaleNotes(kPentMinor));
	CHECK_EQUAL(0, song.numPitchPreserving);
	CHECK(song.key.modeNotes == kPentMinor);
	CHECK((std::vector<int32_t>{60, 62, 63, 65, 67, 69, 70}) == pitchesOf(song));

	CHECK(song.setScaleNotes(kMajor));
	CHECK(song.key.modeNotes == kMajor);
	checkSame(original, song.sorted());
}

TEST(AccidentalsTest, tooSmallScaleWithAccidentalsMapsByDegreeAndRestores) {
	// The same plus accidentals C# and A#
	SimSong song(kMajor, 0, kAllDegrees);
	song.notes.push_back({61, 0});
	song.notes.push_back({70, 0});
	auto original = song.sorted();

	CHECK(song.setScaleNotes(kPentMinor));
	// E->Eb, B->Bb, D and A stay (self-anchored, as pentatonic minor has no 2nd or 6th), C# stays hanging off C
	// (marked -1: pentatonic minor is a flat key, which would hang it off Eb), A# stays hanging off A, which keeps
	// its pitch and remembers it is the 6th
	std::vector<SimNote> expected = {
	    {60, 0}, {61, -1}, {62, AnchorMarker::tagged(1, 0)}, {63, 0},
	    {65, 0}, {67, 0},  {69, AnchorMarker::tagged(5, 0)}, {70, AnchorMarker::tagged(5, -1)},
	    {70, 0}};
	checkSame(expected, song.sorted());

	CHECK(song.setScaleNotes(kMajor));
	checkSame(original, song.sorted());
}

TEST(AccidentalsTest, pentatonicMinorToMajorMapsByDegree) {
	SimSong song(kPentMinor, 0, {60, 63, 65, 67, 70});
	CHECK(song.setScaleNotes(kMajor));
	// 1 b3 4 5 b7 -> 1 3 4 5 7: Eb->E, F->F, G->G, Bb->B
	CHECK((std::vector<int32_t>{60, 64, 65, 67, 71}) == pitchesOf(song));
	CHECK_EQUAL(0, song.numPitchPreserving);
}

TEST(AccidentalsTest, majorHirajoshiBluesAndBack) {
	for (int16_t root : {0, 5, 66}) {
		SimSong song(kMajor, root, {});
		for (int32_t p : kAllDegrees) {
			song.notes.push_back({p + root, 0});
		}
		song.notes.push_back({61 + root, 0});
		song.notes.push_back({70 + root, 0});
		auto original = song.sorted();
		CHECK(song.setScaleNotes(presetScaleNotes[HIRAJOSHI_SCALE]));
		CHECK(song.setScaleNotes(presetScaleNotes[BLUES_SCALE]));
		CHECK(song.setScaleNotes(kMajor));
		checkSame(original, song.sorted());
		CHECK_EQUAL(0, song.numPitchPreserving);
	}
}

TEST(AccidentalsTest, viaSmallScalesRestoresNotesAndMajorPlain) {
	// Through several small scales and back to major, from a state with every degree
	for (int16_t root : {0, 5, 66}) {
		SimSong song(kMajor, root, {});
		for (int32_t p : kAllDegrees) {
			song.notes.push_back({p + root, 0});
		}
		auto original = song.sorted();
		for (const NoteSet& scale : {kPentMinor, presetScaleNotes[HIRAJOSHI_SCALE], presetScaleNotes[BLUES_SCALE],
		                             presetScaleNotes[WHOLE_TONE_SCALE], kPentMinor, kMajor}) {
			CHECK(song.setScaleNotes(scale));
			CHECK_EQUAL(kAllDegrees.size(), song.notes.size());
		}
		checkSame(original, song.sorted());
	}
}

TEST(AccidentalsTest, tooSmallScaleViaSmallScalesRestoresPitches) {
	SimSong song(kMajor, 0, kAllDegrees);
	auto original = song.sorted();
	for (const NoteSet& scale : {kPentMinor, presetScaleNotes[HIRAJOSHI_SCALE], presetScaleNotes[BLUES_SCALE],
	                             presetScaleNotes[WHOLE_TONE_SCALE], kPentMinor, kMajor}) {
		CHECK(song.setScaleNotes(scale));
	}
	checkSame(original, song.sorted());
}

TEST(AccidentalsTest, selfAndTaggedMarkers) {
	MusicalKey pentMinorKey;
	pentMinorKey.modeNotes = kPentMinor; // Flats: D (62) would derive Eb (63) as its anchor
	const SpelledKey minorPent{pentMinorKey};
	CHECK(markerFor(62, 62, minorPent).isSelf());
	CHECK(markerFor(63, 63, minorPent).isDerived()); // in key: derivable
	CHECK_EQUAL(62, lineageAnchor(62, AnchorMarker::self(), minorPent));
	CHECK_EQUAL(62, lineageAnchor(62, AnchorMarker::tagged(1, 0), minorPent));
	CHECK_EQUAL(1, AnchorMarker::tagged(1, 0).letter());
	CHECK(!AnchorMarker::self().isTagged());
	CHECK(!AnchorMarker::offset(-1).isTagged());
	CHECK(!AnchorMarker::derived().isTagged());
	CHECK(!AnchorMarker::offset(127).isTagged());
	// Anchor one above the note, remembering letter 5
	AnchorMarker m = AnchorMarker::tagged(5, 1);
	CHECK(m.isTagged());
	CHECK(!m.isSelf());
	CHECK_EQUAL(5, m.letter());
	CHECK_EQUAL(1, m.semitonesToAnchor());
	CHECK_EQUAL(70, lineageAnchor(69, m, minorPent));
	CHECK(AnchorMarker::tagged(0, 5).isDerived()); // doesn't fit
	for (int letter = 0; letter < 7; letter++) {
		for (int o = -AnchorMarker::kMaxTaggedOffset; o <= AnchorMarker::kMaxTaggedOffset; o++) {
			AnchorMarker tagged = AnchorMarker::tagged(letter, o);
			CHECK(tagged.isTagged());
			CHECK_EQUAL(letter, tagged.letter());
			CHECK_EQUAL(o, tagged.semitonesToAnchor());
			// What song files store must survive a round trip
			CHECK(tagged == AnchorMarker::fromRaw(tagged.raw()));
		}
	}
}

TEST(AccidentalsTest, markerBytesAreStable) {
	// Song files store raw(), so the encoding must not change
	CHECK_EQUAL(0, AnchorMarker::derived().raw());
	CHECK_EQUAL(-128, AnchorMarker::self().raw());
	CHECK_EQUAL(-1, AnchorMarker::offset(-1).raw());
	CHECK_EQUAL(127, AnchorMarker::offset(127).raw());
	CHECK_EQUAL(-127, AnchorMarker::tagged(0, -4).raw());
	CHECK_EQUAL(-65, AnchorMarker::tagged(6, 4).raw());
	CHECK_EQUAL(-64, AnchorMarker::offset(-64).raw());
	CHECK(AnchorMarker::offset(-65).isDerived()); // would collide with the tagged range
}

TEST(AccidentalsTest, inKeyTransposeKeepsNoteShape) {
	// Rotating a pentatonic scale in key moves by per-degree offsets with source == target: unchanged behaviour
	SimSong song(kPentMinor, 0, {60, 63, 65, 67, 70});
	song.notes.push_back({61, 0});
	auto original = song.sorted();
	song.transposeInKey(2);
	song.transposeInKey(-2);
	checkSame(original, song.sorted());
}

TEST(AccidentalsTest, bluesToMajorMapsByDegree) {
	const NoteSet blues = presetScaleNotes[BLUES_SCALE];
	SimSong song(blues, 0, {60, 63, 65, 66, 67, 70});
	CHECK(song.setScaleNotes(kMajor));
	// 1 b3 4 b5 5 b7 -> C E F F# G B, with b5 the secondary on the 5th, which keeps its offset from the 5 (an
	// accidental)
	CHECK((std::vector<int32_t>{60, 64, 65, 66, 67, 71}) == pitchesOf(song));
	// F# is an accidental in major, hanging off the G it was the b5 of
	CHECK(AnchorMarker::offset(1) == song.sorted()[3].marker);
	CHECK(song.setScaleNotes(blues));
	CHECK((std::vector<int32_t>{60, 63, 65, 66, 67, 70}) == pitchesOf(song));
}

TEST(AccidentalsTest, majorBluesMajorRoundTrips) {
	const NoteSet blues = presetScaleNotes[BLUES_SCALE];
	for (int16_t root : {0, 5, 66}) {
		for (bool accidentals : {false, true}) {
			SimSong song(kMajor, root, {});
			for (int32_t p : kAllDegrees) {
				song.notes.push_back({p + root, 0});
			}
			if (accidentals) {
				// F# (hanging off F), C# and A#, as well as G
				for (int32_t p : {66, 61, 70}) {
					song.notes.push_back({p + root, 0});
				}
			}
			auto original = song.sorted();
			CHECK(song.setScaleNotes(blues));
			CHECK_EQUAL(original.size(), song.notes.size());
			CHECK(song.setScaleNotes(kMajor));
			checkSame(original, song.sorted());
			CHECK_EQUAL(0, song.numPitchPreserving);
		}
	}
}

TEST(AccidentalsTest, bluesStateThroughMajorRoundTrips) {
	// Starting in blues with every note of it, plus the b5 as the secondary on the 5th
	const NoteSet blues = presetScaleNotes[BLUES_SCALE];
	for (int16_t root : {0, 5, 66}) {
		SimSong song(blues, root, {});
		for (int32_t p : {60, 63, 65, 66, 67, 70}) {
			song.notes.push_back({p + root, 0});
		}
		auto original = song.sorted();
		CHECK(song.setScaleNotes(kMajor));
		CHECK(song.setScaleNotes(blues));
		checkSame(original, song.sorted());
	}
}
