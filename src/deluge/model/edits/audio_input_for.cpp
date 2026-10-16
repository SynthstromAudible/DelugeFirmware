#include "model/edits/audio_input_for.h"

#include "deluge-project/audio_input.h"
#include "model/edits/setting_edit.h"
#include "model/output.h"
#include "notify/dirty_set.h"
#include "processing/audio_output.h"
#include "util/misc.h"

#include <string>

namespace deluge::edits {

void audioInputChannelFor(::AudioOutput& output, AudioInputChannel channel) {
	const int32_t index = util::to_underlying(channel);
	if (index < 0 || index >= static_cast<int32_t>(std::size(project::kInputChannelWords))) {
		return;
	}
	audioInputSettingFor(output, "inputChannel",
	                     serialization::Value::makeScalar(std::string{project::kInputChannelWords[index]}));
}

void audioInputSourceFor(::AudioOutput& output, ::Output* source) {
	const int64_t id = source != nullptr && source->id() ? int64_t{*source->id()} : -1;
	audioInputSettingFor(output, "outputRecordingFrom", serialization::Value::makeNumber(id));
}

void audioModeFor(::AudioOutput& output, AudioOutputMode mode) {
	audioInputSettingFor(
	    output, "mode",
	    serialization::Value::makeScalar(std::string{project::kAudioModeWords[util::to_underlying(mode)]}));
}

void scrollAudioModeFor(::AudioOutput& output, int32_t offset) {
	audioModeFor(output, output.scrolledMode(offset));
	notify::raise(notify::Dirty::Oled);
}

} // namespace deluge::edits
