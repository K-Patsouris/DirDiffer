#pragma once
#include "string_defs.h"
#include "string_utils.h"	// make_lowercase
#include <filesystem>

namespace diff {

	class lowercase_path {
	public:
		explicit constexpr lowercase_path() noexcept = default;
		constexpr lowercase_path(const lowercase_path&) = default;
		constexpr lowercase_path(lowercase_path&&) noexcept = default;
		constexpr lowercase_path& operator=(const lowercase_path&) = default;
		constexpr lowercase_path& operator=(lowercase_path&&) noexcept = default;
		constexpr ~lowercase_path() noexcept = default;
		
		explicit lowercase_path(const std::filesystem::path& any_path) : val{ any_path.u8string() } {
			make_lowercase(val);
		}
		explicit lowercase_path(u8string&& any_path) noexcept : val{ any_path } {
			make_lowercase(val);
		}
		
		[[nodiscard]] constexpr bool operator==(const lowercase_path& rhs) const noexcept {
			return this->val == rhs.val;
		}
		[[nodiscard]] constexpr auto operator<=>(const lowercase_path& rhs) const noexcept {
			return this->val <=> rhs.val;
		}
		
		
		[[nodiscard]] constexpr bool operator==(const u8string& rhs) const noexcept {
			return this->val == rhs;
		}
		[[nodiscard]] constexpr auto operator<=>(const u8string& rhs) const noexcept {
			return this->val <=> rhs;
		}
		
		
		[[nodiscard]] const u8string& str_cref() const noexcept { return val; }
		
	private:
		u8string val{};
		
		// Only accessible by serializer, for efficient deserialization.
		struct already_lowercase_tag {};
		explicit constexpr lowercase_path(already_lowercase_tag, u8string&& already_lowercase_path) noexcept : val{ std::move(already_lowercase_path) } {}
		
		friend class serialization;
	};
		
}
