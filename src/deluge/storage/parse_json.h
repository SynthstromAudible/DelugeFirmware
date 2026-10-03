#pragma once

// The document tree and its parsers live in deluge_serialization; this keeps the app's existing includes working.
#include "serialization/parse_json.h"

namespace deluge::storage {
using deluge::serialization::parseJson;
} // namespace deluge::storage
