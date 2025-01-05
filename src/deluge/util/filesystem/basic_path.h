#pragma once
#include <cstddef>
#include <memory>
#include <set>
#include <stack>
#include <string>
#include <string_view>

/// @brief A component of a path, i.e. a directory or a filename
template <typename StringT = std::string>
class BasicPathComponent : public StringT, public std::enable_shared_from_this<BasicPathComponent<StringT>> {
	BasicPathComponent(std::shared_ptr<BasicPathComponent<StringT>> parent) : parent_{std::move(parent)} {}

public:
	using string_view_type = std::basic_string_view<typename StringT::value_type, typename StringT::traits_type>;

	/// @brief The root constructor
	constexpr BasicPathComponent() : parent_(nullptr) {}

	/// @brief Construct a path component from a string_view
	constexpr BasicPathComponent(string_view_type path_fragment, std::shared_ptr<BasicPathComponent<StringT>> parent)
	    : StringT{path_fragment}, parent_{std::move(parent)} {}

	/// @brief Construct a path component from a string
	constexpr BasicPathComponent(const StringT&& path_fragment, std::shared_ptr<BasicPathComponent<StringT>> parent)
	    : StringT{path_fragment}, parent_{std::move(parent)} {}

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
	constexpr std::shared_ptr<BasicPathComponent<StringT>> add_child(string_view_type component) {
		clean();

		// search for already existing component...
		for (const auto& weak_child : children_) {
			std::shared_ptr<BasicPathComponent<StringT>> child = weak_child.lock();
			if (string_view_type{*child} == component) {
				return child;
			}
		}

		// add a new component
		std::shared_ptr<BasicPathComponent<StringT>> child =
		    std::make_shared<BasicPathComponent<StringT>>(component, this->shared_from_this());
		children_.insert(child);
		return child;
	}

	/// @brief Get the parent component of this component
	/// @note will be nullptr for the root component
	constexpr std::shared_ptr<BasicPathComponent<StringT>> parent() { return parent_; }

	/// @brief Get whether this component has children or not
	constexpr bool has_children() const { return !children_.empty(); }

private:
	std::shared_ptr<BasicPathComponent<StringT>> parent_; // nullptr for root
	std::set<std::weak_ptr<BasicPathComponent<StringT>>, std::owner_less<std::weak_ptr<BasicPathComponent<StringT>>>>
	    children_;
};

/// @brief An input_iterator for a Path
template <typename PathComponentT>
class BasicPathIterator {
public:
	using value_type = char;
	using difference_type = ptrdiff_t;
	using container_type = std::stack<std::shared_ptr<PathComponentT>>;

	/// @brief Construct a BasicPathIterator from a container of path components
	constexpr BasicPathIterator(container_type components)
	    : components_{components}, component_iterator_{components_.top()->begin()} {}

	/// @brief Construct a BasicPathIterator from a container of path components and an iterator to a component
	/// @note This is used to construct an end sentinel iterator
	constexpr BasicPathIterator(container_type components, PathComponentT::iterator component_iterator)
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
	constexpr BasicPathIterator& operator++() {
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
	constexpr BasicPathIterator operator++(int) {
		BasicPathIterator tmp = *this;
		++*this;
		return tmp;
	}

	/// @brief Compare two iterators for equality
	/// This is used for tests against the end sentinel iterator
	bool operator==(const BasicPathIterator& other) const {
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
	PathComponentT::iterator component_iterator_;
};
static_assert(std::input_iterator<BasicPathIterator<std::string>>);

/// @brief A reverse iterator for a Path
template <typename PathComponentT>
class BasicPathReverseIterator {
public:
	using value_type = char;
	using difference_type = ptrdiff_t;

	/// @brief Construct a PathReverseIterator from a component
	constexpr BasicPathReverseIterator(std::shared_ptr<PathComponentT> component)
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
	constexpr BasicPathReverseIterator& operator++() {
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
	constexpr BasicPathReverseIterator operator++(int) {
		BasicPathReverseIterator tmp = *this;
		++*this;
		return tmp;
	}

	/// @brief Compare two iterators for equality
	bool operator==(const BasicPathReverseIterator& other) const {
		return component_ == other.component_ && component_iterator_ == other.component_iterator_;
	}

private:
	std::shared_ptr<PathComponentT> component_;
	PathComponentT::reverse_iterator component_iterator_;
};

/// @brief A class representing a path
/// This includes filenames, directories, and the root directory
template <typename StringT = std::string>
class BasicPath {
public:
	using string_view_type = std::basic_string_view<typename StringT::value_type, typename StringT::traits_type>;
	using component_type = BasicPathComponent<StringT>;
	using iterator = BasicPathIterator<component_type>;
	using reverse_iterator = BasicPathReverseIterator<component_type>;
	using size_type = size_t;

	/// @brief Construct a Path from a string_view
	template <typename string_view_type>
	constexpr BasicPath(string_view_type path) {
		std::shared_ptr<component_type> component = root_->shared_from_this();
		for (const auto fragment : path | std::views::split('/')) {
			if (!fragment.empty()) {
				component = component->add_child(string_view_type{fragment.data(), fragment.size()});
			}
		}
		basename_ = component;
	}

	/// @brief Construct a Path from a C string
	constexpr BasicPath(const char* path) : BasicPath{string_view_type{path}} {}

	/// @brief Get an iterator to the beginning of the path
	[[nodiscard]] constexpr iterator begin() const { return iterator{components()}; }

	/// @brief Get the end sentinel iterator
	[[nodiscard]] constexpr iterator end() const {
		return {typename iterator::container_type{{basename_}}, basename_->end()}; // the end of the leaf node
	}

	/// @brief Get a reverse iterator for the path
	[[nodiscard]] constexpr reverse_iterator rbegin() const { return reverse_iterator{basename_}; }

	/// @brief Get a reverse iterator end sentinel for the path
	[[nodiscard]] constexpr reverse_iterator rend() const { return reverse_iterator{root_}; }

	/// @brief Get the parent path
	constexpr BasicPath parent_path() const {
		if (basename_ == root_) {
			return *this;
		}
		return {basename_->parent()};
	}

	/// @brief Get the basename of the path (i.e. the last component)
	constexpr string_view_type basename() const {
		if (basename_ == root_) {
			return "";
		}
		return *basename_;
	}

	/// @brief Get the filename of the path, if it is one
	constexpr string_view_type filename() const {
		if (basename_ == root_ || !basename_->contains('.')) {
			return "";
		}
		return *basename_;
	}

	/// @brief Get the non-extension portion of a filename
	constexpr string_view_type stem() const {
		if (basename_ == root_) {
			return "";
		}
		auto basename_view = string_view_type{*basename_};
		size_t idx = basename_view.find_last_of('.');
		if (idx == 0 || idx == basename_view.size() || basename_view == "..") {
			return basename_view;
		}
		return basename_view.substr(0, idx);
	}

	/// @brief Get the extension of a filename
	constexpr string_view_type extension() const {
		if (basename_ == root_ || !basename_->contains('.')) {
			return "";
		}

		auto basename_view = string_view_type{*basename_};
		size_t idx = basename_view.find_last_of('.');
		if (idx == 0 || idx == basename_view.size() || basename_view == "..") {
			return "";
		}
		return basename_view.substr(idx);
	}

	/// @brief Test whether the path ends with a given string
	constexpr bool ends_with(string_view_type string) const {
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

	template <typename string_type>
	constexpr operator string_type() const noexcept(false) {
		string_type path;
		auto fragments = components();

		// special case for just root
		if (fragments.top() == root() && fragments.size() == 1) {
			return "/";
		}

		fragments.pop(); // Otherwise, pop the root
		while (!fragments.empty()) {
			path += '/';
			path += fragments.top()->data();
			fragments.pop();
		}
		return path;
	}

	/// @brief Convert the path to a string
	constexpr std::string to_string() const noexcept(false) { return static_cast<std::string>(*this); }

	/// @brief Get the shared pointer to the basename component
	constexpr std::shared_ptr<component_type> data() const { return basename_; }

	/// @brief Append a path to the current path
	constexpr BasicPath& append(string_view_type path) {
		std::shared_ptr<component_type> component = basename_;
		for (const auto fragment : path | std::views::split('/')) {
			component = component->add_child(string_view_type{fragment.data(), fragment.size()});
		}
		basename_ = component;
		return *this;
	}

	/// @brief Append a path to the current path
	constexpr BasicPath& append(const std::string& path) { return append(string_view_type{path}); }

	/// @brief Append a path to the current path
	constexpr BasicPath& append(const char* path) { return append(string_view_type{path}); }

	/// @brief Append a path to the current path
	constexpr BasicPath& operator/=(string_view_type path) { return append(path); }

	/// @brief Append a path to the current path
	constexpr BasicPath& operator/=(const std::string& path) { return append(path); }

	/// @brief Append a path to the current path
	constexpr BasicPath& operator/=(const char* path) { return append(path); }

	/// @brief Concatenate a string onto the current path (does not add a separator)
	constexpr BasicPath& concat(string_view_type str) {
		if (!basename_->has_children()) {
			basename_->append(str);
		}
		else {
			basename_ = std::make_shared<component_type>(*basename_ + StringT{str}, basename_->parent());
		}
		return *this;
	}

	/// @brief Concatenate a string onto the current path (does not add a separator)
	constexpr BasicPath& concat(const StringT& str) {
		if (!basename_->has_children()) {
			basename_->append(str);
		}
		else {
			basename_ = std::make_shared<component_type>(*basename_ + str, basename_->parent());
		}
		return *this;
	}

	/// @brief Concatenate a string onto the current path (does not add a separator)
	constexpr BasicPath& concat(const std::string& str) { return concat(string_view_type{str}); }

	/// @brief Concatenate a character onto the current path (does not add a separator)
	constexpr BasicPath& concat(typename StringT::value_type c) {
		if (!basename_->has_children()) {
			basename_->push_back(c);
		}
		else {
			basename_ = std::make_shared<component_type>(*basename_ + c, basename_->parent());
		}
		return *this;
	}

	/// @brief Concatenate a string onto the current path (does not add a separator)
	constexpr BasicPath& concat(const char* cstr) { return concat(string_view_type{cstr}); }

	/// @brief Concatenate a string onto the current path (does not add a separator)
	constexpr BasicPath& operator+=(const std::string& path) { return concat(path); }

	/// @brief Concatenate a string onto the current path (does not add a separator)
	constexpr BasicPath& operator+=(string_view_type path) { return concat(path); }

	/// @brief Concatenate a string onto the current path (does not add a separator)
	constexpr BasicPath& operator+=(const char* path) { return concat(string_view_type{path}); }

	/// @brief Get the root path node
	static constexpr std::shared_ptr<component_type> root() { return root_; }

private:
	/// @brief Construct a path from a shared pointer to a basename component
	BasicPath(std::shared_ptr<component_type> basename) : basename_{basename} {}

	/// @brief Get the components of the path as a stack for iterating
	[[nodiscard]] constexpr iterator::container_type components() const {
		typename iterator::container_type stack;
		std::shared_ptr<component_type> current = basename_;
		while (current != nullptr) {
			stack.push(current);
			current = current->parent();
		}

		return stack;
	}

	/// @brief The basename component of the path (i.e. the last component)
	std::shared_ptr<component_type> basename_;

	/// @brief The root component of the path (shared between all paths)
	static std::shared_ptr<component_type> root_;
};

template <typename StringT>
std::shared_ptr<BasicPathComponent<StringT>> BasicPath<StringT>::root_ =
    std::make_shared<BasicPathComponent<StringT>>();
