#include "CppUTest/TestHarness.h"
#include "io/midi/midi_harmonizer.h"

// MIDI note constants for readability
static constexpr uint8_t C1 = 24;
static constexpr uint8_t E1 = 28;
static constexpr uint8_t C4 = 60;
static constexpr uint8_t D4 = 62;
static constexpr uint8_t Eb4 = 63;
static constexpr uint8_t E4 = 64;
static constexpr uint8_t F4 = 65;
static constexpr uint8_t Fsharp4 = 66;
static constexpr uint8_t G4 = 67;
static constexpr uint8_t Ab4 = 68;
static constexpr uint8_t A4 = 69;
static constexpr uint8_t Bb4 = 70;
static constexpr uint8_t B4 = 71;
static constexpr uint8_t C5 = 72;
static constexpr uint8_t D5 = 74;
static constexpr uint8_t E5 = 76;
static constexpr uint8_t G5 = 79;

// C major scale: C D E F G A B
// scaleBits: bit 0=root(C), bit 2=D, bit 4=E, bit 5=F, bit 7=G, bit 9=A, bit 11=B
static constexpr uint8_t kScaleRootC = 0;
static constexpr uint16_t kCMajorBits = 0xAB5; // 1+4+16+32+128+512+2048 = 2741

// D natural minor scale: D E F G A Bb C
// Root = 2 (D). Intervals from D: 0,2,3,5,7,8,10
// Bits: (1<<0)|(1<<2)|(1<<3)|(1<<5)|(1<<7)|(1<<8)|(1<<10) = 1+4+8+32+128+256+1024 = 1453
static constexpr uint8_t kScaleRootD = 2;
static constexpr uint16_t kDMinorBits = 0x5AD; // 1453

/// Helper: build a C major chord (C, E, G) in a ChordState
static ChordState makeCMajorChord() {
	ChordState cs;
	cs.noteOn(C4);
	cs.noteOn(E4);
	cs.noteOn(G4);
	return cs;
}

/// Helper: build a C minor chord (C, Eb, G)
static ChordState makeCMinorChord() {
	ChordState cs;
	cs.noteOn(C4);
	cs.noteOn(Eb4);
	cs.noteOn(G4);
	return cs;
}

/// Helper: build a C dominant 7th chord (C, E, G, Bb)
static ChordState makeCDom7Chord() {
	ChordState cs;
	cs.noteOn(C4);
	cs.noteOn(E4);
	cs.noteOn(G4);
	cs.noteOn(Bb4);
	return cs;
}

/// Helper: build a single-note "chord" (just C)
static ChordState makeSingleNoteChord() {
	ChordState cs;
	cs.noteOn(C4);
	return cs;
}

// ============================================================
// ChordState tests
// ============================================================

TEST_GROUP(ChordState){};

TEST(ChordState, emptyByDefault) {
	ChordState cs;
	CHECK(cs.isEmpty());
	CHECK_EQUAL(0, cs.heldCount());
	CHECK_EQUAL(0, cs.pitchClassCount());
}

TEST(ChordState, noteOnAddsNote) {
	ChordState cs;
	cs.noteOn(C4);
	CHECK(!cs.isEmpty());
	CHECK_EQUAL(1, cs.heldCount());
	CHECK_EQUAL(1, cs.pitchClassCount());
	CHECK_EQUAL(0, cs.pitchClasses()[0]); // C = pitch class 0
}

TEST(ChordState, multipleNotes) {
	ChordState cs = makeCMajorChord();
	CHECK_EQUAL(3, cs.heldCount());
	CHECK_EQUAL(3, cs.pitchClassCount());
	// Pitch classes should be sorted
	CHECK_EQUAL(0, cs.pitchClasses()[0]); // C
	CHECK_EQUAL(4, cs.pitchClasses()[1]); // E
	CHECK_EQUAL(7, cs.pitchClasses()[2]); // G
}

TEST(ChordState, noteOffRemovesNote) {
	ChordState cs = makeCMajorChord();
	cs.noteOff(E4);
	CHECK_EQUAL(2, cs.heldCount());
	CHECK_EQUAL(2, cs.pitchClassCount());
}

TEST(ChordState, noteOffNonexistent) {
	ChordState cs = makeCMajorChord();
	cs.noteOff(D4);                 // Not in chord
	CHECK_EQUAL(3, cs.heldCount()); // Unchanged
}

TEST(ChordState, duplicateNoteOnIgnored) {
	ChordState cs;
	cs.noteOn(C4);
	cs.noteOn(C4); // Duplicate
	CHECK_EQUAL(1, cs.heldCount());
}

TEST(ChordState, sameNoteInDifferentOctaves) {
	ChordState cs;
	cs.noteOn(C4); // C4
	cs.noteOn(C5); // C5
	CHECK_EQUAL(2, cs.heldCount());
	// But only 1 unique pitch class (C)
	CHECK_EQUAL(1, cs.pitchClassCount());
}

TEST(ChordState, reset) {
	ChordState cs = makeCMajorChord();
	cs.reset();
	CHECK(cs.isEmpty());
	CHECK_EQUAL(0, cs.heldCount());
	CHECK_EQUAL(0, cs.pitchClassCount());
}

TEST(ChordState, singleNote) {
	ChordState cs = makeSingleNoteChord();
	CHECK(!cs.isEmpty());
	CHECK_EQUAL(1, cs.heldCount());
	CHECK_EQUAL(1, cs.pitchClassCount());
	CHECK_EQUAL(0, cs.pitchClasses()[0]); // C
}

TEST(ChordState, removeLastNoteMakesEmpty) {
	ChordState cs;
	cs.noteOn(C4);
	cs.noteOff(C4);
	CHECK(cs.isEmpty());
	CHECK_EQUAL(0, cs.heldCount());
}

TEST(ChordState, fourNoteChord) {
	ChordState cs = makeCDom7Chord();
	CHECK_EQUAL(4, cs.heldCount());
	CHECK_EQUAL(4, cs.pitchClassCount());
	// Sorted: C(0), E(4), G(7), Bb(10)
	CHECK_EQUAL(0, cs.pitchClasses()[0]);
	CHECK_EQUAL(4, cs.pitchClasses()[1]);
	CHECK_EQUAL(7, cs.pitchClasses()[2]);
	CHECK_EQUAL(10, cs.pitchClasses()[3]);
}

TEST(ChordState, noteAtMidiBoundaries) {
	ChordState cs;
	cs.noteOn(0);   // Lowest MIDI note
	cs.noteOn(127); // Highest MIDI note
	CHECK_EQUAL(2, cs.heldCount());
	// pitch class 0 (C) and 127%12=7 (G)
	CHECK_EQUAL(0, cs.pitchClasses()[0]);
	CHECK_EQUAL(7, cs.pitchClasses()[1]);
}

// ============================================================
// ChannelState tests
// ============================================================

TEST_GROUP(ChannelState){};

TEST(ChannelState, noMappingByDefault) {
	ChannelState ch;
	auto m = ch.getMapping(60);
	CHECK(!m.active);
}

TEST(ChannelState, setAndGetMapping) {
	ChannelState ch;
	ch.setMapping(60, 64, 100); // C4 → E4, vel 100
	auto m = ch.getMapping(60);
	CHECK(m.active);
	CHECK_EQUAL(64, m.outputNote);
	CHECK_EQUAL(100, m.velocity);
}

TEST(ChannelState, removeMapping) {
	ChannelState ch;
	ch.setMapping(60, 64, 100);
	auto removed = ch.removeMapping(60);
	CHECK(removed.active);
	CHECK_EQUAL(64, removed.outputNote);
	// After removal, should be gone
	auto m = ch.getMapping(60);
	CHECK(!m.active);
}

TEST(ChannelState, intervalMapping) {
	ChannelState ch;
	ch.setIntervalMapping(60, 76, 80); // C4 → E5, vel 80
	auto m = ch.getIntervalMapping(60);
	CHECK(m.active);
	CHECK_EQUAL(76, m.outputNote);
	CHECK_EQUAL(80, m.velocity);
	auto removed = ch.removeIntervalMapping(60);
	CHECK(removed.active);
	CHECK(!ch.getIntervalMapping(60).active);
}

TEST(ChannelState, resetClearsAll) {
	ChannelState ch;
	ch.setMapping(60, 64, 100);
	ch.setIntervalMapping(60, 76, 80);
	ch.lastOutputNote = 64;
	ch.hasLastOutput = true;
	ch.reset();
	CHECK(!ch.getMapping(60).active);
	CHECK(!ch.getIntervalMapping(60).active);
	CHECK(!ch.hasLastOutput);
}

TEST(ChannelState, overwriteMapping) {
	ChannelState ch;
	ch.setMapping(60, 64, 100);
	ch.setMapping(60, 67, 90); // Overwrite same input
	auto m = ch.getMapping(60);
	CHECK(m.active);
	CHECK_EQUAL(67, m.outputNote);
	CHECK_EQUAL(90, m.velocity);
}

TEST(ChannelState, multipleMappings) {
	ChannelState ch;
	ch.setMapping(60, 64, 100);
	ch.setMapping(62, 67, 80);
	CHECK(ch.getMapping(60).active);
	CHECK(ch.getMapping(62).active);
	CHECK(!ch.getMapping(61).active); // Not mapped
	CHECK_EQUAL(64, ch.getMapping(60).outputNote);
	CHECK_EQUAL(67, ch.getMapping(62).outputNote);
}

TEST(ChannelState, removeNonexistentMapping) {
	ChannelState ch;
	auto removed = ch.removeMapping(60);
	CHECK(!removed.active); // Nothing to remove
}

TEST(ChannelState, boundaryNotes) {
	ChannelState ch;
	ch.setMapping(0, 127, 1);
	ch.setMapping(127, 0, 127);
	auto low = ch.getMapping(0);
	auto high = ch.getMapping(127);
	CHECK(low.active);
	CHECK_EQUAL(127, low.outputNote);
	CHECK(high.active);
	CHECK_EQUAL(0, high.outputNote);
}

// ============================================================
// Mapping lifecycle tests — catch stuck-note / duplicate-note bugs
// ============================================================

TEST_GROUP(MappingLifecycle){};

// Simulates: melody note on → chord changes → retrigger (re-harmonize)
// The old output note MUST be retrievable so noteOff can be sent.
TEST(MappingLifecycle, retriggerReturnsOldOutput) {
	ChannelState ch;

	// 1. Melody C4 arrives, harmonized to E4
	ch.setMapping(C4, E4, 100);
	CHECK_EQUAL(E4, ch.getMapping(C4).outputNote);

	// 2. Chord changes → retrigger: must remove old mapping to get old output for noteOff
	auto old = ch.removeMapping(C4);
	CHECK(old.active);
	CHECK_EQUAL(E4, old.outputNote); // Need this to send noteOff(E4)

	// 3. Re-harmonize: new output is G4
	ch.setMapping(C4, G4, 100);
	CHECK_EQUAL(G4, ch.getMapping(C4).outputNote);

	// 4. Final noteOff: removes current mapping
	auto final_ = ch.removeMapping(C4);
	CHECK(final_.active);
	CHECK_EQUAL(G4, final_.outputNote); // Need this for noteOff(G4)

	// 5. Nothing left — no orphaned mappings
	CHECK(!ch.getMapping(C4).active);
}

// Simulates: melody note with interval → chord changes → retrigger
// Both primary AND interval mappings must be removable for noteOff.
TEST(MappingLifecycle, retriggerWithIntervalCleansUpBoth) {
	ChannelState ch;

	// 1. Melody C4 → harmonized to E4, interval voice at G5
	ch.setMapping(C4, E4, 100);
	ch.setIntervalMapping(C4, G5, 80);

	// 2. Chord changes — must clean up BOTH before re-harmonizing
	auto oldPrimary = ch.removeMapping(C4);
	auto oldInterval = ch.removeIntervalMapping(C4);
	CHECK(oldPrimary.active);
	CHECK_EQUAL(E4, oldPrimary.outputNote); // noteOff(E4)
	CHECK(oldInterval.active);
	CHECK_EQUAL(G5, oldInterval.outputNote); // noteOff(G5)

	// 3. Re-harmonize with new chord
	ch.setMapping(C4, G4, 100);
	ch.setIntervalMapping(C4, C5, 80);

	// 4. Verify new state
	CHECK_EQUAL(G4, ch.getMapping(C4).outputNote);
	CHECK_EQUAL(C5, ch.getIntervalMapping(C4).outputNote);

	// 5. Clean noteOff at end — nothing orphaned
	ch.removeMapping(C4);
	ch.removeIntervalMapping(C4);
	CHECK(!ch.getMapping(C4).active);
	CHECK(!ch.getIntervalMapping(C4).active);
}

// Simulates: probability skip stores identity mapping (input→input)
// noteOff must still find it to clean up.
TEST(MappingLifecycle, identityMappingForProbabilitySkip) {
	ChannelState ch;

	// Probability roll failed → identity mapping
	ch.setMapping(C4, C4, 100); // input == output
	auto m = ch.getMapping(C4);
	CHECK(m.active);
	CHECK_EQUAL(C4, m.outputNote); // Output equals input

	// noteOff must still work
	auto removed = ch.removeMapping(C4);
	CHECK(removed.active);
	CHECK_EQUAL(C4, removed.outputNote);
	CHECK(!ch.getMapping(C4).active);
}

// Multiple melody notes active simultaneously — each has independent mapping
TEST(MappingLifecycle, multipleActiveNotesIndependent) {
	ChannelState ch;

	ch.setMapping(C4, E4, 100);
	ch.setMapping(D4, F4, 90);
	ch.setMapping(E4, G4, 80);

	// Remove middle note — others unaffected
	auto removed = ch.removeMapping(D4);
	CHECK(removed.active);
	CHECK_EQUAL(F4, removed.outputNote);

	// Other mappings still intact
	CHECK_EQUAL(E4, ch.getMapping(C4).outputNote);
	CHECK_EQUAL(G4, ch.getMapping(E4).outputNote);
	CHECK(!ch.getMapping(D4).active); // Gone
}

// Overwriting a mapping without removing old one first — the old output note is LOST.
// This is the exact bug pattern that causes stuck notes if the caller isn't careful.
TEST(MappingLifecycle, overwriteWithoutRemoveLosesOldOutput) {
	ChannelState ch;

	ch.setMapping(C4, E4, 100); // Original: C4→E4
	ch.setMapping(C4, G4, 100); // Overwrite: C4→G4

	// The old E4 is gone — can't retrieve it for noteOff anymore
	auto m = ch.getMapping(C4);
	CHECK_EQUAL(G4, m.outputNote); // Only new output visible
	                               // This test documents the danger: caller MUST removeMapping before setMapping
	                               // during retrigger, or E4 will never get a noteOff → stuck note
}

// Double noteOff — second removal should return inactive (no double noteOff sent)
TEST(MappingLifecycle, doubleRemoveReturnsInactive) {
	ChannelState ch;
	ch.setMapping(C4, E4, 100);

	auto first = ch.removeMapping(C4);
	CHECK(first.active);

	auto second = ch.removeMapping(C4);
	CHECK(!second.active); // No mapping to remove → don't send noteOff again
}

// Interval without primary — should work independently
TEST(MappingLifecycle, intervalWithoutPrimary) {
	ChannelState ch;
	ch.setIntervalMapping(C4, E5, 80);

	// No primary mapping
	CHECK(!ch.getMapping(C4).active);
	// Interval exists independently
	CHECK(ch.getIntervalMapping(C4).active);
	CHECK_EQUAL(E5, ch.getIntervalMapping(C4).outputNote);

	auto removed = ch.removeIntervalMapping(C4);
	CHECK(removed.active);
	CHECK_EQUAL(E5, removed.outputNote);
}

// Reset while notes are active — simulates sequencer stop
TEST(MappingLifecycle, resetWithActiveNotes) {
	ChannelState ch;
	ch.setMapping(C4, E4, 100);
	ch.setMapping(D4, F4, 90);
	ch.setIntervalMapping(C4, G5, 80);
	ch.setIntervalMapping(D4, A4, 70);
	ch.lastOutputNote = E4;
	ch.hasLastOutput = true;

	ch.reset();

	// Everything gone — clean slate, no orphans
	CHECK(!ch.getMapping(C4).active);
	CHECK(!ch.getMapping(D4).active);
	CHECK(!ch.getIntervalMapping(C4).active);
	CHECK(!ch.getIntervalMapping(D4).active);
	CHECK(!ch.hasLastOutput);
}

// ============================================================
// Multi-clip integration: two MIDI clips with different settings
// sharing the same chord, verifying independent behavior.
// This simulates the real-world scenario of two clips on
// different channels with different harmonizer configurations.
// ============================================================

TEST_GROUP(MultiClip){};

// Two clips, same input note, different Snap modes → different outputs
TEST(MultiClip, differentSnapModes) {
	HarmonizerState state;
	// Chord: C major (C, E, G) — shared by both clips
	state.chordState.noteOn(C4);
	state.chordState.noteOn(E4);
	state.chordState.noteOn(G4);

	// Clip A on channel 2: Snap=Nearest, Target=Strict
	HarmonizeConfig configA;
	configA.mode = HarmonizerMappingMode::Nearest;
	configA.tightness = HarmonizerTightness::Strict;

	// Clip B on channel 3: Snap=Root, Target=Strict
	HarmonizeConfig configB;
	configB.mode = HarmonizerMappingMode::Root;
	configB.tightness = HarmonizerTightness::Strict;

	uint8_t inputNote = F4; // 65

	// Clip A: F4 → nearest chord tone → E4
	uint8_t outputA = harmonize(inputNote, state.chordState, -1, false, configA);
	state.channelStates[2].setMapping(inputNote, outputA, 100);

	// Clip B: F4 → root (C) → C4
	uint8_t outputB = harmonize(inputNote, state.chordState, -1, false, configB);
	state.channelStates[3].setMapping(inputNote, outputB, 100);

	// Same input, different outputs
	CHECK(outputA != outputB);
	CHECK_EQUAL(E4, outputA);
	CHECK_EQUAL(C4, outputB);

	// Mappings are independent per channel
	CHECK_EQUAL(E4, state.channelStates[2].getMapping(inputNote).outputNote);
	CHECK_EQUAL(C4, state.channelStates[3].getMapping(inputNote).outputNote);
}

// Two clips, same input, different Target modes
TEST(MultiClip, differentTargetModes) {
	HarmonizerState state;
	state.chordState.noteOn(C4);
	state.chordState.noteOn(E4);
	state.chordState.noteOn(G4);

	// Clip A: Target=Strict (everything snaps)
	HarmonizeConfig configA;
	configA.mode = HarmonizerMappingMode::Nearest;
	configA.tightness = HarmonizerTightness::Strict;

	// Clip B: Target=Loose (only notes within 1 semi of chord tone snap)
	HarmonizeConfig configB;
	configB.mode = HarmonizerMappingMode::Nearest;
	configB.tightness = HarmonizerTightness::Loose;

	// A4(69) is 2 semitones from G4 — Strict snaps it, Loose passes it through
	uint8_t outputA = harmonize(A4, state.chordState, -1, false, configA);
	uint8_t outputB = harmonize(A4, state.chordState, -1, false, configB);

	CHECK_EQUAL(G4, outputA); // Strict: snapped to G
	CHECK_EQUAL(A4, outputB); // Loose: passed through (2 semi away, not close enough)
}

// Two clips with different transpose values
TEST(MultiClip, differentTranspose) {
	HarmonizerState state;
	state.chordState.noteOn(C4);
	state.chordState.noteOn(E4);
	state.chordState.noteOn(G4);

	// Clip A: transpose +12 (one octave up)
	HarmonizeConfig configA;
	configA.mode = HarmonizerMappingMode::Nearest;
	configA.tightness = HarmonizerTightness::Strict;
	configA.transpose = 12;

	// Clip B: transpose -12 (one octave down)
	HarmonizeConfig configB;
	configB.mode = HarmonizerMappingMode::Nearest;
	configB.tightness = HarmonizerTightness::Strict;
	configB.transpose = -12;

	uint8_t outputA = harmonize(C4, state.chordState, -1, false, configA);
	uint8_t outputB = harmonize(C4, state.chordState, -1, false, configB);

	CHECK_EQUAL(C5, outputA); // C4 + 12 = C5
	CHECK_EQUAL(48, outputB); // C4 - 12 = C3
}

// Two clips: one with voice leading, one without
TEST(MultiClip, voiceLeadingIndependence) {
	HarmonizerState state;
	state.chordState.noteOn(C4);
	state.chordState.noteOn(E4);
	state.chordState.noteOn(G4);

	// Clip A: voice leading ON (channel 2)
	HarmonizeConfig configA;
	configA.mode = HarmonizerMappingMode::Nearest;
	configA.tightness = HarmonizerTightness::Strict;
	configA.voiceLeading = true;

	// Clip B: voice leading OFF (channel 3)
	HarmonizeConfig configB;
	configB.mode = HarmonizerMappingMode::Nearest;
	configB.tightness = HarmonizerTightness::Strict;
	configB.voiceLeading = false;

	// First note for both: F4 → E4
	uint8_t a1 = harmonize(F4, state.chordState, -1, false, configA);
	uint8_t b1 = harmonize(F4, state.chordState, -1, false, configB);
	state.channelStates[2].setMapping(F4, a1, 100);
	state.channelStates[2].lastOutputNote = a1;
	state.channelStates[2].hasLastOutput = true;
	CHECK_EQUAL(E4, a1);
	CHECK_EQUAL(E4, b1);

	// Second note: B4.
	// Without voice leading: B4 → nearest is C5(72), dist=1
	uint8_t b2 = harmonize(B4, state.chordState, -1, false, configB);
	CHECK_EQUAL(C5, b2);

	// With voice leading, lastOutput=E4(64):
	// B4(71) candidates (within ±7): E4(64)=7, G4(67)=4, C5(72)=1, E5(76)=5
	// Scores: E4: 7*2+0=14, G4: 4*2+3=11, C5: 1*2+8=10, E5: 5*2+12=22
	// C5 wins (same result here, but through different logic path)
	uint8_t a2 = harmonize(B4, state.chordState, static_cast<int32_t>(state.channelStates[2].lastOutputNote),
	                       state.channelStates[2].hasLastOutput, configA);
	CHECK_EQUAL(C5, a2);
}

// Full scenario: chord changes, both clips retrigger independently
TEST(MultiClip, chordChangeRetriggerBothClips) {
	HarmonizerState state;

	// Initial chord: C major
	state.chordState.noteOn(C4);
	state.chordState.noteOn(E4);
	state.chordState.noteOn(G4);

	HarmonizeConfig configA; // Clip A: Nearest/Strict
	configA.mode = HarmonizerMappingMode::Nearest;
	configA.tightness = HarmonizerTightness::Strict;

	HarmonizeConfig configB; // Clip B: RoundDown/Strict
	configB.mode = HarmonizerMappingMode::RoundDown;
	configB.tightness = HarmonizerTightness::Strict;

	// Both clips play F4
	uint8_t a1 = harmonize(F4, state.chordState, -1, false, configA);
	uint8_t b1 = harmonize(F4, state.chordState, -1, false, configB);
	state.channelStates[2].setMapping(F4, a1, 100);
	state.channelStates[3].setMapping(F4, b1, 100);

	CHECK_EQUAL(E4, a1); // Nearest: F→E
	CHECK_EQUAL(E4, b1); // RoundDown: F→E (E is below F)

	// --- Chord changes to F major (F, A, C) ---
	state.chordState.reset();
	state.chordState.noteOn(F4);
	state.chordState.noteOn(A4);
	state.chordState.noteOn(C5);

	// Retrigger clip A: remove old mapping, re-harmonize
	auto oldA = state.channelStates[2].removeMapping(F4);
	CHECK(oldA.active);
	CHECK_EQUAL(E4, oldA.outputNote); // Need this for noteOff(E4)

	uint8_t a2 = harmonize(F4, state.chordState, -1, false, configA);
	state.channelStates[2].setMapping(F4, a2, 100);
	CHECK_EQUAL(F4, a2); // F is now a chord tone!

	// Retrigger clip B: remove old mapping, re-harmonize
	auto oldB = state.channelStates[3].removeMapping(F4);
	CHECK(oldB.active);
	CHECK_EQUAL(E4, oldB.outputNote); // Need this for noteOff(E4)

	uint8_t b2 = harmonize(F4, state.chordState, -1, false, configB);
	state.channelStates[3].setMapping(F4, b2, 100);
	CHECK_EQUAL(F4, b2); // RoundDown: F is a chord tone, stays

	// Both clips properly cleaned up old E4 mapping and have new F4 mapping
	CHECK_EQUAL(F4, state.channelStates[2].getMapping(F4).outputNote);
	CHECK_EQUAL(F4, state.channelStates[3].getMapping(F4).outputNote);
}

// Two clips with different diatonic intervals on same input
TEST(MultiClip, differentIntervals) {
	HarmonizerState state;
	state.chordState.noteOn(C4);
	state.chordState.noteOn(E4);
	state.chordState.noteOn(G4);

	HarmonizeConfig config;
	config.mode = HarmonizerMappingMode::Nearest;
	config.tightness = HarmonizerTightness::Strict;

	// Both clips harmonize C4 → C4 (chord tone)
	uint8_t harmonized = harmonize(C4, state.chordState, -1, false, config);
	CHECK_EQUAL(C4, harmonized);

	// Clip A: 3rd above C4 in C major = E4
	int32_t intervalA = computeDiatonicInterval(harmonized, DiatonicInterval::Third_Above, kScaleRootC, kCMajorBits);
	CHECK_EQUAL(E4, intervalA);
	state.channelStates[2].setMapping(C4, harmonized, 100);
	state.channelStates[2].setIntervalMapping(C4, static_cast<uint8_t>(intervalA), 80);

	// Clip B: 6th above C4 in C major = A4
	int32_t intervalB = computeDiatonicInterval(harmonized, DiatonicInterval::Sixth_Above, kScaleRootC, kCMajorBits);
	CHECK_EQUAL(A4, intervalB);
	state.channelStates[3].setMapping(C4, harmonized, 100);
	state.channelStates[3].setIntervalMapping(C4, static_cast<uint8_t>(intervalB), 80);

	// Verify independent interval mappings
	CHECK_EQUAL(E4, state.channelStates[2].getIntervalMapping(C4).outputNote);
	CHECK_EQUAL(A4, state.channelStates[3].getIntervalMapping(C4).outputNote);

	// Clean up both — no orphaned interval notes
	auto remA = state.channelStates[2].removeIntervalMapping(C4);
	auto remB = state.channelStates[3].removeIntervalMapping(C4);
	CHECK(remA.active);
	CHECK(remB.active);
	CHECK_EQUAL(E4, remA.outputNote);
	CHECK_EQUAL(A4, remB.outputNote);
}

// ============================================================
// Chord channel change scenarios
// Simulates the routing logic from midi_instrument.cpp to verify
// state integrity when chord channel changes mid-performance.
// ============================================================

TEST_GROUP(ChordChannelChange){};

// Helper that mimics the noteOn routing decision in midi_instrument.cpp:
// if (channel == chordCh) → update chord; else → harmonize as melody
static void simulateNoteOn(HarmonizerState& state, uint8_t note, int32_t channel, int32_t chordCh,
                           const HarmonizeConfig& config) {
	if (channel == chordCh) {
		state.chordState.noteOn(note);
	}
	else {
		auto& chState = state.channelStates[channel];
		uint8_t output = harmonize(note, state.chordState, -1, false, config);
		chState.setMapping(note, output, 100);
	}
}

// Helper that mimics the noteOff routing in midi_instrument.cpp:
// Check melody mapping FIRST (regardless of current chordCh), then chord path.
// Returns the output note that needs noteOff, or -1 if chord path / no mapping.
static int32_t simulateNoteOff(HarmonizerState& state, uint8_t note, int32_t channel, int32_t chordCh) {
	auto& chState = state.channelStates[channel];
	bool hasMelodyMapping = chState.getMapping(note).active;

	if (hasMelodyMapping) {
		// Melody path: remove mapping, return old output for noteOff
		auto mapping = chState.removeMapping(note);
		// Also clean up interval if present
		chState.removeIntervalMapping(note);
		return mapping.active ? mapping.outputNote : -1;
	}
	else if (channel == chordCh) {
		// Chord path: remove from chord state
		state.chordState.noteOff(note);
		return -1; // Chord notes don't have output mappings
	}
	return -1;
}

// Basic flow: chord on ch0, melody on ch1, everything works
TEST(ChordChannelChange, normalOperation) {
	HarmonizerState state;
	HarmonizeConfig config;
	config.mode = HarmonizerMappingMode::Nearest;
	config.tightness = HarmonizerTightness::Strict;
	int32_t chordCh = 0;

	// Build chord: C major on ch0
	simulateNoteOn(state, C4, 0, chordCh, config);
	simulateNoteOn(state, E4, 0, chordCh, config);
	simulateNoteOn(state, G4, 0, chordCh, config);
	CHECK_EQUAL(3, state.chordState.heldCount());

	// Melody on ch1: F4 → E4 (nearest chord tone)
	simulateNoteOn(state, F4, 1, chordCh, config);
	CHECK_EQUAL(E4, state.channelStates[1].getMapping(F4).outputNote);

	// NoteOff melody: returns E4 for noteOff
	int32_t offNote = simulateNoteOff(state, F4, 1, chordCh);
	CHECK_EQUAL(E4, offNote);
	CHECK(!state.channelStates[1].getMapping(F4).active);

	// NoteOff chord: removes from chord state
	simulateNoteOff(state, E4, 0, chordCh);
	CHECK_EQUAL(2, state.chordState.heldCount());
}

// KEY TEST: Melody note in flight when chord channel changes.
// Note was harmonized as melody on ch1 with chordCh=0.
// Then chordCh changes to 1. The noteOff for the melody note
// must still find the mapping and clean up — NOT treat it as chord.
TEST(ChordChannelChange, melodyNoteInFlightDuringSwitch) {
	HarmonizerState state;
	HarmonizeConfig config;
	config.mode = HarmonizerMappingMode::Nearest;
	config.tightness = HarmonizerTightness::Strict;

	// Phase 1: chordCh = 0
	int32_t chordCh = 0;
	simulateNoteOn(state, C4, 0, chordCh, config);
	simulateNoteOn(state, E4, 0, chordCh, config);
	simulateNoteOn(state, G4, 0, chordCh, config);

	// Melody note F4 on ch1, harmonized to E4
	simulateNoteOn(state, F4, 1, chordCh, config);
	CHECK_EQUAL(E4, state.channelStates[1].getMapping(F4).outputNote);

	// Phase 2: chord channel changes to 1!
	chordCh = 1;

	// NoteOff for F4 on ch1 — the mapping check finds it FIRST,
	// so it uses the melody path even though ch1 is now the chord channel.
	int32_t offNote = simulateNoteOff(state, F4, 1, chordCh);
	CHECK_EQUAL(E4, offNote);                             // Got the old output → can send noteOff(E4)
	CHECK(!state.channelStates[1].getMapping(F4).active); // Cleaned up
}

// Chord note on old channel after switch: noteOff goes through chord path
// on the old channel (which is no longer chordCh), so it falls through harmlessly.
TEST(ChordChannelChange, chordNoteOrphanedAfterSwitch) {
	HarmonizerState state;
	HarmonizeConfig config;
	config.mode = HarmonizerMappingMode::Nearest;
	config.tightness = HarmonizerTightness::Strict;

	// Phase 1: chord on ch0
	int32_t chordCh = 0;
	simulateNoteOn(state, C4, 0, chordCh, config);
	simulateNoteOn(state, E4, 0, chordCh, config);
	CHECK_EQUAL(2, state.chordState.heldCount());

	// Phase 2: chord channel changes to 2
	chordCh = 2;

	// NoteOff for C4 on ch0: no melody mapping, ch0 ≠ chordCh(2)
	// Falls through — chord state still has C4 (orphaned)
	int32_t offNote = simulateNoteOff(state, C4, 0, chordCh);
	CHECK_EQUAL(-1, offNote); // No output note to turn off
	// The chord state still has C4 — it's orphaned until reset
	CHECK_EQUAL(2, state.chordState.heldCount());

	// This is why reset() on chord channel change is important
	state.chordState.reset();
	CHECK(state.chordState.isEmpty());
}

// Full scenario: switch chord channel, rebuild chord, harmonize on new channels
TEST(ChordChannelChange, fullChannelSwitch) {
	HarmonizerState state;
	HarmonizeConfig config;
	config.mode = HarmonizerMappingMode::Nearest;
	config.tightness = HarmonizerTightness::Strict;

	// Phase 1: chord ch=0, melody ch=1
	int32_t chordCh = 0;
	simulateNoteOn(state, C4, 0, chordCh, config);
	simulateNoteOn(state, E4, 0, chordCh, config);
	simulateNoteOn(state, G4, 0, chordCh, config);

	simulateNoteOn(state, F4, 1, chordCh, config);
	CHECK_EQUAL(E4, state.channelStates[1].getMapping(F4).outputNote);

	// Clean up melody note before switch
	int32_t off1 = simulateNoteOff(state, F4, 1, chordCh);
	CHECK_EQUAL(E4, off1);

	// Phase 2: switch chord channel to 2, reset chord state
	chordCh = 2;
	state.chordState.reset();

	// Build new chord on ch2: D minor (D, F, A)
	simulateNoteOn(state, D4, 2, chordCh, config);
	simulateNoteOn(state, F4, 2, chordCh, config);
	simulateNoteOn(state, A4, 2, chordCh, config);
	CHECK_EQUAL(3, state.chordState.heldCount());

	// Now ch0 is melody (was chord before!)
	// Play E4 on ch0 → nearest chord tone in D minor: D4 or F4. E4(64): D4(62)=2, F4(65)=1 → F4
	simulateNoteOn(state, E4, 0, chordCh, config);
	CHECK_EQUAL(F4, state.channelStates[0].getMapping(E4).outputNote);

	// Ch1 is also melody
	// Play C4 on ch1 → nearest to D minor: D4(62) dist=2, F4(65) dist=5 → D4
	simulateNoteOn(state, C4, 1, chordCh, config);
	CHECK_EQUAL(D4, state.channelStates[1].getMapping(C4).outputNote);

	// Both melody channels work independently against new chord
	int32_t off0 = simulateNoteOff(state, E4, 0, chordCh);
	int32_t off1b = simulateNoteOff(state, C4, 1, chordCh);
	CHECK_EQUAL(F4, off0);
	CHECK_EQUAL(D4, off1b);
}

// Two melody notes on same channel, chord channel changes between them
TEST(ChordChannelChange, twoNotesStraddlingSwitch) {
	HarmonizerState state;
	HarmonizeConfig config;
	config.mode = HarmonizerMappingMode::Nearest;
	config.tightness = HarmonizerTightness::Strict;

	// Chord on ch0: C major
	int32_t chordCh = 0;
	simulateNoteOn(state, C4, 0, chordCh, config);
	simulateNoteOn(state, E4, 0, chordCh, config);
	simulateNoteOn(state, G4, 0, chordCh, config);

	// Melody ch1: note A, harmonized while chord was C major
	simulateNoteOn(state, A4, 1, chordCh, config);
	CHECK_EQUAL(G4, state.channelStates[1].getMapping(A4).outputNote);

	// --- Chord channel switches to 3, chord reset ---
	chordCh = 3;
	state.chordState.reset();

	// New chord on ch3: F major (F, A, C)
	simulateNoteOn(state, F4, 3, chordCh, config);
	simulateNoteOn(state, A4, 3, chordCh, config);
	simulateNoteOn(state, C5, 3, chordCh, config);

	// Melody ch1: new note D4, harmonized against F major
	simulateNoteOn(state, D4, 1, chordCh, config);
	// D4(62): F4(65)=3, A4(69)=7, C4(48+12=60)=2 → C5? No, expanded tones: C4=60, dist=2. F4=65 dist=3 → C4(60)
	// Actually nearest to D4(62): C4(60)=2, F4(65)=3 → C4(60)... wait that's not in F major as pitch class.
	// F major chord: F(5), A(9), C(0). Expanded: C0=0, C1=12, ..., C4=60, C5=72, F1=17, F2=29, ..., F4=65, A1=21,
	// A2=33, ..., A4=69 So near D4(62): C4(60) dist=2, F4(65) dist=3, A3(57) dist=5 → C4(60)
	CHECK_EQUAL(C4, state.channelStates[1].getMapping(D4).outputNote);

	// NoteOff for A4 (the OLD note from before the switch): mapping still there!
	int32_t offOld = simulateNoteOff(state, A4, 1, chordCh);
	CHECK_EQUAL(G4, offOld); // Gets old harmonization → clean noteOff(G4)

	// NoteOff for D4 (the new note after switch)
	int32_t offNew = simulateNoteOff(state, D4, 1, chordCh);
	CHECK_EQUAL(C4, offNew); // Gets new harmonization → clean noteOff(C4)

	// Both cleaned up — no stuck notes
	CHECK(!state.channelStates[1].getMapping(A4).active);
	CHECK(!state.channelStates[1].getMapping(D4).active);
}

// Multiple notes on two clips simultaneously — mapping isolation
TEST(MultiClip, multipleNotesIsolation) {
	HarmonizerState state;
	state.chordState.noteOn(C4);
	state.chordState.noteOn(E4);
	state.chordState.noteOn(G4);

	HarmonizeConfig config;
	config.mode = HarmonizerMappingMode::Nearest;
	config.tightness = HarmonizerTightness::Strict;

	// Clip A (ch 2): plays D4, F4
	state.channelStates[2].setMapping(D4, harmonize(D4, state.chordState, -1, false, config), 100);
	state.channelStates[2].setMapping(F4, harmonize(F4, state.chordState, -1, false, config), 90);

	// Clip B (ch 3): plays D4, A4 (same D4 as clip A!)
	state.channelStates[3].setMapping(D4, harmonize(D4, state.chordState, -1, false, config), 100);
	state.channelStates[3].setMapping(A4, harmonize(A4, state.chordState, -1, false, config), 85);

	// Both channels have D4 mapped independently
	CHECK_EQUAL(C4, state.channelStates[2].getMapping(D4).outputNote);
	CHECK_EQUAL(C4, state.channelStates[3].getMapping(D4).outputNote);

	// Remove D4 from channel 2 — channel 3's D4 mapping unaffected
	state.channelStates[2].removeMapping(D4);
	CHECK(!state.channelStates[2].getMapping(D4).active);
	CHECK(state.channelStates[3].getMapping(D4).active); // Still there!

	// Channel 2 still has F4 mapping
	CHECK_EQUAL(E4, state.channelStates[2].getMapping(F4).outputNote);
	// Channel 3 still has A4 mapping
	CHECK_EQUAL(G4, state.channelStates[3].getMapping(A4).outputNote);
}

// ============================================================
// harmonizeNote — Snap modes
// ============================================================

TEST_GROUP(HarmonizeNote){};

TEST(HarmonizeNote, emptyChordReturnsInput) {
	ChordState empty;
	uint8_t result = harmonizeNote(F4, empty, HarmonizerMappingMode::Nearest);
	CHECK_EQUAL(F4, result);
}

TEST(HarmonizeNote, nearestSnap) {
	ChordState cs = makeCMajorChord();
	// Eb4 (63): 1 semi from E4(64), 3 from C4(60) → E4
	CHECK_EQUAL(E4, harmonizeNote(Eb4, cs, HarmonizerMappingMode::Nearest));
	// F4 (65): 1 semi from E4(64), 2 from G4(67) → E4
	CHECK_EQUAL(E4, harmonizeNote(F4, cs, HarmonizerMappingMode::Nearest));
	// A4 (69): 2 semi from G4(67), 3 from C5(72) → G4
	CHECK_EQUAL(G4, harmonizeNote(A4, cs, HarmonizerMappingMode::Nearest));
	// B4 (71): 4 semi from G4(67), 1 from C5(72) → C5
	CHECK_EQUAL(C5, harmonizeNote(B4, cs, HarmonizerMappingMode::Nearest));
}

TEST(HarmonizeNote, roundDownSnap) {
	ChordState cs = makeCMajorChord();
	// F4 (65) → E4 (64, nearest chord tone at or below)
	CHECK_EQUAL(E4, harmonizeNote(F4, cs, HarmonizerMappingMode::RoundDown));
	// A4 (69) → G4 (67)
	CHECK_EQUAL(G4, harmonizeNote(A4, cs, HarmonizerMappingMode::RoundDown));
	// Chord tone itself should stay
	CHECK_EQUAL(E4, harmonizeNote(E4, cs, HarmonizerMappingMode::RoundDown));
}

TEST(HarmonizeNote, roundUpSnap) {
	ChordState cs = makeCMajorChord();
	// D4 (62) → E4 (64, nearest chord tone at or above)
	CHECK_EQUAL(E4, harmonizeNote(D4, cs, HarmonizerMappingMode::RoundUp));
	// F4 (65) → G4 (67)
	CHECK_EQUAL(G4, harmonizeNote(F4, cs, HarmonizerMappingMode::RoundUp));
	// Chord tone itself should stay
	CHECK_EQUAL(G4, harmonizeNote(G4, cs, HarmonizerMappingMode::RoundUp));
}

TEST(HarmonizeNote, rootSnap) {
	ChordState cs = makeCMajorChord();
	// Root is C (pitch class 0). Nearest C to F4(65) is C5(72) vs C4(60) → C5 is closer
	// Actually: |65-60|=5, |65-72|=7 → C4 is closer
	CHECK_EQUAL(C4, harmonizeNote(F4, cs, HarmonizerMappingMode::Root));
	// B4 (71): |71-60|=11, |71-72|=1 → C5
	CHECK_EQUAL(C5, harmonizeNote(B4, cs, HarmonizerMappingMode::Root));
}

TEST(HarmonizeNote, root5thSnap) {
	ChordState cs = makeCMajorChord();
	// Root=C(0), 5th=G(7). F4(65): C4(60)=5, E4 not eligible, G4(67)=2 → G4
	CHECK_EQUAL(G4, harmonizeNote(F4, cs, HarmonizerMappingMode::Root5th));
	// D4(62): C4(60)=2, G3(55)=7 → C4
	CHECK_EQUAL(C4, harmonizeNote(D4, cs, HarmonizerMappingMode::Root5th));
}

// --- Minor chord (C, Eb, G) ---

TEST(HarmonizeNote, nearestMinorChord) {
	ChordState cs = makeCMinorChord();
	// D4 (62): 1 semi from Eb4(63), 2 from C4(60) → Eb4
	CHECK_EQUAL(Eb4, harmonizeNote(D4, cs, HarmonizerMappingMode::Nearest));
	// E4 (64): 1 semi from Eb4(63), 3 from G4(67) → Eb4
	CHECK_EQUAL(Eb4, harmonizeNote(E4, cs, HarmonizerMappingMode::Nearest));
	// F4 (65): 2 semi from Eb4(63), 2 from G4(67) → Eb4 (tied, first found wins)
	uint8_t fResult = harmonizeNote(F4, cs, HarmonizerMappingMode::Nearest);
	CHECK(fResult == Eb4 || fResult == G4); // Either is acceptable on a tie
}

// --- 7th chord (C, E, G, Bb) ---

TEST(HarmonizeNote, nearest7thChord) {
	ChordState cs = makeCDom7Chord();
	// A4 (69): 2 from G4(67), 1 from Bb4(70) → Bb4
	CHECK_EQUAL(Bb4, harmonizeNote(A4, cs, HarmonizerMappingMode::Nearest));
	// F# (66): 2 from E4(64), 1 from G4(67) → G4
	CHECK_EQUAL(G4, harmonizeNote(Fsharp4, cs, HarmonizerMappingMode::Nearest));
}

// --- Single-note chord ---

TEST(HarmonizeNote, singleNoteChord) {
	ChordState cs = makeSingleNoteChord(); // Just C
	// Everything snaps to nearest C
	CHECK_EQUAL(C4, harmonizeNote(D4, cs, HarmonizerMappingMode::Nearest));
	CHECK_EQUAL(C4, harmonizeNote(E4, cs, HarmonizerMappingMode::Nearest));
	// F# is equidistant between C4(60) and C5(72) → C4 wins (found first)
	CHECK_EQUAL(C4, harmonizeNote(Fsharp4, cs, HarmonizerMappingMode::Nearest));
}

// --- MIDI boundary notes ---

TEST(HarmonizeNote, lowestMidiNote) {
	ChordState cs = makeCMajorChord();
	// Note 0 = C0. Nearest chord tone is C0 itself (pitch class 0)
	CHECK_EQUAL(0, harmonizeNote(0, cs, HarmonizerMappingMode::Nearest));
}

TEST(HarmonizeNote, highestMidiNote) {
	ChordState cs = makeCMajorChord();
	// Note 127 = G9 (pitch class 7). G is a chord tone → passes through
	CHECK_EQUAL(127, harmonizeNote(127, cs, HarmonizerMappingMode::Nearest));
}

TEST(HarmonizeNote, roundDownBelowAllChordTones) {
	// Chord with only high notes
	ChordState cs;
	cs.noteOn(E4); // 64
	cs.noteOn(G4); // 67
	// Note 1 is far below E4 — no chord tone at or below 1
	// RoundDown returns input when nothing found below
	uint8_t result = harmonizeNote(1, cs, HarmonizerMappingMode::RoundDown);
	// expandChordTones includes E0(4), so 4 > 1, E1(16) > 1
	// But E0=4 > 1, so there's nothing at or below 1. Returns input.
	CHECK_EQUAL(1, result);
}

TEST(HarmonizeNote, roundUpAboveAllChordTones) {
	ChordState cs = makeCMajorChord();
	// Note 127 = G9 (chord tone). Already a chord tone, returns 127.
	CHECK_EQUAL(127, harmonizeNote(127, cs, HarmonizerMappingMode::RoundUp));
}

// --- Root snap with non-C root ---

TEST(HarmonizeNote, rootSnapMinorChord) {
	ChordState cs = makeCMinorChord(); // Root = C (pitch class 0, lowest)
	CHECK_EQUAL(C4, harmonizeNote(D4, cs, HarmonizerMappingMode::Root));
	CHECK_EQUAL(C5, harmonizeNote(B4, cs, HarmonizerMappingMode::Root));
}

// ============================================================
// harmonize — Target modes
// ============================================================

TEST_GROUP(HarmonizeTarget){};

TEST(HarmonizeTarget, chordTones) {
	ChordState cs = makeCMajorChord();
	HarmonizeConfig config;
	config.mode = HarmonizerMappingMode::Nearest;
	config.tightness = HarmonizerTightness::Strict;

	CHECK_EQUAL(E4, harmonize(Eb4, cs, -1, false, config));
	CHECK_EQUAL(E4, harmonize(F4, cs, -1, false, config));
	CHECK_EQUAL(G4, harmonize(A4, cs, -1, false, config));
	CHECK_EQUAL(C5, harmonize(B4, cs, -1, false, config));
	// Chord tone passes through
	CHECK_EQUAL(C4, harmonize(C4, cs, -1, false, config));
}

TEST(HarmonizeTarget, scale) {
	ChordState cs = makeCMajorChord();
	HarmonizeConfig config;
	config.mode = HarmonizerMappingMode::Nearest;
	config.tightness = HarmonizerTightness::Scale;
	config.scaleRoot = kScaleRootC;
	config.scaleBits = kCMajorBits;

	// Eb is off-scale → snaps to E
	CHECK_EQUAL(E4, harmonize(Eb4, cs, -1, false, config));
	// F is on-scale → passes through
	CHECK_EQUAL(F4, harmonize(F4, cs, -1, false, config));
	// A is on-scale → passes through
	CHECK_EQUAL(A4, harmonize(A4, cs, -1, false, config));
	// B is on-scale → passes through
	CHECK_EQUAL(B4, harmonize(B4, cs, -1, false, config));
}

TEST(HarmonizeTarget, extensions) {
	ChordState cs = makeCMajorChord();
	HarmonizeConfig config;
	config.mode = HarmonizerMappingMode::Nearest;
	config.tightness = HarmonizerTightness::Extensions;

	// Eb (3): not a chord tone, not an avoid note → passes as color tone
	CHECK_EQUAL(Eb4, harmonize(Eb4, cs, -1, false, config));
	// F (5): avoid note (half-step above E=4) → snaps to E
	CHECK_EQUAL(E4, harmonize(F4, cs, -1, false, config));
	// A (9): not an avoid note → passes
	CHECK_EQUAL(A4, harmonize(A4, cs, -1, false, config));
	// B (11): not an avoid note → passes
	CHECK_EQUAL(B4, harmonize(B4, cs, -1, false, config));
}

TEST(HarmonizeTarget, loose) {
	ChordState cs = makeCMajorChord();
	HarmonizeConfig config;
	config.mode = HarmonizerMappingMode::Nearest;
	config.tightness = HarmonizerTightness::Loose;

	// Eb (63): 1 semi from E(64) → snaps
	CHECK_EQUAL(E4, harmonize(Eb4, cs, -1, false, config));
	// F (65): 1 semi from E(64) → snaps
	CHECK_EQUAL(E4, harmonize(F4, cs, -1, false, config));
	// A (69): 2 semi from G(67) → passes through
	CHECK_EQUAL(A4, harmonize(A4, cs, -1, false, config));
	// B (71): 1 semi from C(72) → snaps
	CHECK_EQUAL(C5, harmonize(B4, cs, -1, false, config));
}

TEST(HarmonizeTarget, transpose) {
	ChordState cs = makeCMajorChord();
	HarmonizeConfig config;
	config.mode = HarmonizerMappingMode::Nearest;
	config.tightness = HarmonizerTightness::Strict;
	config.transpose = 12; // +1 octave

	// C4 → C4 (chord tone) + 12 = C5
	CHECK_EQUAL(C5, harmonize(C4, cs, -1, false, config));
}

TEST(HarmonizeTarget, emptyChordWithTranspose) {
	ChordState empty;
	HarmonizeConfig config;
	config.transpose = 5;

	// No chord → input + transpose
	CHECK_EQUAL(F4, harmonize(C4, empty, -1, false, config));
}

TEST(HarmonizeTarget, negativeTranspose) {
	ChordState cs = makeCMajorChord();
	HarmonizeConfig config;
	config.mode = HarmonizerMappingMode::Nearest;
	config.tightness = HarmonizerTightness::Strict;
	config.transpose = -12; // -1 octave

	// E4(64) → E4 (chord tone) - 12 = E3(52)
	CHECK_EQUAL(52, harmonize(E4, cs, -1, false, config));
}

TEST(HarmonizeTarget, transposeClampsAt127) {
	ChordState cs;
	cs.noteOn(120); // High note chord
	HarmonizeConfig config;
	config.mode = HarmonizerMappingMode::Nearest;
	config.tightness = HarmonizerTightness::Strict;
	config.transpose = 24;

	// 120 + 24 = 144 > 127 → clamped to 127
	CHECK_EQUAL(127, harmonize(120, cs, -1, false, config));
}

TEST(HarmonizeTarget, transposeClampsAt0) {
	ChordState cs;
	cs.noteOn(5);
	HarmonizeConfig config;
	config.mode = HarmonizerMappingMode::Nearest;
	config.tightness = HarmonizerTightness::Strict;
	config.transpose = -24;

	// 5 - 24 = -19 < 0 → clamped to 0
	CHECK_EQUAL(0, harmonize(5, cs, -1, false, config));
}

// --- Extensions with minor chord ---

TEST(HarmonizeTarget, extensionsMinorChord) {
	ChordState cs = makeCMinorChord(); // C(0), Eb(3), G(7)
	HarmonizeConfig config;
	config.mode = HarmonizerMappingMode::Nearest;
	config.tightness = HarmonizerTightness::Extensions;

	// D(2): not chord tone, not avoid. Half-step above C(0)? No, C+1=Db(1)≠D(2). Pass.
	CHECK_EQUAL(D4, harmonize(D4, cs, -1, false, config));
	// E(4): avoid note (half-step above Eb=3). Should snap.
	CHECK_EQUAL(Eb4, harmonize(E4, cs, -1, false, config));
	// Ab(8): avoid note (half-step above G=7). Should snap.
	CHECK_EQUAL(G4, harmonize(Ab4, cs, -1, false, config));
	// A(9): not an avoid note → passes
	CHECK_EQUAL(A4, harmonize(A4, cs, -1, false, config));
}

// --- Loose with a chord tone input ---

TEST(HarmonizeTarget, loosePassesChordTones) {
	ChordState cs = makeCMajorChord();
	HarmonizeConfig config;
	config.mode = HarmonizerMappingMode::Nearest;
	config.tightness = HarmonizerTightness::Loose;

	// Chord tones should pass through untouched
	CHECK_EQUAL(C4, harmonize(C4, cs, -1, false, config));
	CHECK_EQUAL(E4, harmonize(E4, cs, -1, false, config));
	CHECK_EQUAL(G4, harmonize(G4, cs, -1, false, config));
}

// --- Scale target with non-C root ---

TEST(HarmonizeTarget, scaleNonCRoot) {
	ChordState cs;
	cs.noteOn(D4); // 62
	cs.noteOn(F4); // 65
	cs.noteOn(A4); // 69
	// D minor chord, D minor scale
	HarmonizeConfig config;
	config.mode = HarmonizerMappingMode::Nearest;
	config.tightness = HarmonizerTightness::Scale;
	config.scaleRoot = kScaleRootD;
	config.scaleBits = kDMinorBits;

	// C# (61) is off-scale in D minor → should snap
	uint8_t result = harmonize(61, cs, -1, false, config);
	CHECK(result != 61); // Must have snapped
	// G (67) is on-scale in D minor → passes through
	CHECK_EQUAL(G4, harmonize(G4, cs, -1, false, config));
	// Bb (70) is on-scale in D minor → passes through
	CHECK_EQUAL(Bb4, harmonize(Bb4, cs, -1, false, config));
}

// --- Strict with 7th chord: more chord tones available ---

TEST(HarmonizeTarget, chordTones7thChord) {
	ChordState cs = makeCDom7Chord(); // C, E, G, Bb
	HarmonizeConfig config;
	config.mode = HarmonizerMappingMode::Nearest;
	config.tightness = HarmonizerTightness::Strict;

	// A4(69): between G4(67) and Bb4(70). Dist: G=2, Bb=1 → Bb
	CHECK_EQUAL(Bb4, harmonize(A4, cs, -1, false, config));
	// F#(66): between E4(64) and G4(67). Dist: E=2, G=1 → G
	CHECK_EQUAL(G4, harmonize(Fsharp4, cs, -1, false, config));
}

// ============================================================
// Voice Leading
// ============================================================

TEST_GROUP(VoiceLeading){};

TEST(VoiceLeading, withoutVoiceLeading) {
	ChordState cs = makeCMajorChord();
	HarmonizeConfig config;
	config.mode = HarmonizerMappingMode::Nearest;
	config.tightness = HarmonizerTightness::Strict;
	config.voiceLeading = false;

	// A4 (69) nearest to G4(67) or C5(72) → G4
	CHECK_EQUAL(G4, harmonize(A4, cs, -1, false, config));
}

TEST(VoiceLeading, withVoiceLeading) {
	ChordState cs = makeCMajorChord();
	HarmonizeConfig config;
	config.mode = HarmonizerMappingMode::Nearest;
	config.tightness = HarmonizerTightness::Strict;
	config.voiceLeading = true;

	// First note: no previous output, should behave like normal nearest
	uint8_t first = harmonize(F4, cs, -1, false, config);
	CHECK_EQUAL(E4, first); // F→E (nearest)

	// Second note: previous output was E4(64).
	// Voice leading uses score = distToInput*2 + distToPrev.
	// A4(69) with lastOutput=E4(64):
	//   E4(64): distInput=5, distPrev=0, score=10
	//   G4(67): distInput=2, distPrev=3, score=7  ← lowest
	//   C5(72): distInput=3, distPrev=8, score=14
	// G4 wins with the best balance of proximity to input and previous output.
	uint8_t second = harmonize(A4, cs, static_cast<int32_t>(first), true, config);
	CHECK_EQUAL(G4, second);
}

TEST(VoiceLeading, threeNoteSequence) {
	ChordState cs = makeCMajorChord();
	HarmonizeConfig config;
	config.mode = HarmonizerMappingMode::Nearest;
	config.tightness = HarmonizerTightness::Strict;
	config.voiceLeading = true;

	// Note 1: D4(62) → nearest chord tone = C4(60), no previous
	uint8_t n1 = harmonize(D4, cs, -1, false, config);
	CHECK_EQUAL(C4, n1);

	// Note 2: F4(65) with lastOutput=C4(60)
	// Candidates near F4(±7): C4(60)=5, E4(64)=1, G4(67)=2, C5(72)=7
	// Scores (distInput*2 + distPrev):
	//   C4: 5*2+0=10, E4: 1*2+4=6, G4: 2*2+7=11, C5: 7*2+12=26
	// E4 wins
	uint8_t n2 = harmonize(F4, cs, static_cast<int32_t>(n1), true, config);
	CHECK_EQUAL(E4, n2);

	// Note 3: B4(71) with lastOutput=E4(64)
	// Candidates near B4(±7): E4(64)=7, G4(67)=4, C5(72)=1, E5(76)=5
	// Scores:
	//   E4: 7*2+0=14, G4: 4*2+3=11, C5: 1*2+8=10, E5: 5*2+12=22
	// C5 wins
	uint8_t n3 = harmonize(B4, cs, static_cast<int32_t>(n2), true, config);
	CHECK_EQUAL(C5, n3);
}

TEST(VoiceLeading, withLooseTarget) {
	ChordState cs = makeCMajorChord();
	HarmonizeConfig config;
	config.mode = HarmonizerMappingMode::Nearest;
	config.tightness = HarmonizerTightness::Loose;
	config.voiceLeading = true;

	// A4(69) is 2 semi from G4 — not within 1 semi, so Loose passes it through
	CHECK_EQUAL(A4, harmonize(A4, cs, static_cast<int32_t>(E4), true, config));
	// F4(65) is 1 semi from E4 — within range, so it snaps using voice leading
	uint8_t result = harmonize(F4, cs, static_cast<int32_t>(E4), true, config);
	CHECK(result == C4 || result == E4 || result == G4); // Must be a chord tone
}

// ============================================================
// computeDiatonicInterval
// ============================================================

TEST_GROUP(DiatonicInterval){};

TEST(DiatonicInterval, offReturnsNegative) {
	int32_t result = computeDiatonicInterval(C4, DiatonicInterval::Off, kScaleRootC, kCMajorBits);
	CHECK_EQUAL(-1, result);
}

TEST(DiatonicInterval, noScaleReturnsNegative) {
	int32_t result = computeDiatonicInterval(C4, DiatonicInterval::Third_Above, kScaleRootC, 0);
	CHECK_EQUAL(-1, result);
}

TEST(DiatonicInterval, thirdAbove) {
	// C4 + diatonic 3rd in C major = E4 (2 scale steps: C→D→E)
	int32_t result = computeDiatonicInterval(C4, DiatonicInterval::Third_Above, kScaleRootC, kCMajorBits);
	CHECK_EQUAL(E4, result);
}

TEST(DiatonicInterval, thirdBelow) {
	// E4 - diatonic 3rd in C major = C4 (2 scale steps down: E→D→C)
	int32_t result = computeDiatonicInterval(E4, DiatonicInterval::Third_Below, kScaleRootC, kCMajorBits);
	CHECK_EQUAL(C4, result);
}

TEST(DiatonicInterval, sixthAbove) {
	// C4 + diatonic 6th in C major = A4 (5 scale steps: C→D→E→F→G→A)
	int32_t result = computeDiatonicInterval(C4, DiatonicInterval::Sixth_Above, kScaleRootC, kCMajorBits);
	CHECK_EQUAL(A4, result);
}

TEST(DiatonicInterval, octaveAbove) {
	int32_t result = computeDiatonicInterval(C4, DiatonicInterval::Octave_Above, kScaleRootC, kCMajorBits);
	CHECK_EQUAL(C5, result);
}

TEST(DiatonicInterval, octaveOverflow) {
	// Note 120 + 12 = 132 > 127 → should return -1
	int32_t result = computeDiatonicInterval(120, DiatonicInterval::Octave_Above, kScaleRootC, kCMajorBits);
	CHECK_EQUAL(-1, result);
}

TEST(DiatonicInterval, sixthBelow) {
	// C4 - diatonic 6th in C major = E3 (5 scale steps down: C→B→A→G→F→E)
	int32_t result = computeDiatonicInterval(C4, DiatonicInterval::Sixth_Below, kScaleRootC, kCMajorBits);
	CHECK_EQUAL(E1 + 24, result); // E3 = 52
}

TEST(DiatonicInterval, thirdBelowNearBottom) {
	// E1(28) - diatonic 3rd in C major = C1(24) (2 scale steps down: E→D→C)
	int32_t result = computeDiatonicInterval(E1, DiatonicInterval::Third_Below, kScaleRootC, kCMajorBits);
	CHECK_EQUAL(C1, result);
}

TEST(DiatonicInterval, thirdBelowUnderflow) {
	// C1(24) - diatonic 3rd in C major would go to A0(21). Should work.
	int32_t result = computeDiatonicInterval(C1, DiatonicInterval::Third_Below, kScaleRootC, kCMajorBits);
	CHECK(result >= 0);      // Should find A0
	CHECK_EQUAL(21, result); // A0
}

TEST(DiatonicInterval, thirdBelowFromLowestNote) {
	// Note 0 (C0) - diatonic 3rd: would need A-1 which doesn't exist → -1
	int32_t result = computeDiatonicInterval(0, DiatonicInterval::Third_Below, kScaleRootC, kCMajorBits);
	CHECK_EQUAL(-1, result);
}

TEST(DiatonicInterval, nonCRootScale) {
	// D4(62) + diatonic 3rd in D minor = F4(65) (2 scale steps: D→E→F)
	int32_t result = computeDiatonicInterval(D4, DiatonicInterval::Third_Above, kScaleRootD, kDMinorBits);
	CHECK_EQUAL(F4, result);
}

TEST(DiatonicInterval, offScaleInputNote) {
	// C#4 (61) is not in C major scale. computeDiatonicInterval finds nearest scale degree.
	// Nearest to 61 is C4(60). 3rd above C4 in C major = E4(64)
	int32_t result = computeDiatonicInterval(61, DiatonicInterval::Third_Above, kScaleRootC, kCMajorBits);
	CHECK(result >= 0); // Should still produce a valid result
}

// ============================================================
// HarmonizerState
// ============================================================

TEST_GROUP(HarmonizerStateTests){};

TEST(HarmonizerStateTests, resetClearsEverything) {
	HarmonizerState state;
	state.chordState.noteOn(C4);
	state.chordState.noteOn(E4);
	state.channelStates[0].setMapping(60, 64, 100);
	state.channelStates[5].setIntervalMapping(60, 76, 80);
	state.physicallyHeldCount_ = 3;

	state.reset();

	CHECK(state.chordState.isEmpty());
	CHECK(!state.channelStates[0].getMapping(60).active);
	CHECK(!state.channelStates[5].getIntervalMapping(60).active);
	CHECK_EQUAL(0, state.physicallyHeldCount_);
}
