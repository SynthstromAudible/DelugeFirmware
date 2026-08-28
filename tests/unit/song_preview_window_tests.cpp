#include "CppUTest/TestHarness.h"
#include "storage/song_preview.h"

#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <string>
#include <vector>

namespace {

/// Byte offset of the first "preview" occurrence, or -1 if the file has none.
long previewOffset(const std::filesystem::path& path) {
	std::FILE* f = std::fopen(path.c_str(), "rb");
	if (!f) {
		return -1;
	}
	std::vector<char> buf(64 * 1024);
	const size_t got = std::fread(buf.data(), 1, buf.size(), f);
	std::fclose(f);
	const std::string_view hay(buf.data(), got);
	const size_t at = hay.find("preview");
	return at == std::string_view::npos ? -1 : static_cast<long>(at);
}

} // namespace

TEST_GROUP(SongPreviewWindow){};

/// Every song that HAS a preview must carry it inside the window, or the async path would render
/// blank where the synchronous one rendered artwork. Songs with no preview are skipped, not failed:
/// they render blank today and this rung does not change that.
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
		const long at = previewOffset(entry.path());
		if (at < 0) {
			continue; // no preview in this song
		}
		checked++;
		CHECK_TEXT(static_cast<uint32_t>(at) < deluge::storage::kSongPreviewWindow, entry.path().c_str());
	}
	CHECK_TEXT(checked > 0, "corpus mounted but no song with a preview was examined");
}

/// The committed JSON fixture pins the format the corpus cannot: the corpus is entirely XML.
TEST(SongPreviewWindow, theJsonFixtureCarriesItsPreviewInsideTheWindow) {
	const long at = previewOffset("tests/data/songs/JsonPreview.Json");
	CHECK_TEXT(at >= 0, "fixture must contain a preview");
	CHECK(static_cast<uint32_t>(at) < deluge::storage::kSongPreviewWindow);
}
