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
#include "memory/general_memory_allocator.h"
#include "modulation/params/param.h"
#include <cstring>
#include <utility>

namespace params = deluge::modulation::params;

namespace deluge::note_locks {

static_assert(params::LOCAL_LAST <= 64, "VoiceLocks::mask needs a bit per local param");
static_assert(params::LOCAL_LAST <= 256, "Lock::param is a byte");

LockSetView pending;
uint16_t pendingIndex = 0;
uint16_t pendingLiveTag = 0;

bool locksSustain(const uint8_t* params, const int32_t* values, int32_t count) {
	for (int32_t i = 0; i < count; i++) {
		if (params[i] == params::LOCAL_ENV_0_SUSTAIN) {
			return values[i] != -2147483648;
		}
	}
	return false;
}

LiveEdit liveEdits[kMaxLiveEdits];
int32_t numArmedLiveEdits = 0;

namespace {
constexpr uint32_t kMaxSets = kInternFailed; // uint16_t indices, including the 0 sentinel; kInternFailed is never one

uint32_t headerWords(int32_t count) {
	return (uint32_t)(1 + count + 3) >> 2;
}

/// Sort by param, later duplicates win, drop unlockable params, cap at kMaxLocksPerNote. Returns the new count.
int32_t normalise(const Lock* locks, int32_t count, uint8_t* params, int32_t* values) {
	int32_t n = 0;
	for (int32_t i = 0; i < count; i++) {
		int32_t p = locks[i].param;
		if (!isLockableParam(p)) {
			continue;
		}
		int32_t j = 0;
		while (j < n && params[j] < p) {
			j++;
		}
		if (j < n && params[j] == p) {
			values[j] = locks[i].value;
			continue;
		}
		if (n == kMaxLocksPerNote) {
			continue;
		}
		memmove(&params[j + 1], &params[j], n - j);
		memmove(&values[j + 1], &values[j], (n - j) * sizeof(int32_t));
		params[j] = p;
		values[j] = locks[i].value;
		n++;
	}
	return n;
}

/// Grows a GMA allocation to hold at least `needed` elements of `size` bytes, copying the first `used`.
template <typename T>
bool grow(T*& array, uint32_t& capacity, uint32_t used, uint32_t needed, uint32_t minCapacity) {
	if (needed <= capacity) {
		return true;
	}
	uint32_t newCapacity = capacity ? capacity + (capacity >> 1) : minCapacity;
	if (newCapacity < needed) {
		newCapacity = needed;
	}
	T* newArray = (T*)GeneralMemoryAllocator::get().allocLowSpeed(newCapacity * sizeof(T));
	if (newArray == nullptr) {
		return false;
	}
	if (array != nullptr) {
		memcpy(newArray, array, used * sizeof(T));
		delugeDealloc(array);
	}
	array = newArray;
	capacity = newCapacity;
	return true;
}
} // namespace

bool isLockableParam(int32_t param) {
	return param >= 0 && param < params::FIRST_GLOBAL;
}

Pool::~Pool() {
	clear();
}

void Pool::clear() {
	if (words_ != nullptr) {
		delugeDealloc(words_);
	}
	if (offsets_ != nullptr) {
		delugeDealloc(offsets_);
	}
	if (hash_ != nullptr) {
		delugeDealloc(hash_);
	}
	words_ = nullptr;
	offsets_ = nullptr;
	hash_ = nullptr;
	numWords_ = capWords_ = numSets_ = capSets_ = hashCap_ = 0;
}

size_t Pool::bytesAllocated() const {
	return capWords_ * sizeof(uint32_t) + capSets_ * sizeof(uint32_t) + hashCap_ * sizeof(uint16_t);
}

uint32_t Pool::hashOf(const uint8_t* params, const int32_t* values, int32_t count) {
	uint32_t h = 2166136261u;
	for (int32_t i = 0; i < count; i++) {
		h = (h ^ params[i]) * 16777619u;
		h = (h ^ (uint32_t)values[i]) * 16777619u;
	}
	return h ^ (h >> 15);
}

LockSetView Pool::get(uint16_t index) const {
	if (index == 0 || index >= numSets_) {
		return {};
	}
	const uint32_t* entry = &words_[offsets_[index]];
	const uint8_t* bytes = (const uint8_t*)entry;
	int32_t count = bytes[0];
	return {count, bytes + 1, (const int32_t*)(entry + headerWords(count))};
}

bool Pool::equals(uint16_t index, const uint8_t* params, const int32_t* values, int32_t count) const {
	LockSetView v = get(index);
	return v.count == count && !memcmp(v.params, params, count) && !memcmp(v.values, values, count * sizeof(int32_t));
}

bool Pool::rehash(uint32_t newCapacity) {
	uint16_t* newHash = (uint16_t*)GeneralMemoryAllocator::get().allocLowSpeed(newCapacity * sizeof(uint16_t));
	if (newHash == nullptr) {
		return false;
	}
	memset(newHash, 0, newCapacity * sizeof(uint16_t));
	for (uint32_t i = 1; i < numSets_; i++) {
		LockSetView v = get(i);
		uint32_t slot = hashOf(v.params, v.values, v.count) & (newCapacity - 1);
		while (newHash[slot] != 0) {
			slot = (slot + 1) & (newCapacity - 1);
		}
		newHash[slot] = i;
	}
	if (hash_ != nullptr) {
		delugeDealloc(hash_);
	}
	hash_ = newHash;
	hashCap_ = newCapacity;
	return true;
}

uint16_t Pool::intern(const Lock* locks, int32_t count) {
	uint8_t params[kMaxLocksPerNote];
	int32_t values[kMaxLocksPerNote];
	count = normalise(locks, count, params, values);
	return internNormalised(params, values, count);
}

uint16_t Pool::internOrFail(const Lock* locks, int32_t count) {
	uint8_t params[kMaxLocksPerNote];
	int32_t values[kMaxLocksPerNote];
	count = normalise(locks, count, params, values);
	if (count == 0) {
		return 0;
	}
	uint16_t index = internNormalised(params, values, count);
	return index ? index : kInternFailed;
}

uint16_t Pool::internNormalised(const uint8_t* params, const int32_t* values, int32_t count) {
	if (count == 0) {
		return 0;
	}

	uint32_t h = hashOf(params, values, count);
	if (hashCap_ != 0) {
		for (uint32_t slot = h & (hashCap_ - 1); hash_[slot] != 0; slot = (slot + 1) & (hashCap_ - 1)) {
			if (equals(hash_[slot], params, values, count)) {
				return hash_[slot];
			}
		}
	}

	uint32_t firstIndex = numSets_ ? numSets_ : 1; // Index 0 is the sentinel
	if (firstIndex >= kMaxSets) {
		return 0;
	}
	// Keep the hash table at most half full, so probes stay short and always find an empty slot
	if ((firstIndex + 1) * 2 > hashCap_ && !rehash(hashCap_ ? hashCap_ * 2 : 64)) {
		return 0;
	}
	uint32_t entryWords = headerWords(count) + count;
	if (!grow(words_, capWords_, numWords_, numWords_ + entryWords, 256)
	    || !grow(offsets_, capSets_, numSets_, firstIndex + 1, 64)) {
		return 0;
	}

	uint32_t* entry = &words_[numWords_];
	memset(entry, 0, headerWords(count) * sizeof(uint32_t));
	uint8_t* bytes = (uint8_t*)entry;
	bytes[0] = count;
	memcpy(bytes + 1, params, count);
	memcpy(entry + headerWords(count), values, count * sizeof(int32_t));

	if (numSets_ == 0) {
		offsets_[0] = 0;
	}
	offsets_[firstIndex] = numWords_;
	numWords_ += entryWords;
	numSets_ = firstIndex + 1;

	uint32_t slot = h & (hashCap_ - 1);
	while (hash_[slot] != 0) {
		slot = (slot + 1) & (hashCap_ - 1);
	}
	hash_[slot] = firstIndex;
	return firstIndex;
}

uint16_t Pool::withLock(uint16_t base, int32_t param, int32_t value) {
	Lock locks[kMaxLocksPerNote + 1];
	LockSetView v = get(base);
	for (int32_t i = 0; i < v.count; i++) {
		locks[i] = {v.params[i], v.values[i]};
	}
	locks[v.count] = {(uint8_t)param, value}; // normalise() lets this later duplicate win
	return internOrFail(locks, v.count + 1);
}

uint16_t Pool::withoutLock(uint16_t base, int32_t param) {
	Lock locks[kMaxLocksPerNote];
	int32_t n = 0;
	LockSetView v = get(base);
	for (int32_t i = 0; i < v.count; i++) {
		if (v.params[i] != param) {
			locks[n++] = {v.params[i], v.values[i]};
		}
	}
	return internOrFail(locks, n);
}

uint16_t Pool::withLocks(uint16_t base, const Lock* edits, int32_t count) {
	Lock locks[2 * kMaxLocksPerNote];
	LockSetView v = get(base);
	int32_t n = 0;
	for (int32_t i = 0; i < v.count; i++) {
		locks[n++] = {v.params[i], v.values[i]};
	}
	for (int32_t i = 0; i < count && n < 2 * kMaxLocksPerNote; i++) {
		locks[n++] = edits[i]; // normalise() lets these later duplicates win
	}
	return internOrFail(locks, n);
}

uint16_t Pool::importFrom(const Pool& other, uint16_t index) {
	Lock locks[kMaxLocksPerNote];
	LockSetView v = other.get(index);
	for (int32_t i = 0; i < v.count; i++) {
		locks[i] = {v.params[i], v.values[i]};
	}
	return intern(locks, v.count);
}

bool Pool::buildCompacted(Pool& fresh, uint16_t* remap) const {
	// Size everything up front, so the copy below never reallocates and running out of memory can only happen here
	uint32_t keptSets = 0;
	uint32_t keptWords = 0;
	for (uint32_t i = 1; i < numSets_; i++) {
		if (remap[i] != 0) {
			keptSets++;
			LockSetView v = get(i);
			keptWords += headerWords(v.count) + v.count;
		}
	}
	remap[0] = 0;
	if (keptSets == 0) {
		for (uint32_t i = 1; i < numSets_; i++) {
			remap[i] = 0;
		}
		return true; // `fresh` stays empty
	}
	uint32_t hashCap = 64;
	while ((keptSets + 2) * 2 > hashCap) {
		hashCap *= 2;
	}
	if (!grow(fresh.words_, fresh.capWords_, 0, keptWords, keptWords)
	    || !grow(fresh.offsets_, fresh.capSets_, 0, keptSets + 1, keptSets + 1) || !fresh.rehash(hashCap)) {
		fresh.clear();
		return false;
	}
	for (uint32_t i = 1; i < numSets_; i++) {
		if (remap[i] == 0) {
			continue;
		}
		LockSetView v = get(i);
		uint16_t newIndex = fresh.internNormalised(v.params, v.values, v.count);
		if (newIndex == 0) { // Can't happen with the reservations above, but never hand out a half-built pool
			fresh.clear();
			return false;
		}
		remap[i] = newIndex;
	}
	return true;
}

void Pool::swap(Pool& other) {
	std::swap(words_, other.words_);
	std::swap(numWords_, other.numWords_);
	std::swap(capWords_, other.capWords_);
	std::swap(offsets_, other.offsets_);
	std::swap(numSets_, other.numSets_);
	std::swap(capSets_, other.capSets_);
	std::swap(hash_, other.hash_);
	std::swap(hashCap_, other.hashCap_);
}

bool VoiceLocks::setValue(int32_t param, int32_t value) {
	for (int32_t i = 0; i < count; i++) {
		if (params[i] == param) {
			values[i] = value;
			return true;
		}
	}
	if (count == kMaxLocksPerNote) {
		return false;
	}
	params[count] = param;
	values[count] = value;
	count++;
	mask |= (uint64_t)1 << param;
	return true;
}

bool LiveEdit::set(int32_t param, int32_t value) {
	for (int32_t i = 0; i < count; i++) {
		if (params[i] == param) {
			values[i] = value;
			return true;
		}
	}
	if (count == kMaxLocksPerNote) {
		return false;
	}
	params[count] = param;
	values[count] = value;
	count++;
	return true;
}

LockSetView LiveEdit::mergedWith(const LockSetView& committed, VoiceLocks& scratch) const {
	scratch.set(committed);
	for (int32_t i = 0; i < count; i++) {
		scratch.setValue(params[i], values[i]);
	}
	return {scratch.count, scratch.params, scratch.values};
}

void armLiveEdit(int32_t slot, const void* song, const void* clip, int32_t noteRowId, uint32_t pos, uint32_t length,
                 uint32_t wrapLevel, bool samePress) {
	static uint16_t nextTag = 0;
	LiveEdit& e = liveEdits[slot];
	if (!e.armed()) {
		numArmedLiveEdits++;
	}
	if (!samePress || e.tag == 0) {
		if (++nextTag == 0) {
			nextTag = 1;
		}
		e.tag = nextTag;
		e.count = 0;
	}
	e.song = song;
	e.clip = clip;
	e.noteRowId = noteRowId;
	e.pos = pos;
	e.length = length ? length : 1;
	e.wrapLevel = wrapLevel ? wrapLevel : 1u << 31;
}

void disarmLiveEdit(int32_t slot) {
	LiveEdit& e = liveEdits[slot];
	if (e.armed()) {
		numArmedLiveEdits--;
	}
	e.song = nullptr;
	e.clip = nullptr;
	e.count = 0;
	// The tag stays, so re-arming the same press (armLiveEdit's samePress) keeps reaching the voices it started
}

void disarmLiveEditsFor(const void* owner) {
	for (int32_t i = 0; i < kMaxLiveEdits; i++) {
		if (liveEdits[i].armed() && (liveEdits[i].song == owner || liveEdits[i].clip == owner)) {
			disarmLiveEdit(i);
		}
	}
}

const LiveEdit* findLiveEditByTag(uint16_t tag) {
	for (const LiveEdit& e : liveEdits) {
		if (e.armed() && e.tag == tag) {
			return &e;
		}
	}
	return nullptr;
}

const LiveEdit* findLiveEdit(const void* clip, int32_t noteRowId, uint32_t pos) {
	for (const LiveEdit& e : liveEdits) {
		if (e.armed() && e.matches(clip, noteRowId, pos)) {
			return &e;
		}
	}
	return nullptr;
}

void VoiceLocks::set(const LockSetView& view) {
	count = view.count < kMaxLocksPerNote ? view.count : kMaxLocksPerNote;
	mask = 0;
	for (int32_t i = 0; i < count; i++) {
		params[i] = view.params[i];
		values[i] = view.values[i];
		mask |= (uint64_t)1 << view.params[i];
	}
}

} // namespace deluge::note_locks
