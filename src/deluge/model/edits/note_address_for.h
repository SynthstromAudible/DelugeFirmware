#pragma once

#include "deluge-project/note_address.h"
#include <optional>

class ModelStackWithNoteRow;

namespace deluge::edits {

/// @brief The durable address of the note-row a resolved ModelStack names.
///
/// A melodic row is keyed by its y; a kit row by its SoundDrum's id. The structural analogue of addressFor().
///
/// @param modelStack A stack with a clip and note-row set.
/// @return The row's address; nullopt if the clip or row is missing, or the clip, or a kit row's drum, has no id,
///         or the drum is not a SoundDrum.
std::optional<deluge::project::NoteRowAddress> noteRowAddressFor(const ModelStackWithNoteRow& modelStack);

} // namespace deluge::edits
