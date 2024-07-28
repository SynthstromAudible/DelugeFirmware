#include "CppUTest/TestHarness.h"
#include "model/scale/musical_key.h"
#include "model/scale/note_set.h"
#include "model/scale/preset_scales.h"
#include "model/scale/utils.h"

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

TEST(NoteSetTest, applyChanges) {
	NoteSet a;
	a.add(0);
	a.add(2);
	a.add(4);
	a.add(5);
	int8_t changes[12] = {-1, -1, +1, +2, 0, 0, 0, 0, 0, 0, 0, 0};
	a.applyChanges(changes);
	CHECK_EQUAL(0, a[0]);
	CHECK_EQUAL(2, a[1]);
	CHECK_EQUAL(6, a[2]);
	CHECK_EQUAL(8, a[3]);
	CHECK_EQUAL(4, a.count());
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

TEST(NoteSetTest, subscript1) {
	NoteSet a;
	for (int i = 0; i < NoteSet::size; i++) {
		CHECK_EQUAL(0, a[i]);
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

TEST(NoteSetTest, presetScaleId) {
	CHECK_EQUAL(MAJOR_SCALE, presetScaleNotes[MAJOR_SCALE].presetScaleId());
	CHECK_EQUAL(MINOR_SCALE, presetScaleNotes[MINOR_SCALE].presetScaleId());
	CHECK_EQUAL(CUSTOM_SCALE_WITH_MORE_THAN_7_NOTES, NoteSet().presetScaleId());
}

TEST_GROUP(MusicalKeyTest){};

TEST(MusicalKeyTest, ctor) {
	MusicalKey k;
	CHECK_EQUAL(0, k.rootNote);
	CHECK_EQUAL(1, k.modeNotes.count());
	CHECK_EQUAL(true, k.modeNotes.has(0));
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
