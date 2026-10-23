#include "scenes/services/source_names.h"

namespace deluge::scenes::services {

using deluge::l10n::String;

std::string_view source_short_name(PatchSource source) {
	switch (source) {
	case PatchSource::LFO_GLOBAL_1:
		return "lfo1";
	case PatchSource::LFO_GLOBAL_2:
		return "lfo3";
	case PatchSource::LFO_LOCAL_1:
		return "lfo2";
	case PatchSource::LFO_LOCAL_2:
		return "lfo4";
	case PatchSource::ENVELOPE_0:
		return "env1";
	case PatchSource::ENVELOPE_1:
		return "env2";
	case PatchSource::ENVELOPE_2:
		return "env3";
	case PatchSource::ENVELOPE_3:
		return "env4";
	case PatchSource::VELOCITY:
		return "velo";
	case PatchSource::NOTE:
		return "note";
	case PatchSource::SIDECHAIN:
		return "side";
	case PatchSource::RANDOM:
		return "rand";
	case PatchSource::AFTERTOUCH:
		return "pres";
	case PatchSource::X:
		return "mpeX";
	case PatchSource::Y:
		return "mpeY";
	default:
		return "----";
	}
}

deluge::l10n::String source_display_string(PatchSource source) {
	switch (source) {
	case PatchSource::LFO_GLOBAL_1:
		return String::STRING_FOR_PATCH_SOURCE_LFO_GLOBAL_1;
	case PatchSource::LFO_GLOBAL_2:
		return String::STRING_FOR_PATCH_SOURCE_LFO_GLOBAL_2;
	case PatchSource::LFO_LOCAL_1:
		return String::STRING_FOR_PATCH_SOURCE_LFO_LOCAL_1;
	case PatchSource::LFO_LOCAL_2:
		return String::STRING_FOR_PATCH_SOURCE_LFO_LOCAL_2;
	case PatchSource::ENVELOPE_0:
		return String::STRING_FOR_PATCH_SOURCE_ENVELOPE_0;
	case PatchSource::ENVELOPE_1:
		return String::STRING_FOR_PATCH_SOURCE_ENVELOPE_1;
	case PatchSource::ENVELOPE_2:
		return String::STRING_FOR_PATCH_SOURCE_ENVELOPE_2;
	case PatchSource::ENVELOPE_3:
		return String::STRING_FOR_PATCH_SOURCE_ENVELOPE_3;
	case PatchSource::VELOCITY:
		return String::STRING_FOR_PATCH_SOURCE_VELOCITY;
	case PatchSource::NOTE:
		return String::STRING_FOR_PATCH_SOURCE_NOTE;
	case PatchSource::SIDECHAIN:
		return String::STRING_FOR_PATCH_SOURCE_SIDECHAIN;
	case PatchSource::RANDOM:
		return String::STRING_FOR_PATCH_SOURCE_RANDOM;
	case PatchSource::AFTERTOUCH:
		return String::STRING_FOR_PATCH_SOURCE_AFTERTOUCH;
	case PatchSource::X:
		return String::STRING_FOR_PATCH_SOURCE_X;
	case PatchSource::Y:
		return String::STRING_FOR_PATCH_SOURCE_Y;
	default:
		return String::STRING_FOR_PATCH_SOURCE_UNKNOWN;
	}
}

} // namespace deluge::scenes::services
