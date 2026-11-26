#pragma once

/// @file
/// @brief The fixtures of the sample items: a `Sample` with its peak index seeded, and the eight
///        oscillator configurations the relevance predicates are asked under. The spec and the menu oracle freezer
///        share them.

#include "instruments/classic/words.h"
#include "memory/heaps.h"
#include "model/project_bridge/slot_bridge.h"
#include "model/sample/sample.h"
#include "model/song/song.h"
#include "peak_seed.h"
#include "processing/sound/sound.h"
#include "processing/sound/source_resources.h"
#include "storage/cluster/cluster.h"
#include "storage/multi_range/multisample_range.h"
#include "support/record_state.h"

#include <cstddef>
#include <cstdint>
#include <new>
#include <string>

namespace deluge::ui::scenes::spec::sampleitems {

inline constexpr size_t kClusterSize = 32768;
inline constexpr int32_t kBytesPerFrame = 2;

/// @brief A `Sample` with its peak index seeded, so nothing reads the card.
///
/// The samples the marker examples open are 32 and 64 clusters long: different enough that `WaveformView::max_zoom()`
/// tells them apart.
inline Sample* make_sample(int32_t num_clusters) {
	Cluster::set_size(kClusterSize);
	const uint64_t length = static_cast<uint64_t>(kClusterSize) * num_clusters / kBytesPerFrame;

	// Placed in the deluge heap and never freed -- `song_fixture.h`'s own rule, and for its reason.
	void* memory = deluge::memory::alloc_fast(sizeof(Sample));
	auto* s = new (memory) Sample();
	s->byteDepth = 2;
	s->numChannels = 1;
	s->sampleRate = 44100;
	s->audioDataStartPosBytes = 0;
	s->audioDataLengthBytes = length * kBytesPerFrame;
	s->lengthInSamples = length;
	peak_seed::seed_peaks(*s, [&](int32_t i) {
		return std::pair<int8_t, int8_t>{static_cast<int8_t>(static_cast<int8_t>(-(16 + i))),
		                                 static_cast<int8_t>(static_cast<int8_t>(16 + i))};
	});
	return s;
}

/// @brief One oscillator configuration to ask the relevance predicates under.
struct OscCase {
	const char* name;
	::SynthMode synth_mode;
	OscType osc_type;
	bool load_file;
};

inline constexpr OscCase kOscCases[] = {
    {"subtractive+sample+loaded", ::SynthMode::SUBTRACTIVE, OscType::SAMPLE, true},
    {"subtractive+sample+empty", ::SynthMode::SUBTRACTIVE, OscType::SAMPLE, false},
    {"subtractive+wavetable+loaded", ::SynthMode::SUBTRACTIVE, OscType::WAVETABLE, true},
    {"subtractive+square", ::SynthMode::SUBTRACTIVE, OscType::SQUARE, false},
    {"ringmod+sample+loaded", ::SynthMode::RINGMOD, OscType::SAMPLE, true},
    {"ringmod+wavetable+loaded", ::SynthMode::RINGMOD, OscType::WAVETABLE, true},
    {"fm+sample+loaded", ::SynthMode::FM, OscType::SAMPLE, true},
    {"fm+wavetable+loaded", ::SynthMode::FM, OscType::WAVETABLE, true},
};

/// @brief Configure @p sound as @p c says for oscillator @p which (the other one a square wave, empty), and record it
/// as
///        a load does, so the record the menu reads is the sound's truth.
inline void seed_osc_case(Sound& sound, Song& song, Sample* sample, const OscCase& c, int32_t which) {
	// The kinds and the engine go in through their rules, as a load reaches them: a file can hold ring mod or FM over a
	// sample or wavetable kind, which the menus refuse to write
	for (int32_t i = 0; i < 2; i++) {
		SourceResources& s = sound.sourceResources[i];
		*sound.state_ = deluge::test::with_kind(*sound.state_, static_cast<deluge::instruments::classic::SourceId>(i),
		                                        i == which ? c.osc_type : OscType::SQUARE);
		sound.deriveConfig();
		auto* range = static_cast<MultisampleRange*>(sound.getOrCreateFirstRange(i));
		range->sampleHolder.audioFile = (i == which && c.load_file) ? sample : nullptr;
	}
	sound.record() = deluge::instruments::classic::switch_engine(sound.record(), c.synth_mode);
	sound.deriveConfig();
	deluge::bridge::recordLoadedSound(sound, song);
}

} // namespace deluge::ui::scenes::spec::sampleitems
