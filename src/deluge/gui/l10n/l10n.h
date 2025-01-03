#pragma once
#include "gui/l10n/language.h"
#include "util/misc.h"
#include <array>
#include <cstddef>
#include <initializer_list>
#include <optional>

namespace deluge::l10n {
std::string_view get(const deluge::l10n::Language& language, deluge::l10n::String string);
std::string_view get(deluge::l10n::String string);
const char* getRaw(const deluge::l10n::Language& language, deluge::l10n::String string);
const char* getRaw(deluge::l10n::String string);
} // namespace deluge::l10n
