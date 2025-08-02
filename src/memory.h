#pragma once
//#include <cstddef>

//#define DIRDIFFER_ALLOCATION_LOGGING

#ifdef DIRDIFFER_ALLOCATION_LOGGING

namespace diff::diag {
	void program_finished() noexcept;
}

#endif


namespace diff {

	/*
	class mem_interface {
	public:
		mem_interface() = delete;

	private:

		static void* do_allocate(const std::size_t byte_count);
		static void do_deallocate(void* ptr, const std::size_t byte_count);

		static void* do_allocate_aligned(const std::size_t byte_count, const std::size_t alignment);
		static void do_deallocate_aligned(void* ptr, const std::size_t byte_count);

		template<typename T>
		friend class allocator;
	};

	
	template<typename T>
	class allocator {
	public:
		
		// Needed typedefs.
		using value_type = T;
		using size_type = std::size_t;
		using difference_type = std::ptrdiff_t;
		using propagate_on_container_move_assignment = std::true_type;

		constexpr allocator() noexcept {}
		constexpr allocator(const allocator&) noexcept = default;
		constexpr allocator& operator=(const allocator&) noexcept = default;
		template <class U>
		constexpr allocator(const allocator<U>&) noexcept {}
		constexpr ~allocator() noexcept = default;

		T* allocate(const size_type t_count) noexcept {
			if constexpr (alignof(T) > alignof(std::max_align_t)) {
				return static_cast<T*>(mem_interface::do_allocate_aligned(t_count * sizeof(T), alignof(T)));
			}
			else {
				return static_cast<T*>(mem_interface::do_allocate(t_count * sizeof(T)));
			}
		}

		void deallocate(T* ptr, [[maybe_unused]] const size_type t_count) noexcept {
			if constexpr (alignof(T) > alignof(std::max_align_t)) {
				mem_interface::do_deallocate_aligned(ptr, t_count * sizeof(T));
			}
			else {
				mem_interface::do_deallocate(ptr, t_count * sizeof(T));
			}
		}
		
	};
	//*/

}