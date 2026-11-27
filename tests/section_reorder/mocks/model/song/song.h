#ifndef DELUGE_SECTION_REORDER_TEST_MODEL_SONG_SONG_H
#define DELUGE_SECTION_REORDER_TEST_MODEL_SONG_SONG_H
#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>
struct Clip {
	uint8_t section;
};
struct ClipArray {
	std::vector<Clip*> clips;
	[[nodiscard]] int32_t getNumElements() const { return static_cast<int32_t>(clips.size()); }
	[[nodiscard]] Clip* getClipAtIndex(int32_t index) const { return clips.at(static_cast<size_t>(index)); }
};
struct Section {
	int16_t repeats;
	int32_t midi_command;
	uint8_t colour;
	bool operator==(const Section&) const = default;
};
enum class SessionMacroKind { NONE, SECTION, CLIP, OUTPUT_CYCLE };
struct SessionMacro {
	SessionMacroKind kind;
	uint8_t section;
	bool operator==(const SessionMacro&) const = default;
};
// Match the production names used by the consequence; the remaining Song state is not involved.
struct Song {
	int32_t songGridScrollY = 0;
	ClipArray sessionClips;
	std::array<Section, 24> sections{};
	std::array<SessionMacro, 8> sessionMacros{};
};

#endif
