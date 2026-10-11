#pragma once
#include "sequencer/snake.h"
#include <string_view>
namespace deluge::sequencer {
// Version is part of the attribute name (snakeCustomV2). Explicit bytes, no ABI-dependent struct dumps.
class SnakeModulationCodec {
public:
	static constexpr unsigned bytes = 213;
	using Text = std::array<char, bytes * 2 + 1>;
	static Text encode(const SnakeConfig& config, bool enabled) {
		Text text{};
		unsigned pos = 0;
		auto put = [&](uint64_t value, unsigned count) {
			constexpr char hex[] = "0123456789abcdef";
			for (unsigned i = 0; i < count; ++i) {
				const auto byte = (value >> (i * 8)) & 255;
				text[pos++] = hex[byte >> 4];
				text[pos++] = hex[byte & 15];
			}
		};
		put(enabled, 1);
		for (unsigned i = 0; i < 4; ++i) {
			const auto& lane = config.modulation[i];
			const auto& mapping = config.mappings[i];
			for (auto value : lane.values)
				put(value, 2);
			put(lane.division, 2);
			put(lane.length, 1);
			put(static_cast<unsigned>(lane.direction), 1);
			put(lane.seed, 4);
			put(lane.randomMinimum, 2);
			put(lane.randomMaximum, 2);
			put(static_cast<unsigned>(mapping.target), 1);
			put(mapping.oscillator, 1);
			put(mapping.minimum, 2);
			put(mapping.maximum, 2);
			put(static_cast<unsigned>(lane.path), 1);
			put(lane.enabled, 1);
			put(lane.phase, 1);
		}
		return text;
	}
	static bool decode(std::string_view text, SnakeConfig& config, bool& enabled) {
		const bool legacy = text.size() == 402;
		if (!legacy && text.size() != bytes * 2)
			return false;
		for (char c : text)
			if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f')))
				return false;
		unsigned pos = 0;
		auto get = [&](unsigned count) {
			uint32_t value = 0;
			auto nibble = [](char c) { return c <= '9' ? c - '0' : c - 'a' + 10; };
			for (unsigned i = 0; i < count; ++i) {
				const unsigned byte = (nibble(text[pos]) << 4) | nibble(text[pos + 1]);
				pos += 2;
				value |= byte << (i * 8);
			}
			return value;
		};
		SnakeConfig candidate = config;
		const auto enable = get(1);
		if (enable > 1)
			return false;
		for (unsigned i = 0; i < 4; ++i) {
			auto& lane = candidate.modulation[i];
			auto& mapping = candidate.mappings[i];
			for (auto& value : lane.values)
				value = get(2);
			lane.division = get(2);
			lane.length = get(1);
			const auto direction = get(1);
			lane.seed = get(4);
			lane.randomMinimum = get(2);
			lane.randomMaximum = get(2);
			const auto target = get(1);
			mapping.oscillator = get(1);
			mapping.minimum = get(2);
			mapping.maximum = get(2);
			if (!lane.division || !lane.length || lane.length > 16
			    || direction > static_cast<unsigned>(SnakeDirection::Random)
			    || target > static_cast<unsigned>(SnakeTarget::PlaitsModel) || mapping.oscillator > 1)
				return false;
			const auto path = legacy ? 0 : get(1);
			const auto laneEnabled = legacy ? 1 : get(1);
			const auto phase = legacy ? 0 : get(1);
			if (path > static_cast<unsigned>(SnakePath::SerpentineColumns) || laneEnabled > 1 || phase > 15)
				return false;
			lane.path = static_cast<SnakePath>(path);
			lane.enabled = laneEnabled;
			lane.phase = phase;
			lane.direction = static_cast<SnakeDirection>(direction);
			mapping.target = static_cast<SnakeTarget>(target);
		}
		config = candidate;
		enabled = enable;
		return true;
	}
};
} // namespace deluge::sequencer
