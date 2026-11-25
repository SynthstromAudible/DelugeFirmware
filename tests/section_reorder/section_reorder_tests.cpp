#include "model/consequence/consequence_section_reorder.h"
#include "model/model_stack.h"
#include "model/song/song.h"
#include "playback/mode/session.h"
#include <array>
#include <cstdlib>
#include <iostream>

Session session{};

namespace {
void check(bool passed) {
	if (!passed) {
		std::cerr << "Section reorder regression failed\n";
		std::exit(1);
	}
}

uint8_t swapped(uint8_t value, uint8_t first, uint8_t second) {
	if (value == first) {
		return second;
	}
	if (value == second) {
		return first;
	}
	return value;
}

void roundTrip(uint8_t first, uint8_t second, uint8_t armed) {
	Song song{};
	std::array<Clip, 50> clips{};
	for (size_t index = 0; index < song.sections.size(); ++index) {
		song.sections.at(index) = {.repeats = static_cast<int16_t>(static_cast<int>(index) - 1),
		                           .midi_command = static_cast<int32_t>(100 + index),
		                           .colour = static_cast<uint8_t>(23 - index)};
		clips.at(index * 2).section = static_cast<uint8_t>(index);
		clips.at((index * 2) + 1).section = static_cast<uint8_t>(index);
		song.sessionClips.clips.push_back(&clips.at(index * 2));
		song.sessionClips.clips.push_back(&clips.at((index * 2) + 1));
	}
	clips.at(48).section = 254;
	clips.at(49).section = 255;
	song.sessionClips.clips.push_back(&clips.at(48));
	song.sessionClips.clips.push_back(&clips.at(49));
	song.sessionMacros = {{{.kind = SessionMacroKind::SECTION, .section = first},
	                       {.kind = SessionMacroKind::SECTION, .section = second},
	                       {.kind = SessionMacroKind::CLIP, .section = first},
	                       {.kind = SessionMacroKind::OUTPUT_CYCLE, .section = second},
	                       {.kind = SessionMacroKind::NONE, .section = first},
	                       {.kind = SessionMacroKind::SECTION, .section = 254},
	                       {.kind = SessionMacroKind::SECTION, .section = 255},
	                       {.kind = SessionMacroKind::SECTION, .section = first}}};
	const auto original_sections = song.sections;
	const auto original_macros = song.sessionMacros;
	const auto original_clips = clips;
	session.lastSectionArmed = armed;
	ModelStack stack{.song = &song};
	ConsequenceSectionReorder change(first, second);
	ConsequenceSectionReorder::swapSections(song, first, second);
	check(song.sections.at(first) == original_sections.at(second));
	check(song.sections.at(second) == original_sections.at(first));
	for (size_t index = 0; index < clips.size(); ++index) {
		const auto before = original_clips.at(index).section;
		check(clips.at(index).section == swapped(before, first, second));
	}
	check(song.sessionMacros.at(0).section == second);
	check(song.sessionMacros.at(1).section == first);
	for (size_t index = 2; index < 7; ++index) {
		check(song.sessionMacros.at(index) == original_macros.at(index));
	}
	check(session.lastSectionArmed == swapped(armed, first, second));
	const auto moved_sections = song.sections;
	const auto moved_macros = song.sessionMacros;
	const auto moved_clips = clips;
	const auto moved_armed = session.lastSectionArmed;
	for (int cycle = 0; cycle < 3; ++cycle) {
		check(change.revert(BEFORE, &stack) == Error::NONE);
		check(song.sections == original_sections && song.sessionMacros == original_macros);
		check(session.lastSectionArmed == armed);
		for (size_t index = 0; index < clips.size(); ++index) {
			check(clips.at(index).section == original_clips.at(index).section);
		}
		check(change.revert(AFTER, &stack) == Error::NONE);
		check(song.sections == moved_sections && song.sessionMacros == moved_macros);
		check(session.lastSectionArmed == moved_armed);
		for (size_t index = 0; index < clips.size(); ++index) {
			check(clips.at(index).section == moved_clips.at(index).section);
		}
	}
}

void successiveMoves() {
	Song song{};
	for (size_t index = 0; index < song.sections.size(); ++index) {
		song.sections.at(index).colour = static_cast<uint8_t>(index);
	}
	const auto original = song.sections;
	ModelStack stack{.song = &song};
	ConsequenceSectionReorder first(0, 1);
	ConsequenceSectionReorder second(1, 2);
	ConsequenceSectionReorder::swapSections(song, 0, 1);
	ConsequenceSectionReorder::swapSections(song, 1, 2);
	const auto moved = song.sections;
	check(second.revert(BEFORE, &stack) == Error::NONE);
	check(first.revert(BEFORE, &stack) == Error::NONE);
	check(song.sections == original);
	check(first.revert(AFTER, &stack) == Error::NONE);
	check(second.revert(AFTER, &stack) == Error::NONE);
	check(song.sections == moved);
}
} // namespace

int main() try {
	for (uint8_t first = 0; first < 24; ++first) {
		for (uint8_t second = 0; second < 24; ++second) {
			if (first == second) {
				continue;
			}
			for (uint8_t armed = 0; armed < 24; ++armed) {
				roundTrip(first, second, armed);
			}
			roundTrip(first, second, 254);
			roundTrip(first, second, 255);
		}
	}
	successiveMoves();
	std::cout << "Passed 14352 section-swap scenarios, repeated undo/redo and successive moves\n";
} catch (...) {
	std::cerr << "Unexpected exception during section reorder test\n";
	return 1;
}
