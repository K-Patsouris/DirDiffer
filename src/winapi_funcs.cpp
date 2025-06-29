#include "winapi_funcs.h"
#include "logger.h"
#include "aclapi.h" // GetNamedSecurityInfoW, LookupSecurityDescriptorPartsW
#include "stringapiset.h" // WideCharToMultiByte
#include "errhandlingapi.h" // GetLastError

namespace diff::winapi {
	
	
	wstring error_string(DWORD error_code) {
		LPWSTR buf{};
		if (0 == FormatMessageW(
			FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
			nullptr,
			error_code,
			MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
			(LPWSTR)&buf,
			0,
			nullptr
		)) {
			return L"error_string "s + std::to_wstring(error_code) + L" -> " + std::to_wstring(GetLastError());
		}
		else {
			wstring ret{ buf };
			LocalFree(buf);
			ret += L'(';
			ret += std::to_wstring(error_code);
			ret += L')';
			return ret;
		}
	}

	std::optional<u8string> wstring_to_utf8(const wstring& wstr) noexcept {
		/*
		int WideCharToMultiByte(
		  [in]            UINT                               CodePage,
		  [in]            DWORD                              dwFlags,
		  [in]            _In_NLS_string_(cchWideChar)LPCWCH lpWideCharStr,
		  [in]            int                                cchWideChar,
		  [out, optional] LPSTR                              lpMultiByteStr,
		  [in]            int                                cbMultiByte,
		  [in, optional]  LPCCH                              lpDefaultChar,
		  [out, optional] LPBOOL                             lpUsedDefaultChar
		);
		*/
		
		if (wstr.empty()) {
			return u8string{};
		}
		
		auto last_error_string = []() {
			switch(GetLastError()) {
			case ERROR_INSUFFICIENT_BUFFER: return "ERROR_INSUFFICIENT_BUFFER. A supplied buffer size was not large enough, or it was incorrectly set to NULL.";
			case ERROR_INVALID_FLAGS: return "ERROR_INVALID_FLAGS. The values supplied for flags were not valid.";
			case ERROR_INVALID_PARAMETER: return "ERROR_INVALID_PARAMETER. Any of the parameter values was invalid.";
			case ERROR_NO_UNICODE_TRANSLATION: return "ERROR_NO_UNICODE_TRANSLATION. Invalid Unicode was found in a string.";
			default: return "Unexpected error code.";
			}
		};
		
		int res = WideCharToMultiByte(
			CP_UTF8,							// Code page
			0,									// Flags (not using any)
			wstr.c_str(),						// Pointer to input wide char array.
			static_cast<int>(wstr.length()),	// Wide char count (not byte/codepoint/codeunit count). .length() doesn't count the null terminator, the presence of which .c_str() guarantees.
												// -1 as length would let it process all until (and including) the null terminato, but would also include one in the result string which is meh.
			nullptr,							// Pointer to outbut char buffer. Null this time because we just want the function to calculate the needed buffer size for us.
			0,									// Size of the output buffer, in bytes. 0 changes the function to only calculate the needed output size for given input and return it.
			nullptr,							// Pointer to default char (what the fuck), for if a conversion fails. Must be null if Code page is UTF-8.
			nullptr								// Pointer to "default char was needed" flag, for if a conversion failed and had to use the default char. Must be null if Code page is UTF-8.
		);
		
		if (res == 0) { // 0 is failure flag.
			log::error("WinAPI Wide to UTF-8: Failed to get buffer needed size, with error: {}"sv, last_error_string());
			return std::nullopt;
		}
		
		u8string ret{};
		
		try {
			ret.resize(static_cast<std::size_t>(res));
		}
		catch (std::exception& ex) {
			log::error("WinAPI Wide to UTF-8: Exception thrown: {}"sv, ex.what());
			return std::nullopt;
		}
		
		res = WideCharToMultiByte(
			CP_UTF8,								// Code page
			0,										// Flags
			wstr.c_str(),							// Pointer to input wide char array.
			static_cast<int>(wstr.length()),		// Wide char count.
			reinterpret_cast<char*>(ret.data()),	// Pointer to outbut char buffer. This time for real. This reinterpret_cast is well defined, it just makes me die a bit inside.
			static_cast<int>(ret.length()),			// Size of the output buffer, in bytes.
			nullptr,								// Pointer to default char.
			nullptr									// Pointer to "default char was needed" flag.
		);
		
		if (res == 0) {
			log::error("WinAPI Wide to UTF-8: Failed to convert, with error: {}"sv, last_error_string());
			return std::nullopt;
		}
		
		// log::info("WinAPI Wide to UTF-8: Converted a <{}> char (<{}> byte) long wide string to a <{}> char/byte long utf-8 string."sv, wstr.length(), wstr.length() * 2, ret.length());
		return ret;
	}

	std::optional<u8string> get_owner(const std::filesystem::path& full_path) {
		enum : SECURITY_INFORMATION {
			OWNER = OWNER_SECURITY_INFORMATION,
			GROUP = GROUP_SECURITY_INFORMATION,
			DACL = DACL_SECURITY_INFORMATION,
			LABEL = LABEL_SECURITY_INFORMATION,
			ATTRIBUTE = ATTRIBUTE_SECURITY_INFORMATION,
			REQUESTED_INFO = OWNER | GROUP
		};
		constexpr SE_OBJECT_TYPE FILE_OBJECT{ SE_OBJECT_TYPE::SE_FILE_OBJECT };

		PSECURITY_DESCRIPTOR pdesc{};
		if (auto err = GetNamedSecurityInfoW(full_path.wstring().c_str(), FILE_OBJECT, REQUESTED_INFO, nullptr, nullptr, nullptr, nullptr, &pdesc); err != ERROR_SUCCESS) {
			const auto u8err{ wstring_to_utf8(error_string(err)) };
			log::error("WinAPI Get Owner: Failed to get security info for file <{}>, with error: {}"sv, full_path.string(), u8err.has_value() ? reinterpret_cast<const char*>(u8err.value().c_str()) : "unknown");
			return std::nullopt; // Failed for whatever reason.
		}

		PTRUSTEE_W owner_trustee{};
		if (auto err = LookupSecurityDescriptorPartsW(&owner_trustee, nullptr, nullptr, nullptr, nullptr, nullptr, pdesc); err != ERROR_SUCCESS) {
			LocalFree(pdesc);
			const auto u8err{ wstring_to_utf8(error_string(err)) };
			log::error("WinAPI Get Owner: Failed to get security descriptor for file <{}>, with error: {}"sv, full_path.string(), u8err.has_value() ? reinterpret_cast<const char*>(u8err.value().c_str()) : "unknown");
			return std::nullopt; // Failed for whatever reason.
		}

		wstring wide{ owner_trustee->ptstrName };

		LocalFree(owner_trustee);
		LocalFree(pdesc);
		
		if (const auto slash_idx = wide.find(L'\\'); slash_idx < (wide.length() - 1)) {
			//wide = wide.substr(slash_idx + 1);
			wide.erase(0, slash_idx + 1);
		}
		
		return wstring_to_utf8(wide);
	}
	
}