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

#include "storage/temp_swap.h"

#include "io/file.hpp"
#include "libdeluge/file_bytes.h"
#include "libdeluge/file_io.h"
#include "storage/byte_channel.h"

#include <utility>

namespace deluge::storage {

namespace {
/// The longest path the byte and write channels key, in characters: `DELUGE_FILE_BYTES_MAX_PATH` less its NUL.
constexpr size_t kMaxPathChars = DELUGE_FILE_BYTES_MAX_PATH - 1;
constexpr std::string_view kTempSuffix = ".tmp";
} // namespace

std::string tempPathFor(std::string_view target) {
	if (target.empty() || target.size() + kTempSuffix.size() > kMaxPathChars) {
		return {};
	}
	std::string temp(target);
	temp.append(kTempSuffix);
	return temp;
}

SwapTick TempSwap::step() {
	using bytes::ByteIo;

	DelugeStatus status = DELUGE_OK;
	switch (stage_) {
	case Stage::Done:
		return SwapTick::Done;

	case Stage::Unlink:
		switch (bytes::collectUnlink(target_.c_str(), &status)) {
		case ByteIo::Unknown:
			// Ask -- and honour the answer: a path that cannot be keyed reports Failed, and discarding that leaves
			// this asking a channel that will never reply.
			return bytes::requestUnlink(target_.c_str()) == ByteIo::Failed ? SwapTick::Failed : SwapTick::Waiting;
		case ByteIo::Pending:
			return SwapTick::Waiting;
		case ByteIo::Failed:
			++servedOps_;
			if (status != DELUGE_ERR_NOT_FOUND) {
				return SwapTick::Failed;
			}
			// Already gone: the state a delete asks for.
			stage_ = Stage::Rename;
			return SwapTick::Advanced;
		case ByteIo::Ready:
			++servedOps_;
			stage_ = Stage::Rename;
			return SwapTick::Advanced;
		}
		return SwapTick::Failed;

	case Stage::Rename:
		switch (bytes::collectRename(target_.c_str(), &status)) {
		case ByteIo::Unknown:
			return bytes::requestRename(temp_.c_str(), target_.c_str()) == ByteIo::Failed ? SwapTick::Failed
			                                                                              : SwapTick::Waiting;
		case ByteIo::Pending:
			return SwapTick::Waiting;
		case ByteIo::Failed:
			++servedOps_;
			return SwapTick::Failed;
		case ByteIo::Ready:
			++servedOps_;
			stage_ = Stage::Done;
			return SwapTick::Done;
		}
		return SwapTick::Failed;
	}
	return SwapTick::Failed;
}

bool swapSynchronously(const std::string& target, const std::string& temp) {
	auto removed = deluge::io::unlink(target);
	if (!removed.has_value() && removed.error() != deluge::io::Status::NOT_FOUND) {
		return false;
	}
	return deluge::io::rename(temp, target).has_value();
}

void discardTemp(const std::string& temp) {
	if (bytes::usingByteChannel()) {
		(void)bytes::submitUnlink(temp.c_str());
		return;
	}
	std::string path = temp;
	(void)bytes::runInlineWhereLegal([](void* ctx) { (void)deluge::io::unlink(*static_cast<std::string*>(ctx)); },
	                                 &path);
}

} // namespace deluge::storage
