#include "nova/compiler.hpp"
#include <filesystem>
#include <fstream>
#include <functional>
#include <set>

namespace nova {
Module load_module(const std::string &filename, std::vector<std::string> *dependencies) {
    namespace fs=std::filesystem;
    const auto root=fs::weakly_canonical(fs::u8path(filename)).parent_path();
    std::set<fs::path> active,done;
    size_t bytes=0;
    Module merged; merged.file=filename;
    std::function<void(fs::path,Location)> load=[&](fs::path path, Location at) {
        path=fs::weakly_canonical(path);
        auto relative=path.lexically_relative(root);
        if(relative.empty() || *relative.begin()=="..") throw Error(at,"import escapes source root");
        if(active.count(path)) throw Error(at,"cyclic import: "+path.u8string());
        if(done.count(path)) return;
        if(done.size()+active.size()>=32) throw Error(at,"module graph exceeds 32 files");
        std::ifstream file(path,std::ios::binary|std::ios::ate);
        if(!file) throw Error(at,"cannot open module: "+path.u8string());
        auto n=file.tellg();
        if(n<0 || n>1024*1024 || bytes+size_t(n)>4*1024*1024) throw Error(at,"module source size limit exceeded");
        bytes+=size_t(n); file.seekg(0); std::string source(size_t(n),'\0');
        if(!file.read(source.data(),n)) throw Error(at,"cannot read module");
        active.insert(path);
        auto module=parse(lex(source,path.u8string()));
        if(dependencies) dependencies->push_back(path.u8string());
        for(auto &i:module.imports) {
            fs::path p=fs::u8path(i.second);
            if(p.is_absolute()) throw Error(i.first,"import paths must be relative");
            load(path.parent_path()/p,i.first);
        }
        for(auto &s:module.startup) merged.startup.push_back(std::move(s));
        for(auto &f:module.functions) merged.functions.push_back(std::move(f));
        active.erase(path); done.insert(path);
    };
    load(fs::u8path(filename),{filename,1,1});
    return merged;
}
std::string compile_file(const std::string &file,std::vector<std::string> *dependencies) {
    auto module=load_module(file,dependencies); analyze(module); return generate(module);
}
}
