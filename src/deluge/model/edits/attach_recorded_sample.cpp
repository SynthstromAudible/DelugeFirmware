#include "model/edits/attach_recorded_sample.h"

#include "model/edits/set_engine_setting.h"
#include "model/edits/set_source_ranges.h"
#include "model/project_bridge/slot_bridge.h"
#include "model/project_bridge/source_bridge.h"
#include "model/sample/sample.h"
#include "model/sample/sample_recorder.h"
#include "model/song/song.h"
#include "processing/engines/audio_engine.h"
#include "processing/sound/sound.h"
#include "processing/source.h"
#include "storage/audio/audio_file_holder.h"
#include "storage/audio/audio_file_manager.h"
#include "storage/multi_range/multi_range.h"

#include <strings.h>

namespace deluge::edits {

namespace {

/// The take a recorder holds at @p path, or else the sample in memory there, or null: a lookup that never reads the
/// card. A recorder's own take wins over any other sample a file of that path left in memory.
::Sample* residentSample(const std::string& path) {
	for (SampleRecorder* recorder = AudioEngine::firstRecorder; recorder != nullptr; recorder = recorder->next) {
		if (recorder->sample != nullptr && strcasecmp(recorder->sample->filePath.c_str(), path.c_str()) == 0) {
			return recorder->sample;
		}
	}
	const AudioFileVector& files = audioFileManager.audioFiles;
	// The files are sorted by path, so a sample and a wavetable of one file sit together from the first match.
	for (auto i = static_cast<size_t>(files.search(path.c_str())); i < files.size(); i++) {
		AudioFile* file = files[i];
		if (strcasecmp(file->filePath.c_str(), path.c_str()) != 0) {
			break;
		}
		if (file->type == AudioFileType::SAMPLE) {
			return static_cast<::Sample*>(file);
		}
	}
	return nullptr;
}

} // namespace

Error AttachRecordedSample::apply(Context& ctx) {
	::Sound* sound = deluge::bridge::soundAt(ctx.song, slot_);
	::Sample* take = residentSample(path_);
	if (sound == nullptr || take == nullptr || source_ >= kNumSources
	    || range_ >= sound->sources[source_].ranges.size()) {
		return Error::UNSPECIFIED;
	}
	sound->killAllVoices();
	SetEngineSetting type{slot_, "osc" + std::to_string(source_ + 1) + "/type",
	                      deluge::serialization::Value::makeScalar("sample")};
	if (const Error error = type.apply(ctx); error != Error::NONE) {
		return error;
	}
	::Source& source = sound->sources[source_];
	AudioFileHolder* holder =
	    range_ < source.ranges.size() ? source.ranges.getElement(range_)->getAudioFileHolder() : nullptr;
	if (holder == nullptr) {
		return Error::UNSPECIFIED;
	}
	holder->filePath = take->filePath;
	holder->setAudioFile(take, source.sampleControls.isCurrentlyReversed(), true);
	SetSourceRanges ranges{slot_, source_, deluge::bridge::rangesOf(source)};
	return ranges.apply(ctx);
}

} // namespace deluge::edits
