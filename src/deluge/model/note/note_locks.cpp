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

#include "model/note/note_locks.h"
#include "modulation/params/param.h"
#include "util/exceptions.h"
#include <algorithm>
#include <cstring>

namespace params = deluge::modulation::params;

namespace deluge::note_locks {

static_assert(params::FIRST_GLOBAL <= 64, "LockSet::mask() needs a bit per local param");

bool isLockableParam(int32_t param) {
	return param >= 0 && param < params::FIRST_GLOBAL;
}

// LockSet -----------------------------------------------------------------------------------------------------------

bool LockSet::set(int32_t param, int32_t value) {
	if (!isLockableParam(param)) {
		return false;
	}
	auto end = params_.begin() + count_;
	auto at = std::lower_bound(params_.begin(), end, param);
	auto valueAt = values_.begin() + (at - params_.begin());
	if (at == end || *at != param) {
		if (full()) {
			return false;
		}
		std::copy_backward(at, end, end + 1);
		std::copy_backward(valueAt, values_.begin() + count_, values_.begin() + count_ + 1);
		*at = param;
		count_++;
		mask_ |= paramMask(param);
	}
	*valueAt = value;
	return true;
}

bool LockSet::remove(int32_t param) {
	if (!isLockableParam(param) || !has(param)) {
		return false;
	}
	auto end = params_.begin() + count_;
	auto at = std::find(params_.begin(), end, param);
	std::copy(at + 1, end, at);
	auto valueAt = values_.begin() + (at - params_.begin());
	std::copy(valueAt + 1, values_.begin() + count_, valueAt);
	count_--;
	mask_ &= ~paramMask(param);
	return true;
}

bool LockSet::merge(const LockSet& other) {
	bool allFit = true;
	for (int32_t i = 0; i < other.count_; i++) {
		allFit = set(other.params_[i], other.values_[i]) && allFit;
	}
	return allFit;
}

bool LockSet::operator==(const LockSet& other) const {
	return mask_ == other.mask_ && std::ranges::equal(values(), other.values());
}

bool locksSustain(const LockSet& locks) {
	return locks.get(params::LOCAL_ENV_0_SUSTAIN).value_or(INT32_MIN) != INT32_MIN;
}

// Pool --------------------------------------------------------------------------------------------------------------

namespace {
constexpr uint32_t kMaxSets = 0xFFFF; // uint16_t indices, including the 0 sentinel
constexpr uint32_t kMinHashSize = 64;

/// Words taken by an entry's count byte and param bytes
uint32_t headerWords(int32_t count) {
	return static_cast<uint32_t>(1 + count + 3) >> 2;
}

uint32_t hashOf(const LockSet& locks) {
	uint32_t h = 2166136261u; // FNV-1a
	for (int32_t i = 0; i < locks.size(); i++) {
		h = (h ^ locks.params()[i]) * 16777619u;
		h = (h ^ static_cast<uint32_t>(locks.values()[i])) * 16777619u;
	}
	return h ^ (h >> 15);
}
} // namespace

LockSet Pool::get(uint16_t index) const {
	LockSet locks;
	if (index == 0 || index >= offsets_.size()) {
		return locks;
	}
	const uint32_t* entry = &words_[offsets_[index]];
	const auto* bytes = reinterpret_cast<const uint8_t*>(entry);
	int32_t count = bytes[0];
	const uint32_t* values = entry + headerWords(count);
	for (int32_t i = 0; i < count; i++) {
		locks.set(bytes[1 + i], static_cast<int32_t>(values[i]));
	}
	return locks;
}

uint16_t Pool::lookup(uint32_t hash, const LockSet& locks) const {
	if (hash_.empty()) {
		return 0;
	}
	uint32_t slotMask = hash_.size() - 1;
	for (uint32_t slot = hash & slotMask; hash_[slot] != 0; slot = (slot + 1) & slotMask) {
		if (get(hash_[slot]) == locks) {
			return hash_[slot];
		}
	}
	return 0;
}

void Pool::addToHash(uint32_t hash, uint16_t index) {
	uint32_t slotMask = hash_.size() - 1;
	uint32_t slot = hash & slotMask;
	while (hash_[slot] != 0) {
		slot = (slot + 1) & slotMask;
	}
	hash_[slot] = index;
}

void Pool::rehash(uint32_t newCapacity) {
	Array<uint16_t> fresh(newCapacity); // Zeroed. Throws if out of memory, with nothing changed.
	hash_.swap(fresh);
	for (uint32_t i = 1; i < offsets_.size(); i++) {
		addToHash(hashOf(get(i)), i);
	}
}

std::optional<uint16_t> Pool::intern(const LockSet& locks) {
	if (locks.empty()) {
		return 0;
	}
	uint32_t hash = hashOf(locks);
	if (uint16_t existing = lookup(hash, locks)) {
		return existing;
	}

	uint32_t index = offsets_.empty() ? 1 : offsets_.size();
	if (index >= kMaxSets) {
		return std::nullopt;
	}
	const size_t start = words_.size();
	try {
		// Keep the hash table at most half full, so probes stay short and always find an empty slot
		if ((index + 1) * 2 > hash_.size()) {
			rehash(hash_.empty() ? kMinHashSize : hash_.size() * 2);
		}
		if (offsets_.empty()) {
			offsets_.push_back(0); // The sentinel
		}
		words_.resize(start + headerWords(locks.size()) + locks.size()); // Zeroed, so the header's padding is too
		auto* bytes = reinterpret_cast<uint8_t*>(&words_[start]);
		bytes[0] = locks.size();
		std::ranges::copy(locks.params(), bytes + 1);
		std::ranges::copy(locks.values(), &words_[start + headerWords(locks.size())]);
		offsets_.push_back(start); // The entry exists from here
	} catch (deluge::exception&) {
		words_.resize(start);
		return std::nullopt;
	}
	addToHash(hash, index);
	return index;
}

} // namespace deluge::note_locks
