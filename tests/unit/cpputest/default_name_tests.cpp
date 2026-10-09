#include "CppUTest/TestHarness.h"
#include "gui/ui/browser/default_name.h"
#include "util/name_compare.h"
#include <cstdio>
#include <string>
#include <string_view>
#include <vector>

using deluge::gui::browser::NameFamilySurvey;
using deluge::gui::browser::nextDefaultName;

namespace {
/// A folder's file names, as the browser's read passes them.
using Names = std::vector<std::string>;

/// What the browser does: survey `folder` for `current`'s family, then name the next variation.
std::string nextIn(std::string_view current, std::string_view slotPrefix, Names const& folder) {
	NameFamilySurvey family{current, slotPrefix};
	for (auto const& name : folder) {
		family.observe(name);
	}
	return nextDefaultName(current, slotPrefix, family);
}

/// A card holding SONG001.XML .. SONGnnn.XML, as a vanilla card looks.
Names cardWithSongsUpTo(int32_t highest) {
	Names names;
	for (int32_t i = 1; i <= highest; i++) {
		char buf[32];
		snprintf(buf, sizeof buf, "SONG%03d.XML", i);
		names.emplace_back(buf);
	}
	return names;
}
} // namespace

TEST_GROUP(DefaultNameTests){void setup() override{shouldInterpretNoteNames = false;
octaveStartsFromA = false;
}
}
;

// --- Issue #1069: the actual regression -------------------------------------------------

TEST(DefaultNameTests, issue1069_defaultNamedSongGetsALetterSuffix) {
	auto files = cardWithSongsUpTo(185);
	CHECK_EQUAL(std::string{"SONG185A"}, nextIn("SONG185", "SONG", files));
}

TEST(DefaultNameTests, issue1069_neverEmitsTheSlotNumberAsASuffix) {
	// The bug produced "185_186": the slot number leaking in through a wrong string offset.
	auto files = cardWithSongsUpTo(185);
	std::string result = nextIn("SONG185", "SONG", files);
	CHECK(result.find("186") == std::string::npos);
	CHECK(result.find('_') == std::string::npos);
}

TEST(DefaultNameTests, letterSuffixesAdvance) {
	Names files{"SONG185.XML", "SONG185A.XML", "SONG185B.XML"};
	CHECK_EQUAL(std::string{"SONG185C"}, nextIn("SONG185", "SONG", files));
	CHECK_EQUAL(std::string{"SONG185C"}, nextIn("SONG185B", "SONG", files));
}

TEST(DefaultNameTests, letterSuffixFillsTheFirstGap) {
	Names files{"SONG185.XML", "SONG185A.XML", "SONG185C.XML"};
	CHECK_EQUAL(std::string{"SONG185B"}, nextIn("SONG185", "SONG", files));
}

TEST(DefaultNameTests, letterSuffixExhaustedReturnsInputUnchanged) {
	std::vector<std::string> names{"SONG185.XML"};
	for (char c = 'A'; c <= 'Z'; c++) {
		names.push_back(std::string{"SONG185"} + c + ".XML");
	}
	Names files{std::move(names)};
	CHECK_EQUAL(std::string{"SONG185"}, nextIn("SONG185", "SONG", files));
}

// --- Behaviour that must be preserved (this is what OLED does today) ---------------------

TEST(DefaultNameTests, renamedSongGetsANumericSuffix) {
	Names files{"MYTRACK.XML"};
	CHECK_EQUAL(std::string{"MYTRACK 2"}, nextIn("MYTRACK", "SONG", files));
}

TEST(DefaultNameTests, numericSuffixAdvances) {
	Names files{"MYTRACK.XML", "MYTRACK 2.XML", "MYTRACK 3.XML"};
	CHECK_EQUAL(std::string{"MYTRACK 4"}, nextIn("MYTRACK", "SONG", files));
}

// --- Names come from the whole family, never by probing candidates against the listing ----

TEST(DefaultNameTests, numericSuffixAdvancesPastEleven) {
	// The reported symptom: saving over "<name> 11" offered "<name> 2". Probing "<name> 2", "<name> 3", ... against
	// the listing's window got this wrong as soon as the low members sorted out of the window.
	Names folder;
	for (int32_t i = 1; i <= 11; i++) {
		folder.push_back("MY PROJECT " + std::to_string(i) + ".XML");
	}
	CHECK_EQUAL(std::string{"MY PROJECT 12"}, nextIn("MY PROJECT 11", "SONG", folder));
}

TEST(DefaultNameTests, letterSuffixAdvancesPastAWholeAlphabetOfVariations) {
	Names folder{"SONG185.XML"};
	for (char c = 'A'; c <= 'L'; c++) {
		folder.push_back(std::string{"SONG185"} + c + ".XML");
	}
	CHECK_EQUAL(std::string{"SONG185M"}, nextIn("SONG185", "SONG", folder));
}

TEST(DefaultNameTests, letterSuffixesAreCaseInsensitive) {
	// The card may hold either case; they are the same file to the filesystem, so the same variation here.
	Names files{"SONG185.XML", "song185a.xml"};
	CHECK_EQUAL(std::string{"SONG185B"}, nextIn("SONG185", "SONG", files));
}

TEST(DefaultNameTests, aLongerSuffixIsNotALetterVariation) {
	// "SONG185AB" is its own name, not the "A" variation of SONG185.
	Names files{"SONG185.XML", "SONG185AB.XML"};
	CHECK_EQUAL(std::string{"SONG185A"}, nextIn("SONG185", "SONG", files));
}

TEST(DefaultNameTests, numericSuffixNeverGoesBelowTheNameBeingSavedOver) {
	// The name being saved over is on the card, so its own number floors the result even when the survey saw
	// nothing of the family.
	CHECK_EQUAL(std::string{"MY PROJECT 12"}, nextIn("MY PROJECT 11", "SONG", Names{}));
}

TEST(DefaultNameTests, numericSuffixCountsFromTheHighestSiblingNotTheLowestGap) {
	// Gaps are left alone: a deleted "MYTRACK 3" is not handed back to the next save.
	Names files{"MYTRACK.XML", "MYTRACK 2.XML", "MYTRACK 5.XML"};
	CHECK_EQUAL(std::string{"MYTRACK 6"}, nextIn("MYTRACK 2", "SONG", files));
}

TEST(DefaultNameTests, numericSuffixIgnoresSiblingsThatOnlyLookNumbered) {
	Names files{"MYTRACK.XML", "MYTRACK NOTES.XML"};
	CHECK_EQUAL(std::string{"MYTRACK 2"}, nextIn("MYTRACK", "SONG", files));
}

TEST(DefaultNameTests, numericSuffixIgnoresDateSuffixedNeighbours) {
	// "JAM 2024-01-05.XML" starts with "JAM " and a digit, but its number doesn't run to the extension, so it is not
	// in the "JAM " family. Counted as one it would send the next save to "JAM 2025".
	Names files{"JAM 3.XML", "JAM 2024-01-05.XML"};
	CHECK_EQUAL(std::string{"JAM 4"}, nextIn("JAM 3", "SONG", files));
}

TEST(DefaultNameTests, inMemoryNamesWithoutAnExtensionCount) {
	Names files{"MYTRACK.XML", "MYTRACK 4"};
	CHECK_EQUAL(std::string{"MYTRACK 5"}, nextIn("MYTRACK", "", files));
}

TEST(DefaultNameTests, numbersExhaustedReturnsInputUnchanged) {
	Names files{"MYTRACK 9999.XML"};
	CHECK_EQUAL(std::string{"MYTRACK 2"}, nextIn("MYTRACK 2", "SONG", files));
}

TEST(DefaultNameTests, restartForgetsWhatWasObserved) {
	NameFamilySurvey family{"MYTRACK", ""};
	family.observe("MYTRACK 7.XML");
	family.restart();
	family.observe("MYTRACK 2.XML");
	CHECK_EQUAL(std::string{"MYTRACK 3"}, nextDefaultName("MYTRACK", "", family));
}

TEST(DefaultNameTests, existingUnderscoreDelimiterIsReused) {
	// A name that already carries an "_<number>" suffix keeps the underscore.
	Names files{"TRACK_1.XML"};
	CHECK_EQUAL(std::string{"TRACK_2"}, nextIn("TRACK_1", "SONG", files));
}

TEST(DefaultNameTests, nonNumericNameSharingThePrefixIsNotTreatedAsASlot) {
	// "SONGIDEA" starts with SONG but has no digits - it is just a name.
	Names files{"SONGIDEA.XML"};
	CHECK_EQUAL(std::string{"SONGIDEA 2"}, nextIn("SONGIDEA", "SONG", files));
}

// --- Zero-padded names (vanilla cards write songs 1-99 as SONG001..SONG099) -------------

TEST(DefaultNameTests, zeroPaddedSongGetsALetterSuffix) {
	Names files{"SONG001.XML"};
	CHECK_EQUAL(std::string{"SONG001A"}, nextIn("SONG001", "SONG", files));
}

TEST(DefaultNameTests, zeroPaddedLetterSuffixesAdvance) {
	Names files{"SONG001.XML", "SONG001A.XML"};
	CHECK_EQUAL(std::string{"SONG001B"}, nextIn("SONG001A", "SONG", files));
}

TEST(DefaultNameTests, numberPartSkipsZeroPadding) {
	// getSlot() reads a leading '0' as a whole one-digit slot and then chokes on the next digit, so it rejects "001"
	// outright. Everything feeding it must skip the padding first, or 7SEG scrolls "SONG001" instead of showing "1".
	STRCMP_EQUAL("1", deluge::gui::browser::numberPartOf("SONG001", "SONG"));
	STRCMP_EQUAL("10", deluge::gui::browser::numberPartOf("SONG010", "SONG"));
	STRCMP_EQUAL("185", deluge::gui::browser::numberPartOf("SONG185", "SONG"));
	STRCMP_EQUAL("1A", deluge::gui::browser::numberPartOf("SONG001A", "SONG"));
	// The last digit always survives, so slot 0 is still reachable.
	STRCMP_EQUAL("0", deluge::gui::browser::numberPartOf("SONG000", "SONG"));
	STRCMP_EQUAL("0", deluge::gui::browser::numberPartOf("SONG0", "SONG"));
	// A name that does not carry the prefix has no number part.
	CHECK(deluge::gui::browser::numberPartOf("MYTRACK", "SONG") == nullptr);
}

// --- Extensions -------------------------------------------------------------------------

TEST(DefaultNameTests, aJsonFileAlsoTakesTheName) {
	// The browser lists .XML and .Json alike, and saving picks between them via writeJsonFlag. Probing only .XML would
	// hand back SONG185A when SONG185A.Json already exists on the card.
	Names files{"SONG185.XML", "SONG185A.Json"};
	CHECK_EQUAL(std::string{"SONG185B"}, nextIn("SONG185", "SONG", files));
}

TEST(DefaultNameTests, presetsKeepTheNumericSuffix) {
	// Only songs get letter suffixes. Presets pass an empty slotPrefix, so SYNT005 takes the
	// numeric path - preserving current OLED preset behaviour. See Global Constraints.
	Names files{"SYNT005.XML"};
	CHECK_EQUAL(std::string{"SYNT005 2"}, nextIn("SYNT005", "", files));
}

using deluge::gui::browser::nextSlotName;

TEST_GROUP(NextSlotNameTests){};

TEST(NextSlotNameTests, noEntryIsSlotOne) {
	CHECK_EQUAL(std::string{"SYNT1"}, nextSlotName("", "SYNT"));
}

TEST(NextSlotNameTests, followsTheLastNumber) {
	CHECK_EQUAL(std::string{"SYNT7"}, nextSlotName("SYNT6", "SYNT"));
}

TEST(NextSlotNameTests, keepsTheDigitCount) {
	CHECK_EQUAL(std::string{"SYNT008"}, nextSlotName("SYNT007", "SYNT"));
}

TEST(NextSlotNameTests, growsADigit) {
	CHECK_EQUAL(std::string{"SYNT10"}, nextSlotName("SYNT9", "SYNT"));
}

TEST(NextSlotNameTests, nonDigitAfterPrefixIsSlotOne) {
	// Today's parse stops at the first non-digit, having read none: slot 0 + 1, no padding.
	CHECK_EQUAL(std::string{"SYNT1"}, nextSlotName("SYNTHY", "SYNT"));
}

TEST(NextSlotNameTests, digitsThenSuffixUseTheDigits) {
	CHECK_EQUAL(std::string{"SYNT13"}, nextSlotName("SYNT12A", "SYNT"));
}

TEST(NextSlotNameTests, shorterThanPrefixIsSlotOne) {
	// Today's parse indexes past the end of such a name; defined here as "no digits".
	CHECK_EQUAL(std::string{"SYNT1"}, nextSlotName("SY", "SYNT"));
}

TEST(NextSlotNameTests, kitPrefix) {
	CHECK_EQUAL(std::string{"KIT21"}, nextSlotName("KIT20", "KIT"));
}
