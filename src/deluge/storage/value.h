#pragma once

// The document tree and its parsers live in deluge_serialization; this header exposes it under deluge::storage.
#include "serialization/value.h"

namespace deluge::storage {
using deluge::serialization::Value;
} // namespace deluge::storage
