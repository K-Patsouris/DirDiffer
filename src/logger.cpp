#include "logger.h"
#include <fstream>
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
				ofs << msg;
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
	
	bool log::do_log(string_view msg, const severity sev) noexcept {
		static constexpr std::array<string_view, 5> severity_strings{ "<Info>     ", "<Warning>  ", "<Error>    ", "<Critical> ", "<>         " };
		
		const std::size_t sev_idx = std::min(severity_strings.size() - 1, static_cast<std::size_t>(sev)); // severity is self-provided so it should be trustable here, but whatever.
		
		return impl.write(severity_strings[sev_idx]) and impl.write(msg) and impl.write("\r\n");

		// No actual point in timestamps? The whole runtime won't even be a minute, so only message order matters.
		// const auto now{ std::chrono::floor<std::chrono::seconds>(std::chrono::system_clock::now()) };
		// return impl.write(std::format(L"{:%H:%M:%S} {} {}\r\n"sv, now, severity_strings[sev_idx], msg));
	}



	
}