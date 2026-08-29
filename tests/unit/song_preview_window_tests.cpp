#include "CppUTest/TestHarness.h"
#include "storage/song_preview.h"

#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace {

/// Where the `preview` attribute's payload ENDS: the offset one past its closing quote, or -1 if the
/// file has no preview attribute.
///
/// The end, not the start, is what the window has to contain. A decode reads `kSongPreviewWindow`
/// bytes and walks them; a preview whose payload begins inside the window but runs past its far edge
/// is truncated, and truncation is exactly what this gate exists to catch. Asserting on the START
/// passes a song whose ~870-byte payload begins at 4090 and runs to 4960 -- blank artwork, gate green.
///
/// The attribute is found by name rather than by the substring "preview", which also matches
/// `previewNumPads` -- the attribute that PRECEDES it in every song, 22 bytes earlier, so a gate
/// keyed on it measures the wrong thing by a margin that happens to be small today.
///
/// Both spellings are handled from one scan because the two writers differ only in punctuation:
/// XML writes `preview="0000..."` and JSON writes `"preview":"0000..."`. After the name, both run a
/// short stretch of quote/colon/equals/whitespace and then open the value with a quote; the LAST
/// quote in that stretch is the opening one, and the next quote after it closes the payload.
long previewPayloadEnd(const std::filesystem::path& path) {
	std::FILE* f = std::fopen(path.c_str(), "rb");
	if (f == nullptr) {
		return -1;
	}
	// The WHOLE file, not a fixed head of it. A bounded read makes "no preview here" mean "no preview
	// in the first N bytes", which turns the skip below into a second way for this gate to pass
	// without measuring anything -- the very failure it is built to report. Corpus songs are ~100 KB.
	std::vector<char> buf;
	char chunk[65536];
	while (const size_t got = std::fread(chunk, 1, sizeof(chunk), f)) {
		buf.insert(buf.end(), chunk, chunk + got);
	}
	std::fclose(f);
	const std::string_view hay(buf.data(), buf.size());

	size_t at = std::string_view::npos;
	for (size_t from = 0; (at = hay.find("preview", from)) != std::string_view::npos; from = at + 7) {
		if (hay.compare(at + 7, 7, "NumPads") != 0) {
			break; // the preview attribute itself, not the pad count that precedes it
		}
	}
	if (at == std::string_view::npos) {
		return -1;
	}

	constexpr std::string_view kSeparators = "\":= \t\r\n";
	size_t openQuote = std::string_view::npos;
	for (size_t i = at + 7; i < hay.size() && kSeparators.find(hay[i]) != std::string_view::npos; i++) {
		if (hay[i] == '"') {
			openQuote = i;
		}
	}
	if (openQuote == std::string_view::npos) {
		return -1;
	}
	const size_t closeQuote = hay.find('"', openQuote + 1);
	if (closeQuote == std::string_view::npos) {
		return -1;
	}
	return static_cast<long>(closeQuote + 1);
}

} // namespace

TEST_GROUP(SongPreviewWindow){};

/// Every song that HAS a preview must carry the WHOLE of it inside the window, or the async path
/// would render blank where the synchronous one rendered artwork. Songs with no preview are skipped,
/// not failed: they render blank today and this rung does not change that.
TEST(SongPreviewWindow, everyCorpusSongCarriesItsPreviewInsideTheWindow) {
	const char* corpus = std::getenv("DELUGE_SONG_CORPUS");
	if (corpus == nullptr) {
		return; // corpus not mounted in this environment
	}
	int32_t checked = 0;
	for (const auto& entry : std::filesystem::recursive_directory_iterator(corpus)) {
		if (!entry.is_regular_file()) {
			continue;
		}
		const auto ext = entry.path().extension().string();
		if (ext != ".XML" && ext != ".xml" && ext != ".Json") {
			continue;
		}
		const long end = previewPayloadEnd(entry.path());
		if (end < 0) {
			continue; // no preview in this song
		}
		checked++;
		CHECK_TEXT(static_cast<uint32_t>(end) <= deluge::storage::kSongPreviewWindow, entry.path().c_str());
	}
	CHECK_TEXT(checked > 0, "corpus mounted but no song with a preview was examined");
}

/// The committed JSON fixture pins the format the corpus cannot: the corpus is entirely XML.
TEST(SongPreviewWindow, theJsonFixtureCarriesItsPreviewInsideTheWindow) {
	const long end = previewPayloadEnd("tests/data/songs/JsonPreview.Json");
	CHECK_TEXT(end >= 0, "fixture must contain a preview");
	CHECK(static_cast<uint32_t>(end) <= deluge::storage::kSongPreviewWindow);
}
