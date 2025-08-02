#pragma once
#include "int_defs.h"
#include "string_defs.h"
#include "vector_defs.h"
#include <optional>


namespace diff {
	
	// Coverts input to lowercase.
	void make_lowercase(diff::wstring& str) noexcept;
	void make_lowercase(diff::u8string& str) noexcept;
	
	bool u8_iequal(u8string_view s1, u8string_view s2) noexcept;
	// bool iequal(string_view s1, string_view s2) noexcept;

	template<typename T>
	constexpr diff::vector<T> split(const T& str, const typename T::value_type delim) {
		if (str.empty()) {
			return {};
		}
		
		//logger.log(L"Split called with <{}>"sv, str);

		diff::vector<T> ret{};

		const size_t max_idx = str.length() - 1;
		size_t start = 0;
		
		while (true) {
			auto delim_idx = str.find(delim, start);
			//wstring part{ str.substr(start, delim_idx - start) };
			//logger.log(L"Appending <{}>"sv, part);
			ret.emplace_back(str.substr(start, delim_idx - start));
			if (delim_idx >= max_idx) {
				//logger.log(L"Delim <{}> triggers break."sv, delim_idx);
				if (delim_idx == max_idx) {
					ret.emplace_back();
				}
				break;
			}
			start = delim_idx + 1;
		}
		
		return ret;
	}
	
	
	// In-place trim of leading and trailing whitespace (' ' and '\t').
	//void trim(wstring& str) noexcept;
	void trim(diff::u8string& str) noexcept;
	void trim(diff::string& str) noexcept;


	// Returns -1 on failure. Otherwise, it is safe to cast the return to u32.
	i64 ul_parse(u8string_view str) noexcept;
	
}

