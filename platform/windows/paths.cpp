#include "ember/platform.hpp"
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <shlobj.h>
#include <stdexcept>
#include <vector>
namespace ember::platform {
const char *operating_system() noexcept { return "windows"; }
namespace detail {
std::filesystem::path executable_directory() {
    std::vector<wchar_t> buf(512);
    while(buf.size()<=32768){
        DWORD n=GetModuleFileNameW(nullptr,buf.data(),DWORD(buf.size()));
        if(!n)throw std::runtime_error("Cannot locate executable");
        if(n<buf.size())return std::filesystem::path(std::wstring(buf.data(),n)).parent_path();
        buf.resize(buf.size()*2);
    }
    throw std::runtime_error("Executable path exceeds Windows limit");
}
}
Paths paths(const std::string &org,const std::string &app,const std::filesystem::path &root) {
    detail::validate_component(org);detail::validate_component(app);
    PWSTR raw=nullptr;
    if(FAILED(SHGetKnownFolderPath(FOLDERID_LocalAppData,0,nullptr,&raw)))throw std::runtime_error("Cannot locate LocalAppData");
    std::filesystem::path base;
    try{base=raw;}catch(...){CoTaskMemFree(raw);throw;}CoTaskMemFree(raw);
    base/=std::filesystem::u8path(org);base/=std::filesystem::u8path(app);
    Paths p{root.empty()?detail::executable_directory()/L"content":std::filesystem::absolute(root),base/L"saves",base/L"cache"};
    std::filesystem::create_directories(p.saves);std::filesystem::create_directories(p.cache);return p;
}
}
