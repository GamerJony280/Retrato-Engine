#pragma once
#include <filesystem>
#include <string>
#include <vector>
namespace retrato::detail {
// Own UTF-8 strings for the entire entry call. Do not let argv outlive storage.
class Arguments {
    std::vector<std::string> storage_;
    std::vector<char *> pointers_;
public:
    Arguments(int argc,wchar_t **argv) {
        storage_.reserve(size_t(argc));
        for(int i=0;i<argc;++i)storage_.push_back(std::filesystem::path(argv[i]).u8string());
        pointers_.reserve(size_t(argc)+1);
        for(auto &value:storage_)pointers_.push_back(value.data());
        pointers_.push_back(nullptr);
    }
    Arguments(const Arguments &)=delete;
    Arguments &operator=(const Arguments &)=delete;
    char **data() noexcept {return pointers_.data();}
};
}
