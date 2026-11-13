#pragma once
#include <filesystem>
#include <string>
namespace ember::platform {
// These describe the compiled TARGET, never the computer running CMake.
const char *operating_system() noexcept;
const char *architecture() noexcept;
const char *version() noexcept;
struct Paths {
    std::filesystem::path content;
    std::filesystem::path saves;
    std::filesystem::path cache;
};
// Explicit override wins. Otherwise content is next to the executable.
// Writable data uses the OS user directory, never the current directory.
Paths paths(const std::string &organization, const std::string &application,
            const std::filesystem::path &content_override = {});
// A relative asset name, with no '..' components. On Android this streams an
// APK asset to the app cache for the existing file-based engine importers.
std::filesystem::path resource(const Paths &, const std::filesystem::path &relative);
namespace detail {
void validate_component(const std::string &);
void validate_relative(const std::filesystem::path &);
std::filesystem::path executable_directory();
}
} // namespace ember::platform
