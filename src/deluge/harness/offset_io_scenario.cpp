/*
 * Copyright © 2026 Synthstrom Audible Limited
 *
 * This file is part of The Synthstrom Audible Deluge Firmware.
 *
 * The Synthstrom Audible Deluge Firmware is free software: you can redistribute it and/or modify it under the
 * terms of the GNU General Public License as published by the Free Software Foundation,
 * either version 3 of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY;
 * without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along with this program.
 * If not, see <https://www.gnu.org/licenses/>.
 */

#include "harness/offset_io_scenario.h"

#include "io/file.hpp"
#include "io/midi/midi_device.h"
#include "libdeluge/file_bytes.h"
#include "libdeluge/file_directory.h"
#include "storage/byte_channel.h"
#include "storage/fs_status.h"
#include "storage/owner.h"
#include "storage/smsysex.h"
#include "util/pack.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

// The headers above reach `src/lib/printf.h`, which soft-aliases `printf` to the embedded `printf_`.
// That implementation emits through `putchar_`, which only the C host BSP defines -- this harness
// links the Rust BSP and has no provider, so the alias turns every line this file writes into an
// undefined symbol at link time. Every one of those lines IS the gate's output.
#ifdef printf
#undef printf
#endif

namespace {

/// A `MIDICable` that keeps what was sent to it instead of putting it on a wire. Same shape as
/// `path_operations_scenario.cpp`'s, and `sendBufferSpace` is nonzero for the same reason: a zero
/// would have the SysEx path decline to send, and the gate would read "no reply" for one never
/// attempted.
class LoopbackCable final : public MIDICable {
public:
	void writeToFlash(uint8_t* /*memory*/) override {}
	char const* getDisplayName() override { return "OI-LOOPBACK"; }
	void sendMessage(MIDIMessage /*message*/) override {}

	void sendSysex(const uint8_t* data, int32_t len) override {
		lastReply.assign(reinterpret_cast<const char*>(data), static_cast<size_t>(len));
		replies++;
	}

	[[nodiscard]] size_t sendBufferSpace() const override { return 1024; }
	void writeReferenceAttributesToFile(Serializer& /*writer*/) override {}

	std::string lastReply;
	int32_t replies = 0;
};

LoopbackCable g_cable;

constexpr const char* kTransferPath = "OI_TEST/XFER.BIN";
constexpr const char* kCopySrc = "OI_TEST/SRC.BIN";
constexpr const char* kCopyDst = "OI_TEST/DST.BIN";
constexpr const char* kMissing = "OI_TEST/never-existed.bin";
constexpr const char* kGiveUpDst = "OI_TEST/GIVEUP.BIN";

/// One transfer block. Matches `smsysex`'s `blockBufferMax`, which is what a single SysEx message can
/// carry.
constexpr uint32_t kBlock = 1024;

/// THREE blocks, deliberately. A `write_at` that truncated would leave only the last one, and a
/// single-block round trip would pass against exactly that bug.
constexpr uint32_t kBlockCount = 3;

/// How many ticks one verb gets to produce its reply before the gate calls it wedged. Comfortably
/// above the handler's worst case once its channel arm has a stall bound.
constexpr int32_t kMaxVerbTicks = 5000;

/// How long the give-up stage holds every byte-channel operation, in milliseconds of the harness's virtual clock. Far
/// past the handler's 600-tick stall bound (each tick sleeps at least 1 ms) and past the stage's own reply bound below.
constexpr uint32_t kGiveUpLatencyMs = 30000;

/// How many ticks the give-up stage waits for its reply: well above the stall bound, well below the latency above.
constexpr int32_t kMaxGiveUpTicks = 2000;

/// The byte pattern block `index` carries. Distinct per block, so a block written at the wrong offset
/// or lost to a truncation is visible rather than merely absent.
uint8_t patternByte(uint32_t index, uint32_t within) {
	return static_cast<uint8_t>((index * 37u + within * 11u + 1u) & 0xFFu);
}

enum class Stage : uint8_t {
	Fixture,
	FixtureCheck,
	OpenWrite,
	OpenWriteCheck,
	WriteBlocks,
	WriteBlocksCheck,
	CloseWrite,
	CloseWriteCheck,
	OpenRead,
	OpenReadCheck,
	ReadBlocks,
	ReadBlocksCheck,
	CloseRead,
	CloseReadCheck,
	Copy,
	CopyCheck,
	CopyMissing,
	CopyMissingCheck,
	Dir,
	DirCheck,
	Report,
	GiveUp,
	GiveUpCheck,
	DirStall,
	DirStallCheck,
	Summary,
	Done,
};

Stage g_stage = Stage::Fixture;
bool g_stepDone = false;
bool g_summaryPrinted = false;
int32_t g_failures = 0;
int32_t g_verbTicks = 0;
int32_t g_repliesBefore = 0;
uint32_t g_blockIndex = 0;
uint32_t g_writeFid = 0;
uint32_t g_readFid = 0;
int32_t g_matched = 0;
int32_t g_copySame = -1;
int32_t g_copyMissingReports = -1;
int32_t g_dirListed = -1;
/// `illegalInline()` when the give-up stage began.
int32_t g_illegalBefore = 0;
uint8_t g_scratch[kBlock];

/// Pull an integer attribute out of a captured reply, or -1 if absent.
int32_t replyAttr(const std::string& reply, const char* name) {
	std::string key = "\"";
	key += name;
	key += "\"";
	const size_t at = reply.find(key);
	if (at == std::string::npos) {
		return -1;
	}
	const size_t colon = reply.find(':', at);
	if (colon == std::string::npos) {
		return -1;
	}
	return static_cast<int32_t>(strtol(reply.c_str() + colon + 1, nullptr, 10));
}

/// Feed one JSON request in as a real SysEx message.
///
/// `data[1]` is the message sequence number the reply is addressed with, and the JSON starts at
/// `data[2]` -- see `smSysex::sysexReceived` and `processFrontSysEx`.
void sendJson(const std::string& json) {
	static uint8_t buffer[4096];
	static uint8_t seq = 1;
	buffer[0] = 0;
	buffer[1] = seq++;
	if (seq == 0 || seq > 100) {
		seq = 1;
	}
	memcpy(buffer + 2, json.data(), json.size());
	g_cable.replies = g_cable.replies; // no-op; keeps the counter's read obvious at the call site
	smSysex::sysexReceived(g_cable, buffer, static_cast<int32_t>(json.size() + 2));
}

/// Feed a `write` request carrying block `index`'s bytes, 7-bit packed exactly as a real client does.
void sendWriteBlock(uint32_t index) {
	uint8_t raw[kBlock];
	for (uint32_t i = 0; i < kBlock; i++) {
		raw[i] = patternByte(index, i);
	}
	static uint8_t packed[kBlock * 2];
	const int32_t packedLen = pack_8bit_to_7bit(packed, sizeof(packed), raw, static_cast<int32_t>(kBlock));

	static uint8_t buffer[4096];
	static uint8_t seq = 101;
	char header[160];
	const int headerLen = snprintf(header, sizeof(header), R"({"write":{"fid":%u,"addr":%u,"size":%u}})", g_writeFid,
	                               index * kBlock, kBlock);
	buffer[0] = 0;
	buffer[1] = seq++;
	if (seq == 0) {
		seq = 101;
	}
	int32_t at = 2;
	memcpy(buffer + at, header, static_cast<size_t>(headerLen));
	at += headerLen;
	buffer[at++] = 0; // the separator `decodeDataFromReader` reads before the packed payload
	memcpy(buffer + at, packed, static_cast<size_t>(packedLen));
	at += packedLen;
	buffer[at++] = 0xF7; // the trailing byte the decoder excludes from the encoded length
	smSysex::sysexReceived(g_cable, buffer, at);
}

void runTick() {
	switch (g_stage) {
	case Stage::Fixture: {
		(void)deluge::io::mkdir("OI_TEST");
		(void)deluge::io::unlink(kTransferPath);
		(void)deluge::io::unlink(kCopyDst);
		(void)deluge::io::unlink(kMissing);
		(void)deluge::io::unlink(kGiveUpDst);
		// A multi-block source for the copy stage, built with the synchronous API: this is the gate's
		// own setup, not the code under test.
		auto src = deluge::io::File::open(kCopySrc, DELUGE_FILE_WRITE_CREATE);
		if (src.has_value()) {
			for (uint32_t b = 0; b < kBlockCount; b++) {
				for (uint32_t i = 0; i < kBlock; i++) {
					g_scratch[i] = patternByte(b + 7, i);
				}
				(void)src->write(std::span{reinterpret_cast<const std::byte*>(g_scratch), kBlock});
			}
			(void)src->close();
		}
		g_stage = Stage::FixtureCheck;
		break;
	}

	case Stage::FixtureCheck:
		g_stage = Stage::OpenWrite;
		break;

	case Stage::OpenWrite:
		g_repliesBefore = g_cable.replies;
		g_verbTicks = 0;
		sendJson(std::string(R"({"open":{"path":")") + kTransferPath + R"(","write":1,"date":0,"time":0}})");
		g_stage = Stage::OpenWriteCheck;
		break;

	case Stage::OpenWriteCheck: {
		if (g_cable.replies == g_repliesBefore && ++g_verbTicks < kMaxVerbTicks) {
			break;
		}
		const int32_t fid = replyAttr(g_cable.lastReply, "fid");
		const int32_t err = replyAttr(g_cable.lastReply, "err");
		printf("OI-SYSEX verb=open-write replied=%d fid=%d err=%d\n", g_cable.replies > g_repliesBefore ? 1 : 0, fid,
		       err);
		if (err != 0 || fid <= 0) {
			printf("OI-SYSEX FAIL open-for-write err=%d fid=%d\n", err, fid);
			g_failures++;
			g_stage = Stage::Report;
			break;
		}
		g_writeFid = static_cast<uint32_t>(fid);
		g_blockIndex = 0;
		g_stage = Stage::WriteBlocks;
		break;
	}

	case Stage::WriteBlocks:
		g_repliesBefore = g_cable.replies;
		g_verbTicks = 0;
		sendWriteBlock(g_blockIndex);
		g_stage = Stage::WriteBlocksCheck;
		break;

	case Stage::WriteBlocksCheck: {
		if (g_cable.replies == g_repliesBefore && ++g_verbTicks < kMaxVerbTicks) {
			break;
		}
		const int32_t err = replyAttr(g_cable.lastReply, "err");
		if (err != 0) {
			printf("OI-TRANSFER FAIL block %u write reported err=%d\n", g_blockIndex, err);
			g_failures++;
		}
		g_blockIndex++;
		g_stage = (g_blockIndex < kBlockCount) ? Stage::WriteBlocks : Stage::CloseWrite;
		break;
	}

	case Stage::CloseWrite:
		g_repliesBefore = g_cable.replies;
		g_verbTicks = 0;
		sendJson(std::string(R"({"close":{"fid":)") + std::to_string(g_writeFid) + "}}");
		g_stage = Stage::CloseWriteCheck;
		break;

	case Stage::CloseWriteCheck:
		if (g_cable.replies == g_repliesBefore && ++g_verbTicks < kMaxVerbTicks) {
			break;
		}
		g_stage = Stage::OpenRead;
		break;

	case Stage::OpenRead:
		g_repliesBefore = g_cable.replies;
		g_verbTicks = 0;
		sendJson(std::string(R"({"open":{"path":")") + kTransferPath + R"(","write":0,"date":0,"time":0}})");
		g_stage = Stage::OpenReadCheck;
		break;

	case Stage::OpenReadCheck: {
		if (g_cable.replies == g_repliesBefore && ++g_verbTicks < kMaxVerbTicks) {
			break;
		}
		const int32_t fid = replyAttr(g_cable.lastReply, "fid");
		const int32_t size = replyAttr(g_cable.lastReply, "size");
		printf("OI-SYSEX verb=open-read replied=1 fid=%d size=%d\n", fid, size);
		// The size the open reports is the truncation guard's first witness: three blocks were
		// written, so a file shorter than that has lost some of them.
		if (size != static_cast<int32_t>(kBlock * kBlockCount)) {
			printf("OI-TRANSFER FAIL file is %d bytes, expected %u\n", size, kBlock * kBlockCount);
			g_failures++;
		}
		if (fid <= 0) {
			g_failures++;
			g_stage = Stage::Report;
			break;
		}
		g_readFid = static_cast<uint32_t>(fid);
		g_blockIndex = 0;
		g_matched = 0;
		g_stage = Stage::ReadBlocks;
		break;
	}

	case Stage::ReadBlocks:
		g_repliesBefore = g_cable.replies;
		g_verbTicks = 0;
		sendJson(std::string(R"({"read":{"fid":)") + std::to_string(g_readFid) + R"(,"addr":)"
		         + std::to_string(g_blockIndex * kBlock) + R"(,"size":)" + std::to_string(kBlock) + "}}");
		g_stage = Stage::ReadBlocksCheck;
		break;

	case Stage::ReadBlocksCheck: {
		if (g_cable.replies == g_repliesBefore && ++g_verbTicks < kMaxVerbTicks) {
			break;
		}
		// The reply carries the block 7-bit packed after a separator. Rather than unpack it here,
		// compare against the file directly -- what matters is that the BYTES ON THE CARD are the
		// ones that were written, and reading them back through the same encoder that wrote them
		// could hide a symmetric error.
		auto file = deluge::io::File::open(kTransferPath, DELUGE_FILE_READ);
		if (file.has_value()) {
			(void)file->seek(g_blockIndex * kBlock);
			auto got = file->read(std::span{reinterpret_cast<std::byte*>(g_scratch), kBlock});
			bool same = got.has_value() && got->size() == kBlock;
			for (uint32_t i = 0; same && i < kBlock; i++) {
				same = g_scratch[i] == patternByte(g_blockIndex, i);
			}
			if (same) {
				g_matched++;
			}
			else {
				printf("OI-TRANSFER FAIL block %u does not match what was written\n", g_blockIndex);
			}
			(void)file->close();
		}
		g_blockIndex++;
		g_stage = (g_blockIndex < kBlockCount) ? Stage::ReadBlocks : Stage::CloseRead;
		break;
	}

	case Stage::CloseRead:
		g_repliesBefore = g_cable.replies;
		g_verbTicks = 0;
		sendJson(std::string(R"({"close":{"fid":)") + std::to_string(g_readFid) + "}}");
		g_stage = Stage::CloseReadCheck;
		break;

	case Stage::CloseReadCheck:
		if (g_cable.replies == g_repliesBefore && ++g_verbTicks < kMaxVerbTicks) {
			break;
		}
		g_stage = Stage::Copy;
		break;

	case Stage::Copy:
		g_repliesBefore = g_cable.replies;
		g_verbTicks = 0;
		sendJson(std::string(R"({"copy":{"from":")") + kCopySrc + R"(","to":")" + kCopyDst + R"("}})");
		g_stage = Stage::CopyCheck;
		break;

	case Stage::CopyCheck: {
		if (g_cable.replies == g_repliesBefore && ++g_verbTicks < kMaxVerbTicks) {
			break;
		}
		// Byte-for-byte against the source, not a length check: a copy that truncated or reordered
		// would pass a length comparison.
		g_copySame = 1;
		auto dst = deluge::io::File::open(kCopyDst, DELUGE_FILE_READ);
		if (!dst.has_value()) {
			g_copySame = 0;
		}
		else {
			for (uint32_t b = 0; b < kBlockCount && g_copySame == 1; b++) {
				auto got = dst->read(std::span{reinterpret_cast<std::byte*>(g_scratch), kBlock});
				if (!got.has_value() || got->size() != kBlock) {
					g_copySame = 0;
					break;
				}
				for (uint32_t i = 0; i < kBlock; i++) {
					if (g_scratch[i] != patternByte(b + 7, i)) {
						g_copySame = 0;
						break;
					}
				}
			}
			(void)dst->close();
		}
		g_stage = Stage::CopyMissing;
		break;
	}

	case Stage::CopyMissing:
		g_repliesBefore = g_cable.replies;
		g_verbTicks = 0;
		sendJson(std::string(R"({"copy":{"from":")") + kMissing + R"(","to":"OI_TEST/NOPE.BIN"}})");
		g_stage = Stage::CopyMissingCheck;
		break;

	case Stage::CopyMissingCheck: {
		if (g_cable.replies == g_repliesBefore && ++g_verbTicks < kMaxVerbTicks) {
			break;
		}
		// A case whose expected answer is an ERROR. Without one, a handler replying before its
		// operation completed reports the same err=0 a correct one does -- which is how the last rung
		// nearly shipped an untestable gate.
		const int32_t err = replyAttr(g_cable.lastReply, "err");
		g_copyMissingReports = (err > 0) ? 1 : 0;
		g_stage = Stage::Dir;
		break;
	}

	case Stage::Dir:
		g_repliesBefore = g_cable.replies;
		g_verbTicks = 0;
		// The listing verb, whose directory OPEN now goes through the directory channel. Included
		// because converting a site the gate never drives is exactly the shape this ladder keeps
		// catching -- a conversion nothing exercises looks identical to one that works.
		sendJson(R"({"dir":{"path":"OI_TEST","offset":0,"lines":8}})");
		g_stage = Stage::DirCheck;
		break;

	case Stage::DirCheck: {
		if (g_cable.replies == g_repliesBefore && ++g_verbTicks < kMaxVerbTicks) {
			break;
		}
		const int32_t err = replyAttr(g_cable.lastReply, "err");
		const bool listed = g_cable.lastReply.find("SRC.BIN") != std::string::npos;
		g_dirListed = (err == 0 && listed) ? 1 : 0;
		g_stage = Stage::Report;
		break;
	}

	case Stage::Report: {
		printf("OI-TRANSFER blocks=%u matched=%d\n", kBlockCount, g_matched);
		if (g_matched != static_cast<int32_t>(kBlockCount)) {
			printf("OI-TRANSFER FAIL only %d of %u blocks survived\n", g_matched, kBlockCount);
			g_failures++;
		}

		printf("OI-COPY same=%d missingReports=%d\n", g_copySame, g_copyMissingReports);
		if (g_copySame != 1) {
			printf("OI-COPY FAIL the copy does not match its source\n");
			g_failures++;
		}
		if (g_copyMissingReports != 1) {
			printf("OI-COPY FAIL copying a missing source did not report an error\n");
			g_failures++;
		}

		// Reported, never asserted here. With the channel compiled out zero is the honest answer, and
		// the channel-on leg of `offset_io.sh` is what requires it non-zero.
		printf("OI-DIR listed=%d\n", g_dirListed);
		if (g_dirListed != 1) {
			printf("OI-DIR FAIL the listing did not return the directory's contents\n");
			g_failures++;
		}

		printf("OI-CHANNEL served=%d\n", deluge::storage::bytes::channelServedOps());

		{
			// Every verb above ran after boot, off the owner: none may have been refused.
			const int32_t illegal = deluge::storage::bytes::illegalInline();
			printf("OI-ILLEGAL illegal=%d\n", illegal);
			if (illegal != 0) {
				printf("OI-ILLEGAL FAIL a SysEx operation was refused for want of a legal place to run\n");
				g_failures++;
			}
		}
		g_stage = Stage::GiveUp;
		break;
	}

	case Stage::GiveUp:
		// A copy whose channel operation is held far past the handler's stall bound. After boot, off the owner,
		// there is nowhere legal to redo it, so the handler must give up and say so -- not copy on the owner, and not
		// leave the host waiting for a reply that never comes.
#ifdef DELUGE_HOST
		// This scenario is compiled into every app build, but the knob exists only on the host.
		deluge_file_bytes_set_latency_ms(kGiveUpLatencyMs);
#endif
		g_illegalBefore = deluge::storage::bytes::illegalInline();
		g_repliesBefore = g_cable.replies;
		g_verbTicks = 0;
		sendJson(std::string(R"({"copy":{"from":")") + kCopySrc + R"(","to":")" + kGiveUpDst + R"("}})");
		g_stage = Stage::GiveUpCheck;
		break;

	case Stage::GiveUpCheck: {
		if (g_cable.replies == g_repliesBefore && ++g_verbTicks < kMaxGiveUpTicks) {
			break;
		}
#ifdef DELUGE_HOST
		deluge_file_bytes_set_latency_ms(0);
#endif
		const bool replied = g_cable.replies > g_repliesBefore;
		const int32_t err = replied ? replyAttr(g_cable.lastReply, "err") : -1;
		const int32_t illegal = deluge::storage::bytes::illegalInline() - g_illegalBefore;
		auto dst = deluge::io::File::open(kGiveUpDst, DELUGE_FILE_READ);
		const bool copied = dst.has_value();
		if (dst.has_value()) {
			(void)dst->close();
		}
		printf("OI-GIVEUP replied=%d err=%d illegal=%d copied=%d ticks=%d latencyMs=%u\n", (int)replied, err, illegal,
		       (int)copied, g_verbTicks, (unsigned)kGiveUpLatencyMs);
		if (!replied) {
			printf("OI-GIVEUP FAIL the handler never replied to a copy whose channel stopped answering\n");
			g_failures++;
		}
		if (replied && err <= 0) {
			printf("OI-GIVEUP FAIL the handler reported success for a copy it could not make\n");
			g_failures++;
		}
		if (illegal != 1) {
			printf("OI-GIVEUP FAIL expected exactly one refused operation, got %d\n", illegal);
			g_failures++;
		}
		if (copied) {
			printf("OI-GIVEUP FAIL the copy was made anyway\n");
			g_failures++;
		}
		g_stage = Stage::DirStall;
		break;
	}

	case Stage::DirStall:
		// A directory listing whose open is held far past the handler's stall bound. It must be answered with a timeout
		// and no entries, and popped, so the requests queued behind it are not left waiting too.
#ifdef DELUGE_HOST
		deluge_file_directory_set_latency_ms(kGiveUpLatencyMs);
#endif
		g_repliesBefore = g_cable.replies;
		g_verbTicks = 0;
		sendJson(R"({"dir":{"path":"OI_TEST","offset":0,"lines":8}})");
		g_stage = Stage::DirStallCheck;
		break;

	case Stage::DirStallCheck: {
		if (g_cable.replies == g_repliesBefore && ++g_verbTicks < kMaxGiveUpTicks) {
			break;
		}
#ifdef DELUGE_HOST
		deluge_file_directory_set_latency_ms(0);
#endif
		const bool replied = g_cable.replies > g_repliesBefore;
		const int32_t err = replied ? replyAttr(g_cable.lastReply, "err") : -1;
		const bool listed = replied && g_cable.lastReply.find("SRC.BIN") != std::string::npos;
		const int32_t timeout = static_cast<int32_t>(deluge::storage::FsWireStatus::Timeout);
		printf("OI-DIR-STALL replied=%d err=%d listed=%d ticks=%d latencyMs=%u\n", (int)replied, err, (int)listed,
		       g_verbTicks, (unsigned)kGiveUpLatencyMs);
		if (!replied) {
			printf("OI-DIR-STALL FAIL the handler never replied to a listing whose open never answered\n");
			g_failures++;
		}
		else if (err != timeout) {
			printf("OI-DIR-STALL FAIL the reply's err=%d, wanted the timeout (%d)\n", err, timeout);
			g_failures++;
		}
		if (listed) {
			printf("OI-DIR-STALL FAIL the timeout reply listed an earlier listing's entries\n");
			g_failures++;
		}
		const int32_t tier = deluge::storage::bytes::interactionTierIo(deluge::storage::bytes::IoTier::Sysex);
		printf("OI-TIER sysex=%d\n", tier);
		if (tier != 0) {
			printf("OI-TIER FAIL %d SysEx directory open(s) began synchronously off the storage owner\n", tier);
			g_failures++;
		}
		g_stage = Stage::Summary;
		break;
	}

	case Stage::Summary:
		printf("OI-RESULT failures=%d\n", g_failures);
		// Piped into a script stdout is block-buffered and the harness exits without flushing C
		// stdio, so without this the entire gate output vanishes while the run reports success.
		fflush(stdout);
		g_summaryPrinted = true;
		g_stage = Stage::Done;
		break;

	case Stage::Done:
		break;
	}
}

} // namespace

extern "C" {

void deluge_scenario_offset_io_begin(void) {
	setvbuf(stdout, nullptr, _IOLBF, 0);
	g_stage = Stage::Fixture;
	g_stepDone = false;
	g_summaryPrinted = false;
	g_failures = 0;
	g_verbTicks = 0;
	g_repliesBefore = 0;
	g_blockIndex = 0;
	g_writeFid = 0;
	g_readFid = 0;
	g_matched = 0;
	g_copySame = -1;
	g_copyMissingReports = -1;
	g_dirListed = -1;
	g_illegalBefore = 0;
	g_cable.replies = 0;
	g_cable.lastReply.clear();
}

bool deluge_scenario_offset_io_complete(void) {
	return g_summaryPrinted;
}

void deluge_scenario_offset_io_dispatch_step(void) {
	if (g_summaryPrinted) {
		return;
	}
	g_stepDone = false;
	// The SysEx pump. In the firmware this is a repeating task registered in `deluge.cpp`; here the
	// scenario owns the tick, so without this call a queued request would never be processed.
	smSysex::handleNextSysEx();
	runTick();
	if (!deluge::storage::Owner::run([](void*) { g_stepDone = true; }, nullptr)) {
		g_stepDone = true;
	}
}

bool deluge_scenario_offset_io_step_done(void) {
	return g_stepDone;
}

int32_t deluge_scenario_offset_io_failures(void) {
	return g_failures;
}

} // extern "C"
