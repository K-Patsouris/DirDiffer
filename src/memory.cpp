#include "memory.h"
#include "int_defs.h"
#include "logger.h"
#include <cstdlib>	// std::malloc, std::free
#include <array>
#include <bit>		// std::has_single_bit for alignment value verification
#include <new>		// std::hardware_constructive_interference_size

#include <cstddef>	// std::byte
#include <atomic>	// spinlock
#include <intrin.h>
#pragma intrinsic(_mm_pause)


#ifdef DIRDIFFER_ALLOCATION_LOGGING

#include <chrono>

namespace diff::diag {
	using std::size_t;


	const inline auto start_point = std::chrono::steady_clock::now();


	constinit inline size_t total_allocations = 0;
	constinit inline size_t total_deallocations = 0;
	constinit inline size_t max_concurrent_allocations = 0;
	constinit inline size_t unhandled_deallocations = 0;

	constinit inline size_t total_bytes_allocated = 0;
	constinit inline size_t total_bytes_deallocated = 0;
	constinit inline size_t maximum_bytes_in_use = 0;

	struct block_stats {
		size_t uses = 0;
		size_t current = 0;
		size_t max = 0;
		size_t fails = 0;
	};
	constinit inline std::array<block_stats, 8> blocks_records{};
	constinit inline size_t total_blocks_requested_bytes = 0;
	constinit inline size_t current_blocks_used_bytes = 0;
	constinit inline size_t max_blocks_used_bytes = 0;

	class fancy_array {
	private:
		struct simple_pair {
			constexpr void init(const size_t init_block_size) noexcept {
				block_size = init_block_size;
				times_allocated = 1;
			}
			constexpr void increment() noexcept { ++times_allocated; }
			constexpr bool empty() const noexcept { return block_size == 0; }
			constexpr void swap(simple_pair& rhs) noexcept {
				size_t temp1 = this->block_size;
				size_t temp2 = this->times_allocated;
				this->block_size = rhs.block_size;
				this->times_allocated = rhs.times_allocated;
				rhs.block_size = temp1;
				rhs.times_allocated = temp2;
			}
			constexpr auto operator==(const size_t other_block_size) const noexcept { return block_size == other_block_size; }
			constexpr auto operator<=>(const simple_pair& rhs) const noexcept { return this->times_allocated <=> rhs.times_allocated; }
			size_t block_size{ 0 };
			size_t times_allocated{ 0 };
		};

		std::array<simple_pair, 100> data{};

	public:
		constexpr void increment(const size_t block_size) noexcept {
			if (not data.back().empty()) {
				return; // Already holding 100 records.
			}

			const auto begin = data.begin();
			const auto end = data.end();

			auto it = begin;
			for (; it != end; ++it) {
				if (*it == block_size) {
					it->increment();
					break;
				}
				if (it->empty()) {
					it->init(block_size);
					return;
				}
			}

			for (auto previous = it; previous != begin;) {
				--previous;
				if (*it >= *previous) {
					it->swap(*previous);
					--it;
				}
				else {
					return;
				}
			}

		}

		constexpr auto size() const noexcept {
			size_t ret = 0;
			for (const auto& elem : data) {
				ret += not elem.empty();
			}
			return ret;
		}
		constexpr auto begin() const noexcept { return data.begin(); }
		constexpr auto end() const noexcept { return data.end(); }

	};

	constinit inline size_t total_stack_requested_bytes = 0;
	constinit inline size_t current_stack_used_bytes = 0;
	constinit inline size_t max_stack_used_bytes = 0;
	constinit inline size_t stack_failures = 0;
	constinit inline fancy_array stack_top_allocation_sizes_and_counts{};

	constinit inline size_t malloc_bytes = 0;
	constinit inline size_t malloc_failures = 0;

	constinit inline size_t aligned_allocations = 0;


	enum class allocator : size_t {
		bitblocks,
		stack,
		malloc
	};

	constexpr void generic_allocation_bookkeeping(const size_t byte_count, const bool aligned) noexcept {
		if (++total_allocations; total_allocations >= total_deallocations) {
			const auto diff = total_allocations - total_deallocations;
			max_concurrent_allocations = diff > max_concurrent_allocations ? diff : max_concurrent_allocations;
		}

		if (total_bytes_allocated += byte_count; total_bytes_allocated >= total_bytes_deallocated) {
			const auto diff = total_bytes_allocated - total_bytes_deallocated;
			maximum_bytes_in_use = diff > maximum_bytes_in_use ? diff : maximum_bytes_in_use;
		}

		if (aligned) {
			++aligned_allocations;
		}
	}

	constexpr void report_bitblocks_allocation(const size_t byte_count, const bool aligned) noexcept {
		generic_allocation_bookkeeping(byte_count, aligned);

		total_blocks_requested_bytes += byte_count;
		current_blocks_used_bytes += byte_count;
		if (current_blocks_used_bytes > max_blocks_used_bytes) {
			max_blocks_used_bytes = current_blocks_used_bytes;
		}
		const size_t idx = (byte_count >> 4) - 1;
		++blocks_records[idx].uses;
		const size_t current = ++blocks_records[idx].current;
		if (current > blocks_records[idx].max) {
			blocks_records[idx].max = current;
		}
	}
	constexpr void report_stack_allocation(const size_t byte_count, const bool aligned) noexcept {
		generic_allocation_bookkeeping(byte_count, aligned);

		total_stack_requested_bytes += byte_count;
		current_stack_used_bytes += byte_count;
		if (current_stack_used_bytes > max_stack_used_bytes) {
			max_stack_used_bytes = current_stack_used_bytes;
		}
		stack_top_allocation_sizes_and_counts.increment(byte_count);
	}
	constexpr void report_malloc_allocation(const size_t byte_count, const bool aligned) noexcept {
		generic_allocation_bookkeeping(byte_count, aligned);

		malloc_bytes += byte_count;
	}
	
	constexpr void report_bitblocks_allocation_failure(const size_t byte_count) noexcept {
		const size_t idx = (byte_count >> 4) - 1;
		++blocks_records[idx].fails;
	}
	constexpr void report_stack_allocation_failure(const size_t byte_count) noexcept { ++stack_failures; }
	constexpr void report_malloc_allocation_failure(const size_t byte_count) noexcept { ++malloc_failures; }

	constexpr void generic_deallocation_bookkeeping(const size_t byte_count) noexcept {
		++total_deallocations;
		total_bytes_deallocated += byte_count;
	}

	constexpr void report_bitblocks_deallocation(const size_t byte_count) noexcept {
		generic_deallocation_bookkeeping(byte_count);

		current_blocks_used_bytes -= byte_count;
		const size_t idx = (byte_count >> 4) - 1;
		const size_t current = --blocks_records[idx].current;
	}
	constexpr void report_stack_deallocation(const size_t byte_count) noexcept {
		generic_deallocation_bookkeeping(byte_count);

		current_stack_used_bytes -= byte_count;
	}
	constexpr void report_malloc_deallocation(const size_t byte_count) noexcept {
		generic_deallocation_bookkeeping(byte_count);
	}
	constexpr void report_unhandled_deallocation() { ++unhandled_deallocations; }


	void program_finished() noexcept {
		const auto end_point = std::chrono::steady_clock::now();
		const auto microseconds = std::chrono::duration_cast<std::chrono::microseconds>(end_point - start_point).count();

		log::info(""sv);
		log::info("Memory: End of program signalled. Stats:\n"sv);

		log::info("Memory: total allocations = {} (of which aligned = {})"sv, total_allocations, aligned_allocations);
		log::info("Memory: total bytes requested = {}"sv, total_bytes_allocated);
		log::info("Memory: unhandled deallocations = {}\n"sv, unhandled_deallocations);

		log::info("Memory: blocks: requested = {}, max needed = {}\n"sv, total_blocks_requested_bytes, max_blocks_used_bytes);

		const std::array<double, 8> block_counts{ 128.0, 2048.0, 1024.0, 1024.0, 1024.0, 1024.0, 1024.0, 512.0 };

		for (size_t i = 0; i < blocks_records.size(); ++i) {
			const size_t block_size = (i + 1) << 4;
			const size_t uses = blocks_records[i].uses;
			const size_t max = blocks_records[i].max;
			const size_t fails = blocks_records[i].fails;
			const size_t pcnt = static_cast<size_t>((static_cast<double>(max) / block_counts[i]) * 100.0);
			log::info("Memory: {:3} byte blocks: uses = {:4}, max = {:4} ({:2}%), fails = {:4}"sv, block_size, uses, max, pcnt, fails);
		}

		log::info(""sv);
		log::info("Memory: stack: requested = {}, max needed = {}, fails = {}\n"sv, total_stack_requested_bytes, max_stack_used_bytes, stack_failures);

		for (const auto& [amount, count] : stack_top_allocation_sizes_and_counts) {
			if (amount == 0) {
				break;
			}
			log::info("Memory: stack: allocated {:6} bytes {:2} times"sv, amount, count);
		}

		log::info(""sv);
		log::info("Memory: malloc: bytes = {}, failures = {}\n"sv, malloc_bytes, malloc_failures);

		log::info("Total runtime before logger destruction = {}us."sv, microseconds);
	}

}

#endif


namespace diff {
	using std::size_t;


	/*const auto start_point = std::chrono::steady_clock::now();


	constinit size_t total_allocations = 0;
	constinit size_t total_deallocations = 0;
	constinit size_t max_concurrent_allocations = 0;

	constinit size_t total_bytes_allocated = 0;
	constinit size_t total_bytes_deallocated = 0;
	constinit size_t maximum_bytes_in_use = 0;

	constinit size_t spinlock_spins = 0;
	constinit size_t allocation_failures = 0;


	struct byte_amount {
		constexpr explicit byte_amount() noexcept = default;
		constexpr explicit byte_amount(const size_t init) noexcept : val{ init } {}
		constexpr byte_amount& operator=(const size_t num) noexcept { val = num; return *this; }
		constexpr byte_amount& operator-=(const size_t num) noexcept { val -= num; return *this; }
		constexpr auto operator==(const byte_amount& rhs) const noexcept { return this->val == rhs.val; }
		constexpr operator size_t() const noexcept { return val; }
		size_t val{ 0 };
	};
	struct alloc_count {
		constexpr explicit alloc_count() noexcept = default;
		constexpr explicit alloc_count(const size_t init) noexcept : val{ init } {}
		constexpr auto operator<=>(const alloc_count& rhs) const noexcept { return this->val <=> rhs.val; }
		constexpr alloc_count& operator++() noexcept { ++val; return *this; }
		size_t val{ 0 };
	};
	
	class fancy_array {
	private:
		struct simple_pair {
			constexpr void init(const byte_amount init_block_size) noexcept {
				block_size = init_block_size;
				times_allocated = alloc_count{ 1 };
			}
			constexpr void increment() noexcept { ++times_allocated; }
			constexpr bool empty() const noexcept { return block_size == byte_amount{ 0 }; }
			constexpr void swap(simple_pair& rhs) noexcept {
				byte_amount temp1{ this->block_size };
				alloc_count temp2{ this->times_allocated };
				this->block_size = rhs.block_size;
				this->times_allocated = rhs.times_allocated;
				rhs.block_size = temp1;
				rhs.times_allocated = temp2;
			}
			constexpr auto operator==(const byte_amount other_block_size) const noexcept { return block_size == other_block_size; }
			constexpr auto operator<=>(const simple_pair& rhs) const noexcept { return this->times_allocated <=> rhs.times_allocated; }
			byte_amount block_size{ 0 };
			alloc_count times_allocated{ 0 };
		};
		
		std::array<simple_pair, 100> data{};

	public:
		constexpr void increment(const byte_amount block_size) noexcept {
			if (not data.back().empty()) {
				return; // Already holding 100 records.
			}

			const auto begin = data.begin();
			const auto end = data.end();

			auto it = begin;
			for (; it != end; ++it) {
				if (*it == block_size) {
					it->increment();
					break;
				}
				if (it->empty()) {
					it->init(block_size);
					return;
				}
			}

			for (auto previous = it; previous != begin;) {
				--previous;
				if (*it >= *previous) {
					it->swap(*previous);
					--it;
				}
				else {
					return;
				}
			}

		}

		constexpr auto size() const noexcept {
			size_t ret = 0;
			for (const auto& elem : data) {
				ret += not elem.empty();
			}
			return ret;
		}
		constexpr auto begin() const noexcept { return data.begin(); }
		constexpr auto end() const noexcept { return data.end(); }

	};
	
	constinit fancy_array top_allocation_sizes_and_counts{};

	class thread_id_array {
	private:
		struct simple_pair {
			constexpr void init(const std::thread::id init_id) noexcept {
				id = init_id;
				count = 1;
			}
			constexpr void increment() noexcept { ++count; }
			constexpr bool empty() const noexcept { return std::bit_cast<unsigned int>(id) == 0; }
			constexpr void swap(simple_pair& rhs) noexcept {
				unsigned int temp1{ std::bit_cast<unsigned int>(this->id) };
				unsigned int temp2{ this->count };
				this->id = rhs.id;
				this->count = rhs.count;
				rhs.id = std::bit_cast<std::thread::id>(temp1);
				rhs.count = temp2;
			}
			constexpr auto operator==(const std::thread::id other_id) const noexcept { return std::bit_cast<unsigned int>(this->id) == std::bit_cast<unsigned int>(other_id); }
			constexpr auto operator<=>(const simple_pair& rhs) const noexcept { return this->count <=> rhs.count; }

			std::thread::id id{};
			unsigned int count{};
		};

		std::array<simple_pair, 100> data{};

	public:
		constexpr void increment(std::thread::id id) {
			if (not data.back().empty()) {
				return; // Already holding 100 records.
			}

			const auto begin = data.begin();
			const auto end = data.end();

			auto it = begin;
			for (; it != end; ++it) {
				if (*it == id) {
					it->increment();
					break;
				}
				if (it->empty()) {
					it->init(id);
					return;
				}
			}

			for (auto previous = it; previous != begin;) {
				--previous;
				if (*it >= *previous) {
					it->swap(*previous);
					--it;
				}
				else {
					return;
				}
			}

		}

		constexpr auto size() const noexcept {
			size_t ret = 0;
			for (const auto& elem : data) {
				ret += not elem.empty();
			}
			return ret;
		}
		constexpr auto begin() const { return data.begin(); }
		constexpr auto end() const { return data.end(); }

	};

	constinit thread_id_array threads_that_allocated{};

	constinit std::array<size_t, 16> times_reused_chunk{};
	constinit std::array<size_t, 16> times_wasted_deallocation{};

	enum class event {
		allocation,
		deallocation,
		allocation_failure,
	};

	constexpr void report(const event e, const byte_amount bytes) {
		switch (e) {
		case event::allocation: {

			if (++total_allocations; total_allocations >= total_deallocations) {
				const auto diff = total_allocations - total_deallocations;
				max_concurrent_allocations = diff > max_concurrent_allocations ? diff : max_concurrent_allocations;
			}

			if (total_bytes_allocated += bytes; total_bytes_allocated >= total_bytes_deallocated) {
				const auto diff = total_bytes_allocated - total_bytes_deallocated;
				maximum_bytes_in_use = diff > maximum_bytes_in_use ? diff : maximum_bytes_in_use;
			}

			top_allocation_sizes_and_counts.increment(bytes);
			threads_that_allocated.increment(std::this_thread::get_id());

			return;
		}
		case event::deallocation: {
			++total_deallocations;
			total_bytes_deallocated += bytes;
			return;
		}
		case event::allocation_failure: { ++allocation_failures; return; }
		default: return;
		}
	}


	void program_finished() {
		const auto end_point = std::chrono::steady_clock::now();
		const auto microseconds = std::chrono::duration_cast<std::chrono::microseconds>(end_point - start_point).count();

		log::info(""sv);
		log::info("Memory: End of program signalled. Stats:\n"sv);

		log::info("Memory: total allocations = {}"sv, total_allocations);

		log::info("Memory: total bytes requested = {}"sv, total_bytes_allocated);
		const size_t total_used = std::bit_cast<size_t>(global_memory::access(global_memory::access_mode::debug_get_bytes_used, byte_amount{}, {}, nullptr));
		log::info("Memory: total bytes actually used = {}\n"sv, total_used);

		log::info("Memory: allocation failures = {}\n"sv, allocation_failures);

		log::info("Memory: number of different sized allocated chunks = {}\n"sv, top_allocation_sizes_and_counts.size());

		for (const auto& [amount, count] : top_allocation_sizes_and_counts) {
			if (amount == byte_amount{ 0 }) {
				break;
			}
			if (128 >= amount) {
				const size_t idx = (amount >> 3) - 1;
				const size_t reused = times_reused_chunk[idx];
				const size_t wasted = times_wasted_deallocation[idx];
				const size_t pcnt = static_cast<size_t>((static_cast<double>(reused) / static_cast<double>(count.val)) * 100.0);
				log::info("Memory: chunk size {:5}, times {:5}, reused {:5} ({:2}%), wasted {:5}"sv, amount.val, count.val, reused, pcnt, wasted);

				continue;
			}
			log::info("Memory: chunk size {:4}, times {:4}"sv, amount.val, count.val);
		}

		log::info(""sv);
		log::info("Total runtime before logger destruction = {}us."sv, microseconds);

	}

	class global_memory {
	private:
		enum : size_t { minimum_alignment = 8 };
		static_assert(std::has_single_bit(static_cast<size_t>(minimum_alignment)));

		class greedy_memory_resource {
		private:

			class spinlock {
			public:
				void lock() noexcept {
					for (;;) {
						if (is_locked.exchange(true, std::memory_order::acquire) == false) { // acquire is enough, making loads unable to move up.
							return; // Previous value was false. Got the lock.
						}
						do {
							_mm_pause();  // Noop without SSE2 (released in 2000).
						} while (is_locked.load(std::memory_order::relaxed)); // This needn't protect anything, so use relaxed.
					}
				}
				void unlock() noexcept { is_locked.store(false, std::memory_order::release); }
			private:
				std::atomic<bool> is_locked{ false };
				static_assert(decltype(is_locked)::is_always_lock_free, "spinlock flag implementation is not always lock-free, so probably just use a (shared) mutex.");
			};
			static_assert(sizeof(spinlock) == 1);

			class spinlock_guard {
			public:
				explicit spinlock_guard(spinlock& lock_ref) noexcept : lock{ lock_ref } { lock.lock(); }
				~spinlock_guard() noexcept { lock.unlock(); }
			private:
				spinlock& lock;
			};

			template<size_t stack_size> requires((stack_size > 0) and std::has_single_bit(stack_size))
			class ring_stack {
			public:
				constexpr void* push(void* ptr) noexcept {
					++top_idx;
					top_idx &= index_mask;

					void* old_ptr = data[top_idx];
					data[top_idx] = ptr;

					return old_ptr; // For debug purposes
				}
				constexpr void* pop() noexcept {
					// Save old index and do index operations first, before accessing the data, for cache locality.
					const size_t old_top = top_idx;
					--top_idx;
					top_idx &= index_mask;

					void* ptr = data[old_top];
					data[old_top] = nullptr;

					return ptr;
				}
			private:
				enum : size_t { index_mask = stack_size - 1 };
				size_t top_idx{ 1 }; // Start pointing at second element, because first operation will be a pop().
				std::array<void*, stack_size> data{};
			};

		public:

			constexpr greedy_memory_resource(std::byte* block_ptr, const size_t remaining_bytes) noexcept : next_byte{ block_ptr }, remaining{ remaining_bytes } {}
			constexpr ~greedy_memory_resource() noexcept = default;

			constexpr void* allocate(const size_t amount) noexcept {

				const size_t adjusted_amount = round_up_to_multiple_of_x(amount, minimum_alignment);

				spinlock_guard guard{ lock };

				if (128 >= adjusted_amount) {
					void* ptr = nullptr;
					const size_t normalized = (adjusted_amount >> 3) - 1;
					switch (normalized) {
					case 0: { ptr = freelist_8.pop(); break; }		// 8 bytes
					case 1: { ptr = freelist_16.pop(); break; }		// 16 bytes
					case 2: { ptr = freelist_24.pop(); break; }		// 24 bytes
					case 3: { ptr = freelist_32.pop(); break; }		// 32 bytes
					case 4: { ptr = freelist_40.pop(); break; }		// 40 bytes
					case 5: { ptr = freelist_48.pop(); break; }		// 48 bytes
					case 6: { ptr = freelist_56.pop(); break; }		// 56 bytes
					case 7: { ptr = freelist_64.pop(); break; }		// 64 bytes
					case 8: { ptr = freelist_72.pop(); break; }		// 72 bytes
					case 9: { ptr = freelist_80.pop(); break; }		// 80 bytes
					case 10: { ptr = freelist_88.pop(); break; }	// 88 bytes
					case 11: { ptr = freelist_96.pop(); break; }	// 96 bytes
					case 12: { ptr = freelist_104.pop(); break; }	// 104 bytes
					case 13: { ptr = freelist_112.pop(); break; }	// 112 bytes
					case 14: { ptr = freelist_120.pop(); break; }	// 120 bytes
					case 15: { ptr = freelist_128.pop(); break; }	// 128 bytes
					default: { std::unreachable(); }
					}
					if (nullptr != ptr) {
						diag::report_allocation({ .bytes = adjusted_amount, .reused_chunk = true });
						return ptr;
					}
				}

				if (adjusted_amount > remaining) {
					diag::report_allocation_failure({ .bytes = adjusted_amount });
					return nullptr;
				}

				void* ret = next_byte;
				next_byte += adjusted_amount;
				remaining -= adjusted_amount;

				diag::report_allocation({ .bytes = adjusted_amount });
				return ret;
			}

			constexpr void* allocate(const size_t amount, const std::align_val_t alignment) noexcept {

				// Raise alignment to 8 if lower, and round it up to the nearest power of two (if already a power of 2, no changes).
				const size_t adjusted_alignment = std::bit_ceil(std::max<size_t>(static_cast<size_t>(alignment), minimum_alignment));
				const size_t adjusted_amount = round_up_to_multiple_of_x(amount, adjusted_alignment);

				spinlock_guard guard{ lock };

				const size_t increment_to_align_ptr = amount_missing_to_next_multiple_of_x(std::bit_cast<size_t>(next_byte), adjusted_alignment);
				const size_t total_bytes_needed = adjusted_amount + increment_to_align_ptr;

				if (total_bytes_needed > remaining) {
					diag::report_allocation_failure({ .bytes = adjusted_amount });
					return nullptr;
				}

				next_byte += increment_to_align_ptr; // Align current pointer to requested alignment.
				void* ret = next_byte;
				next_byte += adjusted_amount;
				remaining -= total_bytes_needed;

				diag::report_allocation({ .bytes = adjusted_amount, .alignment = alignment });
				return ret;
			}

			constexpr void deallocate(void*) const noexcept { diag::report_deallocation({}); }
			constexpr void deallocate(void* ptr, const size_t amount) noexcept {
				const size_t adjusted_amount = round_up_to_multiple_of_x(amount, minimum_alignment);
				if (128 >= adjusted_amount) {
					void* old_ptr = nullptr;

					const size_t normalized = (adjusted_amount >> 3) - 1;
					switch (normalized) {
					case 0: { old_ptr = freelist_8.push(ptr); break; }		// 8 bytes
					case 1: { old_ptr = freelist_16.push(ptr); break; }		// 16 bytes
					case 2: { old_ptr = freelist_24.push(ptr); break; }		// 24 bytes
					case 3: { old_ptr = freelist_32.push(ptr); break; }		// 32 bytes
					case 4: { old_ptr = freelist_40.push(ptr); break; }		// 40 bytes
					case 5: { old_ptr = freelist_48.push(ptr); break; }		// 48 bytes
					case 6: { old_ptr = freelist_56.push(ptr); break; }		// 56 bytes
					case 7: { old_ptr = freelist_64.push(ptr); break; }		// 64 bytes
					case 8: { old_ptr = freelist_72.push(ptr); break; }		// 72 bytes
					case 9: { old_ptr = freelist_80.push(ptr); break; }		// 80 bytes
					case 10: { old_ptr = freelist_88.push(ptr); break; }	// 88 bytes
					case 11: { old_ptr = freelist_96.push(ptr); break; }	// 96 bytes
					case 12: { old_ptr = freelist_104.push(ptr); break; }	// 104 bytes
					case 13: { old_ptr = freelist_112.push(ptr); break; }	// 112 bytes
					case 14: { old_ptr = freelist_120.push(ptr); break; }	// 120 bytes
					case 15: { old_ptr = freelist_128.push(ptr); break; }	// 128 bytes
					default: { std::unreachable(); }
					}

					diag::report_deallocation({ .bytes = adjusted_amount, .wasted_chunk = (nullptr != old_ptr) });
				}
				else {
					diag::report_deallocation({ .bytes = adjusted_amount });
				}
			}
			constexpr void deallocate(void*, std::align_val_t alignment) const noexcept { diag::report_deallocation({ .alignment = alignment }); }
			constexpr void deallocate(void*, const size_t amount, std::align_val_t alignment) const noexcept { diag::report_deallocation({ .bytes = amount, .alignment = alignment }); }

			constexpr std::byte* get_next() const noexcept { return next_byte; }

		private:
			spinlock lock{};
			std::byte* next_byte;
			size_t remaining;
			ring_stack<128> freelist_8{};
			ring_stack<128> freelist_16{};
			ring_stack<128> freelist_24{};
			ring_stack<256> freelist_32{};
			ring_stack<128> freelist_40{};
			ring_stack<128> freelist_48{};
			ring_stack<128> freelist_56{};
			ring_stack<128> freelist_64{};
			ring_stack<128> freelist_72{};
			ring_stack<128> freelist_80{};
			ring_stack<128> freelist_88{};
			ring_stack<128> freelist_96{};
			ring_stack<128> freelist_104{};
			ring_stack<128> freelist_112{};
			ring_stack<128> freelist_120{};
			ring_stack<128> freelist_128{};

			static constexpr size_t amount_missing_to_next_multiple_of_x(const size_t current, const size_t x) noexcept {
				const size_t x_mask = x - 1;

				size_t addition = current & x_mask;		// Get remainder of division by x (% x).
				addition = (x - addition) & x_mask;		// Get complement of remainder towards x. Mask with x_mask so if addition == 0, it stays == 0 (input already rounded).

				return addition;
			}

			static constexpr size_t round_up_to_multiple_of_x(const size_t amount, const size_t x) noexcept {
				const size_t adjusted_amount = amount + (amount == 0);	// Turn 0 to 1.

				return adjusted_amount + amount_missing_to_next_multiple_of_x(adjusted_amount, x);
			}

		public:
			// Delete copying and moving.
			greedy_memory_resource(const greedy_memory_resource&) = delete;
			greedy_memory_resource(greedy_memory_resource&&) = delete;
			greedy_memory_resource& operator=(const greedy_memory_resource&) = delete;
			greedy_memory_resource& operator=(greedy_memory_resource&&) = delete;
		};

	public:
		global_memory() = delete; // No construction. Static access only.

		enum access_mode {
			unaligned_allocation,
			aligned_allocation,
			unaligned_deallocation_without_size,
			unaligned_deallocation_with_size,
			aligned_deallocation_without_size,
			aligned_deallocation_with_size,

			debug_get_bytes_used
		};

		static void* access(const access_mode mode, const size_t amount, const std::align_val_t alignment, void* ptr) {
			enum : size_t { stack_memory_block_size = size_t{ 1024 } * size_t{ 1024 } };
			// The block could be a member of greedy_memory_resource, but that prevents it from being put in .bss, which blows up binary size by memory_resource_block_size_bytes bytes.
			// Keeping it here as a separate static fixes that.
			static constinit alignas(minimum_alignment) std::byte block[stack_memory_block_size]{};
			static constinit greedy_memory_resource memory{ block, stack_memory_block_size };

			switch (mode) {
			case unaligned_allocation:					{ return memory.allocate(amount); };
			case aligned_allocation:					{ return memory.allocate(amount, alignment); };
			case unaligned_deallocation_without_size:	{ memory.deallocate(ptr); return nullptr; }
			case unaligned_deallocation_with_size:		{ memory.deallocate(ptr, amount); return nullptr; }
			case aligned_deallocation_without_size:		{ memory.deallocate(ptr, alignment); return nullptr; }
			case aligned_deallocation_with_size:		{ memory.deallocate(ptr, amount, alignment); return nullptr; }
			case debug_get_bytes_used:
			{
				const std::byte* next = memory.get_next();
				const size_t total_used = next - block;
				return std::bit_cast<void*>(total_used); // wew
			}
			default:									{ return nullptr; }
			}

		}

	};

	//*/


	class global_memory {
	private:
		enum : size_t {
			minimum_alignment = 16,
			shift_to_normalize = 4,
			//cache_line_size = std::hardware_constructive_interference_size
		};
		static_assert(std::has_single_bit(static_cast<size_t>(minimum_alignment)));

		class spinlock {
		public:
			void lock() noexcept {
				for (;;) {
					if (is_locked.exchange(true, std::memory_order::acquire) == false) { // acquire is enough, making loads unable to move up.
						return; // Previous value was false. Got the lock.
					}
					do {
						_mm_pause();  // Noop without SSE2 (released in 2000).
					} while (is_locked.load(std::memory_order::relaxed)); // This needn't protect anything, so use relaxed.
				}
			}
			void unlock() noexcept { is_locked.store(false, std::memory_order::release); }
		private:
			std::atomic<bool> is_locked{ false };
			static_assert(decltype(is_locked)::is_always_lock_free, "spinlock flag implementation is not always lock-free, so probably just use a (shared) mutex.");
		};
		static_assert(sizeof(spinlock) == 1);

		class spinlock_guard {
		public:
			explicit spinlock_guard(spinlock& lock_ref) noexcept : lock{ lock_ref } { lock.lock(); }
			~spinlock_guard() noexcept { lock.unlock(); }
		private:
			spinlock& lock;
		};

		/*template<size_t stack_size> requires((stack_size > 0) and std::has_single_bit(stack_size))
		class alignas(cache_line_size) ring_stack {
		public:
			constexpr std::byte* push(std::byte* ptr) noexcept {
				++top_idx;
				top_idx &= index_mask;

				std::byte* old_ptr = data[top_idx];
				data[top_idx] = ptr;

				return old_ptr; // For debug purposes
			}
			constexpr std::byte* pop() noexcept {
				// Save old index and do index operations first, before accessing the data, for cache locality.
				const size_t old_top = top_idx;
				--top_idx;
				top_idx &= index_mask;

				std::byte* ptr = data[old_top];
				data[old_top] = nullptr;

				return ptr;
			}
		private:
			enum : size_t { index_mask = stack_size - 1 };
			size_t top_idx{ 1 }; // Start pointing at second element, because first operation will be a pop().
			std::array<std::byte*, stack_size> data{};
		};//*/

		template<size_t po2> requires(std::has_single_bit(po2))
		class multiple_of {
		public:
			constexpr multiple_of(const size_t init) noexcept {
				enum : size_t { po2_mask = po2 - 1 };
				const size_t adjusted_amount = init + (init == 0);	// Turn 0 to 1.
				size_t addition = adjusted_amount & po2_mask;			// Get remainder of division by po2 (% po2).
				addition = (po2 - addition) & po2_mask;					// Get complement of remainder towards po2. Mask with x_mask so if addition == 0, it stays == 0 (input already rounded).
				val = adjusted_amount + addition;
			}
			constexpr multiple_of(const multiple_of&) noexcept = default;
			constexpr multiple_of(multiple_of&&) noexcept = default;
			constexpr multiple_of& operator=(const multiple_of&) noexcept = default;
			constexpr multiple_of& operator=(multiple_of&&) noexcept = default;
			constexpr ~multiple_of() noexcept = default;
			constexpr operator size_t() const noexcept { return val; }
		private:
			size_t val{};
		};

		class fixed_blocks {
		public:
			constexpr std::byte* allocate(const multiple_of<minimum_alignment> byte_count) noexcept {
				std::byte* ptr = nullptr;
				if (byte_count <= 128) {
					const size_t normalized = (byte_count >> shift_to_normalize) - 1;
					switch (normalized) {
					case 0: { ptr = blocks_16.allocate(); break; }
					case 1: { ptr = blocks_32.allocate(); break; }
					case 2: { ptr = blocks_48.allocate(); break; }
					case 3: { ptr = blocks_64.allocate(); break; }
					case 4: { ptr = blocks_80.allocate(); break; }
					case 5: { ptr = blocks_96.allocate(); break; }
					case 6: { ptr = blocks_112.allocate(); break; }
					case 7: { ptr = blocks_128.allocate(); break; }
					default: { std::unreachable(); }
					}
				}
				return ptr;
			}
			constexpr bool deallocate(std::byte* ptr, const multiple_of<minimum_alignment> byte_count) noexcept {
				bool deallocated = false;
				if (byte_count <= 128) {
					const size_t normalized = (byte_count >> shift_to_normalize) - 1;
					switch (normalized) {
					case 0: { deallocated = blocks_16.deallocate(ptr); break; }
					case 1: { deallocated = blocks_32.deallocate(ptr); break; }
					case 2: { deallocated = blocks_48.deallocate(ptr); break; }
					case 3: { deallocated = blocks_64.deallocate(ptr); break; }
					case 4: { deallocated = blocks_80.deallocate(ptr); break; }
					case 5: { deallocated = blocks_96.deallocate(ptr); break; }
					case 6: { deallocated = blocks_112.deallocate(ptr); break; }
					case 7: { deallocated = blocks_128.deallocate(ptr); break; }
					default: { std::unreachable(); }
					}
				}
				return deallocated;
			}

		private:
			template<size_t block_size, size_t block_count> requires(
				(block_size >= minimum_alignment) and
				((block_size % minimum_alignment) == 0) and
				std::has_single_bit(block_count)
			)
			class block_bucket {
			public:
				constexpr std::byte* allocate() noexcept {
					const size_t first_free_idx = already_allocated_flags.find_first_free();
					std::byte* ptr = nullptr;
					if (first_free_idx < block_count) {
						already_allocated_flags.set(first_free_idx);
						ptr = blocks[first_free_idx].storage;
					}
					return ptr;
				}
				constexpr bool deallocate(std::byte* ptr) noexcept {
					const std::uintptr_t ptr_num = std::bit_cast<std::uintptr_t>(ptr);
					const std::uintptr_t storage_num = std::bit_cast<std::uintptr_t>(&blocks[0].storage[0]);
					const std::uintptr_t ptr_idx_in_storage = (ptr_num - storage_num) / block_size;
					const bool in_range = ptr_idx_in_storage < block_count;
					if (in_range) {
						already_allocated_flags.unset(ptr_idx_in_storage);
					}
					return in_range;
				}

			private:
				enum : size_t {
					block_alignment = std::has_single_bit(block_size) ? block_size : minimum_alignment,
					last_valid_ptr_offset = (block_size * block_count) - block_size
				};

				struct bit_array {
					enum : u64 {
						u64s_needed = (block_count >= 64) ? (block_count / 64) : 1,
						shift_for_u64_idx = 6, // Shorthand for /64
						mask_for_bit_idx = 0b11'1111
					};

					constexpr void set(const u64 absolute_bit_idx) noexcept {
						const u64 u64_idx = absolute_bit_idx >> shift_for_u64_idx;
						const u64 bit_idx_in_u64 = absolute_bit_idx & mask_for_bit_idx;
						const u64 bit_mask = u64{ 1 } << bit_idx_in_u64;
						data[u64_idx] |= bit_mask;
					}
					constexpr void unset(const u64 absolute_bit_idx) noexcept {
						const u64 u64_idx = absolute_bit_idx >> shift_for_u64_idx;
						const u64 bit_idx_in_u64 = absolute_bit_idx & mask_for_bit_idx;
						const u64 bit_mask = ~(u64{ 1 } << bit_idx_in_u64);
						data[u64_idx] &= bit_mask;
					}
					constexpr size_t find_first_free() const noexcept {
						for (size_t i = 0; i < u64s_needed; ++i) {
							const size_t first_zero_idx = std::countr_one(data[i]);
							if (first_zero_idx < 64) {
								return first_zero_idx + (i * 64);
							}
						}
						return static_cast<size_t>(-1);
						static_assert(static_cast<size_t>(-1) == std::numeric_limits<size_t>::max());
					}

					u64 data[u64s_needed]{};
				};
				bit_array already_allocated_flags{}; // Aligned to at least minimum_alignment just by being here, so don't specify differently.

				struct block { std::byte storage[block_size]{}; };
				alignas(block_alignment) block blocks[block_count]{};
			};

			block_bucket<16, 128> blocks_16{};
			block_bucket<32, 2048> blocks_32{};
			block_bucket<48, 1024> blocks_48{};
			block_bucket<64, 1024> blocks_64{};
			block_bucket<80, 1024> blocks_80{};
			block_bucket<96, 1024> blocks_96{};
			block_bucket<112, 1024> blocks_112{};
			block_bucket<128, 512> blocks_128{};

		};
		//static_assert(alignof(fixed_blocks) == 128); // This passes as it should, but Intellisense disagrees and thinks it's aligned on 16.

		class stack {
		public:
			constexpr std::byte* allocate(const multiple_of<minimum_alignment> byte_count) noexcept {
				std::byte* ptr = nullptr;
				if (remaining >= byte_count) {
					ptr = next_byte;
					next_byte += byte_count;
					remaining -= byte_count;
				}
				return ptr;
			}
			constexpr std::byte* allocate(const multiple_of<minimum_alignment> byte_count, const std::align_val_t alignment) noexcept {
				// Raise alignment to minimum_alignment if lower, and round it up to the nearest power of two (if already a power of 2, no changes).
				const size_t adjusted_alignment = std::bit_ceil(std::max<size_t>(static_cast<size_t>(alignment), minimum_alignment));
				const size_t bytes_to_skip_to_align = amount_missing_to_next_multiple_of_x(std::bit_cast<size_t>(next_byte), adjusted_alignment);
				const size_t total_bytes_needed = byte_count + bytes_to_skip_to_align;
				std::byte* ptr = nullptr;
				if (remaining >= total_bytes_needed) {
					next_byte += bytes_to_skip_to_align;
					ptr = next_byte;
					next_byte += byte_count;
					remaining -= total_bytes_needed;
				}
				return ptr;
			}
			constexpr bool deallocate(std::byte* ptr, const multiple_of<minimum_alignment> byte_count) noexcept {
				bool deallocated = false;
				const std::uintptr_t ptr_num = std::bit_cast<std::uintptr_t>(ptr);
				const std::uintptr_t storage_num = std::bit_cast<std::uintptr_t>(first_byte);
				const std::uintptr_t offset = ptr_num - storage_num;
				const bool in_range = (ptr_num >= storage_num) bitand (offset < stack_memory_block_size);
				if (in_range) {
					if (ptr + byte_count == next_byte) { // If it was the last allocation, it can be undone.
						next_byte = ptr;
						remaining += byte_count;
					}
					deallocated = true;
				}
				return deallocated;
			}
			// Can't do anything fancy with alignment in deallocation, so no separate method.
		private:
			// MSVC won't accept constinit-ing an object with an 8MB array non-static member (4MB seems to work). Gotta have a raw static array. Probably a bug.
			enum : size_t { stack_memory_block_size = size_t{ 8 } * size_t{ 1024 } * size_t{ 1024 } }; // 8MB
			alignas(minimum_alignment) static constinit std::byte stack_block[stack_memory_block_size];

			size_t remaining{ stack_memory_block_size };
			/*std::byte* next_byte{ stack_block.storage };
			std::byte* first_byte{ stack_block.storage };*/
			std::byte* next_byte{ stack_block };
			std::byte* first_byte{ stack_block };

			static constexpr size_t amount_missing_to_next_multiple_of_x(const size_t current, const size_t x) noexcept {
				const size_t x_mask = x - 1;

				size_t addition = current & x_mask;		// Get remainder of division by x (% x).
				addition = (x - addition) & x_mask;		// Get complement of remainder towards x. Mask with x_mask so if addition == 0, it stays == 0 (input already rounded).

				return addition;
			}
		};

#ifdef DIRDIFFER_ALLOCATION_LOGGING
		static constexpr void report_bitblocks_allocation(const size_t byte_count, const bool aligned = false) noexcept {
			diag::report_bitblocks_allocation(byte_count, aligned);
		}
		static constexpr void report_stack_allocation(const size_t byte_count, const bool aligned = false) noexcept {
			diag::report_stack_allocation(byte_count, aligned);
		}
		static constexpr void report_malloc_allocation(const size_t byte_count, const bool aligned = false) noexcept {
			diag::report_malloc_allocation(byte_count, aligned);
		}

		static constexpr void report_bitblocks_allocation_failure(const size_t byte_count) noexcept {
			diag::report_bitblocks_allocation_failure(byte_count);
		}
		static constexpr void report_stack_allocation_failure(const size_t byte_count) noexcept {
			diag::report_stack_allocation_failure(byte_count);
		}
		static constexpr void report_malloc_allocation_failure(const size_t byte_count) noexcept {
			diag::report_malloc_allocation_failure(byte_count);
		}

		static constexpr void report_bitblocks_deallocation(const size_t byte_count) noexcept {
			diag::report_bitblocks_deallocation(byte_count);
		}
		static constexpr void report_stack_deallocation(const size_t byte_count) noexcept {
			diag::report_stack_deallocation(byte_count);
		}
		static constexpr void report_malloc_deallocation(const size_t byte_count) noexcept {
			diag::report_malloc_deallocation(byte_count);
		}
		static constexpr void report_unhandled_deallocation() noexcept  { diag::report_unhandled_deallocation(); }
#else
		static constexpr void report_bitblocks_allocation(const size_t) noexcept {}
		static constexpr void report_stack_allocation(const size_t) noexcept {}
		static constexpr void report_malloc_allocation(const size_t) noexcept {}

		static constexpr void report_bitblocks_allocation(const size_t, const bool) noexcept {}
		static constexpr void report_stack_allocation(const size_t, const bool) noexcept {}
		static constexpr void report_malloc_allocation(const size_t, const bool) noexcept {}

		static constexpr void report_bitblocks_allocation_failure(const size_t) noexcept {}
		static constexpr void report_stack_allocation_failure(const size_t) noexcept {}
		static constexpr void report_malloc_allocation_failure(const size_t) noexcept {}

		static constexpr void report_bitblocks_deallocation(const size_t byte_count) noexcept {}
		static constexpr void report_stack_deallocation(const size_t byte_count) noexcept {}
		static constexpr void report_malloc_deallocation(const size_t byte_count) noexcept {}
		static constexpr void report_unhandled_deallocation() noexcept {}
#endif

	public:
		static constexpr std::byte* allocate(const size_t byte_count) noexcept {
			{
				const multiple_of<minimum_alignment> adjusted_amount{ byte_count }; // Rounds up if needed.

				spinlock_guard guard{ lock };

				if (std::byte* ptr = primary_bitblocks.allocate(adjusted_amount); nullptr != ptr) {
					report_bitblocks_allocation(adjusted_amount);
					return ptr;
				}
				if (adjusted_amount <= 128) { // Only a failure if it could fit in a bucket to begin with.
					report_bitblocks_allocation_failure(adjusted_amount);
				}

				if (std::byte* ptr = fallback_stack.allocate(adjusted_amount); nullptr != ptr) {
					report_stack_allocation(adjusted_amount);
					return ptr;
				}
				report_stack_allocation_failure(adjusted_amount);
			}

			if (std::byte* ptr = static_cast<std::byte*>(std::malloc(byte_count)); nullptr != ptr) {
				report_malloc_allocation(byte_count);
				return ptr;
			}
			report_malloc_allocation_failure(byte_count);
			
			return nullptr;
		}
		static constexpr std::byte* allocate(const size_t byte_count, const std::align_val_t alignment) noexcept {
			{
				const multiple_of<minimum_alignment> adjusted_amount{ byte_count }; // Rounds up if needed.

				spinlock_guard guard{ lock };

				if (std::byte* ptr = primary_bitblocks.allocate(adjusted_amount); nullptr != ptr) {
					report_bitblocks_allocation(adjusted_amount, true);
					return ptr;
				}
				report_bitblocks_allocation_failure(adjusted_amount);

				const std::align_val_t adjusted_alignment = static_cast<std::align_val_t>(static_cast<size_t>(alignment) < minimum_alignment ? minimum_alignment : static_cast<size_t>(alignment));
				if (std::byte* ptr = fallback_stack.allocate(adjusted_amount, adjusted_alignment); nullptr != ptr) {
					report_stack_allocation(adjusted_amount, true);
					return ptr;
				}
				report_stack_allocation_failure(adjusted_amount);
			}

			if (std::byte* ptr = static_cast<std::byte*>(_aligned_malloc(byte_count, static_cast<size_t>(alignment))); nullptr != ptr) {
				report_malloc_allocation(byte_count, true);
				return ptr;
			}
			report_malloc_allocation_failure(byte_count);

			return nullptr;
		}

		static constexpr void deallocate(std::byte*) noexcept { report_unhandled_deallocation(); }
		static constexpr void deallocate(std::byte* ptr, const size_t byte_count) noexcept {
			{
				const multiple_of<minimum_alignment> adjusted_amount{ byte_count }; // Rounds up if needed.

				spinlock_guard guard{ lock };

				if (primary_bitblocks.deallocate(ptr, adjusted_amount)) {
					report_bitblocks_deallocation(adjusted_amount);
					return;
				}

				if (fallback_stack.deallocate(ptr, adjusted_amount)) {
					report_stack_deallocation(adjusted_amount);
					return;
				}
			}
			std::free(ptr);
			report_malloc_deallocation(byte_count);
		}
		static constexpr void deallocate(std::byte*, std::align_val_t) noexcept { report_unhandled_deallocation(); }
		static constexpr void deallocate(std::byte* ptr, const size_t byte_count, std::align_val_t alignment) noexcept {
			{
				const multiple_of<minimum_alignment> adjusted_amount{ byte_count }; // Rounds up if needed.

				spinlock_guard guard{ lock };

				if (primary_bitblocks.deallocate(ptr, adjusted_amount)) {
					report_bitblocks_deallocation(adjusted_amount);
					return;
				}

				if (fallback_stack.deallocate(ptr, adjusted_amount)) {
					report_stack_deallocation(adjusted_amount);
					return;
				}
			}
			_aligned_free(ptr);
			report_malloc_deallocation(byte_count);
		}

	private:
		static constinit spinlock lock;

		static constinit fixed_blocks primary_bitblocks;
		static constinit stack fallback_stack;

	};

	constinit global_memory::spinlock global_memory::lock{};

	constinit global_memory::fixed_blocks global_memory::primary_bitblocks{};
	//constinit global_memory::stack::memory_block global_memory::stack::stack_block{};
	alignas(global_memory::minimum_alignment) constinit std::byte global_memory::stack::stack_block[stack_memory_block_size]{};
	constinit global_memory::stack global_memory::fallback_stack{};

}



// Unaligned

void* operator new(size_t byte_count) {
	for (;;) {

		void* block = diff::global_memory::allocate(byte_count);

		if (block != nullptr) {
			return block;
		}

		if (auto new_handler = std::get_new_handler(); new_handler != nullptr) {
			new_handler();
		}
		else {
			throw std::bad_alloc{};
		}

	}
}

void operator delete(void* ptr) noexcept { diff::global_memory::deallocate(static_cast<std::byte*>(ptr)); }
void operator delete(void* ptr, size_t byte_count) noexcept { diff::global_memory::deallocate(static_cast<std::byte*>(ptr), byte_count); }


// Aligned

void* operator new(size_t byte_count, std::align_val_t alignment) {
	for (;;) {

		void* block = diff::global_memory::allocate(byte_count, alignment);

		if (nullptr != block) {
			return block;
		}

		if (auto new_handler = std::get_new_handler(); new_handler != nullptr) {
			new_handler();
		}
		else {
			throw std::bad_alloc{};
		}

	}
}

void operator delete(void* ptr, std::align_val_t alignment) noexcept { diff::global_memory::deallocate(static_cast<std::byte*>(ptr), alignment); }
void operator delete(void* ptr, size_t byte_count, std::align_val_t alignment) noexcept { diff::global_memory::deallocate(static_cast<std::byte*>(ptr), byte_count, alignment); }



