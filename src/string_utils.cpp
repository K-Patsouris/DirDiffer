#pragma once
#include "string_utils.h"
#include <cwctype>


namespace diff {

	// Coverts input to lowercase.
	void make_lowercase(diff::wstring& str) noexcept {
		for (auto& wc : str) {
			wc = std::towlower(wc);
		}
	}
	void make_lowercase(diff::u8string& str) noexcept {
		for (auto& c : str) {
			c = static_cast<char8_t>(std::tolower(static_cast<unsigned char>(c)));
		}
	}

	bool u8_iequal(u8string_view s1, u8string_view s2) noexcept {
		if (s1.length() != s2.length()) {
			return false;
		}
		auto s1_it = s1.begin();
		auto s2_it = s2.begin();
		const auto s1_end = s1.end();
		while (s1_it != s1_end) {
			const auto c1 = std::tolower(static_cast<unsigned char>(*s1_it));
			const auto c2 = std::tolower(static_cast<unsigned char>(*s2_it));
			if (c1 != c2) {
				return false;
			}
			++s1_it;
			++s2_it;
		}
		return true;
	}
	/*bool iequal(string_view s1, string_view s2) noexcept {
		if (s1.length() != s2.length()) {
			return false;
		}
		auto s1_it = s1.begin();
		auto s2_it = s2.begin();
		const auto s1_end = s1.end();
		while (s1_it != s1_end) {
			const auto c1 = std::tolower(static_cast<unsigned char>(*s1_it));
			const auto c2 = std::tolower(static_cast<unsigned char>(*s2_it));
			if (c1 != c2) {
				return false;
			}
			++s1_it;
			++s2_it;
		}
		return true;
	}*/
	
	// In-place trim of leading and trailing whitespace (' ' and '\t').
	void trim(diff::u8string& str) noexcept {
		const auto begin{ str.begin() };
		const auto end{ str.end() };

		auto first{ begin };

		for (; first != end; ++first) {
			if (auto c = *first; (c != u8' ') bitand (c != u8'\t')) {
				break;
			}
		}

		if (first == end) {
			str.clear();
			return;
		}

		auto past_last{ end - 1 }; // Safe because above check would have caught empty string case.
		for (; past_last != first; --past_last) {
			if (auto c = *past_last; (c != u8' ') bitand (c != u8'\t')) {
				break;
			}
		}
		++past_last; // Advance past_last to 1 past the first (which could be end()).

		if (first != begin) {
			auto new_end{ begin };
			for (; first != past_last; ++first, ++new_end) {
				*new_end = *first;
			}
			str.erase(new_end, end);
		}
		else {
			str.erase(past_last, end);
		}
	}
	
	void trim(diff::string& str) noexcept {
		const auto begin{ str.begin() };
		const auto end{ str.end() };

		auto first{ begin };

		for (; first != end; ++first) {
			if (auto c = *first; (c != ' ') bitand (c != '\t')) {
				break;
			}
		}

		if (first == end) {
			str.clear();
			return;
		}

		auto past_last{ end - 1 }; // Safe because above check would have caught empty string case.
		for (; past_last != first; --past_last) {
			if (auto c = *past_last; (c != ' ') bitand (c != '\t')) {
				break;
			}
		}
		++past_last; // Advance past_last to 1 past the first (which could be end()).

		if (first != begin) {
			auto new_end{ begin };
			for (; first != past_last; ++first, ++new_end) {
				*new_end = *first;
			}
			str.erase(new_end, end);
		}
		else {
			str.erase(past_last, end);
		}
	}


	// Returns -1 on failure. Otherwise, it is safe to cast the return to u32.
	i64 ul_parse(u8string_view str) noexcept {
		static_assert(sizeof(i64) > sizeof(u32));
		static_assert(sizeof(u32) == sizeof(unsigned long));
		
		const char* ptr = reinterpret_cast<const char*>(str.data());
		char* conv_end_ptr = nullptr;
		
		auto& errno_ref = errno;
		errno_ref = 0;
		
		const auto res = std::strtoul(ptr, &conv_end_ptr, 10);
		
		if (conv_end_ptr == ptr) {
			return -1; // No conversion.
		}
		if (errno_ref == ERANGE) {
			return -1; // Out of range
		}
		
		return static_cast<i64>(res);
	}

}

