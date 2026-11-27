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

// A folder's block is as big as a Cluster, which depends on how the card was formatted. Entries fill it from the front
// and names from the back.

TEST(SampleFolderIndexTests, spaceFillsFromBothEndsUntilTheyMeet) {
	SampleFolderIndex::Space space{100, 1000};
	size_t entriesEnd = 100;
	size_t namesStart = 1000;
	for (int32_t file = 0; file < 18; file++) { // 900 bytes free, 50 per file
		auto names = space.take(12, 38);
		CHECK(names.has_value());
		entriesEnd += 12;
		namesStart -= 38;
		CHECK_EQUAL(1000 - namesStart, static_cast<size_t>(*names)); // Distance back from the end of the block
		CHECK(entriesEnd <= namesStart);
	}
	CHECK_FALSE(space.take(12, 38).has_value());
	CHECK_FALSE(space.take(1, 0).has_value()); // 900 bytes used exactly
}

TEST(SampleFolderIndexTests, aLongNameCanStillFitAfterOneThatDidNot) {
	SampleFolderIndex::Space space{0, 100};
	CHECK_FALSE(space.take(12, 89).has_value());
	CHECK(space.take(12, 88).has_value());
}

TEST(SampleFolderIndexTests, blockTooSmallForTheFolderHasNoSpace) {
	SampleFolderIndex::Space space{300, 200};
	CHECK_FALSE(space.take(12, 1).has_value());
}

TEST(SampleFolderIndexTests, namesStayWithin16BitsOfTheEnd) {
	// A 64 KiB Cluster plus its header is more than 16 bits can count back
	SampleFolderIndex::Space space{300, 65536 + 112};
	size_t files = 0;
	while (auto names = space.take(12, 35)) {
		CHECK(*names <= UINT16_MAX);
		files++;
	}
	CHECK(files > 1300); // Barely fewer than the block would hold otherwise
}

TEST(SampleFolderIndexTests, everyClusterSizeHoldsSomeFiles) {
	// Typical sample: 12 byte Entry, "Piano C3 v1.wav" and "PIANOC~1.WAV"
	size_t lastFiles = 0;
	for (size_t clusterSize = 512; clusterSize <= 65536; clusterSize *= 2) {
		SampleFolderIndex::Space space{300, clusterSize + 112};
		size_t files = 0;
		while (space.take(12, 16 + 13)) {
			files++;
		}
		CHECK(files > lastFiles);
		lastFiles = files;
	}
}
