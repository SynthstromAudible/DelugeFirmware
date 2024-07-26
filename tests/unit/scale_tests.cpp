#include "CppUTest/TestHarness.h"
#include "model/scale/musical_key.h"
#include "model/scale/note_set.h"
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
	NoteSet notes = NoteSet({1, 4, 11});
	CHECK_EQUAL(3, notes.count());
	CHECK_EQUAL(1, notes[0]);
	CHECK_EQUAL(4, notes[1]);
	CHECK_EQUAL(11, notes[2]);
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
