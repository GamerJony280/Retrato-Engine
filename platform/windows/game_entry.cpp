#include "arguments.hpp"
#include <exception>
#include <iostream>
int retrato_game_main(int,char**);
int wmain(int argc,wchar_t **argv){try{
    retrato::detail::Arguments args(argc,argv);
    return retrato_game_main(argc,args.data());
}catch(const std::exception &error){std::cerr<<error.what()<<"\n";return 1;}}
