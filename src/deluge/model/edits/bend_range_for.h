#pragma once

/// @file
/// @brief A bend range change, by the param manager whose expression set holds it.
///
/// A gesture (a menu, MIDI learn) and an incoming bend-range message name a bend range by the param manager that
/// holds it, not by its owner. The clip's own manager holds the clip's ranges, which are clip values; a kit row's
/// holds its finger range, which is the note row's. Each of those goes through its setting's Edit, which writes the
/// record a save reads. A range no record holds (a kit row's whole-row range, a manager no clip owns) is set on the
/// expression set alone, as the file does not save it.

#include <cstdint>

class InstrumentClip;
class ParamManager;

namespace deluge::edits {

/// @brief Set bend range @p which (BEND_RANGE_MAIN or BEND_RANGE_FINGER_LEVEL) of the expression set in @p params.
/// @param clip      The clip whose manager, or one of whose rows' managers, @p params is; null for none.
/// @param params    The param manager holding the expression set.
/// @param which     Which range.
/// @param semitones The new range.
void bendRangeFor(::InstrumentClip* clip, ::ParamManager& params, int32_t which, int32_t semitones);

/// @copydoc bendRangeFor(::InstrumentClip*, ::ParamManager&, int32_t, int32_t)
inline void bendRangeFor(::InstrumentClip& clip, ::ParamManager& params, int32_t which, int32_t semitones) {
	bendRangeFor(&clip, params, which, semitones);
}

} // namespace deluge::edits
