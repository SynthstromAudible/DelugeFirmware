#include "CppUTest/TestHarness.h"
#include "mock_file_io.h"
#include "storage/song_preview.h"
#include "storage/storage_manager.h"

#include <array>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <vector>

namespace {

using Row = RGB[kDisplayWidth + kSideBarWidth];

std::vector<uint8_t> prefixOf(const std::filesystem::path& p, uint32_t n) {
	std::vector<uint8_t> buf(n);
	std::FILE* f = std::fopen(p.c_str(), "rb");
	CHECK(f != nullptr);
	const size_t got = std::fread(buf.data(), 1, n, f);
	std::fclose(f);
	buf.resize(got);
	return buf;
}

/// Copy a host-filesystem file into the mock card at the same path, so the file walk can open it
/// through the ordinary deserializer. The mock (tests/spec_io/mock_file_io.cpp) backs deluge::io::File
/// with an in-memory map keyed by path, so any path string will do as a key.
void stageOnCard(const std::filesystem::path& p) {
	std::vector<uint8_t> whole;
	std::FILE* f = std::fopen(p.c_str(), "rb");
	CHECK(f != nullptr);
	std::array<uint8_t, 65536> chunk{};
	while (const size_t got = std::fread(chunk.data(), 1, chunk.size(), f)) {
		whole.insert(whole.end(), chunk.begin(), chunk.begin() + got);
	}
	std::fclose(f);

	uint32_t handle = 0;
	CHECK_EQUAL(DELUGE_OK, deluge_efatfs_file_open(p.c_str(), DELUGE_FILE_WRITE_CREATE, &handle));
	uint32_t written = 0;
	CHECK_EQUAL(DELUGE_OK, deluge_efatfs_file_write(handle, whole.data(), whole.size(), &written));
	CHECK_EQUAL(whole.size(), written);
	deluge_efatfs_file_close(handle);
}

} // namespace

TEST_GROUP(SongPreviewDecode){};

/// The buffer walk must produce the SAME image as the file walk, for every song in the corpus.
///
/// Output equality is sufficient here even though a field-level differential was structurally
/// insufficient for the WAV/AIFF port: there the bugs were about what the walk DID on the way --
/// validations applied in the wrong mode, a shared byte-source cursor left past the audio start. Here
/// the image IS the entire product: nothing downstream reads a cursor the walk left behind, because
/// every open resets its reader before reading a byte.
///
/// Two globals DO survive the call, and this comparison cannot see either: `song_firmware_version`,
/// which every open overwrites from the document it just read, and `activeDeserializer`, left pointing
/// at whichever reader ran. Both are true of the browser's walk today, unchanged by this rung -- but
/// they are the reason "the reader is discarded" would be too strong a claim to rest on.
///
/// What this comparison also cannot catch is a change applied to BOTH walks: they share one decode
/// loop, so nothing here pins the preview's absolute pixel values. That is the differential's job
/// boundary, not a gap in it -- it exists to prove the buffer walk did not diverge from the file walk.
TEST(SongPreviewDecode, theBufferWalkMatchesTheFileWalkAcrossTheCorpus) {
	const char* corpus = std::getenv("DELUGE_SONG_CORPUS");
	if (corpus == nullptr) {
		return;
	}
	int32_t rendered = 0;
	for (const auto& entry : std::filesystem::recursive_directory_iterator(corpus)) {
		if (!entry.is_regular_file()) {
			continue;
		}
		const auto ext = entry.path().extension().string();
		const bool isJson = (ext == ".Json");
		if (ext != ".XML" && ext != ".xml" && !isJson) {
			continue;
		}

		std::array<Row, kDisplayHeight> fromBuffer{};
		std::array<Row, kDisplayHeight> fromFile{};

		auto buf = prefixOf(entry.path(), deluge::storage::kSongPreviewWindow);
		const auto outcome = deluge::storage::decodeSongPreview(buf, isJson, fromBuffer.data());

		// The reference: today's whole-file walk, reading through the ordinary file reader.
		mock_file_io_reset();
		stageOnCard(entry.path());
		const auto reference = deluge::storage::decodeSongPreviewFromFile(entry.path().c_str(), fromFile.data());

		CHECK_TEXT(outcome != deluge::storage::SongPreviewOutcome::Truncated, entry.path().c_str());
		CHECK_EQUAL(static_cast<int>(reference), static_cast<int>(outcome));
		MEMCMP_EQUAL_TEXT(fromFile.data(), fromBuffer.data(), sizeof(fromFile), entry.path().c_str());
		if (outcome == deluge::storage::SongPreviewOutcome::Rendered) {
			rendered++;
		}
	}
	CHECK_TEXT(rendered > 0, "corpus mounted but nothing rendered -- the differential proved nothing");
}

/// The checked-in JSON fixture is the only JSON song there is, so run the differential on it whether or
/// not the corpus is mounted -- otherwise the JSON arm of the buffer walk is never compared to anything.
TEST(SongPreviewDecode, theBufferWalkMatchesTheFileWalkOnTheJsonFixture) {
	std::array<Row, kDisplayHeight> fromBuffer{};
	std::array<Row, kDisplayHeight> fromFile{};

	auto buf = prefixOf("tests/data/songs/JsonPreview.Json", deluge::storage::kSongPreviewWindow);
	const auto outcome = deluge::storage::decodeSongPreview(buf, true, fromBuffer.data());

	mock_file_io_reset();
	stageOnCard("tests/data/songs/JsonPreview.Json");
	const auto reference =
	    deluge::storage::decodeSongPreviewFromFile("tests/data/songs/JsonPreview.Json", fromFile.data());

	CHECK_EQUAL(static_cast<int>(deluge::storage::SongPreviewOutcome::Rendered), static_cast<int>(reference));
	CHECK_EQUAL(static_cast<int>(reference), static_cast<int>(outcome));
	MEMCMP_EQUAL(fromFile.data(), fromBuffer.data(), sizeof(fromFile));
}

/// A prefix that stops before the preview reports Truncated, NOT NoPreview. Conflating them is how
/// artwork disappears silently.
TEST(SongPreviewDecode, aShortPrefixReportsTruncatedRatherThanNoPreview) {
	std::array<Row, kDisplayHeight> image{};
	auto buf = prefixOf("tests/data/songs/JsonPreview.Json", 64);
	CHECK_EQUAL(static_cast<int>(deluge::storage::SongPreviewOutcome::Truncated),
	            static_cast<int>(deluge::storage::decodeSongPreview(buf, true, image.data())));
}

/// A borrowed buffer must not stay reachable through `activeDeserializer` after the call -- on ANY
/// path, the failed open included. Left there, the next thing to write through that global writes
/// Cluster::size bytes into a buffer that is smaller than that and by then usually gone. This asserts
/// the restore rather than trusting a comment, because the caller that arms it is a task away.
TEST(SongPreviewDecode, decodeSongPreviewLeavesTheGlobalDeserializerWhereItFoundIt) {
	std::array<Row, kDisplayHeight> image{};
	FileDeserializer* const before = activeDeserializer;

	// Success path.
	auto whole = prefixOf("tests/data/songs/JsonPreview.Json", deluge::storage::kSongPreviewWindow);
	CHECK_EQUAL(static_cast<int>(deluge::storage::SongPreviewOutcome::Rendered),
	            static_cast<int>(deluge::storage::decodeSongPreview(whole, true, image.data())));
	POINTERS_EQUAL(before, activeDeserializer);

	// Truncated path.
	auto shortPrefix = prefixOf("tests/data/songs/JsonPreview.Json", 64);
	CHECK_EQUAL(static_cast<int>(deluge::storage::SongPreviewOutcome::Truncated),
	            static_cast<int>(deluge::storage::decodeSongPreview(shortPrefix, true, image.data())));
	POINTERS_EQUAL(before, activeDeserializer);

	// Failed-open path: openDelugeBuffer points the global at the memory reader BEFORE it knows whether
	// the document opened, so this is the path a restore is easiest to forget.
	std::vector<uint8_t> notASong{'n', 'o', 'p', 'e'};
	CHECK_EQUAL(static_cast<int>(deluge::storage::SongPreviewOutcome::Error),
	            static_cast<int>(deluge::storage::decodeSongPreview(notASong, false, image.data())));
	POINTERS_EQUAL(before, activeDeserializer);
}

/// An EMPTY buffer is an Error, and reports the same `Error` a failed open does.
///
/// This is what the prefix channel now hands the decoder for a genuinely empty FILE, and the caller
/// leans on it: since a failed READ is reported separately (DELUGE_FILE_PREFIX_FAILED), zero bytes
/// reaching this function means an empty file, and an empty file is not a song. A decode that
/// answered NoPreview here would draw blank artwork for it while the file backing put an error on
/// screen for the same file -- the exact divergence the two backings were carrying.
TEST(SongPreviewDecode, anEmptyBufferIsAnErrorWithAnErrorCode) {
	std::array<Row, kDisplayHeight> image{};
	Error error = Error::NONE;
	CHECK_EQUAL(static_cast<int>(deluge::storage::SongPreviewOutcome::Error),
	            static_cast<int>(deluge::storage::decodeSongPreview({}, false, image.data(), &error)));
	CHECK_EQUAL(static_cast<int>(Error::FILE_CORRUPTED), static_cast<int>(error));
}

/// The `openError` out-param is written on the Error path and left ALONE on every other, exactly as
/// the file decode's is. A caller that reported it unconditionally would show a stale error over a
/// song that decoded fine.
TEST(SongPreviewDecode, theOpenErrorOutParamIsWrittenOnlyOnError) {
	std::array<Row, kDisplayHeight> image{};
	Error error = Error::NO_FURTHER_FILES_THIS_DIRECTION; // a sentinel no open ever produces
	auto whole = prefixOf("tests/data/songs/JsonPreview.Json", deluge::storage::kSongPreviewWindow);
	CHECK_EQUAL(static_cast<int>(deluge::storage::SongPreviewOutcome::Rendered),
	            static_cast<int>(deluge::storage::decodeSongPreview(whole, true, image.data(), &error)));
	CHECK_EQUAL(static_cast<int>(Error::NO_FURTHER_FILES_THIS_DIRECTION), static_cast<int>(error));

	auto shortPrefix = prefixOf("tests/data/songs/JsonPreview.Json", 64);
	CHECK_EQUAL(static_cast<int>(deluge::storage::SongPreviewOutcome::Truncated),
	            static_cast<int>(deluge::storage::decodeSongPreview(shortPrefix, true, image.data(), &error)));
	CHECK_EQUAL(static_cast<int>(Error::NO_FURTHER_FILES_THIS_DIRECTION), static_cast<int>(error));
}
