#pragma once
#include <string>
#include <string_view>
//#include "memory.h"

namespace diff {
	
	/*template<typename T>
	using custom_string = std::basic_string<T, std::char_traits<T>, allocator<T>>;
	
	using string = custom_string<char>;
	using wstring = custom_string<wchar_t>;
	using u8string = custom_string<char8_t>;*/

	using string = std::string;
	using wstring = std::wstring;
	using u8string = std::u8string;
	
	using std::string_view;
	using std::wstring_view;
	using std::u8string_view;
	
	using namespace std::literals;
	
}