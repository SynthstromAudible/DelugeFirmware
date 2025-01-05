#include <string>

/// @brief A case-insensitive character traits class
/// @note from https://en.cppreference.com/w/cpp/string/char_traits
struct ci_char_traits : public std::char_traits<char> {
	static char to_upper(char ch) { return std::toupper((unsigned char)ch); }
	static bool eq(char c1, char c2) { return to_upper(c1) == to_upper(c2); }
	static bool lt(char c1, char c2) { return to_upper(c1) < to_upper(c2); }

	static int compare(const char* s1, const char* s2, std::size_t n) {
		while (n-- != 0) {
			if (to_upper(*s1) < to_upper(*s2))
				return -1;
			if (to_upper(*s1) > to_upper(*s2))
				return 1;
			++s1;
			++s2;
		}
		return 0;
	}

	static const char* find(const char* s, std::size_t n, char a) {
		const auto ua{to_upper(a)};
		while (n-- != 0) {
			if (to_upper(*s) == ua)
				return s;
			s++;
		}
		return nullptr;
	}
};

template <class DstTraits, class CharT, class SrcTraits>
constexpr std::basic_string_view<CharT, DstTraits>
traits_cast(const std::basic_string_view<CharT, SrcTraits> src) noexcept {
	return {src.data(), src.size()};
}

/// @brief A case-insensitive string type
using ci_string = std::basic_string<char, ci_char_traits>;

/// @brief A case-insensitive string view type
using ci_string_view = std::basic_string_view<char, ci_char_traits>;
