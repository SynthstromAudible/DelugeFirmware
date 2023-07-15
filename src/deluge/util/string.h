#pragma once
#include "memory/fallback_allocator.h"
#include <string>

namespace deluge {
using string = std::basic_string<char, std::char_traits<char>, memory::fallback_allocator<char>>;
using wstring = std::basic_string<wchar_t, std::char_traits<wchar_t>, memory::fallback_allocator<wchar_t>>;

// Needs C++20
//using std::u8string = std::basic_string<char8_t, std::char_traits<char8_t>, memory::fallback_allocator<char8_t>>;

using u16string = std::basic_string<char16_t, std::char_traits<char16_t>, memory::fallback_allocator<char16_t>>;
using u32string = std::basic_string<char32_t, std::char_traits<char32_t>, memory::fallback_allocator<char32_t>>;
} // namespace deluge
