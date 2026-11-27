#include "gui/ui/shell_host/host_ports.h"
#include "gui/ui/shell_host/shell_host.h"
#include "gui/ui/shell_slice_ops.h"
#include "model/clip/audio_clip.h"
#include "model/project_bridge/slot_bridge.h"
#include "model/sample/sample.h"
#include "model/sample/sample_recorder.h"
#include "model/sample/waveform_scan.h"
#include "model/song/song.h"
#include "model/voice/voice_part.h"
#include "model/voice/voice_sample.h"
#include "processing/engines/audio_engine.h"
#include "processing/sound/sound.h"
#include "processing/sound/sound_drum.h"
#include "processing/sound/source_resources.h"
#include "storage/multi_range/multisample_range.h"

#include <cstdint>
#include <optional>

namespace deluge::gui::shell_host {

uint64_t waveform_identity(const ::Sample& sample) {
	return sample.serial;
}

namespace {

/// @brief The waveforms of the current song: the sample a sound's source range holds, an audio clip's (its recorder's
///        take while it records linearly, as legacy's audio clip view draws it), or the preview sound's, which main's
///        `SampleBrowser::previewIfPossible` (`origin/main:src/deluge/gui/ui/browser/sample_browser.cpp`) draws from
///        its source 0, range 0.
class HostWaveforms final : public deluge::ui::scenes::bind::Waveforms {
public:
	[[nodiscard]] std::optional<deluge::ui::scenes::bind::WaveformInfo>
	info(const deluge::ui::scenes::bind::WaveformAddress& address) const override {
		const Resolved at = resolve(address);
		if (at.sample == nullptr) {
			return std::nullopt;
		}
		return deluge::ui::scenes::bind::WaveformInfo{.length_samples = at.sample->lengthInSamples,
		                                              .sample_rate = at.sample->sampleRate,
		                                              .channels = at.sample->numChannels,
		                                              .max_peak_from_zero = at.sample->getMaxPeakFromZero(),
		                                              .centre = at.sample->getFoundValueCentrePoint(),
		                                              .span = at.sample->getValueSpan(),
		                                              .recording = at.recorder != nullptr,
		                                              .identity = waveform_identity(*at.sample)};
	}
	[[nodiscard]] deluge::ui::views::waveform::PeakRead peaks(const deluge::ui::scenes::bind::WaveformAddress& address,
	                                                          deluge::ui::views::waveform::Window at) const override {
		const Resolved r = resolve(address);
		if (r.sample == nullptr) {
			return deluge::ui::views::waveform::PeakRead::all_past_end();
		}
		return sample::waveform_columns(
		    *r.sample, r.recorder != nullptr ? std::optional<uint64_t>{r.recorder->numSamplesCaptured} : std::nullopt,
		    at);
	}
	[[nodiscard]] std::optional<int64_t>
	play_position(const deluge::ui::scenes::bind::WaveformAddress& address) const override {
		// `SampleMarkerEditor::graphicsRoutine`'s walk: an instrument clip's newest voice (`orderSounded`) on the
		// range's holder, its middle unison part; an audio clip's own voice sample.
		using Kind = deluge::ui::scenes::bind::WaveformAddress::Kind;
		const Resolved at = resolve(address);
		if (at.sample == nullptr || currentSong == nullptr) {
			return std::nullopt;
		}
		VoiceSample* voice_sample = nullptr;
		SamplePlaybackGuide* guide = nullptr;
		if (address.kind == Kind::AUDIO_CLIP) {
			::AudioClip* clip = audio_clip_at(address.clip);
			if (clip == nullptr) {
				return std::nullopt;
			}
			voice_sample = clip->voiceSample;
			guide = &clip->guide;
		}
		else if (address.kind == Kind::RANGE) {
			::Sound* sound = bridge::soundAt(*currentSong, address.slot);
			if (sound == nullptr || !sound->hasActiveVoices()) {
				return std::nullopt;
			}
			auto* range =
			    static_cast<MultisampleRange*>(sound->sourceResources[address.source].ranges.getElement(address.range));
			// The newest voice playing the range, read with the render excluded.
			sound->voicePool().read([&](const SoundVoiceHost::List& voices) {
				SoundVoice* newest = nullptr;
				for (SoundVoice* voice : voices) {
					if (voice->part.guides[address.source].audioFileHolder == range->getAudioFileHolder()
					    && (newest == nullptr || voice->order > newest->order)) {
						newest = voice;
					}
				}
				const int32_t u = sound->config().voicing.unison.count >> 1;
				if (newest == nullptr || !newest->kernel.unison_parts[u].slots[address.source].active) {
					return;
				}
				voice_sample = newest->part.ports(u, address.source).sample.voiceSample();
				guide = &newest->part.guides[address.source];
			});
		}
		if (voice_sample == nullptr) {
			return std::nullopt;
		}
		return voice_sample->getPlaySample(at.sample, guide);
	}

private:
	/// @brief The sample an address draws, and the recorder writing it, if one is.
	struct Resolved {
		::Sample* sample{nullptr};           ///< the sample; null where the address resolves to none
		::SampleRecorder* recorder{nullptr}; ///< the recorder writing @ref sample, while it records linearly
	};
	/// @brief What @p address draws in the current song.
	/// @param address The waveform's address.
	/// @return The sample and its recorder; both null where the address resolves to no sample.
	static Resolved resolve(const deluge::ui::scenes::bind::WaveformAddress& address) {
		using Kind = deluge::ui::scenes::bind::WaveformAddress::Kind;
		if (address.kind == Kind::SLICE) {
			return {gui::ui::shellSlicedSample()};
		}
		if (address.kind == Kind::PREVIEW) {
			if (AudioEngine::sampleForPreview == nullptr) {
				return {};
			}
			::SourceResources& source = AudioEngine::sampleForPreview->sourceResources[0];
			if (source.ranges.size() < 1 || source.ranges.currentElementSize() != sizeof(MultisampleRange)) {
				return {};
			}
			return {static_cast<::Sample*>(
			    static_cast<MultisampleRange*>(source.ranges.getElement(0))->sampleHolder.audioFile)};
		}
		if (currentSong == nullptr) {
			return {};
		}
		if (address.kind == Kind::AUDIO_CLIP) {
			::Clip* clip = currentSong->clipForId(address.clip.clip_id);
			if (clip == nullptr || clip->type != ClipType::AUDIO) {
				return {};
			}
			auto* audio = static_cast<::AudioClip*>(clip);
			if (audio->getCurrentlyRecordingLinearly()) {
				return audio->recorder != nullptr ? Resolved{audio->recorder->sample, audio->recorder} : Resolved{};
			}
			return {static_cast<::Sample*>(audio->sampleHolder.audioFile)};
		}
		::Sound* sound = bridge::soundAt(*currentSong, address.slot);
		if (sound == nullptr || address.source >= kNumSources) {
			return {};
		}
		::SourceResources& source = sound->sourceResources[address.source];
		if (source.ranges.size() <= address.range || source.ranges.currentElementSize() != sizeof(MultisampleRange)) {
			return {};
		}
		auto* range = static_cast<MultisampleRange*>(source.ranges.getElement(address.range));
		return {static_cast<::Sample*>(range->sampleHolder.audioFile)};
	}
};

} // namespace

const deluge::ui::scenes::bind::Waveforms& waveforms() {
	// Constant-initialised, so it links as `.data` into SDRAM with the host's other port objects (`sdram_sections.x`).
	static constinit const HostWaveforms host;
	return host;
}

} // namespace deluge::gui::shell_host
