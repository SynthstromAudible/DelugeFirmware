#pragma once
#include <gsl/gsl>
#include <memory>
#include <stack>

namespace deluge {
/// @brief A simple object pool implementation
/// @tparam T The type of object to pool
/// @tparam Alloc The allocator to use for the pool
/// @note ObjectPool instances are thread-local singletons
template <typename T, template <typename> typename Alloc = std::allocator>
class ObjectPool {
	static constexpr size_t kDefaultSize = 16;
	ObjectPool() { repopulate(); };

public:
	~ObjectPool() { clear(); };

	/// @brief Gets the global pool for a given object
	/// @returns A reference to the pool
	static ObjectPool& get() {
		static thread_local ObjectPool instance;
		return instance;
	}

	/// @brief Sets the size of the pool
	/// @param size The new size of the pool
	void setCapacity(size_t n) {
		capacity_ = n;
		if (capacity_ < objects_.size()) {
			// We need to shrink the pool
			while (objects_.size() > capacity_) {
				alloc_.deallocate(objects_.top(), 1);
				objects_.pop();
			}
		}
	}

	/// @brief Gets the capacity of the pool
	/// @returns The capacity of the pool
	[[nodiscard]] size_t capacity() const { return capacity_; }

	/// @brief Gets the number of objects in the pool
	/// @returns The number of objects in the pool
	[[nodiscard]] size_t size() const { return objects_.size(); }

	/// Recycles an object back into the pool
	/// @param obj The object to recycle
	static void recycle(gsl::owner<T*> obj) {
		ObjectPool& op = get();

		if (op.objects_.size() < op.capacity_) {
			op.objects_.push(obj);
		}
		else {
			Alloc<T>().deallocate(obj, 1);
		}
	}

	/// @brief Repopulates the pool to its original size
	/// @throws deluge::exception::BAD_ALLOC if memory allocation fails
	void repopulate() noexcept(false) {
		for (size_t i = objects_.size(); i < capacity_; ++i) {
			T* mem = alloc_.allocate(1);
			objects_.push(new (mem) T());
		}
	}

	using ptr_t = std::unique_ptr<T, decltype(&recycle)>;

	/// @brief Acquires an object from the pool
	/// @throws deluge::exception::BAD_ALLOC if memory allocation fails
	/// @returns A managed pointer to the acquired object
	ptr_t acquire() noexcept(false) {
		if (objects_.empty()) {
			T* memory = alloc_.allocate(1);
			return {new (memory) T(), &recycle};
		}
		T* obj = objects_.top();
		objects_.pop();
		return {obj, &recycle};
	}

	void clear() {
		while (!objects_.empty()) {
			alloc_.deallocate(objects_.top(), 1);
			objects_.pop();
		}
	}

	[[nodiscard]] constexpr bool empty() const { return objects_.empty(); }

private:
	size_t capacity_ = kDefaultSize;
	std::stack<T*, std::deque<T*, Alloc<T*>>> objects_;
	Alloc<T> alloc_;
};
} // namespace deluge
