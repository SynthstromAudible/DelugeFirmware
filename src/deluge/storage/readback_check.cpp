#include "storage/readback_check.h"

#include "io/file.hpp"
#include "libdeluge/file_io.h"
#include "storage/byte_channel.h"

#include <cstddef>
#include <span>

namespace deluge::storage {

namespace {
/// Large enough for any head or tail a document save compares -- each is a few dozen bytes. A stage
/// wanting more is treated as a mismatch rather than read past the end of this buffer.
constexpr uint32_t kReadBackBufferBytes = 64;
} // namespace

ReadBackTick ReadBackCheck::step(const char* path) {
	using bytes::ByteIo;

	if (stage_ == ReadBackStage::Passed) {
		return ReadBackTick::Passed;
	}

	DelugeStatus status = DELUGE_OK;

	if (stage_ == ReadBackStage::Length) {
		uint32_t size = 0;
		switch (bytes::collectStat(path, &size, &status)) {
		case ByteIo::Unknown:
			// Ask -- and honour the answer: a path that cannot be keyed reports Failed, and discarding
			// that leaves this asking a channel that will never reply.
			return bytes::requestStat(path) == ByteIo::Failed ? ReadBackTick::Failed : ReadBackTick::Waiting;
		case ByteIo::Pending:
			return ReadBackTick::Waiting;
		case ByteIo::Failed:
			++servedOps_;
			return ReadBackTick::Failed;
		case ByteIo::Ready:
			++servedOps_;
			if (!readBackLengthMatches(size, expect_)) {
				return ReadBackTick::Failed;
			}
			stage_ = nextReadBackStage(stage_, expect_);
			return stage_ == ReadBackStage::Passed ? ReadBackTick::Passed : ReadBackTick::Advanced;
		}
		return ReadBackTick::Failed;
	}

	const uint32_t offset = readBackOffset(stage_, expect_);
	const uint32_t want = readBackWant(stage_, expect_);
	if (want == 0 || want > kReadBackBufferBytes) {
		return ReadBackTick::Failed;
	}

	char buffer[kReadBackBufferBytes];
	uint32_t got = 0;
	switch (bytes::collectReadAt(path, buffer, sizeof(buffer), &got, &status)) {
	case ByteIo::Unknown:
		return bytes::requestReadAt(path, offset, want) == ByteIo::Failed ? ReadBackTick::Failed
		                                                                  : ReadBackTick::Waiting;
	case ByteIo::Pending:
		return ReadBackTick::Waiting;
	case ByteIo::Failed:
		++servedOps_;
		return ReadBackTick::Failed;
	case ByteIo::Ready:
		++servedOps_;
		if (!readBackBytesMatch(stage_, expect_, buffer, got)) {
			return ReadBackTick::Failed;
		}
		stage_ = nextReadBackStage(stage_, expect_);
		return stage_ == ReadBackStage::Passed ? ReadBackTick::Passed : ReadBackTick::Advanced;
	}
	return ReadBackTick::Failed;
}

bool readBackSynchronously(const char* path, const ReadBackExpectation& expect) {
	auto opened = deluge::io::File::open(path, DELUGE_FILE_READ);
	if (!opened) {
		return false;
	}
	auto size = opened->size();
	if (!size || !readBackLengthMatches(static_cast<uint32_t>(*size), expect)) {
		(void)opened->close();
		return false;
	}
	for (ReadBackStage stage = nextReadBackStage(ReadBackStage::Length, expect); stage != ReadBackStage::Passed;
	     stage = nextReadBackStage(stage, expect)) {
		const uint32_t offset = readBackOffset(stage, expect);
		const uint32_t want = readBackWant(stage, expect);
		char buffer[kReadBackBufferBytes];
		if (want == 0 || want > kReadBackBufferBytes || !opened->seek(offset)) {
			(void)opened->close();
			return false;
		}
		auto read = opened->read(std::span<std::byte>(reinterpret_cast<std::byte*>(buffer), want));
		if (!read || !readBackBytesMatch(stage, expect, buffer, static_cast<uint32_t>(read->size()))) {
			(void)opened->close();
			return false;
		}
	}
	(void)opened->close();
	return true;
}

} // namespace deluge::storage
