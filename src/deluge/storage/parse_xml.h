#pragma once

// The document tree and its parsers live in deluge_serialization; this header exposes them under deluge::storage.
#include "serialization/parse_xml.h"

namespace deluge::storage {
using deluge::serialization::parseXml;
} // namespace deluge::storage
