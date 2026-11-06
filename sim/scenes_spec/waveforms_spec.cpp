// A scene reaches a waveform by its address: a sound's source range, an audio clip or the sample being previewed,
// answered from the sample the address names (nothing where no file is loaded), its per-column peaks the same the scan
// fills for that sample; `paint_full` draws them as legacy's full-screen renderer does.

#include "scenes/menu/bind_host.h"
#include "scenes/services/waveform_paint.h"
#include "scenes/services/waveforms.h"

#include "gui/waveform/waveform_renderer.h"
#include "model/clip/audio_clip.h"
#include "model/project_bridge/slot_bridge.h"
#include "model/sample/sample.h"
#include "model/sample/waveform_navigator.h"
#include "model/sample/waveform_scan.h"
#include "model/song/song.h"
#include "processing/engines/audio_engine.h"
#include "processing/sound/sound.h"
#include "processing/sound/sound_drum.h"
#include "processing/source.h"
#include "storage/cluster/cluster.h"
#include "storage/multi_range/multisample_range.h"

#include "support/song_fixture.h"

#include "cppspec.hpp"

#include "spec/support/spec_main.h"
#include "spec/ui/support/frames.h"

#include <algorithm>
#include <new>
#include <optional>

using namespace deluge::scenes;
using services::WaveformAddress;

namespace {

constexpr size_t kClusterSize = 32768;
constexpr int32_t kNumClusters = 32;
constexpr uint64_t kSampleLength = static_cast<uint64_t>(kClusterSize) * kNumClusters / 2; // 16-bit mono

/// A sample whose overview cache already holds every cluster's peaks, so a scan reads no card.
Sample* make_sample() {
	Cluster::set_size(kClusterSize);
	void* memory = deluge::memory::alloc_fast(sizeof(Sample));
	auto* s = new (memory) Sample();
	s->byteDepth = 2;
	s->numChannels = 1;
	s->sampleRate = 44100;
	s->audioDataStartPosBytes = 0;
	s->audioDataLengthBytes = kSampleLength * 2;
	s->lengthInSamples = kSampleLength;
	s->maxValueFound = 127 << 24;
	s->minValueFound = -128 << 24;
	s->resizeOverviewCache(kNumClusters);
	for (int32_t i = 0; i < kNumClusters; i++) {
		OverviewCacheEntry& e = s->overviewCacheEntry(static_cast<uint32_t>(i));
		e.min = static_cast<int8_t>(-(16 + 3 * i));
		e.max = static_cast<int8_t>(16 + 3 * i);
		e.investigated = true;
	}
	return s;
}

bool same_peaks(const WaveformRenderData& a, const WaveformRenderData& b) {
	return std::equal(std::begin(a.colStatus), std::end(a.colStatus), std::begin(b.colStatus))
	       && std::equal(std::begin(a.maxPerCol), std::end(a.maxPerCol), std::begin(b.maxPerCol))
	       && std::equal(std::begin(a.minPerCol), std::end(a.minPerCol), std::begin(b.minPerCol));
}

/// The browser's preview sound, as `AudioEngine::init` makes it (which nothing here runs), for one example; the
/// previous one is put back after.
struct PreviewSound {
	alignas(SoundDrum) char memory[sizeof(SoundDrum)]{};
	SoundDrum* const saved = AudioEngine::sampleForPreview;
	SoundDrum* drum = new (memory) SoundDrum();
	PreviewSound() {
		drum->sources[0].oscType = OscType::SAMPLE;
		AudioEngine::sampleForPreview = drum;
	}
	~PreviewSound() {
		AudioEngine::sampleForPreview = saved;
		drum->~SoundDrum();
	}
	PreviewSound(const PreviewSound&) = delete;
	PreviewSound& operator=(const PreviewSound&) = delete;
	/// Put @p sample in the preview's range, as `SampleBrowser::showPreview`'s load does.
	void hold(Sample* sample) {
		static_cast<MultisampleRange*>(drum->sources[0].getOrCreateFirstRange())->sampleHolder.audioFile = sample;
	}
};

} // namespace

// clang-format off
describe waveforms("a waveform by its address", ${
	it("answers a synth's range and an audio clip, and nothing where no file is loaded", _ {
		spec::SongFixture synth = spec::make_synth_song();
		Song* const saved = currentSong;
		currentSong = synth.song;
		const deluge::project::SlotAddress slot{deluge::project::SlotAddress::Kind::OUTPUT, *synth.instrument->id()};
		const WaveformAddress range{.kind = WaveformAddress::Kind::RANGE, .slot = slot, .source = 0, .range = 0};
		const services::Waveforms& waveforms = menu::host_waveforms();
		expect(waveforms.info(range).has_value()).to_be_false();

		auto* sound = static_cast<Sound*>(static_cast<ModControllableAudio*>(synth.instrument->toModControllable()));
		Source& source = sound->sources[0];
		source.oscType = OscType::SAMPLE;
		auto* multisample = static_cast<MultisampleRange*>(source.getOrCreateFirstRange());
		Sample* sample = make_sample();
		multisample->sampleHolder.audioFile = sample;
		const auto info = waveforms.info(range);
		expect(info.has_value() && info->length_samples == kSampleLength && info->sample_rate == 44100u
		       && info->channels == 1 && info->centre == sample->getFoundValueCentrePoint()
		       && info->span == sample->getValueSpan() && !info->recording)
		    .to_be_true();

		spec::AudioSongFixture audio = spec::make_audio_clip_song();
		currentSong = audio.song;
		const WaveformAddress clip{.kind = WaveformAddress::Kind::AUDIO_CLIP,
		                           .clip = deluge::project::ClipAddress{audio.clip->id()}};
		expect(waveforms.info(clip).has_value()).to_be_false();
		audio.clip->sampleHolder.audioFile = make_sample();
		expect(waveforms.info(clip).has_value() && waveforms.info(clip)->length_samples == kSampleLength).to_be_true();
		audio.clip->sampleHolder.audioFile = nullptr;
		currentSong = saved;
	});

	it("fills the peaks the scan fills for the sample the address names", _ {
		spec::SongFixture synth = spec::make_synth_song();
		Song* const saved = currentSong;
		currentSong = synth.song;
		auto* sound = static_cast<Sound*>(static_cast<ModControllableAudio*>(synth.instrument->toModControllable()));
		Source& source = sound->sources[0];
		source.oscType = OscType::SAMPLE;
		auto* multisample = static_cast<MultisampleRange*>(source.getOrCreateFirstRange());
		Sample* sample = make_sample();
		multisample->sampleHolder.audioFile = sample;
		const WaveformAddress range{.kind = WaveformAddress::Kind::RANGE,
		                            .slot = {deluge::project::SlotAddress::Kind::OUTPUT, *synth.instrument->id()}};
		const uint64_t zoom = ((kSampleLength - 1) >> 4) + 1;
		WaveformRenderData through{};
		WaveformRenderData direct{};
		(void)menu::host_waveforms().peaks(range, 0, zoom, through);
		(void)deluge::sample::find_peaks_per_col(sample, 0, zoom, &direct);
		expect(through.colStatus[0] == COL_STATUS_INVESTIGATED).to_be_true();
		expect(same_peaks(through, direct)).to_be_true();
		multisample->sampleHolder.audioFile = nullptr;
		currentSong = saved;
	});

	it("answers the preview sample under PREVIEW, and nothing with no preview loaded", _ {
		const WaveformAddress preview{.kind = WaveformAddress::Kind::PREVIEW};
		const services::Waveforms& waveforms = menu::host_waveforms();
		PreviewSound sound;
		expect(waveforms.info(preview).has_value()).to_be_false();
		Sample* sample = make_sample();
		sound.hold(sample);
		const auto info = waveforms.info(preview);
		expect(info.has_value() && info->length_samples == kSampleLength && info->centre == sample->getFoundValueCentrePoint()
		       && info->span == sample->getValueSpan() && !info->recording)
		    .to_be_true();
		sound.hold(nullptr);
		expect(waveforms.info(preview).has_value()).to_be_false();
	});

	it("gives two previewed files two identities", _ {
		const WaveformAddress preview{.kind = WaveformAddress::Kind::PREVIEW};
		const services::Waveforms& waveforms = menu::host_waveforms();
		PreviewSound sound;
		sound.hold(make_sample());
		const std::optional<services::WaveformInfo> first = waveforms.info(preview);
		sound.hold(make_sample());
		const std::optional<services::WaveformInfo> second = waveforms.info(preview);
		expect(first.has_value() && second.has_value() && first->identity != second->identity).to_be_true();
		sound.hold(nullptr);
	});

	it("paints the preview's main columns as legacy's full-screen renderer does", _ {
		const WaveformAddress preview{.kind = WaveformAddress::Kind::PREVIEW};
		const services::Waveforms& waveforms = menu::host_waveforms();
		PreviewSound sound;
		Sample* sample = make_sample();
		sound.hold(sample);
		const std::optional<services::WaveformInfo> found = waveforms.info(preview);
		expect(found.has_value()).to_be_true();
		const services::WaveformInfo info = found.value_or(services::WaveformInfo{});
		deluge::sample::WaveformNav nav;
		nav.length_samples = info.length_samples;
		nav.opened(0, 0);

		deluge::ui::test::GridRig rig;
		rig.begin_tick(0);
		(void)services::paint_full(rig.frame, waveforms, preview, info, nav);

		RGB legacy[kDisplayHeight][kDisplayWidth + kSideBarWidth]{};
		WaveformRenderData data{};
		data.xScroll = -1;
		(void)waveformRenderer.renderFullScreen(sample, nav.xScroll, nav.xZoom, legacy, &data);
		int32_t differ = 0;
		int32_t lit = 0;
		for (int32_t x = 0; x < kDisplayWidth; x++) {
			for (int32_t y = 0; y < kDisplayHeight; y++) {
				const RGB ours = rig.grid.at(static_cast<uint8_t>(x), static_cast<uint8_t>(y));
				differ += ours == legacy[y][x] ? 0 : 1;
				lit += legacy[y][x] == RGB{} ? 0 : 1;
			}
		}
		expect(lit > 0).to_be_true();
		expect(differ).to_equal(0);
		sound.hold(nullptr);
	});
});

CPPSPEC_SPEC(waveforms)
