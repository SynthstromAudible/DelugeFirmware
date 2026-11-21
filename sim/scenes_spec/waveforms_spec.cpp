// A scene reaches a waveform by its address: a sound's source range, an audio clip or the sample being previewed,
// answered from the sample the address names (nothing where no file is loaded), its per-column peaks the same the scan
// fills for that sample; `read_waveform` and `paint_columns` draw them as legacy's full-screen renderer does.

#include "scenes/menu/bind_host.h"
#include "scenes/services/waveform_paint.h"
#include "scenes/services/waveforms.h"

#include "sample-view/view.h"

#include "gui/waveform/waveform_renderer.h"
#include "model/clip/audio_clip.h"
#include "model/project_bridge/slot_bridge.h"
#include "model/sample/sample.h"
#include "model/sample/sample_recorder.h"
#include "model/sample/waveform_scan.h"
#include "model/song/song.h"
#include "processing/engines/audio_engine.h"
#include "processing/sound/sound.h"
#include "processing/sound/sound_drum.h"
#include "processing/sound/source_resources.h"
#include "storage/cluster/cluster.h"
#include "storage/multi_range/multisample_range.h"

#include "support/song_fixture.h"

#include "cppspec.hpp"
#include "support/record_state.h"

#include "support/spec_main.h"
#include "unit/ui/support/frames.h"

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
/// @param clusters   How many clusters the sample holds.
/// @param memory     Where to construct it; fresh memory when null.
/// @param first_peak Cluster 0's overview peak, in Q8.24 pad units' top byte; cluster i's is `first_peak + 3 * i`.
Sample* make_sample(int32_t clusters = kNumClusters, void* memory = nullptr, int32_t first_peak = 16) {
	Cluster::set_size(kClusterSize);
	if (memory == nullptr) {
		memory = deluge::memory::alloc_fast(sizeof(Sample));
	}
	auto* s = new (memory) Sample();
	s->byteDepth = 2;
	s->numChannels = 1;
	s->sampleRate = 44100;
	s->audioDataStartPosBytes = 0;
	const uint64_t length = static_cast<uint64_t>(kClusterSize) * clusters / 2;
	s->audioDataLengthBytes = length * 2;
	s->lengthInSamples = length;
	s->maxValueFound = 127 << 24;
	s->minValueFound = -128 << 24;
	s->resizeOverviewCache(clusters);
	for (int32_t i = 0; i < clusters; i++) {
		OverviewCacheEntry& e = s->overviewCacheEntry(static_cast<uint32_t>(i));
		e.min = static_cast<int8_t>(-(first_peak + 3 * i));
		e.max = static_cast<int8_t>(first_peak + 3 * i);
		e.investigated = true;
	}
	return s;
}

// The running range the legacy scan leaves over the sample above: a fresh read at the whole zoom, and a read at one
// sample twice as long, read at two clusters a column and scrolled three columns on. The overview's peaks are `(16 + 3
// * cluster) << 24`, either sign.
constexpr int32_t kFreshMax = (16 + 3 * 31) << 24; // clusters 0 to 31
constexpr int32_t kFreshMin = -kFreshMax;
constexpr int32_t kScrolledMax = (16 + 3 * 37) << 24; // clusters 0 to 37: columns 0 to 15, then 3 to 18
constexpr int32_t kScrolledMin = -kScrolledMax;

/// The fixture's whole-sample zoom, `ceil(length / 16)`: two clusters a column.
constexpr int64_t kWholeZoom = static_cast<int64_t>(((kSampleLength - 1) >> 4) + 1);

using deluge::sample_view::ColumnMask;
using deluge::sample_view::ColumnRead;
using deluge::sample_view::ColumnState;
using deluge::sample_view::Peak;
using deluge::sample_view::PeakCache;
using deluge::sample_view::PeakRead;
using deluge::sample_view::Window;

/// @brief A synth song whose source 0, range 0 holds a sample, made current for one example; the song before it is
///        put back after, and the range emptied.
struct RangeHeld {
	spec::SongFixture synth = spec::make_synth_song();
	Song* const saved = currentSong;
	MultisampleRange* multisample = nullptr;
	WaveformAddress address{};
	explicit RangeHeld(Sample* sample) {
		currentSong = synth.song;
		auto* sound = synth.instrument->toModControllable()->effectsOwner()->sound;
		deluge::test::set(sound->record(), "osc1/type", "sample");
		sound->projectSnapshot();
		multisample = static_cast<MultisampleRange*>(sound->getOrCreateFirstRange(0));
		multisample->sampleHolder.audioFile = sample;
		address = {.kind = WaveformAddress::Kind::RANGE,
		           .slot = {deluge::project::SlotAddress::Kind::OUTPUT, *synth.instrument->id()},
		           .source = 0,
		           .range = 0};
	}
	~RangeHeld() {
		multisample->sampleHolder.audioFile = nullptr;
		currentSong = saved;
	}
	RangeHeld(const RangeHeld&) = delete;
	RangeHeld& operator=(const RangeHeld&) = delete;
};

/// @brief How many columns of @p read are in @p state.
int32_t count_of(const PeakRead& read, ColumnState state) {
	int32_t n = 0;
	for (int32_t c = 0; c < deluge::sample_view::kColumns; c++) {
		n += read.column(c).state() == state ? 1 : 0;
	}
	return n;
}

/// The browser's preview sound, as `AudioEngine::init` makes it (which nothing here runs), for one example; the
/// previous one is put back after.
struct PreviewSound {
	alignas(SoundDrum) char memory[sizeof(SoundDrum)]{};
	SoundDrum* const saved = AudioEngine::sampleForPreview;
	SoundDrum* drum = new (memory) SoundDrum();
	PreviewSound() {
		deluge::test::set(drum->record(), "osc1/type", "sample");
		drum->projectSnapshot();
		AudioEngine::sampleForPreview = drum;
	}
	~PreviewSound() {
		AudioEngine::sampleForPreview = saved;
		drum->~SoundDrum();
	}
	PreviewSound(const PreviewSound&) = delete;
	PreviewSound& operator=(const PreviewSound&) = delete;
	/// Put @p sample in the preview's range, as the load of main's `SampleBrowser::previewIfPossible`
	/// (`origin/main:src/deluge/gui/ui/browser/sample_browser.cpp`) does.
	void hold(Sample* sample) {
		static_cast<MultisampleRange*>(drum->getOrCreateFirstRange(0))->sampleHolder.audioFile = sample;
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

		auto* sound = synth.instrument->toModControllable()->effectsOwner()->sound;
		SourceResources& source = sound->sourceResources[0];
		deluge::test::set(sound->record(), "osc1/type", "sample");
		sound->projectSnapshot();
		auto* multisample = static_cast<MultisampleRange*>(sound->getOrCreateFirstRange(0));
		Sample* sample = make_sample();
		multisample->sampleHolder.audioFile = sample;
		const auto info = waveforms.info(range);
		expect(info.has_value()).to_be_true();
		if (info.has_value()) {
			expect(info->length_samples).to_equal(kSampleLength);
			expect(info->sample_rate).to_equal(44100u);
			expect(info->channels).to_equal(1);
			expect(info->centre).to_equal(sample->getFoundValueCentrePoint());
			expect(info->span).to_equal(sample->getValueSpan());
			expect(info->recording).to_be_false();
		}

		spec::AudioSongFixture audio = spec::make_audio_clip_song();
		currentSong = audio.song;
		const WaveformAddress clip{.kind = WaveformAddress::Kind::AUDIO_CLIP,
		                           .clip = deluge::project::ClipAddress{audio.clip->id()}};
		expect(waveforms.info(clip).has_value()).to_be_false();
		audio.clip->sampleHolder.audioFile = make_sample();
		expect(waveforms.info(clip).has_value()).to_be_true();
		if (waveforms.info(clip).has_value()) {
			expect(waveforms.info(clip)->length_samples).to_equal(kSampleLength);
		}
		audio.clip->sampleHolder.audioFile = nullptr;
		currentSong = saved;
	});

	it("fills the peaks the scan fills for the sample the address names", _ {
		spec::SongFixture synth = spec::make_synth_song();
		Song* const saved = currentSong;
		currentSong = synth.song;
		auto* sound = synth.instrument->toModControllable()->effectsOwner()->sound;
		SourceResources& source = sound->sourceResources[0];
		deluge::test::set(sound->record(), "osc1/type", "sample");
		sound->projectSnapshot();
		auto* multisample = static_cast<MultisampleRange*>(sound->getOrCreateFirstRange(0));
		Sample* sample = make_sample();
		multisample->sampleHolder.audioFile = sample;
		const WaveformAddress range{.kind = WaveformAddress::Kind::RANGE,
		                            .slot = {deluge::project::SlotAddress::Kind::OUTPUT, *synth.instrument->id()}};
		const int64_t zoom = ((kSampleLength - 1) >> 4) + 1;
		const deluge::sample_view::PeakRead through = menu::host_waveforms().peaks(range, {.scroll = 0, .zoom = zoom});
		deluge::sample_view::PeakCache cache;
		const deluge::sample_view::PeakRead direct =
		    deluge::sample::find_peaks_per_col(*sample, {.scroll = 0, .zoom = zoom}, cache);
		expect(through.column(0).state()).to_equal(deluge::sample_view::ColumnState::PEAK);
		expect(through).to_equal(direct);
		multisample->sampleHolder.audioFile = nullptr;
		currentSong = saved;
	});

	it("widens the sample's running range by the peaks a fresh read reads", _ {
		Sample* sample = make_sample();
		sample->maxValueFound = INT32_MIN;
		sample->minValueFound = INT32_MAX;
		const int64_t zoom = ((kSampleLength - 1) >> 4) + 1;
		deluge::sample_view::PeakCache cache;
		(void)deluge::sample::find_peaks_per_col(*sample, {.scroll = 0, .zoom = zoom}, cache);
		expect(sample->maxValueFound).to_equal(kFreshMax);
		expect(sample->minValueFound).to_equal(kFreshMin);
	});

	it("widens the range by a scroll's new columns, and keeps it over the columns it slid", _ {
		// Twice the clusters, read at two clusters a column (the largest power of two at which it spans 20 columns or
		// more), so each column is whole clusters and comes from the overview.
		constexpr int32_t kClusters = 2 * kNumClusters;
		Sample* sample = make_sample(kClusters);
		sample->maxValueFound = INT32_MIN;
		sample->minValueFound = INT32_MAX;
		const int64_t zoom = kClusterSize;
		static_assert(kClusterSize * kClusters / 2 / kClusterSize >= 20);
		deluge::sample_view::PeakCache cache;
		(void)deluge::sample::find_peaks_per_col(*sample, {.scroll = 0, .zoom = zoom}, cache);
		(void)deluge::sample::find_peaks_per_col(*sample, {.scroll = 3 * zoom, .zoom = zoom}, cache);
		expect(sample->maxValueFound).to_equal(kScrolledMax);
		expect(sample->minValueFound).to_equal(kScrolledMin);
	});

	it("widens the range by a fresh read through the host port as the scan does", _ {
		Sample* sample = make_sample();
		sample->maxValueFound = INT32_MIN;
		sample->minValueFound = INT32_MAX;
		RangeHeld held(sample);
		(void)menu::host_waveforms().peaks(held.address, {.scroll = 0, .zoom = kWholeZoom});
		expect(sample->maxValueFound).to_equal(kFreshMax);
		expect(sample->minValueFound).to_equal(kFreshMin);
	});

	it("widens the range by a scroll's new columns through the host port as the scan does", _ {
		constexpr int32_t kClusters = 2 * kNumClusters;
		Sample* sample = make_sample(kClusters);
		sample->maxValueFound = INT32_MIN;
		sample->minValueFound = INT32_MAX;
		RangeHeld held(sample);
		const int64_t zoom = kClusterSize;
		(void)menu::host_waveforms().peaks(held.address, {.scroll = 0, .zoom = zoom});
		(void)menu::host_waveforms().peaks(held.address, {.scroll = 3 * zoom, .zoom = zoom});
		expect(sample->maxValueFound).to_equal(kScrolledMax);
		expect(sample->minValueFound).to_equal(kScrolledMin);
	});

	it("takes a take's running range from its recorder, not from the peaks it reads", _ {
		Sample* sample = make_sample();
		auto* recorder = new SampleRecorder(); // left to the heap: its destructor would free the take
		recorder->sample = sample;
		recorder->numSamplesCaptured = static_cast<uint32_t>(kSampleLength);
		recorder->recordMax = 5 << 24;
		recorder->recordMin = -(5 << 24);
		PeakCache cache;
		const PeakRead read =
		    deluge::sample::find_peaks_per_col(*sample, {.scroll = 0, .zoom = kWholeZoom}, cache, recorder);
		expect(read.column(0).state()).to_equal(ColumnState::PEAK);
		expect(sample->maxValueFound).to_equal(5 << 24);
		expect(sample->minValueFound).to_equal(-(5 << 24));
	});

	it("answers every column of a window over the whole sample PEAK, or PAST_END from the length on", _ {
		RangeHeld held(make_sample());
		const PeakRead whole = menu::host_waveforms().peaks(held.address, {.scroll = 0, .zoom = kWholeZoom});
		expect(count_of(whole, ColumnState::PEAK)).to_equal(16);
		const PeakRead wider = menu::host_waveforms().peaks(held.address, {.scroll = 0, .zoom = 2 * kWholeZoom});
		for (int32_t c = 0; c < 8; c++) {
			expect(wider.column(c).state()).to_equal(ColumnState::PEAK);
		}
		for (int32_t c = 8; c < 16; c++) {
			expect(wider.column(c).state()).to_equal(ColumnState::PAST_END);
		}
		expect(wider.pending()).to_be_false();
	});

	it("Q2: folds only the columns a read reads, so a kept column is not folded again", _ {
		// Q2: the running range takes in the columns a read reads; a kept column is not folded again.
		Sample* sample = make_sample();
		sample->maxValueFound = INT32_MIN;
		sample->minValueFound = INT32_MAX;
		const Window at{.scroll = 0, .zoom = kWholeZoom};
		constexpr Peak kForeign{.min = -0x7000'0000, .max = 0x7000'0000}; // a peak the sample does not have
		PeakCache cache;
		const PeakRead first = cache.update(at, static_cast<int64_t>(kSampleLength), false, [&](ColumnMask wanted) {
			ColumnMask others = wanted;
			others.reset(9);
			PeakRead read = deluge::sample::read_columns(*sample, nullptr, at, others);
			read.set(9, ColumnRead::of(kForeign));
			return read;
		});
		int32_t max = INT32_MIN;
		int32_t min = INT32_MAX;
		int32_t folded = 0;
		for (int32_t c = 0; c < deluge::sample_view::kColumns; c++) {
			if (const std::optional<Peak> p = first.column(c).peak(); p && c != 9) {
				max = std::max(max, p->max);
				min = std::min(min, p->min);
				folded++;
			}
		}
		expect(folded).to_equal(15);
		expect(first.column(9)).to_equal(ColumnRead::of(kForeign));
		const PeakRead second = deluge::sample::find_peaks_per_col(*sample, at, cache);
		expect(second).to_equal(first); // every column kept: none read again
		expect(sample->maxValueFound).to_equal(max);
		expect(sample->minValueFound).to_equal(min);
	});

	it("Q1: reads every column again after a forget, at a zoom of 1 near the start", _ {
		// Q1: a forgotten cache keeps no column to slide. The reader answers every column it is asked for a peak:
		// at one frame a column the fixture's sample would read through the card, which the spec has not.
		PeakCache cache;
		size_t asked = 0;
		const auto counting = [&](ColumnMask wanted) {
			asked = wanted.count();
			PeakRead read;
			for (int32_t c = 0; c < deluge::sample_view::kColumns; c++) {
				read.set(c, ColumnRead::of({.min = -c, .max = c}));
			}
			return read;
		};
		const Window at{.scroll = 5, .zoom = 1};
		(void)cache.update(at, static_cast<int64_t>(kSampleLength), false, counting);
		expect(asked).to_equal(size_t{16});
		(void)cache.update(at, static_cast<int64_t>(kSampleLength), false, counting);
		expect(asked).to_equal(size_t{0});
		cache.forget();
		(void)cache.update(at, static_cast<int64_t>(kSampleLength), false, counting);
		expect(asked).to_equal(size_t{16});
	});

	it("Q3: answers a window whose zoom is below 1 every column PAST_END through the host", _ {
		RangeHeld held(make_sample());
		expect(menu::host_waveforms().peaks(held.address, {.scroll = 5, .zoom = 0})).to_equal(PeakRead::all_past_end());
	});

	it("Q22: reads a column straddling the start from frame 0, and one wholly before it as PAST_END", _ {
		// Q22. At two clusters a column, the straddling column 0 covers frames -3 to 32764 and takes the whole first
		// cluster's overview peak; a column narrower than a cluster reads frames the fixture's sample has not.
		RangeHeld held(make_sample());
		const services::Waveforms& waveforms = menu::host_waveforms();
		const PeakRead straddling = waveforms.peaks(held.address, {.scroll = -3, .zoom = kWholeZoom});
		expect(straddling.column(0)).to_equal(ColumnRead::of({.min = -16 << 24, .max = 16 << 24}));
		const PeakRead before = waveforms.peaks(held.address, {.scroll = -16, .zoom = 8});
		expect(before.column(0).state()).to_equal(ColumnState::PAST_END);
		expect(before.column(1).state()).to_equal(ColumnState::PAST_END);
		expect(waveforms.peaks(held.address, {.scroll = -16 * kWholeZoom, .zoom = kWholeZoom}))
		    .to_equal(PeakRead::all_past_end());
	});

	it("answers a take's captured columns PEAK and its edge column and those after it PAST_END", _ {
		spec::AudioSongFixture audio = spec::make_audio_clip_song();
		Song* const saved = currentSong;
		currentSong = audio.song;
		Sample* sample = make_sample();
		auto* recorder = new SampleRecorder(); // left to the heap: its destructor would free the take
		recorder->sample = sample;
		recorder->numSamplesCaptured = static_cast<uint32_t>(5 * kWholeZoom + kWholeZoom / 2); // into column 5
		recorder->recordMax = 5 << 24;
		recorder->recordMin = -(5 << 24);
		audio.clip->recorder = recorder;
		const WaveformAddress clip{.kind = WaveformAddress::Kind::AUDIO_CLIP,
		                           .clip = deluge::project::ClipAddress{audio.clip->id()}};
		const services::Waveforms& waveforms = menu::host_waveforms();
		const std::optional<services::WaveformInfo> info = waveforms.info(clip);
		expect(info.has_value()).to_be_true();
		if (info.has_value()) {
			expect(info->recording).to_be_true();
		}
		const PeakRead read = waveforms.peaks(clip, {.scroll = 0, .zoom = kWholeZoom});
		for (int32_t c = 0; c < 5; c++) {
			expect(read.column(c).state()).to_equal(ColumnState::PEAK);
		}
		for (int32_t c = 5; c < 16; c++) {
			expect(read.column(c).state()).to_equal(ColumnState::PAST_END);
		}
		expect(read.pending()).to_be_false();
		expect(sample->maxValueFound).to_equal(5 << 24);
		expect(sample->minValueFound).to_equal(-(5 << 24));
		audio.clip->recorder = nullptr;
		currentSong = saved;
	});

	// A sample freed and another constructed in its memory: the host keys its columns by the sample's lifetime serial,
	// so the new one is read afresh rather than shown the freed one's kept columns.
	it("reads every column of a file constructed in a freed file's memory, not the freed file's", _ {
		void* const memory = deluge::memory::alloc_fast(sizeof(Sample));
		const Window whole{.scroll = 0, .zoom = kWholeZoom};
		Sample* const first = make_sample(kNumClusters, memory);
		RangeHeld held(first);
		const PeakRead before = menu::host_waveforms().peaks(held.address, whole);
		held.multisample->sampleHolder.audioFile = nullptr;
		first->~Sample();
		Sample* const second = make_sample(kNumClusters, memory, 20); // the same length, other peaks
		expect(static_cast<void*>(second)).to_equal(memory);
		held.multisample->sampleHolder.audioFile = second;
		const PeakRead after = menu::host_waveforms().peaks(held.address, whole);
		PeakCache fresh;
		const PeakRead own = deluge::sample::find_peaks_per_col(*second, whole, fresh);
		expect(count_of(own, ColumnState::PEAK)).to_equal(16);
		expect(after).to_equal(own);
		expect(after).not_().to_equal(before);
	});

	it("reads every captured column of a take constructed in a freed take's memory, not the freed take's", _ {
		spec::AudioSongFixture audio = spec::make_audio_clip_song();
		Song* const saved = currentSong;
		currentSong = audio.song;
		const WaveformAddress clip{.kind = WaveformAddress::Kind::AUDIO_CLIP,
		                           .clip = deluge::project::ClipAddress{audio.clip->id()}};
		const Window whole{.scroll = 0, .zoom = kWholeZoom};
		const auto captured = static_cast<uint32_t>(5 * kWholeZoom + kWholeZoom / 2); // into column 5
		void* const memory = deluge::memory::alloc_fast(sizeof(Sample));
		auto* recorder = new SampleRecorder(); // left to the heap: its destructor would free the take
		recorder->numSamplesCaptured = captured;
		recorder->recordMax = 5 << 24;
		recorder->recordMin = -(5 << 24);
		audio.clip->recorder = recorder;
		Sample* const first = make_sample(kNumClusters, memory);
		recorder->sample = first;
		const PeakRead before = menu::host_waveforms().peaks(clip, whole);
		first->~Sample();
		Sample* const second = make_sample(kNumClusters, memory, 20); // a take aborted and recorded again
		expect(static_cast<void*>(second)).to_equal(memory);
		recorder->sample = second;
		const PeakRead after = menu::host_waveforms().peaks(clip, whole);
		PeakCache fresh;
		const PeakRead own = deluge::sample::find_peaks_per_col(*second, whole, fresh, recorder);
		expect(count_of(own, ColumnState::PEAK)).to_equal(5);
		expect(after).to_equal(own);
		expect(after).not_().to_equal(before);
		audio.clip->recorder = nullptr;
		currentSong = saved;
	});

	it("answers every column PAST_END at an address that resolves to nothing", _ {
		const services::Waveforms& waveforms = menu::host_waveforms();
		PreviewSound sound;
		const WaveformAddress preview{.kind = WaveformAddress::Kind::PREVIEW};
		expect(waveforms.peaks(preview, {.scroll = 0, .zoom = kWholeZoom})).to_equal(PeakRead::all_past_end());
		RangeHeld held(nullptr);
		expect(waveforms.peaks(held.address, {.scroll = 0, .zoom = kWholeZoom})).to_equal(PeakRead::all_past_end());
	});

	it("answers the preview sample under PREVIEW, and nothing with no preview loaded", _ {
		const WaveformAddress preview{.kind = WaveformAddress::Kind::PREVIEW};
		const services::Waveforms& waveforms = menu::host_waveforms();
		PreviewSound sound;
		expect(waveforms.info(preview).has_value()).to_be_false();
		Sample* sample = make_sample();
		sound.hold(sample);
		const auto info = waveforms.info(preview);
		expect(info.has_value()).to_be_true();
		if (info.has_value()) {
			expect(info->length_samples).to_equal(kSampleLength);
			expect(info->centre).to_equal(sample->getFoundValueCentrePoint());
			expect(info->span).to_equal(sample->getValueSpan());
			expect(info->recording).to_be_false();
		}
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
		expect(first.has_value()).to_be_true();
		expect(second.has_value()).to_be_true();
		if (first.has_value() && second.has_value()) {
			expect(first->identity).not_().to_equal(second->identity);
		}
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
		const deluge::sample_view::WaveformView view = deluge::sample_view::WaveformView::open(info.length_samples, std::nullopt);

		deluge::ui::test::GridRig rig;
		rig.begin_tick(0);
		const std::optional<services::WaveformFrame> wf = services::read_waveform(waveforms, preview, view.window());
		expect(wf.has_value()).to_be_true();
		if (wf) {
			services::paint_columns(rig.frame, wf->read, wf->centre, wf->span);
		}

		RGB legacy[kDisplayHeight][kDisplayWidth + kSideBarWidth]{};
		deluge::sample_view::PeakCache cache;
		(void)waveformRenderer.renderFullScreen(sample, static_cast<uint64_t>(view.scroll()),
		                                        static_cast<uint64_t>(view.zoom()), legacy, cache);
		int32_t differ = 0;
		int32_t lit = 0;
		for (int32_t x = 0; x < kDisplayWidth; x++) {
			for (int32_t y = 0; y < kDisplayHeight; y++) {
				const RGB ours = rig.grid.at(static_cast<uint8_t>(x), static_cast<uint8_t>(y));
				differ += ours == legacy[y][x] ? 0 : 1;
				lit += legacy[y][x] == RGB{} ? 0 : 1;
			}
		}
		expect(lit).to_be_greater_than(0);
		expect(differ).to_equal(0);
		sound.hold(nullptr);
	});
});

CPPSPEC_SPEC(waveforms)
