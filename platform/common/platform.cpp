#include "ember/platform.hpp"
#include "ember/version.hpp"
#include <stdexcept>
namespace ember::platform {
const char *version() noexcept { return RETRATO_VERSION; }
const char *architecture() noexcept {
#if defined(__aarch64__) || defined(_M_ARM64)
    return "arm64";
#elif defined(__x86_64__) || defined(_M_X64)
    return "x86_64";
#elif defined(__arm__) || defined(_M_ARM)
    return "armv7";
#elif defined(__i386__) || defined(_M_IX86)
    return "x86";
#else
    return "generic";
#endif
}
namespace detail {
void validate_component(const std::string &s) {
    if(s.empty() || s=="." || s==".." || s.find_first_of("/\\:\0",0,4)!=std::string::npos)
        throw std::invalid_argument("Organization/application must be nonempty directory names");
}
void validate_relative(const std::filesystem::path &p) {
    if(p.empty() || p.is_absolute() || p.has_root_name())
        throw std::invalid_argument("Asset path must be relative");
    for(const auto &part:p)if(part=="..")throw std::invalid_argument("Asset path cannot traverse parents");
}
}
#ifndef __ANDROID__
std::filesystem::path resource(const Paths &p,const std::filesystem::path &relative) {
    detail::validate_relative(relative);return p.content/relative;
}
#endif
}
