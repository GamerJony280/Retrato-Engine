#include "nova/compiler.hpp"
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
int nova_main(int argc, char **argv) {
    try {
        std::string input, output, depfile;
        bool emit = false;
        for (int i = 1; i < argc; ++i) {
            std::string arg = argv[i];
            if(arg=="--module-api"){std::cout<<"2\n";return 0;}
            if(arg=="--version"){std::cout<<"Nova 0.2 / native module API 2\n";return 0;}
            if (arg == "--help") {
                std::cout << "Nova MVP: nova --emit-cpp source.nova -o module.cpp\nCompiles Nova to an "
                             "inspectable C++17 module. Use CMake for native linking.\n";
                return 0;
            }
            if (arg == "--emit-cpp")
                emit = true;
            else if (arg == "--depfile" && i + 1 < argc) depfile=argv[++i];
            else if (arg == "-o" && i + 1 < argc)
                output = argv[++i];
            else if (!arg.empty() && arg[0] != '-' && input.empty())
                input = arg;
            else
                throw std::runtime_error("Unknown or incomplete argument: " + arg);
        }
        if (!emit || input.empty() || output.empty())
            throw std::runtime_error("Usage: nova --emit-cpp source.nova -o module.cpp");
        if (std::filesystem::weakly_canonical(std::filesystem::u8path(input)) == std::filesystem::weakly_canonical(std::filesystem::u8path(output)))
            throw std::runtime_error("Input and output must differ");
        std::vector<std::string> dependencies;
        auto cpp = nova::compile_file(input,&dependencies);
        auto outpath=std::filesystem::weakly_canonical(std::filesystem::u8path(output));
        auto deppath=depfile.empty() ? std::filesystem::path{} : std::filesystem::weakly_canonical(std::filesystem::u8path(depfile));
        if(!depfile.empty() && deppath==outpath) throw std::runtime_error("Output and dependency file must differ");
        for(const auto &path:dependencies) {
            if(outpath==std::filesystem::u8path(path) || (!depfile.empty() && deppath==std::filesystem::u8path(path)))
                throw std::runtime_error("Output cannot overwrite a source module");
        }
        for(const auto &path:dependencies) if(path.find_first_of("\r\n")!=std::string::npos)
            throw std::runtime_error("Newlines in dependency paths are unsupported");
        std::ofstream out(std::filesystem::u8path(output), std::ios::binary);
        if (!out || !out.write(cpp.data(), static_cast<std::streamsize>(cpp.size())) || !out.flush())
            throw std::runtime_error("Cannot write output: " + output);
        if (!depfile.empty()) {
            auto escape=[](const std::string &p) { std::string s; for(char c:p) { if(c==' '||c=='#'||c==':'||c=='\\') s+='\\'; if(c=='$') s+='$'; s+=c; } return s; };
            std::ofstream deps(std::filesystem::u8path(depfile));
            deps << escape(output) << ":";
            for(const auto &path:dependencies) deps << " " << escape(path);
            deps << '\n';
            if(!deps || !deps.flush()) throw std::runtime_error("Cannot write dependency file");
        }
        return 0;
    } catch (const std::exception &e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
