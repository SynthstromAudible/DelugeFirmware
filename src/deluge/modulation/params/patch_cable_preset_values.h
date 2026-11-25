#pragma once

#include "definitions_cxx.hpp"
#include <array>
#include <cstdint>
#include <span>

// A preset has at most kMaxNumPatchCables. New routes absent from that preset
// have a zero baseline. Nonzero entries never move. Before reusing a zero slot,
// the owner detaches its bindings in live and backed-up parameter managers.
struct PatchCablePresetValues {
	struct Entry {
		uint32_t id;
		int32_t value;
	};
	std::array<Entry, kMaxNumPatchCables> entries{};
	int32_t count = 0;
	static constexpr int32_t zero = 0;
	// The slot reuseZeroEntry() would hand out, so bindings to it can be detached first.
	[[nodiscard]] const int32_t* zeroEntry() const {
		for (const auto& entry : std::span(entries).first(count)) {
			if (entry.value == 0) {
				return &entry.value;
			}
		}
		return nullptr;
	}
	int32_t* reuseZeroEntry(uint32_t id) {
		for (auto& entry : std::span(entries).first(count)) {
			if (entry.value == 0) {
				entry.id = id;
				return &entry.value;
			}
		}
		return nullptr;
	}
	[[nodiscard]] const int32_t* find(uint32_t id) const {
		for (int32_t i = 0; i < count; ++i) {
			if (entries[i].id == id) {
				return &entries[i].value;
			}
		}
		return &zero;
	}
};
