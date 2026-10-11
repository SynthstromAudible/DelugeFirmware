#pragma once
// Original native sequencing core; no MDD SnAkE source or assets are used.
#include <algorithm>
#include <array>
#include <cstdint>

namespace deluge::sequencer {
enum class SnakePath : uint8_t { Rows, Columns, Serpentine, Spiral, SerpentineColumns };
enum class SnakeDirection : uint8_t { Forward, Reverse, Pendulum, Random };
enum class SnakeTarget : uint8_t {
	None,
	PlaitsHarmonics,
	PlaitsTimbre,
	PlaitsMorph,
	FilterCutoff,
	DelayFeedback,
	ReverbSend,
	MidiCC,
	EnvelopeAttack,
	EnvelopeDecay,
	EnvelopeSustain,
	EnvelopeRelease,
	PlaitsModel
};
constexpr std::array<uint8_t, 16> snakePath(SnakePath shape) {
	switch (shape) {
	case SnakePath::Columns:
		return {0, 4, 8, 12, 1, 5, 9, 13, 2, 6, 10, 14, 3, 7, 11, 15};
	case SnakePath::Serpentine:
		return {0, 1, 2, 3, 7, 6, 5, 4, 8, 9, 10, 11, 15, 14, 13, 12};
	case SnakePath::SerpentineColumns:
		return {0, 4, 8, 12, 13, 9, 5, 1, 2, 6, 10, 14, 15, 11, 7, 3};
	case SnakePath::Spiral:
		return {0, 1, 2, 3, 7, 11, 15, 14, 13, 12, 8, 4, 5, 6, 10, 9};
	default:
		return {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15};
	}
}
struct SnakeLane {
	std::array<uint16_t, 16> values{};
	uint8_t length = 16;
	uint16_t division = 1; // Host transport ticks, never an independent clock.
	SnakeDirection direction = SnakeDirection::Forward;
	SnakePath path = SnakePath::Rows;
	bool enabled = true;
	uint8_t phase = 0;
	uint32_t seed = 1;
	uint16_t randomMinimum = 0;
	uint16_t randomMaximum = 65535;
	// Counter-based randomness: reproducible after seek/restart, constant work per tick.
	static uint64_t randomWord(uint64_t step, uint32_t seed) {
		uint64_t x = step + (static_cast<uint64_t>(seed) << 32) + 0x9e3779b97f4a7c15ULL;
		x = (x ^ (x >> 30)) * 0xbf58476d1ce4e5b9ULL;
		x = (x ^ (x >> 27)) * 0x94d049bb133111ebULL;
		return x ^ (x >> 31);
	}
	void randomize(uint16_t minimum, uint16_t maximum, uint32_t newSeed) {
		if (minimum > maximum)
			std::swap(minimum, maximum);
		seed = newSeed;
		randomMinimum = minimum;
		randomMaximum = maximum;
		const uint32_t range = static_cast<uint32_t>(maximum) - minimum + 1;
		for (unsigned i = 0; i < values.size(); ++i)
			values[i] = minimum + randomWord(i, seed) % range;
	}
	uint8_t cellAt(uint64_t tick) const {
		const uint8_t n = std::clamp<unsigned>(length, 1, 16);
		const uint64_t step = tick / std::max<unsigned>(division, 1) + phase;
		uint8_t index = 0;
		switch (direction) {
		case SnakeDirection::Forward:
			index = step % n;
			break;
		case SnakeDirection::Reverse:
			index = n - 1 - step % n;
			break;
		case SnakeDirection::Random:
			index = randomWord(step, seed) % n;
			break;
		case SnakeDirection::Pendulum: {
			const auto period = std::max<unsigned>(1, 2 * (n - 1));
			const auto phase = step % period;
			index = phase < n ? phase : period - phase;
			break;
		}
		}
		return snakePath(path)[index];
	}
	uint16_t valueAt(uint64_t tick) const { return enabled ? values[cellAt(tick)] : 0; }
};
struct SnakeMapping {
	SnakeTarget target = SnakeTarget::None;
	uint8_t oscillator = 0;
	uint8_t midiCC = 1;
	uint8_t envelope = 0; // ENV 1..4; host validates destination support.
	// Normalized endpoints allow inverted ranges without changing lane values.
	uint16_t minimum = 0;
	uint16_t maximum = 65535;
	static uint8_t plaitsModel(uint16_t value) {
		return (static_cast<uint32_t>(value) * 3) >> 16; // FM / Wave / VA, including 65535.
	}
	// Mod-matrix source value: unipolar 0..INT32_MAX, independently of destination range.
	static int32_t sourceValue(uint16_t value) {
		return static_cast<int32_t>(static_cast<uint64_t>(value) * 2147483647 / 65535);
	}
	uint16_t apply(uint16_t value) const {
		const int64_t span = static_cast<int32_t>(maximum) - minimum;
		return static_cast<uint16_t>(minimum + span * value / 65535);
	}
};
struct SnakeConfig {
	SnakePath path = SnakePath::Serpentine;
	SnakeLane pitch; // Values select positions along the grid traversal.
	std::array<uint8_t, 16> notes{};
	std::array<bool, 16> muted{};
	SnakeLane gate;
	SnakeLane velocity;
	std::array<SnakeLane, 4> modulation;
	std::array<SnakeMapping, 4> mappings;
	SnakeConfig() {
		for (unsigned i = 0; i < 16; ++i) {
			pitch.values[i] = i;
			notes[i] = 48 + i;
		}
		for (unsigned i = 0; i < 4; ++i) {
			modulation[i].seed = i + 2;
			modulation[i].division = 24;
		}
		gate.values.fill(65535);
		velocity.values.fill(100);
	}
};
struct SnakeFrame {
	uint8_t cell = 0;
	uint8_t note = 48;
	uint8_t velocity = 100;
	bool gate = true;
	std::array<uint16_t, 4> modulation{};
	std::array<int32_t, 4> customSources{};
};
class Snake {
public:
	static constexpr std::array<uint8_t, 16> path(SnakePath shape) { return snakePath(shape); }
	// Pure evaluation allows stop/restart/seek without stale read-head state.
	static SnakeFrame at(const SnakeConfig& config, uint64_t transportTick) {
		SnakeFrame frame;
		frame.cell = path(config.path)[config.pitch.valueAt(transportTick) % 16];
		frame.note = std::min<unsigned>(config.notes[frame.cell], 127);
		frame.gate = !config.muted[frame.cell] && config.gate.valueAt(transportTick) != 0;
		frame.velocity = std::clamp<unsigned>(config.velocity.valueAt(transportTick), 1, 127);
		for (unsigned i = 0; i < 4; ++i) {
			const auto raw = config.modulation[i].valueAt(transportTick);
			frame.customSources[i] = SnakeMapping::sourceValue(raw);
			frame.modulation[i] = config.mappings[i].apply(raw);
		}
		return frame;
	}
	// Bridge for a Sound-like destination. The transport driver supplies clip-relative ticks.
	// Discrete model routing is separate from the continuous mod-matrix sources.
	template <typename Destination>
	static void updateModulation(const SnakeConfig& config, uint64_t transportTick, Destination& destination) {
		const auto frame = at(config, transportTick);
		destination.setSnakeCustomValues(frame.customSources);
		std::array<int, 2> models{-1, -1};
		for (unsigned lane = 0; lane < config.mappings.size(); ++lane) {
			const auto& mapping = config.mappings[lane];
			if (config.modulation[lane].enabled && mapping.target == SnakeTarget::PlaitsModel
			    && mapping.oscillator < models.size())
				models[mapping.oscillator] = SnakeMapping::plaitsModel(frame.modulation[lane]);
		}
		for (unsigned oscillator = 0; oscillator < models.size(); ++oscillator)
			destination.setSnakePlaitsModel(oscillator, models[oscillator]);
	}
	template <typename Destination>
	static void clearModulation(Destination& destination) {
		destination.setSnakeCustomValues({});
		destination.setSnakePlaitsModel(0, -1);
		destination.setSnakePlaitsModel(1, -1);
	}
};
} // namespace deluge::sequencer
