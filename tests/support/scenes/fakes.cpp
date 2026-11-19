#include "fakes.h"

namespace deluge::scenes::bind::test {

uint8_t FakeSoundFacts::loaded_sources(project::SlotAddress slot) const {
	asked.push_back(slot);
	const auto found = loaded.find(slot.id);
	return found != loaded.end() ? found->second : 0;
}

const instrument::SoundState* FakeSoundFacts::sound_state(project::SlotAddress slot) const {
	const auto found = states.find(slot.id);
	return found != states.end() ? &found->second : nullptr;
}

bool FakeSoundFacts::kit_takes_whole_folder(project::SlotAddress slot) const {
	const auto found = whole_folder.find(slot.id);
	return found != whole_folder.end() && found->second;
}

std::optional<uint16_t> FakeSoundFacts::default_range(project::SlotAddress slot, uint8_t source) const {
	const auto found = default_ranges.find({slot.id, source});
	return found != default_ranges.end() ? std::optional{found->second} : std::nullopt;
}

std::optional<uint64_t> FakeSoundFacts::range_identity(project::SlotAddress slot, uint8_t source,
                                                       uint16_t range) const {
	const auto found = identities.find({slot.id, source, range});
	return found != identities.end() ? std::optional{found->second} : std::nullopt;
}

std::optional<uint16_t> FakeSoundFacts::take_range(const TakeTarget& /*target*/) const {
	return take;
}

CableList FakeSoundFacts::cables(const project::ParamAddress& /*owner*/) const {
	CableList list;
	for (const CableFact& cable : cable_list) {
		if (list.count < list.items.size()) {
			list.items[list.count++] = cable;
		}
	}
	return list;
}

std::optional<AudioClipFact> FakeClipFacts::audio_clip(project::ClipAddress clip) const {
	const auto found = audio_clips.find(clip.clip_id);
	return found != audio_clips.end() ? std::optional{found->second} : std::nullopt;
}

bool FakeClipFacts::clip_playing(project::ClipAddress clip) const {
	return playing_at.contains(clip.clip_id);
}

int32_t FakeClipFacts::clip_live_pos(project::ClipAddress clip) const {
	const auto found = playing_at.find(clip.clip_id);
	return found != playing_at.end() ? found->second : 0;
}

bool FakeClipFacts::clip_arrangement_only(project::ClipAddress clip) const {
	const auto found = arrangement_only.find(clip.clip_id);
	return found != arrangement_only.end() && found->second;
}

bool FakeClipFacts::clip_recording_linearly(project::ClipAddress clip) const {
	const auto found = recording_linearly.find(clip.clip_id);
	return found != recording_linearly.end() && found->second;
}

std::pair<int64_t, int64_t> FakeClipFacts::audio_clip_samples(project::ClipAddress clip, int32_t /*scroll*/,
                                                              int32_t /*zoom*/) const {
	return audio_clips.contains(clip.clip_id) ? samples : std::pair<int64_t, int64_t>{0, 0};
}

} // namespace deluge::scenes::bind::test
