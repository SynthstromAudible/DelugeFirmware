#pragma once
#include "memory/external_allocator.h"
#include "memory/fast_allocator.h"
#include <deque>
#include <forward_list>
#include <functional>
#include <list>
#include <map>
#include <queue>
#include <set>
#include <stack>
#include <unordered_map>
#include <vector>

namespace deluge {
// For most purposes containers relying on a fallback allocation strategy should be fine.
// At some point this will probably move to a priority-based allocation system, so default template arguments
// are used instead of hardcoded allocator selection.
//
// If we decide to require explicit allocation choice in the future, we can also remove the default argument and
// fix any broken container uses, adding fallback_allocator to their template arguments or a different allocator
// if appropriate.

// Vector (resizeable variable-length array, unknown size)
template <typename T, typename Alloc = memory::external_allocator<T>>
using vector = std::vector<T, Alloc>;

// Doubly-ended queue
template <typename T, typename Alloc = memory::external_allocator<T>>
using deque = std::deque<T, Alloc>;

// Singly linked list
template <typename T, typename Alloc = memory::external_allocator<T>>
using forward_list = std::forward_list<T, Alloc>;

// Doubly-linked list
template <typename T, typename Alloc = memory::external_allocator<T>>
using list = std::list<T, Alloc>;

// Tree map
template <typename Key, typename T, typename Alloc = memory::external_allocator<std::pair<const Key, T>>>
using map = std::map<Key, T, std::less<Key>, Alloc>;

// Hash map
template <typename Key, typename T, typename Alloc = memory::external_allocator<std::pair<const Key, T>>>
using unordered_map = std::unordered_map<Key, T, std::hash<Key>, std::equal_to<Key>, Alloc>;

// Stack
template <typename T, typename Alloc = memory::external_allocator<T>>
using stack = std::stack<T, deque<T, Alloc>>;

// Queue
template <typename T, typename Alloc = memory::external_allocator<T>>
using queue = std::queue<T, deque<T, Alloc>>;

template <class T>
using fast_list = std::list<T, memory::fast_allocator<T>>;

// Vector (resizeable variable-length array, unknown size)
template <typename T>
using fast_vector = std::vector<T, memory::fast_allocator<T>>;

template <typename T>
using fast_priority_queue = std::priority_queue<T, fast_vector<T>>;

template <class T, class Compare = std::less<T>>
using fast_set = std::set<T, Compare, memory::fast_allocator<T>>;

template <typename Key, typename T, class Compare = std::less<Key>>
using fast_multimap = std::multimap<Key, T, Compare, memory::fast_allocator<std::pair<const Key, T>>>;

template <typename Key, typename T, class Compare = std::equal_to<Key>>
using fast_unordered_map =
    std::unordered_map<Key, T, std::hash<Key>, Compare, memory::fast_allocator<std::pair<const Key, T>>>;

template <typename Key, typename T, class Compare = std::less<Key>>
using fast_map = std::map<Key, T, Compare, memory::fast_allocator<std::pair<const Key, T>>>;
} // namespace deluge
