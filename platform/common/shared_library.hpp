#pragma once
namespace ember::platform::detail {
// Private OS adapter. Backend owns it longer than every GPU object/function.
class SharedLibrary {
    void *handle_=nullptr;
public:
    explicit SharedLibrary(const char *name);
    ~SharedLibrary();
    SharedLibrary(const SharedLibrary&)=delete;
    SharedLibrary&operator=(const SharedLibrary&)=delete;
    void *symbol(const char *) const;
};
}
