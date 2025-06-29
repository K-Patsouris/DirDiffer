#pragma once
#include "string_utils.h"
#include <cwctype>


namespace diff {

	// Coverts input to lowercase.
	void make_lowercase(wstring& str) noexcept {
		for (auto& wc : str) {
			wc = std::towlower(wc);
		}
	}
	void make_lowercase(u8string& str) noexcept {
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
	void trim(u8string& str) noexcept {
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
	
	void trim(string& str) noexcept {
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

	bool valid_folder_name(wstring_view name) noexcept {
		if (name.empty()) {
			return false;
		}
		if (std::iswspace(name.front()) bitor std::iswspace(name.back())) {
			return false;
		}
		/*
			\ / : * ? " < > |
		*/
		for (const auto c : name) {
			const bool back = (c == L'\\');
			const bool fore = (c == L'/');
			const bool colon = (c == L':');
			const bool star = (c == L'*');
			const bool qst = (c == L'?');
			const bool quote = (c == L'\"');
			const bool lt = (c == L'<');
			const bool gt = (c == L'>');
			const bool pipe = (c == L'|');
			if (back bitor fore bitor colon bitor star bitor qst bitor quote bitor lt bitor gt bitor pipe bitor !std::iswprint(c)) {
				return false;
			}
		}
		return true;
	}

	bool valid_directory(wstring_view dir) {
		if (dir.length() < 4) {
			return false; // <C:\a> is 4, <<\\a\b> is 5, so we need at least 4.
		}

		static auto valid_drive_letter = [] (const wchar_t c) {
			switch (c) {
			case L'A':
			case L'B':
			case L'C':
			case L'D':
			case L'E':
			case L'F':
			case L'G':
			case L'H':
			case L'I':
			case L'J':
			case L'K':
			case L'L':
			case L'M':
			case L'N':
			case L'O':
			case L'P':
			case L'Q':
			case L'R':
			case L'S':
			case L'T':
			case L'U':
			case L'V':
			case L'W':
			case L'X':
			case L'Y':
			case L'Z':
			case L'a':
			case L'b':
			case L'c':
			case L'd':
			case L'e':
			case L'f':
			case L'g':
			case L'h':
			case L'i':
			case L'j':
			case L'k':
			case L'l':
			case L'm':
			case L'n':
			case L'o':
			case L'p':
			case L'q':
			case L'r':
			case L's':
			case L't':
			case L'u':
			case L'v':
			case L'w':
			case L'x':
			case L'y':
			case L'z':
			{
				return true;
			}
			default:
			{
				return false;
			}
			}
		};

		std::vector<wstring> folder_names{};

		if (dir[0] == L'\\') { // UNC
			if (dir[1] != L'\\') {
				return false; // Must start with \\ .
			}
			folder_names = split<wstring>({ dir.begin() + 2, dir.end() }, L'\\');
			if (folder_names.size() < 2) {
				return false; // First after \\ is host name, so there must be at least one more thing after to be a valid dir.
			}
		}
		else if (valid_drive_letter(dir[0])) {
			if ((dir[1] != L':') bitor (dir[2] != L'\\')) {
				return false;
			}
			folder_names = split<wstring>({ dir.begin() + 3, dir.end() }, L'\\');
		}
		else {
			return false;
		}

		for (const auto& name : folder_names) {
			if (!valid_folder_name(name)) {
				return false;
			}
		}

		return true;
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

