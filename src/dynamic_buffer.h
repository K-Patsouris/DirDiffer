#pragma once
#include "int_defs.h"
#include "string_defs.h"
#include <cstdlib>	// std::malloc, std::free, std::realloc, std::memcpy
#include <bit>		// std::bit_ceil for calculating growing size
#include <fstream>	// std::ifstream for reading from stream

namespace diff {

	// Gotta put this up here because concepts can't be defined in class scopes, because of non-type template parameter ambiguities.
	template<typename S>
	concept string_type = requires(S s, typename S::size_type z) {
		typename S::value_type;
		{ s.length() } noexcept -> std::same_as<typename S::size_type>;
		{ s.data() } noexcept -> std::same_as<typename S::value_type*>;
		{ s.clear() } noexcept -> std::same_as<void>;
		{ s.resize(z) } -> std::same_as<void>;
	};

	class dynamic_buffer {
	private:
		using buf_t = unsigned char;
		static_assert(sizeof(buf_t) == 1, "dynamic_buffer: sizeof(unsigned char) must be 1 or things might get weird.");
		using size_t = std::size_t;

	public:
		explicit constexpr dynamic_buffer() noexcept = default;
		constexpr dynamic_buffer(dynamic_buffer&& rhs) noexcept :
			mem(rhs.mem),
			off(rhs.off) {
			static_assert(sizeof(dynamic_buffer) == (sizeof(buf_t*) + sizeof(offsets)), "dynamic_buffer size changed. Update non-defaulted move operations!");
			// I don't want to include a header just for std::exchange().
			rhs.mem = nullptr;
			rhs.off.zero();
		}
		constexpr dynamic_buffer& operator=(dynamic_buffer&& rhs) noexcept {
			static_assert(sizeof(dynamic_buffer) == (sizeof(buf_t*) + sizeof(offsets)), "dynamic_buffer size changed. Update non-defaulted move operations!");
			if (&rhs != this) {
				free_self();
				this->mem = rhs.mem;
				this->off = rhs.off;
				rhs.mem = nullptr;
				rhs.off.zero();
			}
			return *this;
		}
		~dynamic_buffer() noexcept { free_self(); }
		dynamic_buffer(const dynamic_buffer&) = delete;				// Would have to throw on alloc failure, and I don't want to throw.
		dynamic_buffer& operator=(const dynamic_buffer&) = delete;	// Similar. Can't force success check on caller, unless with some bool return garbage.
		explicit dynamic_buffer(std::ifstream& ifs, const std::size_t count) noexcept {
			if (not expand_to(calc_regular_size(off.pos + count))) {
				return;
			}
			try {
				if (not ifs.read(reinterpret_cast<char*>(mem + off.pos), count)) {
					return;
				}
				else {
					off.pos += count;
					off.len = off.pos > off.len ? off.pos : off.len;
					return;
				}
			}
			catch (...) {
				return;
			}
		}
			

		// Makes sure buffer is at least byte_count bytes long.
		// Reallocates if already owning memory.
		// position() and length() return the same pre and post.
		// Does nothing if byte_count <= capacity().
		// byte_count == 0 and capacity() == 0 is considered a failure.
		// Returns true on success and false on failure. If failed, object state remains unchanged.
		[[nodiscard]] bool reserve(const size_t byte_count) noexcept { return expand_to(byte_count); }

		// Equivalent to reserve(capacity() + byte_count).
		[[nodiscard]] bool expand_by(const size_t byte_count) noexcept { return expand_to(off.cap + byte_count); }

		// Equivalent to reserve(length() + byte_count).
		[[nodiscard]] bool expand_for_extra(const size_t byte_count) noexcept { return expand_to(off.len + byte_count); }

		// Copies byte_count bytes to dst from current cursor position, and adjusts cursor position accordingly.
		// Fails if length() == 0, even if byte_count == 0.
		// Fails if byte_count > (length() - position()).
		// Returns true on success and false on failure. If failed, object state remains unchanged.
		[[nodiscard]] bool read(void* dst, const size_t byte_count) const noexcept {
			if (byte_count == 0) {
				return off.len != 0;
			}
			else {
				if (byte_count <= (off.len - off.pos)) {
					std::memcpy(dst, mem + off.pos, byte_count);
					off.pos += byte_count;
					return true;
				}
				else {
					return false;
				}
			}
		}

		// Copies byte_count bytes from src to current cursor position, and adjusts cursor position and length accordingly.
		// Fails if byte_count == 0 and capacity() == 0.
		// Fails if byte_count > (capacity() - position()) and expansion fails.
		// Returns true on success and false on failure. If failed, object state remains unchanged.
		[[nodiscard]] bool write(const void* src, const size_t byte_count) noexcept {
			if (byte_count == 0) {
				return off.cap != 0;
			}
			else {
				if ((byte_count <= (off.cap - off.pos)) or (expand_to(calc_regular_size(off.pos + byte_count)))) {
					std::memcpy(mem + off.pos, src, byte_count);
					off.pos += byte_count;
					off.len = off.pos > off.len ? off.pos : off.len;
					return true;
				}
				else {
					return false;
				}
			}
		}


		// Trivial read/write helpers.
		template<typename T> requires (std::is_trivially_copyable_v<T> and not std::is_pointer_v<T>)
		[[nodiscard]] bool read(T& out) const noexcept {
			return read(std::addressof(out), sizeof(T));
		}

		template<typename T> requires (std::is_trivially_copyable_v<T> and not std::is_pointer_v<T>)
		[[nodiscard]] bool write(const T& val) noexcept {
			return write(std::addressof(val), sizeof(T));
		}


		// String read/write helpers.
		template<string_type S>
		[[nodiscard]] bool read(S& out) const noexcept {
			offsets old_offsets{ off };

			u64 length = 0;
			if (!read(length)) {
				off = old_offsets;
				return false;
			}

			try {
				out.resize(length);
			}
			catch (...) {
				off = old_offsets;
				out.clear();
				return false;
			}

			if (length == 0) {
				return true;
			}

			return read(out.data(), length * sizeof(S::value_type));
		}

		template<string_type S>
		[[nodiscard]] bool write(const S& val) noexcept {
			const size_t string_bytes = (val.length() * sizeof(S::value_type));
			const size_t total_bytes_needed = 8 + string_bytes; // Extra 8 for storing length.
			if (not expand_to(calc_regular_size(off.pos + total_bytes_needed))) {
				return false;
			}
			(void)write(static_cast<u64>(val.length()));
			(void)write(val.data(), string_bytes);
			return true;
		}


		// Resets cursor position to the start.
		constexpr void rewind() const noexcept { off.pos = 0; }

		// Positions cursor at new_pos.
		// Fails if new_pos > length() or if capacity() == 0.
		// Returns true on success and false on failure. If failed, object state remains unchanged.
		[[nodiscard]] constexpr bool reposition(const size_t new_pos) const noexcept {
			if ((new_pos > off.len) bitor (off.cap == 0)) {
				return false;
			}
			else {
				off.pos = new_pos;
				return true;
			}
		}

		[[nodiscard]] constexpr buf_t* begin() const noexcept { return mem; }
		[[nodiscard]] constexpr buf_t* end() const noexcept { return mem + off.len; }

		// Returns the current position of the cursor.
		[[nodiscard]] constexpr size_t position() const noexcept { return off.pos; }

		// Returns the count of bytes that have been written in the buffer.
		[[nodiscard]] constexpr size_t length() const noexcept { return off.len; }

		// Returns the count of bytes currently allocated.
		[[nodiscard]] constexpr size_t capacity() const noexcept { return off.cap; }

		// Returns a pointer to the start of the buffer.
		[[nodiscard]] constexpr const buf_t* data() const noexcept { return mem; }

		// Frees buffer, if allocated, and resets object to default-constructed state.
		void reset() noexcept { free_self(); }


	private:
		struct offsets {
			constexpr offsets() noexcept = default;
			constexpr offsets(const offsets&) noexcept = default;
			constexpr offsets(offsets&&) noexcept = default;
			constexpr offsets& operator=(const offsets&) noexcept = default;
			constexpr offsets& operator=(offsets&&) noexcept = default;
			constexpr ~offsets() noexcept = default;

			constexpr void zero() noexcept {
				pos = 0;
				len = 0;
				cap = 0;
			}

			size_t pos{ 0 };
			size_t len{ 0 };
			size_t cap{ 0 };
		};

		buf_t* mem{ nullptr };
		mutable offsets off{};

		void free_self() noexcept {
			if (mem != nullptr) {
				std::free(mem);
				mem = nullptr;
				off.zero();
			}
		}

		// Expands buffer to hold exactly to_size bytes, be it by allocating from null, or reallocating from existing allocation.
		// Does nothing if to_size <= cap. to_size == 0 and cap == 0 is considered a failure.
		// Returns true on success and false on failure. If failed, object state remains unchanged.
		[[nodiscard]] bool expand_to(const size_t to_size) noexcept {
			if (to_size <= off.cap) {
				return off.cap != 0;
			}
			else {
				void* new_block{ nullptr };
				if (mem != nullptr) { // Already had memory. Must reallocate.
					new_block = std::realloc(mem, to_size);
				}
				else { // Had no memory. Must allocate.
					new_block = std::malloc(to_size);
				}
				if (new_block != nullptr) {
					mem = static_cast<buf_t*>(new_block);
					off.cap = to_size;
					return true;
				}
				else {
					return false;
				}
			}
		}

		// Implements a sort of x1.5 step behavior, because x2 is kind of steep.
		// Returns three quarters of the way to the next power of 2, if that is >= raw_size.
		// Returns the next power of 2 otherwise.
		static size_t calc_regular_size(const size_t raw_size) noexcept {
			const size_t ceil = std::bit_ceil(raw_size);
			const size_t three_quarters = (ceil >> size_t{ 1 }) bitor (ceil >> size_t{ 2 });
			return three_quarters >= raw_size ? three_quarters : ceil;
		}

	};

}
