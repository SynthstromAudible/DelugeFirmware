#include <compare> // IWYU pragma: keep -used for operator <=>, false positive
#include <cstdint>
#include <expected>
#include <string_view>

/**
 * @brief Represents a simple Semantic Version number such as "4.3.7-beta"
 */
struct SemVer {
	/// Major version
	uint8_t major;

	/// Minor version
	uint8_t minor;

	/// Patch version
	uint8_t patch;

	/// Pre-release string
	std::string_view pre_release{};

	// std::optional<std::string_view> build{};

	/// @brief Default equality operator
	bool operator==(const SemVer&) const = default;

	/**
	 * @brief Compare two SemVer objects.
	 * @details This is a custom operator implementation (instead of default) due
	 *          due to the way semver handles "pre-release" information. If a SemVer
	 *          has a pre-release extension, it is considered _older_ (less than)
	 *          than the same SemVer without that extension. This means that simple
	 *          field-by-field compare cannot be done, as the pre-release would
	 *          normally require lexicographical comparison and count anything longer
	 *          as "greater than", which is the opposite of what's desired.
	 */
	std::strong_ordering operator<=>(const SemVer& other) const;

	/// @brief Used to parse SemVers from strings
	class Parser {
	public:
		/// Common parsing errors
		enum class Error {
			/// Could not parse the numeric
			INVALID_NUMBER,

			/// Did not find the expected char
			WRONG_CHAR,

			/// Reached end of stream while parsing
			END_OF_STREAM,
		};

		/// Create a new parser with the given string
		Parser(std::string_view input) : input_(input) {};

		/// Execute the parser, returning either the well-formed @ref SemVer or a @ref Parser::Error
		std::expected<SemVer, Error> parse();

	private:
		std::string_view input_;
		size_t index_ = 0;

		/**
		 * @brief Expect a character to be the next in the stream
		 * @note This consumes the character if it exists
		 * @param[in] expected The character to read
		 *
		 * @return std::expected<void, Error> If no error, the character was successfully found
		 * @retval Error::WRONG_CHAR The character was not found
		 * @retval Error::END_OF_STREAM The input string ran out of characters to read
		 */
		std::expected<void, Error> expect(char expected);

		/**
		 * @brief Parses a Semantic Versioning "version core" (i.e. '1.20.7') from the input stream
		 * @details see https://semver.org/#backusnaur-form-grammar-for-valid-semver-versions
		 *
		 * @returns std::expected<SemVer, Error> The well-formed Semantic Version from the core
		 */
		std::expected<SemVer, Error> parseVersionCore();

		/**
		 * @brief Parse a number from the input stream
		 * @details Technically less strict than the true SemVer grammar requires,
		 *          this internally uses std::from_chars to parse the string to an int.
		 *
		 * @return std::expected<uint8_t, Error> The parsed number or Error
		 * @retval Error::INVALID_NUMBER If the number cannot be parsed
		 */
		std::expected<uint8_t, Error> parseNumericIdentifier();

		/**
		 * @brief Consumes all characters up to the build-tag identifier '+' and returns them
		 *
		 * @return std::string_view The characters found
		 */
		std::string_view parsePreRelease();

		// std::string_view parseBuild();
	};

	/**
	 * @brief Parse a SemVer from a string
	 * @details This is just a helper object to construct the parser and run it
	 *
	 * @param input The string to parse
	 * @return std::expected<SemVer, Parser::Error> Either the successfully parsed SemVer, or the Error that arose
	 * during parsing.
	 */
	static std::expected<SemVer, Parser::Error> parse(std::string_view input) { return Parser{input}.parse(); }
};
