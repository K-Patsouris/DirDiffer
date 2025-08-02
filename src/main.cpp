#include "logger.h"
#include "memory.h"
#include "smtp.h"
#include "file.h"
#include "configuration.h"
#include "filesystem_interface.h"
#include "serialization.h"
#include "differ.h"
#include "string_utils.h"
#include "sample_config.h"
#include <iostream>
#include <filesystem>
#include <chrono>
#include <format>


namespace diff {
	
	static constexpr std::string_view log_folder_name{ "logs" };
	static constexpr std::string_view config_file_name{ "config.txt" };
	static constexpr std::string_view data_file_name{ "data.bin" };
	static constexpr std::string_view old_data_file_name{ "data.bin.old" };
	static constexpr std::string_view new_data_file_name{ "data.bin.new" };
	
	void normal_routine(const std::filesystem::path& startup_path) {

		const auto start_time{ std::chrono::floor<std::chrono::seconds>(std::chrono::system_clock::now()) };

		const std::filesystem::path config_path{ startup_path / config_file_name };
		const std::filesystem::path savedata_path{ startup_path / data_file_name };
		const std::filesystem::path old_savedata_path{ startup_path / old_data_file_name };
		const std::filesystem::path logfile_path{ startup_path / log_folder_name / std::format("{:%Y-%m-%d_%UTC-%Hh-%Mm-%Ss_%a-%d-%B}.log"sv, start_time) };
		//const std::filesystem::path logfile_path{ startup_path / log_folder_name / "aaaa.log" };
		// e.g. "...\logs\2025-01-08_UTC-17h-02m-08s_Wed-08-January.log"
		const std::filesystem::path reportfile_path{ startup_path / log_folder_name / std::format("{:%Y-%m-%d_%UTC-%Hh-%Mm-%Ss_%a-%d-%B}_report.txt"sv, start_time) };
		//const std::filesystem::path reportfile_path{ startup_path / log_folder_name / "aaaa_report.txt"};
		// e.g. "...\logs\2025-01-08_UTC-17h-02m-08s_Wed-08-January_report.txt"
		
		// Init logging.
		if (not folder_create_or_exists(log_folder_name)
			or not log::init(logfile_path)) {
			return;
		}
		if (not log::info("Main: Program started and logging initialized.\r\n"sv)) {
			std::cout << "Normal routine failed to init logging.\n";
		}
		else {
			std::cout << "Normal routine initialized logging.\n";
		}
		
		
		
		// Read config.
		configuration config{};
		{
			auto opt{ get_configuration(config_path) };
			if (not opt.has_value()) {
				log::error("Main: Failed to read config."sv);
				return;
			}
			config = std::move(opt.value());
			log::info("Main: Parsed configuration file <{}>"sv, config_file_name);
		}
		
		
		
		// Read saved data (smtp info and old filelist).
		smtp_info smtp{};
		old_files_t old_files{};
		{
			auto opt{ read_dbuf_from_file(savedata_path).and_then(serialization::deserialize_from_buffer) };
			if (not opt.has_value()) {
				log::error("Main: Failed to read saved data."sv);
				return;
			}
			smtp = std::move(opt.value().smtp);
			old_files.files = std::move(opt.value().files);
			log::info("Main: Read old serialized data from <{}>, containing entries for <{}> files"sv, data_file_name, old_files.files.size());
			// No sort needed for old files. We always store sorted.
		}
		


		// Enumerate files currently on disk.
		new_files_t new_files{};
		{
			auto opt{ get_files_recursive(config) };
			if (not opt.has_value()) {
				log::error("Main: Failed to enumerate files from disk."sv);
				return;
			}
			new_files.files = std::move(opt.value());
			std::sort(new_files.files.begin(), new_files.files.end()); // Sort new files. recursive_directory_iterator makes no order guarantees.
			log::info("Main: Enumerated files of interest currently on disk ({} files) and sorted them."sv, new_files.files.size());
		}
		


		// Diff old and new files.
		diff::u8string report{};
		{
			auto opt{ diff_sorted_files(old_files, new_files) };
			if (not opt.has_value()) {
				log::error("Main: Failed to diff old and new state."sv);
				return;
			}
			report = std::move(opt.value());
			log::info("Main: Generated UTF-8 report string ({} bytes long)."sv, report.length());
		}
		
		
		
		if (not write_to_file(reportfile_path, report)) {
			log::warning("Main: Failed to write diff report to disk. Report will be sent via email later so this is not a hard error."sv);
		}
		log::info("Main: Wrote report to disk in <{}>."sv, reportfile_path.string());
		
		
		
		// Generate serializable buffer from new files.
		dynamic_buffer new_data_buf{};
		{
			auto opt{ serialization::serialize_to_buffer_encrypted(smtp, new_files.files) };
			if (not opt.has_value()) {
				log::error("Main: Failed to serialize data."sv);
				return;
			}
			new_data_buf = std::move(opt.value());
			log::info("Main: Serialized new data into internal buffer."sv);
		}
		
		
		
		// Rename old datafile and write new one to disk.
		{
			if (not rename_file(savedata_path, old_data_file_name)) {
				log::error("Main: Failed to rename <{}> to <{}>."sv, data_file_name, old_data_file_name);
				return;
			}
			log::info("Main: Renamed old data file <{}> to <{}> to keep as a backup."sv, data_file_name, old_data_file_name);
			if (not write_dbuf_to_file(savedata_path, new_data_buf)) {
				log::error("Main: Failed to write serialized buffer to <{}>."sv, data_file_name);
				if (not rename_file(old_savedata_path, data_file_name)) {
					log::critical("Main: Failed to rename <{}> back to <{}> while cleaning up. Do so manually."sv, old_data_file_name, data_file_name);
				}
				return;
			}
			log::info("Main: Wrote new new data to <{}>."sv, data_file_name);
		}
		
		
		
		// Send email.
		
		if (not send_email(smtp, config.get_email_metadata(), report)) {
			log::error("Main: Failed to send report email."sv);
			if (not delete_file(savedata_path)) {
				log::critical("Main: Failed to delete <{0}>. It contains new data that should be discarded because email dispatch failed. Delete it manually, and rename <{1}> back to <{0}>"sv, data_file_name, old_data_file_name);
			}
			else if (not rename_file(old_savedata_path, data_file_name)) {
				log::critical("Main: Failed to rename <{}> to <{}> when cleaning up after email dispatch failed. Do so manually."sv, old_data_file_name, data_file_name);
			}
			return;
		}
		log::info("Main: Sent report email."sv);
		//*/
		//log::info("Main: Skipped sending email."sv);
		
		
		if (not delete_file(old_savedata_path)) {
			log::warning("Main: Failed to delete backup file <{}> when finishing up. All other operations were successful and the file is no longer needed. It is safe to delete it manually, or to just ignore it."sv);
			return;
		}
		log::info("Main: All operations completed successfully. Deleted <{}> backup file."sv);
		
	}
	
	
	void set_smtp(const std::filesystem::path& startup_path, string_view smtp_filename) {
		const std::filesystem::path savedata_path{ startup_path / data_file_name };
		const std::filesystem::path old_savedata_path{ startup_path / old_data_file_name };
		
		diff::vector<diff::u8string> lines{ [](const std::filesystem::path& smtp_file_path) -> diff::vector<diff::u8string> {
			const auto content{ read_from_file(smtp_file_path) };
			if (not content.has_value()) {
				std::cout << "Error:    Failed to read specified file <" << smtp_file_path.filename().string() << ">. Aborting without effect.\n\n";
				return {};
			}
			std::cout << "Info:     Read file <" << smtp_file_path.filename().string() << "> with length " << content.value().length() << ". Parsing...\n";
			
			auto ret{ split(content.value(), u8'\n')};
			for (auto& line : ret) {
				if (not line.empty() and (line.back() == '\r')) {
					line.pop_back();
				}
				trim(line);
			}
			std::erase_if(ret, [](const auto& line) { return line.empty(); });
			
			return ret;
		}(startup_path / smtp_filename)};
		
		if (lines.size() != 3) {
			std::cout << "Error:    Invalid input file. There must be exactly 3 non-empty lines, containing in order: smtp url, username, and password. Aborting without effect.\n\n";
			system("pause");
			return;
		}
		
		smtp_info smtp{};
		smtp.url = std::move(lines[0]);
		smtp.username = std::move(lines[1]);
		smtp.password = std::move(lines[2]);
		
		diff::vector<file> files{};

		const auto savedata_exists = file_exists(savedata_path);

		if (not savedata_exists.has_value()) {
			std::cout << "Error:    Could not verify whether <" << data_file_name << "> (containing serialized data) exists or not. Aborting without effect. Try running the program again.\n\n";
			system("pause");
			return;
		}
		else if (savedata_exists.value()) {
			std::cout << "Info:     Found <" << data_file_name << ">. Updating it with new SMTP info.\n";
			auto data{ read_dbuf_from_file(savedata_path).and_then(serialization::deserialize_from_buffer) };
			if (not data.has_value()) {
				std::cout << "Error:    <" << data_file_name << "> exists but could not read it from disk. Aborting without effect. Try running the program again.\n\n";
				system("pause");
				return;
			}
			std::cout << "Info:     Existing data smtp = <"
				<< reinterpret_cast<const char*>(data.value().smtp.url.c_str()) << ", "
				<< reinterpret_cast<const char*>(data.value().smtp.username.c_str()) << ", "
				<< reinterpret_cast<const char*>(data.value().smtp.password.c_str()) << ">\n";
			files = std::move(data.value().files);
			std::cout << "Info:     Loaded the serialized data from disk.\n";
		}
		else {
			std::cout << "Info:     <" << data_file_name << "> not found. It will be created now with provided SMTP info.\n";
		}
		
		dynamic_buffer data_buf{};
		{
			auto opt{ serialization::serialize_to_buffer_encrypted(smtp, files) };
			if (not opt.has_value()) {
				std::cout << "Error:    Failed to serialize data with new SMTP info into internal buffer. Aborting without effect. Try running the program again.\n\n";
				system("pause");
				return;
			}
			data_buf = std::move(opt.value());
			if (savedata_exists.value()) {
				std::cout << "Info:     Re-serialized data with updated SMTP info into internal bufffer.\n";
			}
			else {
				std::cout << "Info:     Initialized data with provided SMTP info into internal bufffer.\n";
			}
		}
		
		
		if (savedata_exists.value()) {
			if (not rename_file(savedata_path, old_data_file_name)) {
				std::cout << "Error:    Failed to rename <" << data_file_name << "> to <" << old_data_file_name << ">. Aborting without effect. Try running the program again.\n\n";
				system("pause");
				return;
			}
			std::cout << "Info:     Renamed <" << data_file_name << "> to <" << old_data_file_name << ">.\n";

			if (not write_dbuf_to_file(savedata_path, data_buf)) {
				std::cout << "Error:    Failed to write updated serialized data to <" << data_file_name << ">. Trying to rename <" << old_data_file_name << "> back to <" << data_file_name << ">...\n";
				if (not rename_file(old_savedata_path, data_file_name)) {
					std::cout << "Critical: Failed to rename <" << old_data_file_name << "> back to <" << data_file_name << ">. Do so manually before trying to run the program again.\n\n";
				}
				else {
					std::cout << "Info:     Renamed <" << old_data_file_name << "> back to <" << data_file_name << ">. Aborting without effect. Try running the program again.\n\n";
				}
				system("pause");
				return;
			}
			std::cout << "Info:     Wrote updated serialized data to <" << data_file_name << ">.\n";

			if (not delete_file(old_savedata_path)) {
				std::cout << "Warning:  Failed to delete <" << old_data_file_name << "> backup file. It is safe to delete it manually, or to just ignore it.\n\n";
				system("pause");
				return;
			}

			std::cout << "Info:     Deleted backup file <" << old_data_file_name << ">. <" << data_file_name << "> now contains the updated SMTP info. You can now delete <" << smtp_filename << ">.\n\n";
			system("pause");
			return;
		}
		else {
			if (not write_dbuf_to_file(savedata_path, data_buf)) {
				std::cout << "Error:    Failed to write new savedata to <" << data_file_name << ">.\n";
				system("pause");
				return;
			}
		}
		
	}
	

	void show_help(const std::filesystem::path& startup_path) {
		using std::cout;

		cout << "\nTo start, create a text file with exactly 3 lines, containing SMTP url, username, and password, in that order.";
		cout << "Then, call the program with \"-set path\\to\\the\\file.txt\" arguments.\n";
		cout << "This will create a savefile with the given credentials, encrypted, or update an existing one if found.\n";
		cout << "Afterwards, each invocation of the program will work as usual.\n";
		cout << "If you never provide SMTP info like this, hence never having a savedata file, the program will do nothing.\n";
		cout << "Everything the program stores is in the savedata file. Deleting it essentially resets everything to zero.\n\n";

		cout << "For normal use, the program needs a file named specificall \"config.txt\" in the same directory as the executable.\n";
		cout << "In \"config.txt\" you can specify the parameters of the directory monitoring, and the email dispatch details.\n";
		cout << "The syntax is similar to the classic INI file syntax, except with angle brackets (<>) replacing brackets ([]) for category tags, and double slashes (//) replacing semicolon (;) for line comments.\n";
		cout << "The valid category tags are: <root>, <file extensions>, <excluded folders>, <min depth>, <email from>, <email to>, <email cc>, and <email subject>.\n\n";
		
		cout << "Would you like to create a sample \"config.txt\" with more details about the syntax inside (no effect if a \"config.txt\" already exists)? Y/N\n";

		char choice{};
		std::cin >> choice;
		cout << '\n';

		if ((choice == 'y') or (choice == 'Y')) {
			const std::filesystem::path config_path{ startup_path / config_file_name };
			if (const auto exists_check = file_exists(config_path); not exists_check.has_value()) {
				cout << "Failed to verify if \"config.txt\" already exists on disk. Generation aborted.\n";
			}
			else if (exists_check.value()) {
				cout << "\"config.txt\" already exists. Generation aborted.\n";
			}
			else if (not write_to_file(config_path, sample_config_contents)) {
				cout << "Failed to write \"config.txt\" to disk. Try rerunning the program.\n";
			}
			else {
				cout << "Sample \"config.txt\" written to disk.\n";
			}
		}
		else if ((choice == 'n') or (choice == 'N')) {
			cout << "No sample config generated.\n";
		}
		else {
			cout << "Unrecognized input. No sample config generated.\n";
		}

		cout << "\n";
		system("pause");
		return;
	}
	
	/*void test(const std::filesystem::path& startup_path) {

		const auto start_time{ std::chrono::floor<std::chrono::seconds>(std::chrono::system_clock::now()) };

		const std::filesystem::path logfolder_path{ startup_path / log_folder_name };
		const std::filesystem::path logfile_path{ logfolder_path / std::format("{:%Y-%m-%d_%UTC-%Hh-%Mm-%Ss_%a-%d-%B}.log"sv, start_time) };
		const std::filesystem::path config_path{ startup_path / config_file_name };

		// Init logging.
		if (not folder_create_or_exists(logfolder_path) or not log::init(logfile_path)) {
			std::cout << "Failed to init logging!\n\n";
			return;
		}
		log::info("Program started and logging initialized.\r\n"sv);



		// Read config.
		configuration config{};
		{
			auto opt{ get_configuration(config_path) };
			if (not opt.has_value()) {
				std::cout << "Failed to read config.\n";
				return;
			}
			config = std::move(opt.value());
			std::cout << "Read configuration file <" << config_file_name << ">.\n";
		}

		std::cout << "Config dump:\n";
		std::cout << reinterpret_cast<const char*>(config.dump().c_str());
		std::cout << "\n\nTest done.\n";
	}//*/



}


int __cdecl main([[maybe_unused]] int argc, [[maybe_unused]] char** argv) {

	std::cout << "Program ran. Setting locale...\n";
	
	std::locale::global(std::locale{ "en_US.UTF-8" });

	std::cout << "Set locale to en_US.UTF-8. Starting program...\n";
	
	if (argc < 1) {
		std::cout << "Too few arguments!\n\n";
		//system("pause");
		return 1;
	}
	
	const std::filesystem::path startup_path{ std::filesystem::path{ argv[0] }.parent_path() };

	//diff::test(startup_path);
	//std::cout << "\n\n";
	//system("pause");
	//return 0;
	
	if (argc == 1) {
		std::cout << "Normal routine\n";
		diff::normal_routine(startup_path);
	}
	else {
		if (std::string{ "-h" } == argv[1]) {
			diff::show_help(startup_path);
		}
		else if (std::string{ "-set" } != argv[1]) {
			std::cout << "Unrecognized argument \"" << argv[1] << "\".\n";
			return 1;
		}
		else if (argc != 3) {
			std::cout << "Argument \"-set\" must be followed by the path to the file containing the smtp information.\n";
			return 1;
		}
		else {
			std::cout << "Setting routine\n";
			diff::set_smtp(startup_path, argv[2]);
		}
	}

	//std::cout << "\n\n";
	//system("pause");
	std::cout << "Execution finished.";

#ifdef DIRDIFFER_ALLOCATION_LOGGING

	diff::diag::program_finished();

#endif

	return 0;
	
}
