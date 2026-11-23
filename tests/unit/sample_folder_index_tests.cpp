#include "CppUTest/TestHarness.h"
#include "storage/audio/sample_folder_index.h"

// SampleFolderIndex only looks up names it matches just the way FatFs does. Any other name has to be left to FatFs,
// or the index could find a different file than f_open() would, or one f_open() would refuse.

TEST_GROUP(SampleFolderIndexTests){};

TEST(SampleFolderIndexTests, ordinarySampleNamesAreLookedUp) {
	CHECK(SampleFolderIndex::canLookUp("C3.wav"));
	CHECK(SampleFolderIndex::canLookUp("Exit Summer Pad C#3 v127 (rr2).WAV"));
	CHECK(SampleFolderIndex::canLookUp("PIANO~12.WAV")); // A short name
	CHECK(SampleFolderIndex::canLookUp("x"));
	CHECK(SampleFolderIndex::canLookUp("a+b,c;d=e[f]g.aif")); // Not allowed in short names, but in long ones
}

TEST(SampleFolderIndexTests, namesFatFsTrimsAreLeftToIt) {
	// FatFs ignores dots and spaces at the end of a name, so "C3.wav." opens "C3.wav"
	CHECK_FALSE(SampleFolderIndex::canLookUp("C3.wav."));
	CHECK_FALSE(SampleFolderIndex::canLookUp("C3.wav "));
	CHECK_FALSE(SampleFolderIndex::canLookUp("."));
	CHECK_FALSE(SampleFolderIndex::canLookUp(""));
}

TEST(SampleFolderIndexTests, namesNotInAsciiAreLeftToFatFs) {
	// FatFs ignores the case of these letters too, which plain strcasecmp() doesn't
	CHECK_FALSE(SampleFolderIndex::canLookUp("Caf\x82.wav"));            // e acute in code page 437
	CHECK_FALSE(SampleFolderIndex::canLookUp("Fortepian \xC4\x85.wav")); // a with ogonek in UTF-8
	CHECK_FALSE(SampleFolderIndex::canLookUp("tab\there.wav"));
	CHECK_FALSE(SampleFolderIndex::canLookUp("delete\x7F.wav"));
}

TEST(SampleFolderIndexTests, namesFatFsRefusesAreLeftToIt) {
	for (char const* name : {"a\\b.wav", "a*.wav", "a:b.wav", "a<b.wav", "a>b.wav", "a|b.wav", "a\"b.wav", "?"}) {
		CHECK_FALSE_TEXT(SampleFolderIndex::canLookUp(name), name);
	}
}
