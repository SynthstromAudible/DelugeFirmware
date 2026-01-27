/**
 * Copyright (c) 2025 Katherine Whitlock
 *
 * This file is part of The Synthstrom Audible Deluge Firmware.
 *
 * The Synthstrom Audible Deluge Firmware is free software: you can redistribute it and/or modify it under the
 * terms of the GNU General Public License as published by the Free Software Foundation,
 * either version 3 of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY;
 * without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along with this program.
 * If not, see <https://www.gnu.org/licenses/>.
 */

#pragma once
#include "dsp_ng/core/generator.hpp"
#include "dsp_ng/core/processor.hpp"
#include "dsp_ng/core/types.hpp"
#include <concepts>
#include <list>
#include <memory>

namespace deluge::dsp {

/// @class Chain
/// @brief A class that represents a chain of block processors for audio signal processing.
///
/// The `Chain` class allows for the composition of multiple `BlockProcessor` instances
/// into a processing chain. Each processor in the chain processes the input signal
/// sequentially, passing the output of one processor as the input to the next.
///
/// @tparam T The data type of the audio samples being processed.
///
/// @details
/// The `Chain` class maintains a list of pointers to `BlockProcessor` instances, which
/// are executed in the order they are added to the chain.
///
/// Example usage:
/// ```
/// auto processor1 = std::make_unique<MyProcessor<float>>();
/// auto processor2 = std::make_unique<MyProcessor<float>>();
/// deluge::dsp::Chain<float> chain({processor1, processor2});
/// chain.renderBlock(inputSignal, outputBuffer);
/// ```
///
/// @see BlockProcessor
template <typename T>
class Chain : public BlockProcessor<T> {
	using chain_type = std::list<std::shared_ptr<BlockProcessor<T>>>;
	chain_type processors_;

public:
	using value_type = T;

	/// @brief Constructor for the Chain class.
	/// @param processors A list of unique pointers to `BlockProcessor` instances.
	/// @details The processors are stored in a list and will be executed in the order they are added.
	Chain(std::list<std::shared_ptr<BlockProcessor<T>>> processors) : processors_{processors} {}

	/// @brief Constructor for the Chain class.
	/// @param processors A list of unique pointers to `BlockProcessor` instances.
	/// @details The processors are stored in a list and will be executed in the order they are added.
	Chain(std::list<std::unique_ptr<BlockProcessor<T>>> processors) {
		for (auto& processor : processors) {
			processors_.push_back(std::move(processor));
		}
	}

	/// @brief Constructor for the Chain class.
	/// @param args A variadic list of arguments to construct the processors.
	/// @details This constructor uses perfect forwarding to create the processors in place.
	/// @note The processors are stored in a list and will be executed in the order they are added.
	/// @tparam Args The types of the arguments used to construct the processors.
	template <typename... Args>
	requires(std::derived_from<BlockProcessor<T>, Args> && ...)
	Chain(Args&&... args) {
		// Use perfect forwarding to construct the processors
		(processors_.push_back(std::make_shared<Args>(std::forward<Args>(args))), ...);
	}

	/// @brief Renders a block of audio samples by processing the input through each processor in the chain
	/// @param input The input signal to be processed.
	/// @param output The output buffer where the processed signal will be stored.
	/// @details The input signal is passed through each processor in the chain, and the output of each
	/// processor is used as the input for the next processor. The final output is stored in the provided
	/// output buffer.
	/// @note The output is overwritten with the processed signal.
	[[gnu::always_inline]] void renderBlock(Signal<T> input, Buffer<T> output) final {
		for (auto& processor : processors_) {
			processor->renderBlock(input, output); // Call the process function for each sample
		}
	}

	/// @brief Get the processors in the chain.
	chain_type& processors() { return processors_; }

	/// @brief Get the processors in the chain (const version).
	const chain_type& processors() const { return processors_; }

	/// @brief Get the number of processors in the chain.
	/// @return The number of processors in the chain.
	[[nodiscard]] size_t size() const { return processors_.size(); }

	/// @brief Check if the chain is empty.
	/// @return `true` if the chain is empty, `false` otherwise.
	[[nodiscard]] bool empty() const { return processors_.empty(); }

	/// @brief Clear the chain of processors.
	/// @details This method removes all processors from the chain.
	void clear() { processors_.clear(); }

	/// @brief Add a processor to the chain.
	/// @param processor A unique pointer to the `BlockProcessor` to be added.
	/// @details The processor is added to the end of the chain.
	void add(std::shared_ptr<BlockProcessor<T>> processor) { processors_.push_back(std::move(processor)); }

	/// @brief Add a processor to the chain.
	/// @param processor A `BlockProcessor` to be added.
	/// @details The processor is added to the end of the chain.
	/// @note This method uses perfect forwarding to construct the processor in place.
	void add(BlockProcessor<T>&& processor) {
		processors_.push_back(std::make_unique<BlockProcessor<T>>(std::move(processor)));
	}

	/// @brief Remove a processor from the chain.
	/// @param processor A pointer to the `BlockProcessor` to be removed.
	/// @details The processor is removed from the chain if it exists.
	/// @return `true` if the processor was removed, `false` otherwise.
	bool remove(std::shared_ptr<BlockProcessor<T>> processor) {
		auto it = std::ranges::find(processors_, processor);
		if (it != processors_.end()) {
			processors_.erase(it);
			return true;
		}
		return false;
	}

	/// @brief Remove a processor from the chain by index.
	/// @param index The index of the processor to be removed.
	/// @details The processor is removed from the chain if the index is valid.
	/// @return `true` if the processor was removed, `false` otherwise.
	/// @note The index is zero-based, so the first processor has an index of 0.
	bool remove(size_t index) {
		if (index < processors_.size()) {
			auto it = std::next(processors_.begin(), index);
			processors_.erase(it);
			return true;
		}
		return false;
	}

	/// @brief Erase a processor from the chain.
	/// @param it An iterator to the processor to be erased.
	/// @details The processor is removed from the chain.
	/// @return An iterator to the next processor in the chain.
	chain_type::iterator erase(chain_type::iterator it) { return processors_.erase(it); }
};

} // namespace deluge::dsp
