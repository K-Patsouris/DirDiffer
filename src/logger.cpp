#include "logger.h"
#include <fstream>
#include <array>
// #include <chrono> // Timestamp log messages.


namespace diff {
	
	class log::log_impl { // Yes, this is valid syntax.
	public:
	
		[[nodiscard]] bool init(const std::filesystem::path& log_file_path) noexcept {
			ofs.open(log_file_path,std::ios_base::binary);
			return ofs.is_open();
		}
		
		bool write(string_view msg) noexcept {
			if (not ofs.is_open()) {
				return false;
			}
			try {
				return true;
			}
			catch (...) {
				return false;
			}
		}
		
	private:
		std::ofstream ofs{};
	};
	
	
	log::log_impl log::impl{};
	
	
	bool log::init(const std::filesystem::path& log_file_path) noexcept {
		return impl.init(log_file_path);
	}
	
	bool log::do_log(const string& msg, const severity sev) noexcept {
		static constexpr std::array<string_view, 5> severity_strings{ "<Info>    ", "<Warning> ", "<Error>   ", "<Critical>", "<>        " };
		
		const std::size_t sev_idx = std::min(severity_strings.size() - 1, static_cast<std::size_t>(sev)); // severity is self-provided so it should be trustable here, but whatever.
		
		try {
			const string msg_with_severity{ severity_strings[sev_idx].data() + msg + "\r\n"};
			return impl.write(msg_with_severity);
		}
		catch (...) {
			return false;
		}
		
		// No actual point in timestamps? The whole runtime won't even be a minute, so only message order matters.
		// const auto now{ std::chrono::floor<std::chrono::seconds>(std::chrono::system_clock::now()) };
		// return impl.write(std::format(L"{:%H:%M:%S} {} {}\r\n"sv, now, severity_strings[sev_idx], msg));
	}
	
	
}