// Recordings from a Deluge's internal mic for the mic velocity tests (data/mic_velocity): 16-bit WAVs, the mic on the
// first channel and, in stereo clips, what was sent to the speaker on the second; and a CSV of the pad presses in each.

#pragma once

#include <cstdint>
#include <cstdio>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

namespace mic_velocity_test {

inline std::string dataPath(char const* name) {
	return std::string(MIC_VELOCITY_TEST_DATA) + "/" + name;
}

struct Clip {
	std::vector<int32_t> mic;  ///< q31
	std::vector<int32_t> sent; ///< q31, empty for a mono clip
};

/// A 16-bit WAV with the plain 44-byte header the clips are written with
inline Clip readClip(char const* name, int32_t channels) {
	std::ifstream file(dataPath(name), std::ios::binary);
	std::vector<char> bytes((std::istreambuf_iterator<char>(file)), {});
	Clip clip;
	auto sample = [&](size_t i) {
		auto const s = static_cast<int16_t>(static_cast<uint8_t>(bytes[i]) | (static_cast<uint8_t>(bytes[i + 1]) << 8));
		return static_cast<int32_t>(s) << 16;
	};
	size_t const frameBytes = 2 * static_cast<size_t>(channels);
	for (size_t i = 44; i + frameBytes <= bytes.size(); i += frameBytes) {
		clip.mic.push_back(sample(i));
		if (channels == 2) {
			clip.sent.push_back(sample(i + 2));
		}
	}
	return clip;
}

struct Press {
	uint32_t frame;   ///< When the press was handled
	int32_t strength; ///< Asked for: 1 soft, 2 normal, 3 hard
	int32_t x, y;
};

inline std::vector<Press> readPresses(char const* name) {
	std::ifstream file(dataPath(name));
	std::vector<Press> presses;
	std::string line;
	while (std::getline(file, line)) {
		Press p{};
		if (std::sscanf(line.c_str(), "%u,%d,%d,%d", &p.frame, &p.strength, &p.x, &p.y) == 4) {
			presses.push_back(p);
		}
	}
	return presses;
}

} // namespace mic_velocity_test
