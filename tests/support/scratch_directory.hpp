#pragma once

#include <filesystem>
#include <fstream>
#include <random>
#include <stdexcept>
#include <string>
#include <string_view>
#include <system_error>

namespace kalshi::test {

/// A directory under the system's temporary directory, removed with its
/// contents when the test ends. The random name keeps concurrent runs apart.
class ScratchDirectory {
public:
	ScratchDirectory() {
		std::random_device entropy;
		path_ = std::filesystem::temp_directory_path() /
				("kalshi-test-" + std::to_string(entropy()) + "-" + std::to_string(entropy()));
		std::filesystem::create_directories(path_);
	}
	~ScratchDirectory() {
		std::error_code ignored;
		std::filesystem::remove_all(path_, ignored);
	}
	ScratchDirectory(const ScratchDirectory&) = delete;
	ScratchDirectory& operator=(const ScratchDirectory&) = delete;

	[[nodiscard]] std::string path() const { return path_.string(); }

	/// Writes `contents` to a file named `name` and returns its path.
	[[nodiscard]] std::string write(std::string_view name, std::string_view contents) const {
		const std::filesystem::path file = path_ / name;
		std::ofstream output{file, std::ios::binary};
		output.write(contents.data(), static_cast<std::streamsize>(contents.size()));
		output.close();
		if (!output) {
			throw std::runtime_error("Failed to write a test file");
		}
		return file.string();
	}

private:
	std::filesystem::path path_;
};

} // namespace kalshi::test
