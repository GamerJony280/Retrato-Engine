#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include "../common/shared_library.hpp"
#include <windows.h>
#include <stdexcept>
#include <string>
namespace ember::platform::detail {
SharedLibrary::SharedLibrary(const char *name){
    // Vulkan's system loader; don't search the current working directory.
    std::wstring wide;for(const char *p=name;*p;++p)wide+=wchar_t(*p);
    handle_=LoadLibraryExW(wide.c_str(),nullptr,LOAD_LIBRARY_SEARCH_SYSTEM32);
    if(!handle_)throw std::runtime_error(std::string("Cannot load system library: ")+name);
}
SharedLibrary::~SharedLibrary(){if(handle_)FreeLibrary(static_cast<HMODULE>(handle_));}
void *SharedLibrary::symbol(const char *name) const{return reinterpret_cast<void*>(GetProcAddress(static_cast<HMODULE>(handle_),name));}
}
