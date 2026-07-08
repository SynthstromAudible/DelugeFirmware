#pragma once

#include "deluge-project/note_address.h"
#include <optional>

class ModelStackWithNoteRow;

namespace deluge::edits {

/// The durable address of the melodic note-row a resolved ModelStack names. nullopt for a kit row (M2c) or a
/// clip with no serial/session id (unaddressable). The structural analogue of addressFor().
std::optional<deluge::project::NoteRowAddress> noteRowAddressFor(const ModelStackWithNoteRow& modelStack);

} // namespace deluge::edits
