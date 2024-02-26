#include <compare> // IWYU pragma: keep -used for operator <=>, false positive
#include <cstdint>
#include <expected>
#include <string_view>

struct SemVer {
	~SemVer() = default;

	uint8_t major;
	uint8_t minor;
	uint8_t patch;
	std::string_view pre_release{};
	// std::optional<std::string_view> build{};

	bool operator==(const SemVer&) const = default;
	std::strong_ordering operator<=>(const SemVer& other) const;

	class Parser {
	public:
		enum class Error {
			INVALID_NUMBER,
			WRONG_CHAR,
			END_OF_STREAM,
		};

		Parser(std::string_view input) : input_(input){};
		std::expected<SemVer, Error> parse();

	private:
		std::string_view input_;
		size_t index = 0;

		std::expected<void, Error> expect(char expected);
		std::expected<SemVer, Error> parseVersionCore();
		std::expected<uint8_t, Error> parseNumericIdentifier();
		std::string_view parsePreRelease();
		std::string_view parseBuild();
	};

	static std::expected<SemVer, Parser::Error> parse(std::string_view input) { return Parser{input}.parse(); }
};
