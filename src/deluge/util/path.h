#pragma once
#include "util/path.h"
#include <algorithm>
#include <cstddef>
#include <memory>
#include <ranges>
#include <set>
#include <stack>
#include <string>
#include <utility>

/// @brief A case-insensitive character traits class
struct case_insensitive_char_traits : public std::char_traits<char> {
	static bool eq(char c1, char c2) { return std::tolower(c1) == std::tolower(c2); }
	static bool ne(char c1, char c2) { return std::tolower(c1) != std::tolower(c2); }
	static bool lt(char c1, char c2) { return std::tolower(c1) < std::tolower(c2); }

	/// @brief Compare two strings
	static int compare(const char* str1, const char* str2, size_t n) {
		for (size_t i = 0; i < n; ++i) {
			if (std::tolower(str1[i]) < std::tolower(str2[i])) {
				return -1;
			}
			if (std::tolower(str1[i]) > std::tolower(str2[i])) {
				return 1;
			}
		}
		return 0;
	}

	/// @brief Find a character in a string
	static const char* find(const char* s, int n, char a) {
		for (size_t i = 0; i < n; ++i) {
			if (std::tolower(s[i]) != std::tolower(a)) {
				break;
			}
		}
		return s;
	}
};

/// case-insensitive string_view, for paths
using path_view = std::basic_string_view<char, case_insensitive_char_traits>;

/// case-insensitive string, for paths
using path_string = std::basic_string<char, case_insensitive_char_traits>;

/// @brief A component of a path, i.e. a directory or a filename
class PathComponent : public path_string, public std::enable_shared_from_this<PathComponent> {
	PathComponent(std::shared_ptr<PathComponent> parent) : parent_{std::move(parent)} {}

public:
	/// @brief The root constructor
	PathComponent() : parent_(nullptr) {}

	/// @brief Construct a path component from a string_view
	PathComponent(path_view path_fragment, std::shared_ptr<PathComponent> parent)
	    : path_string{path_fragment}, parent_{std::move(parent)} {}

	/// @brief Construct a path component from a string
	PathComponent(path_string&& path_fragment, std::shared_ptr<PathComponent> parent)
	    : path_string{path_fragment}, parent_{std::move(parent)} {}

	/// @brief Remove all expired children
	constexpr void clean() {
		std::erase_if(children_, [](auto& child) -> bool { return child.expired(); });
	}

	/// @brief Recursively remove all expired children
	constexpr void deep_clean() {
		clean();

		// recurse
		for (const auto& weak_child : children_) {
			auto child = weak_child.lock();
			child->deep_clean();
		}
	}

	/// @brief Add a child component to this component
	constexpr std::shared_ptr<PathComponent> add_child(path_view component) {
		clean();

		// search for already existing component...
		for (const auto& weak_child : children_) {
			std::shared_ptr<PathComponent> child = weak_child.lock();
			if (path_view{*child} == component) {
				return child;
			}
		}

		// add a new component
		std::shared_ptr<PathComponent> child = std::make_shared<PathComponent>(component, shared_from_this());
		children_.insert(child);
		return child;
	}

	/// @brief Get the parent component of this component
	/// @note will be nullptr for the root component
	std::shared_ptr<PathComponent> parent() { return parent_; }

	/// @brief Get whether this component has children or not
	constexpr bool has_children() const { return !children_.empty(); }

private:
	std::shared_ptr<PathComponent> parent_; // nullptr for root
	std::set<std::weak_ptr<PathComponent>, std::owner_less<std::weak_ptr<PathComponent>>> children_;
};

/// @brief An input_iterator for a Path
class PathIterator {
public:
	using value_type = char;
	using difference_type = ptrdiff_t;
	using container_type = std::stack<std::shared_ptr<PathComponent>>;

	/// @brief Construct a PathIterator from a container of path components
	constexpr PathIterator(container_type components)
	    : components_{components}, component_iterator_{components_.top()->begin()} {}

	/// @brief Construct a PathIterator from a container of path components and an iterator to a component
	/// @note This is used to construct an end sentinel iterator
	constexpr PathIterator(container_type components, PathComponent::iterator component_iterator)
	    : components_{components}, component_iterator_{component_iterator} {}

	/// @brief Dereference the iterator
	constexpr const value_type operator*() const {
		if (components_.empty()) {
			return '\0';
		}

		// At end of current component
		if (component_iterator_ == components_.top()->end()) {
			return '/';
		}
		return *component_iterator_;
	}

	/// @brief Increment the iterator
	constexpr PathIterator& operator++() {
		// At end of Path
		if (components_.empty()) {
			return *this;
		}

		if (component_iterator_ == components_.top()->end()) {
			components_.pop();
			if (!components_.empty()) {
				component_iterator_ = components_.top()->begin();
			}
			return *this;
		}

		component_iterator_++;
		return *this;
	}

	/// @brief Post-increment the iterator
	constexpr PathIterator operator++(int) {
		PathIterator tmp = *this;
		++*this;
		return tmp;
	}

	/// @brief Compare two iterators for equality
	/// This is used for tests against the end sentinel iterator
	bool operator==(const PathIterator& other) const {
		if (components_.empty() && other.components_.empty()) {
			return true;
		}

		if (components_.empty() || other.components_.empty()) {
			return false;
		}

		return components_.top() == other.components_.top() && component_iterator_ == other.component_iterator_;
	}

private:
	container_type components_;
	PathComponent::iterator component_iterator_;
};
static_assert(std::input_iterator<PathIterator>);

/// @brief A reverse iterator for a Path
class PathReverseIterator {
public:
	using value_type = char;
	using difference_type = ptrdiff_t;

	/// @brief Construct a PathReverseIterator from a component
	constexpr PathReverseIterator(std::shared_ptr<PathComponent> component)
	    : component_{component}, component_iterator_{component->rbegin()} {}

	/// @brief Dereference the iterator
	constexpr const value_type operator*() const {
		if (component_->parent() == nullptr) {
			return '\0'; // root
		}

		// At beginning of current component
		if (component_iterator_ == component_->rend()) {
			return '/';
		}
		return *component_iterator_;
	}

	/// @brief Increment the iterator
	constexpr PathReverseIterator& operator++() {
		if (component_->parent() == nullptr) {
			return *this;
		}

		// At beginning of current component
		if (component_iterator_ == component_->rend()) {
			component_ = component_->parent();
			component_iterator_ = component_->rbegin();
			return *this;
		}

		component_iterator_++;
		return *this;
	}

	/// @brief Post-increment the iterator
	constexpr PathReverseIterator operator++(int) {
		PathReverseIterator tmp = *this;
		++*this;
		return tmp;
	}

	/// @brief Compare two iterators for equality
	bool operator==(const PathReverseIterator& other) const {
		return component_ == other.component_ && component_iterator_ == other.component_iterator_;
	}

private:
	std::shared_ptr<PathComponent> component_;
	PathComponent::reverse_iterator component_iterator_;
};

/// @brief A class representing a path
/// This includes filenames, directories, and the root directory
class Path {
public:
	using iterator = PathIterator;
	using reverse_iterator = PathReverseIterator;
	using size_type = size_t;

	/// @brief Construct a Path from a path_view
	template <typename string_view_type>
	constexpr Path(string_view_type path) {
		std::shared_ptr<PathComponent> component = root_->shared_from_this();
		for (const auto fragment : path | std::views::split('/')) {
			if (!fragment.empty()) {
				component = component->add_child(path_view{fragment.data(), fragment.size()});
			}
		}
		basename_ = component;
	}

	/// @brief Construct a Path from a C string
	constexpr Path(const char* path) : Path{path_view{path}} {}

	/// @brief Get an iterator to the beginning of the path
	[[nodiscard]] constexpr iterator begin() const { return iterator{components()}; }

	/// @brief Get the end sentinel iterator
	[[nodiscard]] constexpr iterator end() const {
		return iterator{iterator::container_type{{basename_}}, basename_->end()}; // the end of the leaf node
	}

	/// @brief Get a reverse iterator for the path
	[[nodiscard]] constexpr reverse_iterator rbegin() const { return reverse_iterator{basename_}; }

	/// @brief Get a reverse iterator end sentinel for the path
	[[nodiscard]] constexpr reverse_iterator rend() const { return reverse_iterator{root_}; }

	/// @brief Get the parent path
	constexpr Path parent_path() const {
		if (basename_ == root_) {
			return *this;
		}
		return Path{basename_->parent()};
	}

	/// @brief Get the basename of the path (i.e. the last component)
	constexpr path_view basename() const {
		if (basename_ == root_) {
			return "";
		}
		return *basename_;
	}

	/// @brief Get the filename of the path, if it is one
	constexpr path_view filename() const {
		if (basename_ == root_ || !basename_->contains('.')) {
			return "";
		}
		return *basename_;
	}

	/// @brief Get the non-extension portion of a filename
	constexpr path_view stem() const {
		if (basename_ == root_) {
			return "";
		}
		auto basename_view = path_view{*basename_};
		size_t idx = basename_view.find_last_of('.');
		if (idx == 0 || idx == basename_view.size() || basename_view == "..") {
			return basename_view;
		}
		return basename_view.substr(0, idx);
	}


	/// @brief Get the extension of a filename
	constexpr path_view extension() const {
		if (basename_ == root_ || !basename_->contains('.')) {
			return "";
		}

		auto basename_view = path_view{*basename_};
		size_t idx = basename_view.find_last_of('.');
		if (idx == 0 || idx == basename_view.size() || basename_view == "..") {
			return "";
		}
		return basename_view.substr(idx);
	}

	/// @brief Test whether the path ends with a given string
	constexpr bool ends_with(path_view string) const {
		// fast match
		if (basename_->ends_with(string)) {
			return true;
		}

		// exhaustive match
		auto it = this->rbegin();
		auto string_it = string.rbegin();
		for (; it != this->rend() && string_it != string.rend(); ++it, ++string_it) {
			if (*it != *string_it) {
				return false;
			}
		}
		return true;
	}

	static constexpr bool isAudioFile(path_view filename) {
		if (filename[0] == '.') {
			return false; // macOS invisible files
		}
		return filename.ends_with(".wav") || isAIFF(filename);
	}

	static constexpr bool isAIFF(path_view filename) {
		return filename.ends_with(".aiff") || filename.ends_with(".aif");
	}

	template <typename string_type>
	constexpr operator string_type() const {
		string_type path;
		auto fragments = components();
		fragments.pop(); // skip root
		while (!fragments.empty()) {
			path += '/';
			path += fragments.top()->data();
			fragments.pop();
		}
		return path;
	}

	constexpr std::string to_string() const { return static_cast<std::string>(*this); }
	//constexpr operator std::string() const { return to_string(); }
	constexpr operator path_string() const { return static_cast<path_string>(*this); }

	constexpr std::shared_ptr<PathComponent> data() { return basename_; }

	constexpr Path& append(path_view path) {
		std::shared_ptr<PathComponent> component = basename_;
		for (const auto fragment : path | std::views::split('/')) {
			component = component->add_child(path_view{fragment.data(), fragment.size()});
		}
		basename_ = component;
		return *this;
	}

	constexpr Path& append(const std::string& path) { return append(path_view{path}); }
	constexpr Path& append(const char* path) { return append(path_view{path}); }

	template <class InputIt>
	Path& append(InputIt first, InputIt last) {
		for (auto it = first; it != last; ++it) {
			append(*it);
		}
		return *this;
	}

	constexpr Path& operator/=(path_view path) { return append(path); }
	constexpr Path& operator/=(const std::string& path) { return append(path); }
	constexpr Path& operator/=(const char* path) { return append(path); }

	constexpr Path& concat(path_view path) {
		if (!basename_->has_children()) {
			basename_->append(path);
		}
		else {
			basename_ = std::make_shared<PathComponent>(*basename_ + path_string{path}, basename_->parent());
		}
		return *this;
	}

	constexpr Path& concat(const path_string& path) {
		if (!basename_->has_children()) {
			basename_->append(path);
		}
		else {
			basename_ = std::make_shared<PathComponent>(*basename_ + path, basename_->parent());
		}
		return *this;
	}

	constexpr Path& concat(const std::string& path) { return concat(path_view{path}); }
	constexpr Path& concat(char c) {
		if (!basename_->has_children()) {
			basename_->push_back(c);
		}
		else {
			basename_ = std::make_shared<PathComponent>(*basename_ + c, basename_->parent());
		}
		return *this;
	}
	constexpr Path& concat(const char* path) { return concat(path_view{path}); }

	constexpr Path& operator+=(const std::string& path) { return concat(path); }
	constexpr Path& operator+=(path_view path) { return concat(path); }
	constexpr Path& operator+=(const char* path) { return concat(path_view{path}); }

	static constexpr std::shared_ptr<PathComponent> root() { return root_; }

private:
	Path(std::shared_ptr<PathComponent> basename) : basename_{basename} {}
	[[nodiscard]] constexpr iterator::container_type components() const {
		iterator::container_type stack;
		std::shared_ptr<PathComponent> current = basename_;
		while (current != nullptr) {
			stack.push(current);
			current = current->parent();
		}

		return stack;
	}

	std::shared_ptr<PathComponent> basename_;
	static std::shared_ptr<PathComponent> root_;
};
