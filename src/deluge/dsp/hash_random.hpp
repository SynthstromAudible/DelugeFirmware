/*
 * Copyright © 2024-2025 Owlet Records
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
 *
 * --- Additional terms under GNU GPL version 3 section 7 ---
 * This file requires preservation of the above copyright notice and author attribution
 * in all copies or substantial portions of this file.
 */

#pragma once

#include <cstdint>

namespace deluge::dsp::hash {

// ============================================================================
// Hash-based random utilities for fast deterministic randomness
// ~10 cycles for full hash, ~1-4 cycles for derived values
// Use when smooth evolution is not needed (probability decisions, binary choices)
// ============================================================================

/**
 * Fast integer hash (MurmurHash3 finalizer)
 * Proven statistical quality, ~10 cycles on ARM
 * @param h Input value to hash
 * @return Well-distributed 32-bit hash
 */
[[gnu::always_inline]] inline uint32_t mix(uint32_t h) {
	h ^= h >> 16;
	h *= 0x85ebca6bu;
	h ^= h >> 13;
	h *= 0xc2b2ae35u;
	h ^= h >> 16;
	return h;
}

/**
 * Cheap per-param hash derivation from precomputed base hash
 * Uses XOR + rotate + one avalanche step (~4 cycles vs ~10 for full mix)
 * @param baseHash Precomputed hash from seed
 * @param paramSeed Per-param decorrelation offset
 * @return Well-distributed 32-bit hash
 */
[[gnu::always_inline]] inline uint32_t derive(uint32_t baseHash, uint32_t paramSeed) {
	uint32_t h = baseHash ^ paramSeed;
	// ARM ROR is single-cycle, provides good bit mixing
	h = (h >> 7) | (h << 25); // rotr(h, 7)
	h ^= h >> 16;             // One avalanche step for quality
	return h;
}

/**
 * Generate deterministic random [0,1] from combined seed
 * @param seed Base seed (e.g., sliceIndex)
 * @param paramSeed Per-param decorrelation offset
 * @return Pseudo-random float in [0,1]
 */
[[gnu::always_inline]] inline float random(uint32_t seed, uint32_t paramSeed) {
	uint32_t h = mix(seed ^ paramSeed);
	return static_cast<float>(h) * (1.0f / 4294967296.0f);
}

/**
 * Extract multiple bools/values from a single hash (~10 cycles for up to 32 bools)
 * Each bit is an independent 50% probability decision.
 *
 * For non-50% thresholds, combine bits:
 *   - 25%: bit0 && bit1
 *   - 75%: bit0 || bit1
 *   - 12.5%: bit0 && bit1 && bit2
 *
 * Usage:
 *   Bits bits{sliceIndex};
 *   bool reverse = bits.get(0);              // 50%
 *   bool rareEvent = bits.all(0b11);         // 25% (bits 0 AND 1)
 *   uint8_t val = bits.getNibble(0);         // 4-bit value [0-15]
 *   float coarse = bits.getFloat4(0);        // [0,1) with 16 levels
 */
struct Bits {
	uint32_t bits; ///< 32 random bits from one hash

	explicit Bits(uint32_t seed) : bits(mix(seed)) {}

	/// Get single bit as bool (~1 cycle)
	[[gnu::always_inline]] bool get(uint8_t index) const { return (bits >> index) & 1; }

	/// Check if ALL bits in mask are set (AND) - for low probability events
	/// P(all N bits) = (1/2)^N: 2 bits=25%, 3 bits=12.5%, 4 bits=6.25%
	[[gnu::always_inline]] bool all(uint32_t mask) const { return (bits & mask) == mask; }

	/// Check if ANY bits in mask are set (OR) - for high probability events
	/// P(any of N bits) = 1 - (1/2)^N: 2 bits=75%, 3 bits=87.5%, 4 bits=93.75%
	[[gnu::always_inline]] bool any(uint32_t mask) const { return (bits & mask) != 0; }

	/// Count set bits in range [0, count) for variable probability
	[[gnu::always_inline]] uint8_t countBits(uint8_t count) const {
		uint32_t mask = (1u << count) - 1;
		return __builtin_popcount(bits & mask);
	}

	/// Get N bits as integer [0, 2^N) for uniform random in power-of-2 range
	[[gnu::always_inline]] uint32_t getBits(uint8_t startBit, uint8_t count) const {
		return (bits >> startBit) & ((1u << count) - 1);
	}

	/// Get 4-bit value [0-15] from nibble index (8 independent values per hash)
	[[gnu::always_inline]] uint8_t getNibble(uint8_t index) const { return (bits >> (index * 4)) & 0xF; }

	/// Get extended nibble using rotation to generate more values from one hash
	/// Indices 0-7: direct nibbles, 8-15: rotated view, 16-23: second rotation
	/// Cost: ~2 extra cycles per 8 additional nibbles
	[[gnu::always_inline]] uint8_t getNibbleExt(uint8_t index) const {
		uint32_t h;
		if (index < 8) {
			h = bits;
		}
		else if (index < 16) {
			h = ((bits >> 13) | (bits << 19)) ^ 0x5A5A5A5Au;
			index -= 8;
		}
		else {
			h = ((bits >> 7) | (bits << 25)) ^ 0xA5A5A5A5u;
			index -= 16;
		}
		return (h >> (index * 4)) & 0xF;
	}

	/// Get 4-bit value scaled to [0,1) with 16 discrete levels (~2 cycles)
	[[gnu::always_inline]] float getFloat4(uint8_t nibbleIndex) const {
		return static_cast<float>(getNibble(nibbleIndex)) * (1.0f / 16.0f);
	}

	/// Get extended nibble scaled to [0,1) - 24 values from one hash
	[[gnu::always_inline]] float getFloat4Ext(uint8_t index) const {
		return static_cast<float>(getNibbleExt(index)) * (1.0f / 16.0f);
	}

	/// Get 8-bit value scaled to [0,1) with 256 discrete levels (~2 cycles)
	[[gnu::always_inline]] float getFloat8(uint8_t byteIndex) const {
		uint8_t byte = (bits >> (byteIndex * 8)) & 0xFF;
		return static_cast<float>(byte) * (1.0f / 256.0f);
	}

	/// Map 4-bit value to discrete set (e.g., subdivisions {1,2,3,4,6,8})
	template <typename T>
	[[gnu::always_inline]] T mapToTable(uint8_t nibbleIndex, const T* table, uint8_t tableSize) const {
		uint8_t idx = (getNibble(nibbleIndex) * tableSize) >> 4;
		return table[idx];
	}

	/// Coarse threshold using 4-bit groups (16 probability levels)
	/// @param nibbleIndex Which 4-bit group (0-7)
	/// @param threshold Threshold 0-15: P(true) = threshold/16
	[[gnu::always_inline]] bool threshold4(uint8_t nibbleIndex, uint8_t threshold) const {
		return getNibble(nibbleIndex) < threshold;
	}

	/// Coarse threshold using 3-bit groups (8 probability levels)
	/// @param tripleIndex Which 3-bit group (0-9)
	/// @param threshold Threshold 0-7: P(true) = threshold/8
	[[gnu::always_inline]] bool threshold3(uint8_t tripleIndex, uint8_t threshold) const {
		uint8_t triple = (bits >> (tripleIndex * 3)) & 0x7;
		return triple < threshold;
	}
};

/// Probability levels for 4-bit threshold (use with Bits::threshold4)
namespace prob {
constexpr uint8_t p0 = 0;    ///< 0%
constexpr uint8_t p6 = 1;    ///< 6.25%
constexpr uint8_t p12 = 2;   ///< 12.5%
constexpr uint8_t p19 = 3;   ///< 18.75%
constexpr uint8_t p25 = 4;   ///< 25%
constexpr uint8_t p31 = 5;   ///< 31.25%
constexpr uint8_t p37 = 6;   ///< 37.5%
constexpr uint8_t p44 = 7;   ///< 43.75%
constexpr uint8_t p50 = 8;   ///< 50%
constexpr uint8_t p56 = 9;   ///< 56.25%
constexpr uint8_t p62 = 10;  ///< 62.5%
constexpr uint8_t p69 = 11;  ///< 68.75%
constexpr uint8_t p75 = 12;  ///< 75%
constexpr uint8_t p81 = 13;  ///< 81.25%
constexpr uint8_t p87 = 14;  ///< 87.5%
constexpr uint8_t p94 = 15;  ///< 93.75%
constexpr uint8_t p100 = 16; ///< 100% (always true)
} // namespace prob

/**
 * Context for hash-based random evaluations with amortized cost
 *
 * Amortized cost: First param ~10 cycles (mix), subsequent ~4 cycles (derive)
 * Total for N params: 10 + 4*(N-1) cycles vs 10*N without amortization
 *
 * Usage:
 *   Context ctx{sliceIndex};
 *   bool shouldReverse = ctx.evalBool(0x12345678u, 0.3f);
 *   float amount = ctx.evalFloat(0x9ABCDEF0u);
 */
struct Context {
	uint32_t baseHash; ///< Precomputed hash of seed (amortizes across params)

	explicit Context(uint32_t seed) : baseHash(mix(seed)) {}

	/// Precompute threshold for fast integer comparison (can be constexpr)
	static constexpr uint32_t toU32Threshold(float threshold) {
		return (threshold >= 1.0f)   ? 0xFFFFFFFFu
		       : (threshold <= 0.0f) ? 0u
		                             : static_cast<uint32_t>(threshold * 4294967296.0f);
	}

	/// Get random float [0,1] for a param using cheap derivation
	[[gnu::always_inline]] float getFloat(uint32_t paramSeed) const {
		uint32_t h = derive(baseHash, paramSeed);
		return static_cast<float>(h) * (1.0f / 4294967296.0f);
	}

	/// Fast boolean eval using integer comparison (~5 cycles)
	[[gnu::always_inline]] bool evalBoolFast(uint32_t paramSeed, uint32_t thresholdU32) const {
		return derive(baseHash, paramSeed) < thresholdU32;
	}

	/// Evaluate hash and compare to threshold
	[[gnu::always_inline]] bool evalBool(uint32_t paramSeed, float threshold) const {
		return evalBoolFast(paramSeed, toU32Threshold(threshold));
	}

	/// Evaluate hash and return raw random value [0,1]
	[[gnu::always_inline]] float evalFloat(uint32_t paramSeed) const { return getFloat(paramSeed); }

	/// Integer-only duty cycle with 8-bit precision for activity, 4-bit for magnitude
	/// @param threshold Active threshold [0-255]: hash byte < threshold = active
	/// @return 16 if inactive, else [0-15] magnitude from different hash bits
	/// ~4 cycles, pure integer
	[[gnu::always_inline]] uint8_t evalDutyU8(uint32_t paramSeed, uint8_t threshold) const {
		uint32_t h = derive(baseHash, paramSeed);
		uint8_t byte = h & 0xFF; // 8 bits for activity decision
		if (byte >= threshold) {
			return 16; // Inactive sentinel
		}
		return (h >> 8) & 0xF; // 4 bits from different position for magnitude
	}

	/// Evaluate hash to integer range [0, max)
	[[gnu::always_inline]] int32_t evalInt(uint32_t paramSeed, int32_t max) const {
		if (max <= 0) {
			return 0;
		}
		uint32_t h = derive(baseHash, paramSeed);
		return static_cast<int32_t>(h % static_cast<uint32_t>(max));
	}
};

} // namespace deluge::dsp::hash
