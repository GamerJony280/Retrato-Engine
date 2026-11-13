#include "ember/platform.hpp"
#include <cstdlib>
#include <stdexcept>
#include <unistd.h>
#include <vector>
namespace ember::platform {
const char *operating_system() noexcept { return "linux"; }
namespace detail {
std::filesystem::path executable_directory() {
    std::vector<char> bytes(512);
    while(bytes.size()<=1024*1024){
        auto n=::readlink("/proc/self/exe",bytes.data(),bytes.size());
        if(n<0)throw std::runtime_error("Cannot locate executable; provide an explicit content directory");
        if(size_t(n)<bytes.size())return std::filesystem::u8path(std::string(bytes.data(),size_t(n))).parent_path();
        bytes.resize(bytes.size()*2);
    }
    throw std::runtime_error("Executable path exceeds limit");
}
}
Paths paths(const std::string &org,const std::string &app,const std::filesystem::path &root) {
    detail::validate_component(org);detail::validate_component(app);
    auto from_env=[](const char *name){auto value=std::getenv(name);auto path=value?std::filesystem::u8path(value):std::filesystem::path{};return path.is_absolute()?path:std::filesystem::path{};};
    auto home=from_env("HOME"),d=from_env("XDG_DATA_HOME"),c=from_env("XDG_CACHE_HOME");
    // XDG specifies that relative values are ignored, not interpreted from cwd.
    if((d.empty()||c.empty())&&home.empty())throw std::runtime_error("Absolute HOME or XDG paths are required");
    if(d.empty())d=home/".local/share";
    if(c.empty())c=home/".cache";
    Paths result{root.empty()?detail::executable_directory()/"content":std::filesystem::absolute(root),d/org/app,c/org/app};
    std::filesystem::create_directories(result.saves);std::filesystem::create_directories(result.cache);return result;
}
}
