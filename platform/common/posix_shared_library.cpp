#include "shared_library.hpp"
#include <dlfcn.h>
#include <stdexcept>
#include <string>
namespace ember::platform::detail {
SharedLibrary::SharedLibrary(const char *name){handle_=dlopen(name,RTLD_NOW|RTLD_LOCAL);if(!handle_)throw std::runtime_error(std::string("Cannot load ")+name+": "+dlerror());}
SharedLibrary::~SharedLibrary(){if(handle_)dlclose(handle_);}
void *SharedLibrary::symbol(const char *name) const{return dlsym(handle_,name);}
}
