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

#include "storage/byte_channel.h"

#include <string>

namespace deluge::storage {

namespace {

/// How many consecutive refusals before falling back to the storage owner.
///
/// The channel is single-flight across all operation kinds, so another consumer's operation in
/// flight refuses this one. Retrying is right -- the handler is re-entered every tick anyway -- but
/// an unbounded retry would let a busy channel stall the SysEx queue behind one request.
constexpr int32_t kMaxRefusals = 8;

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

/// Whether the request has been issued (as opposed to started but still refused).
bool g_issued = false;
int32_t g_refusals = 0;
int32_t g_pendingTicks = 0;

/// The owner arm's answer, which arrives on a later tick than the dispatch that asked for it.
bool g_ownerDone = false;
deluge::io::Status g_ownerStatus = deluge::io::Status::OK;

/// Whether the channel should carry this operation, or the owner arm should.
bool viaChannel() {
	return deluge::storage::bytes::usingByteChannel() && g_refusals < kMaxRefusals && g_pendingTicks < kMaxPendingTicks;
}

/// Run the operation synchronously. The body of the owner arm's dispatch, and never called anywhere
/// a card wait is disallowed.
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
	deluge::storage::bytes::runOnStorageOwner([](void*) { runOnOwner(); }, nullptr);
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
			// The channel accepted and never answered. Re-issue on the owner arm, which `viaChannel`
			// now refuses to bypass.
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
