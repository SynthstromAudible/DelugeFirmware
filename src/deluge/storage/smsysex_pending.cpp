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

#include "storage/smsysex_pending.h"

#include "libdeluge/file_bytes.h"
#include "storage/byte_channel.h"

#include <cstring>
#include <string>

namespace deluge::storage {

namespace {

/// How many consecutive refusals before this stops asking the channel.
///
/// The channel is single-flight across all operation kinds, so another consumer's operation in
/// flight refuses this one. Retrying is right -- the handler is re-entered every tick anyway -- but
/// an unbounded retry would let a busy channel stall the SysEx queue behind one request.
///
/// As many as the stall bound below. After boot, off the owner, there is no owner to fall back to,
/// so giving up on a channel that is merely busy would answer the host with an error for a request
/// that would have succeeded a few ticks later.
constexpr int32_t kMaxRefusals = 600;

/// How many ticks one operation may sit PENDING before this gives up on the channel.
///
/// A separate, far larger bound, because it guards a different failure. A refusal is instant and
/// means "busy"; PENDING means "working", and a real card operation is legitimately allowed to take
/// a while. But a channel whose driver is not running answers PENDING **forever** -- the request sits
/// in its slot with nothing to take it -- and that is neither a refusal nor an answer, so the bound
/// above never sees it. The startup fail-safe found this by neutering, after it wedged boot entirely;
/// here it would wedge the SysEx queue.
constexpr int32_t kMaxPendingTicks = 600;

PathOp g_op = PathOp::None;
std::string g_pathA;
std::string g_pathB;
DelugeTimestamp g_stamp{};
uint8_t g_stage = 0;

/// The offset an offset-addressed operation works at.
uint32_t g_offset = 0;
/// A write's payload, or a read's answer. One buffer for both directions because only one operation
/// is ever pending.
uint8_t g_block[DELUGE_FILE_BYTES_BLOCK];
/// How many of `g_block` are meaningful: a write's length going out, a read's length coming back.
uint32_t g_blockLen = 0;
/// A `Stat`'s size or a `WriteAt`'s byte count. See @ref pathOpResultValue.
uint32_t g_resultValue = 0;

/// Whether the request has been issued (as opposed to started but still refused).
bool g_issued = false;
int32_t g_refusals = 0;
int32_t g_pendingTicks = 0;

/// The synchronous arm's answer, read on the tick after the one that ran (or refused) it.
bool g_ownerDone = false;
deluge::io::Status g_ownerStatus = deluge::io::Status::OK;

/// Whether the channel should carry this operation, or the synchronous arm should.
bool viaChannel() {
	return deluge::storage::bytes::usingByteChannel() && g_refusals < kMaxRefusals && g_pendingTicks < kMaxPendingTicks;
}

/// Copy `from` to `to` synchronously, block by block.
///
/// Only the owner arm reaches this. Its shape is `smSysex::performFileCopy`'s former body, minus the
/// path-creation retry: the destination's parent is created by the open below, and the channel arm
/// does not come here at all.
deluge::io::Status copyOnOwner(const std::string& from, const std::string& to) {
	auto src = deluge::io::File::open(from, DELUGE_FILE_READ);
	if (!src.has_value()) {
		return src.error();
	}
	auto dst = deluge::io::File::open(to, DELUGE_FILE_WRITE_CREATE);
	if (!dst.has_value()) {
		(void)src->close();
		return dst.error();
	}
	deluge::io::Status status = deluge::io::Status::OK;
	for (;;) {
		auto read = src->read(std::span{reinterpret_cast<std::byte*>(g_block), DELUGE_FILE_BYTES_BLOCK});
		if (!read.has_value()) {
			status = read.error();
			break;
		}
		if (read->empty()) {
			break;
		}
		auto written = dst->write(*read);
		if (!written.has_value()) {
			status = written.error();
			break;
		}
		if (*written != read->size()) {
			// Preserving the pre-existing quirk: a short write here is not itself flagged, and the
			// copy simply stops.
			break;
		}
	}
	(void)dst->close();
	(void)src->close();
	return status;
}

/// Run the operation synchronously. Reached only through `runInlineWhereLegal`, so never anywhere a
/// card wait is disallowed.
void runOnOwner() {
	std::expected<void, deluge::io::Status> result{};
	switch (g_op) {
	case PathOp::Unlink:
		result = deluge::io::unlink(g_pathA);
		break;
	case PathOp::Rename:
		result = deluge::io::rename(g_pathA, g_pathB);
		break;
	case PathOp::MkdirAll:
		// One level, matching what this handler's synchronous form always did: its callers pass a
		// path whose parent already exists. The channel arm creates every segment, which is a
		// superset and breaks nothing.
		result = deluge::io::mkdir(g_pathA);
		break;
	case PathOp::SetTime:
		result = deluge::io::set_time(g_pathA, g_stamp);
		break;
	case PathOp::ReadAt: {
		auto file = deluge::io::File::open(g_pathA, DELUGE_FILE_READ);
		if (!file.has_value()) {
			g_ownerStatus = file.error();
			g_ownerDone = true;
			return;
		}
		auto seeked = file->seek(g_offset);
		if (!seeked.has_value()) {
			g_ownerStatus = seeked.error();
			(void)file->close();
			g_ownerDone = true;
			return;
		}
		auto read = file->read(std::span{reinterpret_cast<std::byte*>(g_block), g_blockLen});
		// A short read is end-of-file, not a failure -- the same rule the channel arm follows.
		g_blockLen = read.has_value() ? static_cast<uint32_t>(read->size()) : 0;
		g_ownerStatus = read.has_value() ? deluge::io::Status::OK : read.error();
		(void)file->close();
		g_ownerDone = true;
		return;
	}
	case PathOp::WriteAt: {
		// DELUGE_FILE_READ, not WRITE_CREATE, for the reason the Rust arm gives at length: the create
		// mode truncates, which would erase the file on every block.
		auto file = deluge::io::File::open(g_pathA, DELUGE_FILE_READ);
		if (!file.has_value() && file.error() == deluge::io::Status::NOT_FOUND) {
			auto created = deluge::io::File::open(g_pathA, DELUGE_FILE_WRITE_CREATE);
			if (created.has_value()) {
				(void)created->close();
			}
			file = deluge::io::File::open(g_pathA, DELUGE_FILE_READ);
		}
		if (!file.has_value()) {
			g_ownerStatus = file.error();
			g_ownerDone = true;
			return;
		}
		auto seeked = file->seek(g_offset);
		if (!seeked.has_value()) {
			g_ownerStatus = seeked.error();
			(void)file->close();
			g_ownerDone = true;
			return;
		}
		auto written = file->write(std::span{reinterpret_cast<const std::byte*>(g_block), g_blockLen});
		g_resultValue = written.has_value() ? static_cast<uint32_t>(*written) : 0;
		g_ownerStatus = written.has_value() ? deluge::io::Status::OK : written.error();
		(void)file->close();
		g_ownerDone = true;
		return;
	}
	case PathOp::Stat: {
		auto file = deluge::io::File::open(g_pathA, DELUGE_FILE_READ);
		if (!file.has_value()) {
			g_ownerStatus = file.error();
			g_ownerDone = true;
			return;
		}
		auto size = file->size();
		g_resultValue = size.has_value() ? *size : 0;
		g_ownerStatus = size.has_value() ? deluge::io::Status::OK : size.error();
		(void)file->close();
		g_ownerDone = true;
		return;
	}
	case PathOp::CreateEmpty: {
		// Truncating on purpose here, unlike WriteAt: this IS the create-and-empty step.
		auto file = deluge::io::File::open(g_pathA, DELUGE_FILE_WRITE_CREATE);
		g_ownerStatus = file.has_value() ? deluge::io::Status::OK : file.error();
		if (file.has_value()) {
			(void)file->close();
		}
		g_ownerDone = true;
		return;
	}
	case PathOp::Copy:
		// The loop, kept HERE and only here. The channel arm has none -- `copy_file` is one request
		// and the loop runs in Rust -- but a build without the channel must still be able to copy a
		// file, so the fallback keeps the walk. Same division as the browser's recursive mkdir: the
		// channel arm is loop-free, the owner arm is not.
		g_ownerStatus = copyOnOwner(g_pathA, g_pathB);
		g_ownerDone = true;
		return;
	case PathOp::None:
		return;
	}
	g_ownerStatus = result.has_value() ? deluge::io::Status::OK : result.error();
	g_ownerDone = true;
}

/// Issue the request on whichever arm is in force.
void issue() {
	using deluge::storage::bytes::ByteIo;
	if (viaChannel()) {
		ByteIo accepted = ByteIo::Failed;
		switch (g_op) {
		case PathOp::Unlink:
			accepted = deluge::storage::bytes::requestUnlink(g_pathA.c_str());
			break;
		case PathOp::Rename:
			accepted = deluge::storage::bytes::requestRename(g_pathA.c_str(), g_pathB.c_str());
			break;
		case PathOp::MkdirAll:
			accepted = deluge::storage::bytes::requestMkdirAll(g_pathA.c_str());
			break;
		case PathOp::SetTime:
			accepted = deluge::storage::bytes::requestSetTime(g_pathA.c_str(), g_stamp);
			break;
		case PathOp::ReadAt:
			accepted = deluge::storage::bytes::requestReadAt(g_pathA.c_str(), g_offset, g_blockLen);
			break;
		case PathOp::WriteAt:
			accepted = deluge::storage::bytes::requestWriteAt(g_pathA.c_str(), g_offset,
			                                                  reinterpret_cast<const char*>(g_block), g_blockLen);
			break;
		case PathOp::Stat:
			accepted = deluge::storage::bytes::requestStat(g_pathA.c_str());
			break;
		case PathOp::Copy:
			accepted = deluge::storage::bytes::requestCopy(g_pathA.c_str(), g_pathB.c_str());
			break;
		case PathOp::CreateEmpty:
			// The whole-file write with no bytes: it creates the parent, creates the file and
			// truncates it, which is precisely what an open-for-write meant.
			accepted = deluge::storage::bytes::requestWrite(g_pathA.c_str(), "", 0);
			break;
		case PathOp::None:
			return;
		}
		if (accepted == ByteIo::Pending) {
			g_issued = true;
		}
		else {
			g_refusals++;
		}
		return;
	}
	g_ownerDone = false;
	if (!deluge::storage::bytes::runInlineWhereLegal([](void*) { runOnOwner(); }, nullptr)) {
		// Refused: after boot, off the owner, only a channel that stopped answering reaches here. The host
		// is told the operation timed out rather than left waiting for a reply that never comes.
		g_ownerStatus = deluge::io::Status::TIMEOUT;
		g_ownerDone = true;
	}
	g_issued = true;
}

} // namespace

bool pathOpPending() {
	return g_op != PathOp::None;
}

bool pathOpStart(PathOp op, const char* a, const char* b, DelugeTimestamp stamp) {
	g_op = op;
	g_pathA = (a != nullptr) ? a : "";
	g_pathB = (b != nullptr) ? b : "";
	g_stamp = stamp;
	g_issued = false;
	g_refusals = 0;
	g_pendingTicks = 0;
	g_ownerDone = false;
	issue();
	if (!g_issued) {
		// Refused on the first attempt. Left PENDING deliberately rather than cleared: the handler is
		// re-entered next tick, sees the record, and `pathOpCollect` issues again.
		return false;
	}
	return true;
}

bool pathOpStartBlock(PathOp op, const char* path, uint32_t offset, const uint8_t* bytes, uint32_t len) {
	if (len > DELUGE_FILE_BYTES_BLOCK) {
		len = DELUGE_FILE_BYTES_BLOCK;
	}
	g_op = op;
	g_pathA = (path != nullptr) ? path : "";
	g_pathB.clear();
	g_offset = offset;
	g_blockLen = len;
	g_resultValue = 0;
	if (bytes != nullptr && len > 0) {
		// COPIED, so the caller's decode buffer is free the instant this returns -- a resumed handler
		// re-parses and re-decodes into that same buffer.
		memcpy(g_block, bytes, len);
	}
	g_issued = false;
	g_refusals = 0;
	g_pendingTicks = 0;
	g_ownerDone = false;
	issue();
	return g_issued;
}

std::optional<deluge::io::Status> pathOpCollectBlock(uint8_t* outBuf, uint32_t* outLen) {
	auto outcome = pathOpCollect();
	if (!outcome.has_value()) {
		return std::nullopt;
	}
	if (*outcome == deluge::io::Status::OK && outBuf != nullptr && outLen != nullptr) {
		memcpy(outBuf, g_block, g_blockLen);
		*outLen = g_blockLen;
	}
	return outcome;
}

uint32_t pathOpResultValue() {
	return g_resultValue;
}

std::optional<deluge::io::Status> pathOpCollect() {
	using deluge::storage::bytes::ByteIo;
	if (g_op == PathOp::None) {
		return std::nullopt;
	}
	if (!g_issued) {
		issue(); // a previous tick's request was refused
		return std::nullopt;
	}

	if (!viaChannel()) {
		if (!g_ownerDone) {
			return std::nullopt;
		}
		return g_ownerStatus;
	}

	DelugeStatus status = DELUGE_OK;
	ByteIo outcome = ByteIo::Failed;
	switch (g_op) {
	case PathOp::Unlink:
		outcome = deluge::storage::bytes::collectUnlink(g_pathA.c_str(), &status);
		break;
	case PathOp::Rename:
		outcome = deluge::storage::bytes::collectRename(g_pathB.c_str(), &status);
		break;
	case PathOp::MkdirAll:
		outcome = deluge::storage::bytes::collectMkdirAll(g_pathA.c_str(), &status);
		break;
	case PathOp::SetTime:
		outcome = deluge::storage::bytes::collectSetTime(g_pathA.c_str(), &status);
		break;
	case PathOp::ReadAt:
		outcome = deluge::storage::bytes::collectReadAt(g_pathA.c_str(), reinterpret_cast<char*>(g_block),
		                                                DELUGE_FILE_BYTES_BLOCK, &g_blockLen, &status);
		break;
	case PathOp::WriteAt:
		outcome = deluge::storage::bytes::collectWriteAt(g_pathA.c_str(), &g_resultValue, &status);
		break;
	case PathOp::Stat:
		outcome = deluge::storage::bytes::collectStat(g_pathA.c_str(), &g_resultValue, &status);
		break;
	case PathOp::Copy:
		outcome = deluge::storage::bytes::collectCopy(g_pathB.c_str(), &status);
		break;
	case PathOp::CreateEmpty:
		outcome = deluge::storage::bytes::collectWrite(g_pathA.c_str(), &status);
		break;
	case PathOp::None:
		return std::nullopt;
	}

	switch (outcome) {
	case ByteIo::Ready:
		return deluge::io::Status::OK;
	case ByteIo::Failed:
		return deluge::io::to_status(status);
	case ByteIo::Pending:
		if (++g_pendingTicks >= kMaxPendingTicks) {
			// The channel accepted and never answered. Re-issue on the synchronous arm, which
			// `viaChannel` now refuses to bypass.
			g_issued = false;
		}
		return std::nullopt;
	case ByteIo::Unknown:
		// Displaced before we took it, or dropped. Ask again -- every operation here is idempotent
		// in the sense that matters: doing it twice reaches the same state.
		g_issued = false;
		g_refusals++;
		return std::nullopt;
	}
	return std::nullopt;
}

void pathOpClear() {
	g_op = PathOp::None;
	g_stage = 0;
	g_offset = 0;
	g_blockLen = 0;
	g_resultValue = 0;
	g_issued = false;
	g_refusals = 0;
	g_pendingTicks = 0;
	g_ownerDone = false;
}

uint8_t pathOpStage() {
	return g_stage;
}

void pathOpSetStage(uint8_t stage) {
	g_stage = stage;
}

} // namespace deluge::storage
