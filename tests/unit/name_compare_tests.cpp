#include "CppUTest/TestHarness.h"
#include "util/name_compare.h"

TEST_GROUP(NameCompareTests){void setup() override{shouldInterpretNoteNames = false;
octaveStartsFromA = false;
}
}
;

TEST(NameCompareTests, digitRunsCompareAsNumbers) {
	// The whole point of "special": plain strcmp would put SONG10 before SONG2.
	CHECK(strcmpspecial("SONG2.XML", "SONG10.XML") < 0);
	CHECK(strcmpspecial("SONG10.XML", "SONG2.XML") > 0);
	CHECK_EQUAL(0, strcmpspecial("SONG185.XML", "SONG185.XML"));
}

TEST(NameCompareTests, letterSuffixSortsAfterBareNumber) {
	// Relied on by the subslot probe: SONG185A must sort after SONG185.
	CHECK(strcmpspecial("SONG185.XML", "SONG185A.XML") < 0);
	CHECK(strcmpspecial("SONG185A.XML", "SONG185B.XML") < 0);
}

TEST(NameCompareTests, prefixIsOrderPreserving) {
	// The invariant this whole change rests on: adding the constant "SONG" prefix to every
	// entry does not reorder them relative to each other.
	CHECK(strcmpspecial("1.XML", "2.XML") < 0);
	CHECK(strcmpspecial("SONG1.XML", "SONG2.XML") < 0);
	CHECK(strcmpspecial("2.XML", "10.XML") < 0);
	CHECK(strcmpspecial("SONG2.XML", "SONG10.XML") < 0);
}
