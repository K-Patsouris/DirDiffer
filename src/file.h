#pragma once
#include "int_defs.h"
#include "string_defs.h"
#include "lowercase_path.h"
#include <chrono>
#include <filesystem>

namespace diff {

	struct file {
	public:
		explicit constexpr file() noexcept = default;
		constexpr file(const file&) = default;
		constexpr file(file&&) noexcept = default;
		constexpr file& operator=(const file&) = default;
		constexpr file& operator=(file&&) noexcept = default;
		constexpr ~file() noexcept = default;
		
		
		struct owner_name {
		public:
			explicit constexpr owner_name() noexcept = default;
			constexpr owner_name(const owner_name&) = default;
			constexpr owner_name(owner_name&&) noexcept = default;
			constexpr owner_name& operator=(const owner_name&) = default;
			constexpr owner_name& operator=(owner_name&&) noexcept = default;
			constexpr ~owner_name() noexcept = default;
			
			explicit constexpr owner_name(u8string&& name) noexcept : val{ std::move(name) } {}
			
			[[nodiscard]] constexpr bool operator==(const owner_name& rhs) const noexcept {
				return this->val == rhs.val;
			}
			
			u8string val{};
		};
		
		
		explicit constexpr file(
			std::filesystem::path&& original_path_a,
			lowercase_path&& parent_a,
			lowercase_path&& filename_a,
			owner_name&& owner_a,
			u64 size_in_bytes_a,
			std::chrono::seconds last_write_a
		) noexcept :
			original_path{ std::move(original_path_a) },
			parent{ std::move(parent_a) },
			filename{ std::move(filename_a) },
			owner{ std::move(owner_a) },
			size_in_bytes{ size_in_bytes_a },
			last_write{ last_write_a }
		{}
		
		
		[[nodiscard]] constexpr bool operator==(const file& rhs) const noexcept {
			return (this->parent == rhs.parent) and (this->filename == rhs.filename);
		}
		
		[[nodiscard]] constexpr auto operator<=>(const file& rhs) const noexcept {
			const auto parent_cmp = this->parent <=> rhs.parent;
			return parent_cmp != 0 ? parent_cmp : this->filename <=> rhs.filename;
		}
		
		
		
		std::filesystem::path original_path{};	// Relative to root.
		lowercase_path parent{};				// Relative to root.
		lowercase_path filename{};
		owner_name owner{};
		u64 size_in_bytes{ 0 };
		std::chrono::seconds last_write{ 0 };
		
	};

}