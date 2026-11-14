#include "mock_file_io.h"
#include "storage/song_preview.h"
#include "storage/storage_manager.h"

#include <algorithm>
#include <array>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <vector>

#include "cppspec.hpp"
#include "support/spec_main.h"

namespace {
constexpr const char* kJsonFixture = DELUGE_REPO_ROOT "/tests/support/data/songs/JsonPreview.Json";

using Row = RGB[kDisplayWidth + kSideBarWidth];

/// The offset of the first byte at which two images differ, or their size when they are the same.
std::size_t firstDifference(const auto& a, const auto& b) {
	const auto* x = reinterpret_cast<const uint8_t*>(a.data());
	const auto* y = reinterpret_cast<const uint8_t*>(b.data());
	return static_cast<std::size_t>(std::mismatch(x, x + sizeof(a), y).first - x);
}

std::vector<uint8_t> prefixOf(auto& self, const std::filesystem::path& p, uint32_t n) {
	std::vector<uint8_t> buf(n);
	std::FILE* f = std::fopen(p.c_str(), "rb");
	expect(f).not_().to_be_null();
	if (f == nullptr) {
		return {};
	}
	const size_t got = std::fread(buf.data(), 1, n, f);
	std::fclose(f);
	buf.resize(got);
	return buf;
}

/// Copy a host-filesystem file into the mock card at the same path, so the file walk can open it
/// through the ordinary deserializer. The mock (tests/support/mocks/mock_file_io.cpp) backs deluge::io::File
/// with an in-memory map keyed by path, so any path string will do as a key.
void stageOnCard(auto& self, const std::filesystem::path& p) {
	std::vector<uint8_t> whole;
	std::FILE* f = std::fopen(p.c_str(), "rb");
	expect(f).not_().to_be_null();
	if (f == nullptr) {
		return;
	}
	std::array<uint8_t, 65536> chunk{};
	while (const size_t got = std::fread(chunk.data(), 1, chunk.size(), f)) {
		whole.insert(whole.end(), chunk.begin(), chunk.begin() + got);
	}
	std::fclose(f);

	DelugeHandle handle = DELUGE_HANDLE_NONE;
	expect(deluge_efatfs_file_open(p.c_str(), DELUGE_FILE_WRITE_CREATE, &handle)).to_equal(DELUGE_OK);
	uint32_t written = 0;
	expect(deluge_efatfs_file_write(handle, whole.data(), whole.size(), &written)).to_equal(DELUGE_OK);
	expect(written).to_equal(whole.size());
	deluge_efatfs_file_close(handle);
}

} // namespace

// clang-format off
describe song_preview_decode_group("song preview decode", ${
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
	/// at whichever reader ran. Both are true of the browser's walk today, unchanged here -- but
	/// they are the reason "the reader is discarded" would be too strong a claim to rest on.
	///
	/// What this comparison also cannot catch is a change applied to BOTH walks: they share one decode
	/// loop, so nothing here pins the preview's absolute pixel values. That is the differential's job
	/// boundary, not a gap in it -- it exists to prove the buffer walk did not diverge from the file walk.
	it("the buffer walk matches the file walk across the corpus", _ {
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
			const bool isJson = (ext == ".json" || ext == ".JSON");
			if (ext != ".XML" && ext != ".xml" && !isJson) {
				continue;
			}

			std::array<Row, kDisplayHeight> fromBuffer{};
			std::array<Row, kDisplayHeight> fromFile{};

			auto buf = prefixOf(self, entry.path(), deluge::storage::kSongPreviewWindow);
			const auto outcome = deluge::storage::decodeSongPreview(buf, isJson, fromBuffer.data());

			// The reference: today's whole-file walk, reading through the ordinary file reader.
			mock_file_io_reset();
			stageOnCard(self, entry.path());
			const auto reference = deluge::storage::decodeSongPreviewFromFile(entry.path().c_str(), fromFile.data());

			expect(std::to_underlying(outcome)).not_().to_equal(std::to_underlying(deluge::storage::SongPreviewOutcome::Truncated), std::string{entry.path().c_str()});
			expect(static_cast<int>(outcome)).to_equal(static_cast<int>(reference));
			expect(firstDifference(fromFile, fromBuffer)).to_equal(sizeof(fromFile), std::string{entry.path().c_str()});
			if (outcome == deluge::storage::SongPreviewOutcome::Rendered) {
				rendered++;
			}
		}
		expect(rendered).to_be_greater_than(0, std::string{"corpus mounted but nothing rendered -- the differential proved nothing"});
	});

	/// The checked-in JSON fixture is the only JSON song there is, so run the differential on it whether or
	/// not the corpus is mounted -- otherwise the JSON arm of the buffer walk is never compared to anything.
	it("the buffer walk matches the file walk on the json fixture", _ {
		std::array<Row, kDisplayHeight> fromBuffer{};
		std::array<Row, kDisplayHeight> fromFile{};

		auto buf = prefixOf(self, kJsonFixture, deluge::storage::kSongPreviewWindow);
		const auto outcome = deluge::storage::decodeSongPreview(buf, true, fromBuffer.data());

		mock_file_io_reset();
		stageOnCard(self, kJsonFixture);
		const auto reference =
		    deluge::storage::decodeSongPreviewFromFile(kJsonFixture, fromFile.data());

		expect(static_cast<int>(reference)).to_equal(static_cast<int>(deluge::storage::SongPreviewOutcome::Rendered));
		expect(static_cast<int>(outcome)).to_equal(static_cast<int>(reference));
		expect(firstDifference(fromFile, fromBuffer)).to_equal(sizeof(fromFile));
	});

	/// A prefix that stops before the preview reports Truncated, NOT NoPreview. Conflating them is how
	/// artwork disappears silently.
	it("a short prefix reports truncated rather than no preview", _ {
		std::array<Row, kDisplayHeight> image{};
		auto buf = prefixOf(self, kJsonFixture, 64);
		expect(static_cast<int>(deluge::storage::decodeSongPreview(buf, true, image.data()))).to_equal(static_cast<int>(deluge::storage::SongPreviewOutcome::Truncated));
	});

	/// A borrowed buffer must not stay reachable through `activeDeserializer` after the call -- on ANY
	/// path, the failed open included. Left there, the next thing to write through that global writes
	/// Cluster::size bytes into a buffer that is smaller than that and by then usually gone. This asserts
	/// the restore rather than trusting a comment, because the caller that arms it is a task away.
	it("decode song preview leaves the global deserializer where it found it", _ {
		std::array<Row, kDisplayHeight> image{};
		FileDeserializer* const before = activeDeserializer;

		// Success path.
		auto whole = prefixOf(self, kJsonFixture, deluge::storage::kSongPreviewWindow);
		expect(static_cast<int>(deluge::storage::decodeSongPreview(whole, true, image.data()))).to_equal(static_cast<int>(deluge::storage::SongPreviewOutcome::Rendered));
		expect(activeDeserializer).to_equal(before);

		// Truncated path.
		auto shortPrefix = prefixOf(self, kJsonFixture, 64);
		expect(static_cast<int>(deluge::storage::decodeSongPreview(shortPrefix, true, image.data()))).to_equal(static_cast<int>(deluge::storage::SongPreviewOutcome::Truncated));
		expect(activeDeserializer).to_equal(before);

		// Failed-open path: openDelugeBuffer points the global at the memory reader BEFORE it knows whether
		// the document opened, so this is the path a restore is easiest to forget.
		std::vector<uint8_t> notASong{'n', 'o', 'p', 'e'};
		expect(static_cast<int>(deluge::storage::decodeSongPreview(notASong, false, image.data()))).to_equal(static_cast<int>(deluge::storage::SongPreviewOutcome::Error));
		expect(activeDeserializer).to_equal(before);
	});

	/// An EMPTY buffer is an Error, and reports the same `Error` a failed open does.
	///
	/// This is what the prefix channel now hands the decoder for a genuinely empty FILE, and the caller
	/// leans on it: since a failed READ is reported separately (DELUGE_CHANNEL_FAILED), zero bytes
	/// reaching this function means an empty file, and an empty file is not a song. A decode that
	/// answered NoPreview here would draw blank artwork for it while the file backing put an error on
	/// screen for the same file -- the exact divergence the two backings were carrying.
	it("an empty buffer is an error with an error code", _ {
		std::array<Row, kDisplayHeight> image{};
		Error error = Error::NONE;
		expect(static_cast<int>(deluge::storage::decodeSongPreview({}, false, image.data(), &error))).to_equal(static_cast<int>(deluge::storage::SongPreviewOutcome::Error));
		expect(static_cast<int>(error)).to_equal(static_cast<int>(Error::FILE_CORRUPTED));
	});

	/// The `openError` out-param is written on the Error path and left ALONE on every other, exactly as
	/// the file decode's is. A caller that reported it unconditionally would show a stale error over a
	/// song that decoded fine.
	it("the open error out param is written only on error", _ {
		std::array<Row, kDisplayHeight> image{};
		Error error = Error::NO_FURTHER_FILES_THIS_DIRECTION; // a sentinel no open ever produces
		auto whole = prefixOf(self, kJsonFixture, deluge::storage::kSongPreviewWindow);
		expect(static_cast<int>(deluge::storage::decodeSongPreview(whole, true, image.data(), &error))).to_equal(static_cast<int>(deluge::storage::SongPreviewOutcome::Rendered));
		expect(static_cast<int>(error)).to_equal(static_cast<int>(Error::NO_FURTHER_FILES_THIS_DIRECTION));

		auto shortPrefix = prefixOf(self, kJsonFixture, 64);
		expect(static_cast<int>(deluge::storage::decodeSongPreview(shortPrefix, true, image.data(), &error))).to_equal(static_cast<int>(deluge::storage::SongPreviewOutcome::Truncated));
		expect(static_cast<int>(error)).to_equal(static_cast<int>(Error::NO_FURTHER_FILES_THIS_DIRECTION));
	});
});

DELUGE_SPEC(song_preview_decode_spec, song_preview_decode_group)
