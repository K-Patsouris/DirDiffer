#pragma once
#include "string_defs.h"
#include <filesystem>
#include <format>

namespace diff {

	class log {
	private:
		
		log() = delete; // Must only be used staticly.
		
		enum severity {
			sev_info,
			sev_warning,
			sev_error,
			sev_critical
		};
		
		static bool do_log(const string& msg, const severity sev) noexcept;
		
	public:
		
		[[nodiscard]] static bool init(const std::filesystem::path& log_file_path) noexcept;
		
		static bool info(string_view fmt, auto&&... args) noexcept {
			try { return do_log(std::vformat(fmt, std::make_format_args(args...)), severity::sev_info); }
			catch (...) { return false; }
		}
		
		static bool warning(string_view fmt, auto&&... args) noexcept {
			try { return do_log(std::vformat(fmt, std::make_format_args(args...)), severity::sev_warning); }
			catch (...) { return false; }
		}
		
		static bool error(string_view fmt, auto&&... args) noexcept {
			try { return do_log(std::vformat(fmt, std::make_format_args(args...)), severity::sev_error); }
			catch (...) { return false; }
		}
		
		static bool critical(string_view fmt, auto&&... args) noexcept {
			try { return do_log(std::vformat(fmt, std::make_format_args(args...)), severity::sev_critical); }
			catch (...) { return false; }
		}

	private:
		class log_impl;
		static log_impl impl;
	};


}
