#include "semver.h"
#include "try.h"
#include <charconv>
#include <expected>

std::strong_ordering SemVer::operator<=>(const SemVer& other) const {
	if (auto cmp = (this->major <=> other.major); cmp != 0) {
		return cmp;
	}
	if (auto cmp = (this->minor <=> other.minor); cmp != 0) {
		return cmp;
	}
	if (auto cmp = (this->patch <=> other.patch); cmp != 0) {
		return cmp;
	}
	if (this->pre_release.empty() && !other.pre_release.empty()) {
		return std::strong_ordering::greater;
	}
	if (!this->pre_release.empty() && other.pre_release.empty()) {
		return std::strong_ordering::less;
	}
	return (this->pre_release <=> other.pre_release);
}

std::expected<SemVer, SemVer::Parser::Error> SemVer::Parser::parse() {
	SemVer semver = D_TRY(parseVersionCore());

	// Version Core alone is valid
	if (index_ == input_.size()) {
		return semver;
	}

	// Parse pre-release
	if (input_[index_] == '-') {
		index_++;
		semver.pre_release = parsePreRelease();
	}
	return semver;

	// // Return if complete
	// if (index_ == input_.size()) {
	// 	return semver;
	// }

	// // Parse build
	// if (input_[index_] == '+') {
	// 	index_++;
	// 	semver.build = parseBuild();
	// }

	// return std::unexpected(Error::END_OF_STREAM);
}

// Version Core
std::expected<SemVer, SemVer::Parser::Error> SemVer::Parser::parseVersionCore() {
	SemVer semver{};
	semver.major = D_TRY(parseNumericIdentifier());
	D_TRY(expect('.'));
	semver.minor = D_TRY(parseNumericIdentifier());
	D_TRY(expect('.'));
	semver.patch = D_TRY(parseNumericIdentifier());
	return semver;
}

std::string_view SemVer::Parser::parsePreRelease() {
	size_t start = index_;
	while (index_ < input_.size() && input_[index_] != '+') {
		index_++;
	}
	return input_.substr(start, index_ - start);
}

/*
std::string_view SemVer::Parser::parseBuild() {
    size_t start = index_;
    while (index_ < input_.size()) {
        index_++;
    }
    return input_.substr(start, index_ - start);
}
*/

std::expected<uint8_t, SemVer::Parser::Error> SemVer::Parser::parseNumericIdentifier() {
	uint8_t number;
	auto [ptr, ec] = std::from_chars(input_.data() + index_, input_.data() + input_.size(), number);
	if (ec != std::errc{}) {
		return std::unexpected(Error::INVALID_NUMBER);
	}
	index_ = ptr - input_.data();
	return number;
}

std::expected<void, SemVer::Parser::Error> SemVer::Parser::expect(char expected) {
	if (index_ >= input_.size()) {
		return std::unexpected(Error::END_OF_STREAM);
	}

	if (input_[index_] != expected) {
		return std::unexpected(Error::WRONG_CHAR);
	}

	++index_;

	return {};
}
