#include "CppUTest/TestHarness.h"
#include "gui/ui/browser/default_name.h"
#include "util/name_compare.h"
#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

using deluge::gui::browser::FileListView;
using deluge::gui::browser::nextDefaultName;

namespace {
/// Stands in for the card. contains() can be narrowed to a window, the way Browser::fileItems is; highestNumberedName()
/// always sees the whole folder, the way the firmware's directory scan does.
class FakeFileList : public FileListView {
public:
	explicit FakeFileList(std::vector<std::string> names) : names_{std::move(names)} {}

	/// A folder whose file list, as contains() sees it, holds only the entries within `radius` sort positions of
	/// `centre` - what the browser's 20-entry window leaves behind on a card with plenty of songs on it.
	static FakeFileList windowed(std::vector<std::string> names, std::string const& centre, size_t radius) {
		FakeFileList list{std::move(names)};
		std::vector<std::string> sorted = list.names_;
		std::sort(sorted.begin(), sorted.end(),
		          [](std::string const& a, std::string const& b) { return strcmpspecial(a.c_str(), b.c_str()) < 0; });
		auto at = std::find(sorted.begin(), sorted.end(), centre);
		CHECK(at != sorted.end());
		size_t centreIndex = at - sorted.begin();
		for (size_t i = 0; i < sorted.size(); i++) {
			size_t distance = i > centreIndex ? i - centreIndex : centreIndex - i;
			if (distance <= radius) {
				list.window_.push_back(sorted[i]);
			}
		}
		list.windowed_ = true;
		return list;
	}

	bool contains(char const* nameWithExtension) const override {
		for (auto const& n : (windowed_ ? window_ : names_)) {
			if (strcmpspecial(n.c_str(), nameWithExtension) == 0) {
				return true;
			}
		}
		return false;
	}

	std::string highestNumberedName(char const* prefix) const override {
		std::string highest;
		size_t prefixLength = strlen(prefix);
		for (auto const& n : names_) {
			if (n.size() <= prefixLength || strncasecmp(n.c_str(), prefix, prefixLength) != 0) {
				continue;
			}
			// Mirrors Browser::highestNumberedFileName: digits must reach the extension, and the extension must be
			// one the browser lists. Diverging here would let a test pass on a name the firmware would skip.
			size_t pos = prefixLength;
			while (pos < n.size() && std::isdigit(static_cast<unsigned char>(n[pos]))) {
				pos++;
			}
			if (pos == prefixLength || (pos != n.size() && n[pos] != '.')) {
				continue;
			}
			if (!hasListedExtension(n)) {
				continue;
			}
			if (highest.empty() || strcmpspecial(n.c_str(), highest.c_str()) > 0) {
				highest = n;
			}
		}
		return highest;
	}

private:
	/// allowedFileExtensionsXML, as the song and preset browsers set it.
	static bool hasListedExtension(std::string const& name) {
		size_t dot = name.rfind('.');
		if (dot == std::string::npos) {
			return false;
		}
		std::string extension = name.substr(dot + 1);
		return strcasecmp(extension.c_str(), "XML") == 0 || strcasecmp(extension.c_str(), "Json") == 0;
	}

	std::vector<std::string> names_;
	std::vector<std::string> window_;
	bool windowed_ = false;
};

/// A card holding SONG001.XML .. SONGnnn.XML, as a vanilla card looks.
FakeFileList cardWithSongsUpTo(int32_t highest) {
	std::vector<std::string> names;
	for (int32_t i = 1; i <= highest; i++) {
		char buf[32];
		snprintf(buf, sizeof buf, "SONG%03d.XML", i);
		names.emplace_back(buf);
	}
	return FakeFileList{std::move(names)};
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
	CHECK_EQUAL(std::string{"SONG185A"}, nextDefaultName("SONG185", "SONG", files));
}

TEST(DefaultNameTests, issue1069_neverEmitsTheSlotNumberAsASuffix) {
	// The bug produced "185_186": the slot number leaking in through a wrong string offset.
	auto files = cardWithSongsUpTo(185);
	std::string result = nextDefaultName("SONG185", "SONG", files);
	CHECK(result.find("186") == std::string::npos);
	CHECK(result.find('_') == std::string::npos);
}

TEST(DefaultNameTests, letterSuffixesAdvance) {
	FakeFileList files{{"SONG185.XML", "SONG185A.XML", "SONG185B.XML"}};
	CHECK_EQUAL(std::string{"SONG185C"}, nextDefaultName("SONG185", "SONG", files));
	CHECK_EQUAL(std::string{"SONG185C"}, nextDefaultName("SONG185B", "SONG", files));
}

TEST(DefaultNameTests, letterSuffixFillsTheFirstGap) {
	FakeFileList files{{"SONG185.XML", "SONG185A.XML", "SONG185C.XML"}};
	CHECK_EQUAL(std::string{"SONG185B"}, nextDefaultName("SONG185", "SONG", files));
}

TEST(DefaultNameTests, letterSuffixExhaustedReturnsInputUnchanged) {
	std::vector<std::string> names{"SONG185.XML"};
	for (char c = 'A'; c <= 'Z'; c++) {
		names.push_back(std::string{"SONG185"} + c + ".XML");
	}
	FakeFileList files{std::move(names)};
	CHECK_EQUAL(std::string{"SONG185"}, nextDefaultName("SONG185", "SONG", files));
}

// --- Behaviour that must be preserved (this is what OLED does today) ---------------------

TEST(DefaultNameTests, renamedSongGetsANumericSuffix) {
	FakeFileList files{{"MYTRACK.XML"}};
	CHECK_EQUAL(std::string{"MYTRACK 2"}, nextDefaultName("MYTRACK", "SONG", files));
}

TEST(DefaultNameTests, numericSuffixAdvances) {
	FakeFileList files{{"MYTRACK.XML", "MYTRACK 2.XML", "MYTRACK 3.XML"}};
	CHECK_EQUAL(std::string{"MYTRACK 4"}, nextDefaultName("MYTRACK", "SONG", files));
}

// --- The file list is a window, so candidates must not be probed from one end -------------

TEST(DefaultNameTests, numericSuffixAdvancesEvenWhenLowSiblingsAreOutsideTheFileListWindow) {
	// The reported bug: a song saved as "<name> 11" proposed "<name> 2" and offered to overwrite the real second
	// file. Nothing is wrong with the folder - it is contains() that cannot see that far. Browser::fileItems keeps
	// only ~8 entries either side of the file being saved, and by 11 the "2" has dropped out of it, so probing
	// upwards from 2 believed it was free. The break lands at exactly 11 on any card with more than 20 songs.
	std::vector<std::string> folder;
	for (int32_t i = 1; i <= 11; i++) {
		folder.push_back("MY PROJECT " + std::to_string(i) + ".XML");
	}
	auto files = FakeFileList::windowed(folder, "MY PROJECT 11.XML", 8);
	CHECK(!files.contains("MY PROJECT 2.XML")); // The window really does hide it.
	CHECK_EQUAL(std::string{"MY PROJECT 12"}, nextDefaultName("MY PROJECT 11", "SONG", files));
}

TEST(DefaultNameTests, numericSuffixNeverGoesBelowTheNameBeingSavedOver) {
	// If the folder scan cannot answer - a card error, say - the name we are saving over is still known to exist, so
	// its own number floors the result. Without this floor an unanswered scan would send "MY PROJECT 11" back to 2.
	class SilentFileList : public FileListView {
		bool contains(char const*) const override { return false; }
		std::string highestNumberedName(char const*) const override { return {}; }
	} files;
	CHECK_EQUAL(std::string{"MY PROJECT 12"}, nextDefaultName("MY PROJECT 11", "SONG", files));
}

TEST(DefaultNameTests, numericSuffixCountsFromTheHighestSiblingNotTheLowestGap) {
	// Gaps are left alone: a deleted "MYTRACK 3" must not be handed back to the next save, which would quietly
	// overwrite it if the user restored it. This also keeps the answer independent of how much of the folder is in
	// the window.
	FakeFileList files{{"MYTRACK.XML", "MYTRACK 2.XML", "MYTRACK 5.XML"}};
	CHECK_EQUAL(std::string{"MYTRACK 6"}, nextDefaultName("MYTRACK 2", "SONG", files));
}

TEST(DefaultNameTests, numericSuffixIgnoresSiblingsThatOnlyLookNumbered) {
	// "MYTRACK NOTES.XML" shares the prefix but carries no number, so it must not be read as one.
	FakeFileList files{{"MYTRACK.XML", "MYTRACK NOTES.XML"}};
	CHECK_EQUAL(std::string{"MYTRACK 2"}, nextDefaultName("MYTRACK", "SONG", files));
}

TEST(DefaultNameTests, numericSuffixIgnoresDateSuffixedNeighbours) {
	// "JAM 2024-01-05.XML" starts with "JAM " and a digit, but its number does not run to the extension, so it is a
	// neighbour and not a member of the "JAM " family. Counted as one it would read as 2024 and send the next save
	// to "JAM 2025". Date-suffixed names are common on real cards.
	FakeFileList files{{"JAM 3.XML", "JAM 2024-01-05.XML"}};
	CHECK_EQUAL(std::string{"JAM 4"}, nextDefaultName("JAM 3", "SONG", files));
}

TEST(DefaultNameTests, numericSuffixIgnoresFilesTheBrowserWouldNotList) {
	// A stray "MYTRACK 99.TXT" is not a song and cannot be overwritten by one, so it must not push the count to 100.
	FakeFileList files{{"MYTRACK 2.XML", "MYTRACK 99.TXT"}};
	CHECK_EQUAL(std::string{"MYTRACK 3"}, nextDefaultName("MYTRACK 2", "SONG", files));
}

TEST(DefaultNameTests, existingUnderscoreDelimiterIsReused) {
	// A name that already carries an "_<number>" suffix keeps the underscore.
	FakeFileList files{{"TRACK_1.XML"}};
	CHECK_EQUAL(std::string{"TRACK_2"}, nextDefaultName("TRACK_1", "SONG", files));
}

TEST(DefaultNameTests, nonNumericNameSharingThePrefixIsNotTreatedAsASlot) {
	// "SONGIDEA" starts with SONG but has no digits - it is just a name.
	FakeFileList files{{"SONGIDEA.XML"}};
	CHECK_EQUAL(std::string{"SONGIDEA 2"}, nextDefaultName("SONGIDEA", "SONG", files));
}

// --- Zero-padded names (vanilla cards write songs 1-99 as SONG001..SONG099) -------------

TEST(DefaultNameTests, zeroPaddedSongGetsALetterSuffix) {
	FakeFileList files{{"SONG001.XML"}};
	CHECK_EQUAL(std::string{"SONG001A"}, nextDefaultName("SONG001", "SONG", files));
}

TEST(DefaultNameTests, zeroPaddedLetterSuffixesAdvance) {
	FakeFileList files{{"SONG001.XML", "SONG001A.XML"}};
	CHECK_EQUAL(std::string{"SONG001B"}, nextDefaultName("SONG001A", "SONG", files));
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
	FakeFileList files{{"SONG185.XML", "SONG185A.Json"}};
	CHECK_EQUAL(std::string{"SONG185B"}, nextDefaultName("SONG185", "SONG", files));
}

TEST(DefaultNameTests, presetsKeepTheNumericSuffix) {
	// Only songs get letter suffixes. Presets pass an empty slotPrefix, so SYNT005 takes the
	// numeric path - preserving current OLED preset behaviour. See Global Constraints.
	FakeFileList files{{"SYNT005.XML"}};
	CHECK_EQUAL(std::string{"SYNT005 2"}, nextDefaultName("SYNT005", "", files));
}
