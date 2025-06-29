#include "filesystem_interface.h"
#include <fstream>			// std::ifstream
#include "winapi_funcs.h"	// get_owner


namespace diff {
	
	using namespace std::filesystem;
	
	std::optional<u8string> read_from_file(const path& file_path) noexcept {
		try {
			std::ifstream ifs{ file_path, std::ios_base::binary | std::ios_base::ate };
			if (not ifs.is_open()) {
				log::error("File->String: Failed to open <{}>."sv, file_path.string());
				return std::nullopt;
			}
			
			const auto filesize = static_cast<std::size_t>(ifs.tellg());
			ifs.seekg(0, std::ios::beg);
			if (not ifs.good()) {
				log::error("File->String: Failed to calculate file <{}> size."sv, file_path.string());
				return std::nullopt;
			}
			
			u8string str{};
			str.resize(filesize);
			
			if (not ifs.read(reinterpret_cast<char*>(str.data()), filesize)) {
				log::error("File->String: Failed to read file <{}> into string buffer."sv, file_path.string());
				return std::nullopt;
			}
			
			log::info("File->String: Read file <{}> into string."sv, file_path.string());
			return str;
		}
		catch (std::exception& ex) {
			log::error("File->String: Exception thrown: {}"sv, ex.what());
			return std::nullopt;
		}
	}
	
	bool write_to_file(const path& file_path, string_view data) noexcept {
		try {
			std::ofstream ofs{ file_path, std::ios::binary | std::ios::trunc };
			if (not ofs.is_open()) {
				log::error("String->File: Failed to open/create output file <{}>."sv, file_path.string());
				return false;
			}
			
			if (not (ofs << data.data())) {
				log::error("String->File: Failed to write text to file <{}>."sv, file_path.string());
				return false;
			}
			
			log::info("String->File: Wrote string to file <{}>."sv, file_path.string());
			return true;
		}
		catch (std::exception& ex) {
			log::error("String->File: Exception thrown: {}"sv, ex.what());
			return false;
		}
	}
	bool write_to_file(const path& file_path, u8string_view data) noexcept {
		return write_to_file(file_path, string_view{ reinterpret_cast<const char*>(data.data()), data.length() });
	}
	

	std::optional<dynamic_buffer> read_dbuf_from_file(const path& file_path) noexcept {
		try {
			std::ifstream ifs{ file_path, std::ios::binary | std::ios::ate }; // ::ate = seek to end after opening
			if (not ifs.is_open()) {
				log::error("File->Buffer: File <{}> could not be opened."sv, file_path.string());
				return std::nullopt;
			}

			const auto filesize = static_cast<std::size_t>(ifs.tellg());
			ifs.seekg(0, std::ios::beg);
			if (not ifs.good()) {
				log::error("File->Buffer: Failed to calculate file <{}> size."sv, file_path.string());
				return std::nullopt;
			}
			
			dynamic_buffer buf{ ifs, filesize };

			if (buf.length() != filesize) {
				log::error("File->Buffer: Failed to read file <{}> into buffer."sv, file_path.string());
				return std::nullopt;
			}
			
			log::info("File->Buffer: Read file <{}> into buffer. Bytes in buffer = <{}>."sv, file_path.string(), buf.length());
			return buf;
		}
		catch (std::exception& ex) {
			log::error("File->Buffer: Exception thrown: {}"sv, ex.what());
			return std::nullopt;
		}
	}

	bool write_dbuf_to_file(const path& file_path, const dynamic_buffer& dbuf) noexcept {
		try {
			std::ofstream ofs{ file_path, std::ios::binary | std::ios::trunc };
			if (not ofs.is_open()) {
				log::error("Buffer->File: Failed to open file <{}>."sv, file_path.string());
				return false;
			}
			if (dbuf.length() == 0) {
				log::info("Buffer->File: Buffer was empty so file <{}> was only created and left empty."sv, file_path.string());
				return true; // Just make the file.
			}
			if (not ofs.write(reinterpret_cast<const char*>(dbuf.data()), dbuf.length())) {
				log::error("Buffer->File: Failed to write buffer to file <{}>."sv, file_path.string());
				return false;
			}
			log::info("Buffer->File: Wrote <{}> bytes from buffer to file <{}>."sv, dbuf.length(), file_path.string());
			return true;
		}
		catch (std::exception& ex) {
			log::error("Buffer->File: Exception thrown: {}"sv, ex.what());
			return false;
		}
	}


	std::optional<configuration> get_configuration(const path& file_path) noexcept {
		try {
			std::ifstream ifs{ file_path, std::ios::binary | std::ios::ate }; // ::ate = seek to end after opening
			if (not ifs.is_open()) {
				log::error("File->Config: Failed to open file <{}>."sv, file_path.string());
				return std::nullopt;
			}
			
			const auto filesize = static_cast<std::size_t>(ifs.tellg());
			ifs.seekg(0, std::ios::beg);
			if (not ifs.good()) {
				log::error("File->Config: Failed to calculate file <{}> size."sv, file_path.string());
				return std::nullopt;
			}
			
			u8string str{};
			str.resize(filesize);
			
			if (not ifs.read(reinterpret_cast<char*>(str.data()), filesize)) {
				log::error("File->Config: Failed to read file <{}>."sv, file_path.string());
				return std::nullopt;
			}
			
			log::info("File->Config: Read file <{}>. Parsing..."sv, file_path.string());
			return configuration::parse_file_contents(str);
		}
		catch (std::exception& ex) {
			log::error("File->Config: Exception thrown:"sv, ex.what());
			return std::nullopt;
		}
	}


	std::optional<std::vector<file>> get_files_recursive(const configuration& filter) noexcept {
		try {
			std::vector<file> ret{};
			ret.reserve(500);
			
			const path& root{ filter.get_root() };
			
			// rec_it::depth() returns an int, so we're stuck with signed types for simplicity. configuration::get_min_depth() returns u32, which fits in i64.
			const i64 min_depth = [] (const u32 min_depth) {
				static_assert(std::same_as<u32, std::remove_const_t<decltype(min_depth)>>, "configuration::get_min_depth() expected to return u32. Ensure new return type fits in i64 before updating this static_assert.");
				return static_cast<i64>(min_depth);
			}(filter.get_min_depth());
			
			using rec_it = recursive_directory_iterator;
			for (rec_it it{ root, directory_options::skip_permission_denied }; it != rec_it{}; ++it) {

				if (std::error_code ec{}; (not it->is_regular_file(ec)) or ec) {
					if (ec) {
						log::warning("Disk->Filelist: Failed to check if <{}> is a file. Skipped it."sv, it->path().string());
					}
					continue; // Entry not a file.
				}

				if (static_cast<i64>(it.depth()) < min_depth) { // This cast is always safe. int will never be bigger than i64.
					continue; // Entry depth too shallow.
				}

				if (not filter.ext_is_accepted(lowercase_path{ it->path().extension() })) {
					continue; // Entry file extension not relevant.
				}

				// p1.lexically_relative(p2) returns the path to follow to get from p2 to p1. In this case, it just trims root from current path since the latter is a subdir of the former.
				// The root part of all entries recursively iterated over retains the case of the original root variable, regardless of actual case of the root in the filesystem.
				// Hence, passing that same root variable to .lexically_relative() trims correctly.
				path original_relative{ it->path().lexically_relative(root) };

				lowercase_path parent_lower{ original_relative.parent_path() };

				if (filter.folder_is_excluded(parent_lower)) {
					continue; // Entry is in an to-ignore folder.
				}

				lowercase_path filename{ original_relative.filename() };

				auto owner{ winapi::get_owner(it->path()) };
				if (not owner.has_value()) {
					log::warning("Disk->Filelist: Failed to get owner of file <{}>. Skipped it."sv, it->path().string());
					continue;
				}

				std::error_code ec{};
				// Technically, std::uintmax_t could be(come) larger than u64, so this cast would fuck things up for files larger than 18 exabytes, but I'll be too dead to care by the time this happens.
				const u64 size_in_bytes = static_cast<u64>(it->file_size(ec));
				if (ec) {
					log::warning("Disk->Filelist: Failed to get size of file <{}>. Skipped it."sv, it->path().string());
					continue;
				}

				const auto last_write_time{ it->last_write_time(ec) };
				if (ec) {
					log::warning("Disk->Filelist: Failed to get last write time of file <{}>. Skipped it."sv, it->path().string());
					continue;
				}

				ret.emplace_back(
					std::move(original_relative),
					std::move(parent_lower),
					std::move(filename),
					file::owner_name{ std::move(owner.value()) },
					size_in_bytes,
					std::chrono::seconds{ last_write_time.time_since_epoch().count() }
				);
			}
			
			log::info("Disk->Filelist: Enumerated <{}> relevant files from disk with root <{}>"sv, ret.size(), root.string());
			return ret;
		}
		catch (std::exception& ex) {
			log::error("Disk->Filelist: Excpetion thrown:"sv, ex.what());
			return std::nullopt;
		}
	}
	
	
	std::optional<bool> file_exists(const path& file_path) noexcept {
		std::error_code ec{};

		const bool exists = std::filesystem::exists(file_path, ec);
		if (ec) {
			log::error("File Exists: Failed to check whether filesystem entry <{}> already exists."sv, file_path.string());
			return std::nullopt;
		}
		if (not exists) {
			log::info("File Exists: Filesystem entry <{}> does not exist, much less be a file."sv, file_path.string());
			return false;
		}
		
		const bool is_file = std::filesystem::is_regular_file(file_path, ec);
		if (ec) {
			log::error("File Exists: Failed to check if filesystem entry <{}> is a regular file."sv, file_path.string());
			return std::nullopt;
		}
		
		log::info("File Exists: Filesystem entry <{}> exists. Also being a regular file is <{}>."sv, file_path.string(), is_file);
		return is_file;
	}
	
	bool rename_file(const path& file_path, string_view new_name) noexcept {
		try {
			if (new_name.empty()) {
				log::error("File Rename: Failed to rename file <{}> because new name is empty string."sv, file_path.string());
				return false;
			}
			
			std::error_code ec{};
			
			if (const bool reg = std::filesystem::is_regular_file(file_path, ec); ec or not reg) {
				if (ec) {
					log::error("File Rename: Failed to rename file <{}> because path could not be verified to refer to an existing file."sv, file_path.string());
				}
				return false;
			}
			
			std::filesystem::rename(file_path, file_path.parent_path() / new_name, ec);
			
			if (ec) {
				log::error("File Rename: Failed to rename file <{}>."sv, file_path.string());
				return false;
			}
			
			log::info("File Rename: Renamed file <{}> to <{}>."sv, file_path.string(), new_name);
			return true;
		}
		catch (std::exception& ex) {
			log::error("File Rename: Exception thrown: {}"sv, ex.what());
			return false;
		}
	}
	
	bool delete_file(const path& file_path) noexcept {
		std::error_code ec{};
		
		if (const bool reg = std::filesystem::is_regular_file(file_path, ec); ec or not reg) {
			log::error("File Delete: Failed to delete file <{}> because path could not be verified to refer to an existing file."sv, file_path.string());
			return false;
		}
		
		const bool removed = std::filesystem::remove(file_path, ec);
		
		if (ec or not removed) {
			log::error("File Delete: Failed to delete file <{}>."sv, file_path.string());
			return false;
		}
		
		log::info("File Delete: Deleted file <{}>."sv, file_path.string());
		return true;
	}
	
	bool folder_create_or_exists(const path& folder_name) noexcept {
		std::error_code ec{};
		
		const bool exists = std::filesystem::exists(folder_name, ec);
		if (ec) {
			log::error("Folder Create: Failed to check whether folder <{}> already exists."sv, folder_name.string());
			return false;
		}
		if (exists) {
			log::info("Folder Create: Folder <{}> already exists."sv, folder_name.string());
			return true;
		}
		
		const bool created = std::filesystem::create_directory(folder_name, ec);
		if (ec or not created) {
			log::error("Folder Create: Failed to create folder <{}>."sv, folder_name.string());
			return false;
		}
		
		log::info("Folder Create: Created folder <{}>."sv, folder_name.string());
		return true;
	}
	
	
}